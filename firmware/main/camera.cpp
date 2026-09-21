#include "camera.h"
#include "display.h"
#include <esp_camera.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <img_converters.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>
#include "xiaozhi.h"

// Upload format follows 78/xiaozhi-esp32 main/boards/common/esp32_camera.cc: multipart/form-data
// with a "question" field and a "file" field, plus Device-Id/Client-Id/Authorization headers.
namespace bitbot {
static constexpr const char* kTag = "bitbot_camera";
static bool ready = false;
static std::mutex camera_mutex;  // one photo at a time
// The whole 4:3 frame at 3/4 scale, so it fills the 240 px width with nothing cropped (bars above and
// below). Frames are decoded at 320x240: QVGA straight, VGA at half scale.
static constexpr int kW = 240, kH = 180, kSrcW = 320, kSrcH = 240;
static constexpr int kLiveViewMs = 20000, kLiveFrameMs = 100, kPhotoHoldMs = 30000;
static std::atomic<bool> live_view_active{false}, live_view_stop{false}, photo_answering{false};
static std::atomic<int64_t> live_until{0};

bool InitCamera() {
    camera_config_t config = {};
    config.pin_pwdn = -1; config.pin_reset = -1; config.pin_xclk = 10;
    config.pin_sccb_sda = 40; config.pin_sccb_scl = 39;
    config.pin_d7 = 48; config.pin_d6 = 11; config.pin_d5 = 12; config.pin_d4 = 14;
    config.pin_d3 = 16; config.pin_d2 = 18; config.pin_d1 = 17; config.pin_d0 = 15;
    config.pin_vsync = 38; config.pin_href = 47; config.pin_pclk = 13;
    config.xclk_freq_hz = 20000000;
    config.ledc_timer = LEDC_TIMER_1; config.ledc_channel = LEDC_CHANNEL_1;  // LEDC 0 is free for later backlight use
    config.pixel_format = PIXFORMAT_JPEG; config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12; config.fb_count = 2;  // 2: the next frame is captured while this one is decoded
    config.fb_location = CAMERA_FB_IN_PSRAM; config.grab_mode = CAMERA_GRAB_LATEST;
    if (esp_camera_init(&config) != ESP_OK) {
        ESP_LOGW(kTag, "No camera found; the assistant will say it cannot see");
        return false;
    }
    sensor_t* sensor = esp_camera_sensor_get();
    camera_sensor_info_t* info = sensor ? esp_camera_sensor_get_info(&sensor->id) : nullptr;
    ESP_LOGI(kTag, "Camera ready (%s)", info ? info->name : "unknown sensor");
    ready = true;
    return true;
}
bool CameraReady() { return ready; }

// Width from the JPEG's own header: right after a size change the driver can hand back a frame of
// the old size, and decoding that into a smaller buffer would overrun it.
static int JpegWidth(const uint8_t* p, size_t n) {
    for (size_t i = 2; i + 8 < n;) {
        if (p[i] != 0xFF) { ++i; continue; }
        const uint8_t marker = p[i + 1];
        if (marker == 0xC0 || marker == 0xC2) return p[i + 7] << 8 | p[i + 8];
        if (marker == 0xFF) { ++i; continue; }
        i += (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) ? 2 : 2 + (p[i + 2] << 8 | p[i + 3]);
    }
    return 0;
}
// `half` holds kSrcW*kSrcH pixels, `out` holds kW*kH. Nearest-neighbour 4:3 shrink.
// ponytail: nearest is jagged on fine detail; average the 4->3 pixels if that ever matters.
static bool Decode(const camera_fb_t* frame, uint16_t* half, uint16_t* out) {
    const int width = JpegWidth(frame->buf, frame->len);
    if (width != kSrcW && width != 2 * kSrcW) return false;
    if (!jpg2rgb565(frame->buf, frame->len, reinterpret_cast<uint8_t*>(half), width == kSrcW ? JPG_SCALE_NONE : JPG_SCALE_2X)) return false;
    for (int y = 0; y < kH; ++y) {
        const uint16_t* row = half + y * 4 / 3 * kSrcW;
        for (int x = 0; x < kW; ++x) out[y * kW + x] = row[x * 4 / 3];
    }
    return true;
}
static void ShowPhoto(const camera_fb_t* frame) {
    auto* half = static_cast<uint16_t*>(heap_caps_malloc(kSrcW * kSrcH * 2, MALLOC_CAP_SPIRAM));
    auto* rgb = static_cast<uint16_t*>(heap_caps_malloc(kW * kH * 2, MALLOC_CAP_SPIRAM));
    if (half && rgb && Decode(frame, half, rgb)) SetDisplayImage(rgb, kW, kH, kPhotoHoldMs);
    heap_caps_free(half); heap_caps_free(rgb);
}
void EndPhotoPreview() {
    if (photo_answering.exchange(false) && !live_view_active.load()) ClearDisplayImage();
}
// Live view is QVGA: a quarter of the pixels to decode is what makes it fast. `camera_mutex` is
// taken fresh each frame, not once for the whole run: this driver's frame buffers are shared with
// CameraExplain(), which only ever waits out one frame, so "what do you see" stays prompt while a
// live view is up, and the view carries on across it. CameraExplain() also extends live_until.
static void LiveViewTask(void*) {
    auto* half = static_cast<uint16_t*>(heap_caps_malloc(kSrcW * kSrcH * 2, MALLOC_CAP_SPIRAM));
    auto* rgb = static_cast<uint16_t*>(heap_caps_malloc(kW * kH * 2, MALLOC_CAP_SPIRAM));
    sensor_t* sensor = esp_camera_sensor_get();
    if (half && rgb && sensor) {
        { std::lock_guard<std::mutex> lock(camera_mutex); sensor->set_framesize(sensor, FRAMESIZE_QVGA); }
        live_until = NowMs() + kLiveViewMs;
        for (int64_t next = NowMs(); NowMs() < live_until.load() && !live_view_stop.load();) {
            {
                std::lock_guard<std::mutex> lock(camera_mutex);
                if (camera_fb_t* frame = esp_camera_fb_get()) {
                    // Held past the next frame so the face never flashes back between two of them.
                    if (Decode(frame, half, rgb)) SetDisplayImage(rgb, kW, kH, 5 * kLiveFrameMs);
                    esp_camera_fb_return(frame);
                }
            }
            next = std::max(next + kLiveFrameMs, NowMs());  // paced from a schedule, not from the end of the work
            vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(next - NowMs())));
        }
        std::lock_guard<std::mutex> lock(camera_mutex);
        sensor->set_framesize(sensor, FRAMESIZE_VGA);
    }
    heap_caps_free(half); heap_caps_free(rgb);
    live_view_active = false;
    vTaskDelete(nullptr);
}
bool StartLiveView() {
    if (!ready || live_view_active.exchange(true)) return false;
    live_view_stop = false;
    if (xTaskCreate(LiveViewTask, "live-view", 8192, nullptr, 4, nullptr) != pdPASS) { live_view_active = false; return false; }
    return true;
}
bool StopLiveView() {
    if (!live_view_active.load()) return false;
    live_view_stop = true;  // LiveViewTask notices within one frame interval and exits
    return true;
}
static bool WriteAll(esp_http_client_handle_t http, const std::string& text) {
    return esp_http_client_write(http, text.data(), text.size()) == static_cast<int>(text.size());
}

