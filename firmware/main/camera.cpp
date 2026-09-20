#include "camera.h"
#include "display.h"
#include <esp_camera.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <img_converters.h>
#include <mutex>
#include <vector>
#include "xiaozhi.h"

// Upload format follows 78/xiaozhi-esp32 main/boards/common/esp32_camera.cc: multipart/form-data
// with a "question" field and a "file" field, plus Device-Id/Client-Id/Authorization headers.
namespace bitbot {
static constexpr const char* kTag = "bitbot_camera";
static bool ready = false;
static std::mutex camera_mutex;  // one photo at a time
// Preview size on the 240x240 screen: VGA scaled down by four.
static constexpr int kPreviewW = 160, kPreviewH = 120, kPreviewMs = 6000;

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
    config.jpeg_quality = 12; config.fb_count = 1;
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

static void ShowPhoto(const camera_fb_t* frame) {
    auto* rgb = static_cast<uint8_t*>(heap_caps_malloc(kPreviewW * kPreviewH * 2, MALLOC_CAP_SPIRAM));
    if (!rgb) return;
    if (jpg2rgb565(frame->buf, frame->len, rgb, JPG_SCALE_4X))
        SetDisplayImage(reinterpret_cast<uint16_t*>(rgb), kPreviewW, kPreviewH, kPreviewMs);
    heap_caps_free(rgb);
}
static bool WriteAll(esp_http_client_handle_t http, const std::string& text) {
    return esp_http_client_write(http, text.data(), text.size()) == static_cast<int>(text.size());
}

std::string CameraExplain(const std::string& url, const std::string& token, const std::string& question) {
    if (!ready || url.empty()) return "";
    std::lock_guard<std::mutex> lock(camera_mutex);
    camera_fb_t* frame = esp_camera_fb_get();
    if (!frame) { ESP_LOGE(kTag, "Capture failed"); return ""; }
    ShowPhoto(frame);

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
        const int total = head.size() + frame->len + tail.size();
        if (esp_http_client_open(http, total) == ESP_OK && WriteAll(http, head) &&
            esp_http_client_write(http, reinterpret_cast<const char*>(frame->buf), frame->len) == static_cast<int>(frame->len) &&
            WriteAll(http, tail) && esp_http_client_fetch_headers(http) >= 0) {
            char buffer[512];
            for (int n; answer.size() < 4096 && (n = esp_http_client_read(http, buffer, sizeof(buffer))) > 0;) answer.append(buffer, n);
            const int status = esp_http_client_get_status_code(http);
            ESP_LOGI(kTag, "Photo %dx%d, %u bytes, vision service replied %d", frame->width, frame->height,
                     static_cast<unsigned>(frame->len), status);
            if (status != 200) answer.clear();
        } else {
            ESP_LOGE(kTag, "Upload to the vision service failed");
        }
        esp_http_client_cleanup(http);
    }
    esp_camera_fb_return(frame);
    return answer;
}
}