std::string CameraExplain(const std::string& url, const std::string& token, const std::string& question) {
    if (!ready || url.empty()) return "";
    if (live_view_active.load()) live_until = NowMs() + kLiveViewMs;  // still being asked about: keep it up
    // The camera hardware (fb_count = 1: one frame buffer, period) is only needed for the capture
    // itself. The JPEG is copied out and the frame returned before the network upload starts, so a
    // live view can resume capturing its own frames right away instead of stalling for however long
    // the vision service takes to answer (up to the 30 s timeout below).
    std::vector<uint8_t> jpeg;
    int width = 0, height = 0;
    {
        std::lock_guard<std::mutex> lock(camera_mutex);
        camera_fb_t* frame = esp_camera_fb_get();
        if (!frame) { ESP_LOGE(kTag, "Capture failed"); return ""; }
        ShowPhoto(frame);
        jpeg.assign(frame->buf, frame->buf + frame->len);
        width = frame->width; height = frame->height;
        esp_camera_fb_return(frame);
    }

    const std::string boundary = "----BitBotBoundary";
    const std::string head = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"question\"\r\n\r\n" + question +
                             "\r\n--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"camera.jpg\"\r\n" +
                             "Content-Type: image/jpeg\r\n\r\n";
    const std::string tail = "\r\n--" + boundary + "--\r\n";

    esp_http_client_config_t config = {};
    config.url = url.c_str(); config.method = HTTP_METHOD_POST; config.timeout_ms = 30000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.buffer_size_tx = 2048;
    esp_http_client_handle_t http = esp_http_client_init(&config);
    std::string answer;
    if (http) {
        const std::string authorization = "Bearer " + token;
        esp_http_client_set_header(http, "Device-Id", XiaozhiDeviceId().c_str());
        esp_http_client_set_header(http, "Client-Id", XiaozhiClientId().c_str());
        if (!token.empty()) esp_http_client_set_header(http, "Authorization", authorization.c_str());
        esp_http_client_set_header(http, "Content-Type", ("multipart/form-data; boundary=" + boundary).c_str());
        const int total = head.size() + jpeg.size() + tail.size();
        if (esp_http_client_open(http, total) == ESP_OK && WriteAll(http, head) &&
            esp_http_client_write(http, reinterpret_cast<const char*>(jpeg.data()), jpeg.size()) == static_cast<int>(jpeg.size()) &&
            WriteAll(http, tail) && esp_http_client_fetch_headers(http) >= 0) {
            char buffer[512];
            for (int n; answer.size() < 4096 && (n = esp_http_client_read(http, buffer, sizeof(buffer))) > 0;) answer.append(buffer, n);
            const int status = esp_http_client_get_status_code(http);
            ESP_LOGI(kTag, "Photo %dx%d, %u bytes, vision service replied %d", width, height,
                     static_cast<unsigned>(jpeg.size()), status);
            if (status != 200) answer.clear();
        } else {
            ESP_LOGE(kTag, "Upload to the vision service failed");
        }
        esp_http_client_cleanup(http);
    }
    photo_answering = true;  // the photo stays up until the spoken answer ends: EndPhotoPreview()
    return answer;
}
}
