// BitBot hardware test: shows on the ST7789 which parts respond.
// Pins follow docs/wiring.md. Press BOOT to step the speaker test tone through its levels.
// Press BOOT in the first 3 s after reset for the audio-only diagnostic: I2S and nothing else.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <driver/spi_master.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_psram.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "../../firmware/main/font5x7.h"
#include "tone.h"

static const char* TAG = "hwtest";

// Calibration knobs. Same defaults as firmware/main/Kconfig.projbuild.
static constexpr bool kInvert = true;
static constexpr int kYGap = 0;
static constexpr float kDividerRatio = 2.0f;  // 100k/100k; tune against a multimeter.

static constexpr int W = 240, H = 240, kRows = 16;
static constexpr uint16_t kBg = 0x0862, kOrange = 0xfc83, kGreen = 0x07e0, kRed = 0xf800,
                          kYellow = 0xffe0, kWhite = 0xffff, kGrey = 0x8410, kBlue = 0x001f;

// ---------- Display ----------
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t sent;
static uint16_t* px;  // one DMA stripe, W * kRows

static bool OnSent(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(sent, &woken);
    return woken == pdTRUE;
}
static void Blit(int x, int y, int w, int h) {
    if (esp_lcd_panel_draw_bitmap(panel, x, y, x + w, y + h, px) == ESP_OK) xSemaphoreTake(sent, pdMS_TO_TICKS(1000));
}
static void Fill(int x, int y, int w, int h, uint16_t c) {
    std::fill_n(px, W * kRows, c);
    for (int top = y; top < y + h; top += kRows) Blit(x, top, w, std::min(kRows, y + h - top));
}
// One 16-px text line at 2x scale (20 chars), optional bar after the text.
static void Line(int y, const char* s, uint16_t fg, int bar = 0, uint16_t barColor = kGreen) {
    std::fill_n(px, W * kRows, kBg);
    int n = 0;
    for (; *s && n < 20; ++s, ++n) {
        int c = (*s < 32 || *s > 126) ? '?' : *s;
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if (kFont5x7[c - 32][col] >> row & 1)
                    for (int d = 0; d < 4; ++d) px[(1 + row * 2 + d / 2) * W + n * 12 + col * 2 + d % 2] = fg;
    }
    for (int r = 3; r < 13 && bar > 0; ++r) std::fill_n(px + r * W + n * 12 + 4, std::min(bar, W - n * 12 - 4), barColor);
    Blit(0, y, W, kRows);
}
static bool InitDisplay() {
    spi_bus_config_t bus = {};
    bus.sclk_io_num = GPIO_NUM_7; bus.mosi_io_num = GPIO_NUM_9;
    bus.miso_io_num = -1; bus.quadwp_io_num = -1; bus.quadhd_io_num = -1;
    bus.max_transfer_sz = W * kRows * sizeof(uint16_t);
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
    sent = xSemaphoreCreateBinary();
    px = static_cast<uint16_t*>(heap_caps_malloc(bus.max_transfer_sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.cs_gpio_num = GPIO_NUM_4;  // 8-pin module: CS on D3
    io_cfg.dc_gpio_num = GPIO_NUM_5;
    io_cfg.spi_mode = 3;  // no-CS modules need CPOL=1/CPHA=1 to frame bytes
    io_cfg.pclk_hz = 10 * 1000 * 1000;
    io_cfg.trans_queue_depth = 1; io_cfg.lcd_cmd_bits = 8; io_cfg.lcd_param_bits = 8;
    io_cfg.on_color_trans_done = OnSent;
    if (esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &io_cfg, &io) != ESP_OK) return false;
    esp_lcd_panel_dev_config_t cfg = {};
    cfg.reset_gpio_num = GPIO_NUM_2;
    cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    cfg.bits_per_pixel = 16; cfg.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE;
    return esp_lcd_new_panel_st7789(io, &cfg, &panel) == ESP_OK && esp_lcd_panel_reset(panel) == ESP_OK &&
           esp_lcd_panel_init(panel) == ESP_OK && esp_lcd_panel_set_gap(panel, 0, kYGap) == ESP_OK &&
           esp_lcd_panel_invert_color(panel, kInvert) == ESP_OK && esp_lcd_panel_disp_on_off(panel, true) == ESP_OK;
}

// ---------- Mic (INMP441) + amp (MAX98357A) on one full-duplex I2S ----------
// The speaker has its own task and never waits on the microphone: a single writer, paced only by
// the blocking i2s_channel_write. Anything that can stall that task for longer than the DMA buffer
// (~120 ms) comes out of the speaker as a scratch, so nothing slow may live in it - printf least
// of all, since the USB-CDC console blocks for seconds when the host is slow to read.
static constexpr int kRate = 16000, kFrames = 256, kToneHz = 440;
static constexpr int kDmaDescs = 8, kDmaFrames = 240;  // how much audio the DMA holds, see InitAudio
static i2s_chan_handle_t tx, rx;
static std::atomic<int> micDb{-120}, micSlot{0};
static std::atomic<bool> micAlive{false}, beep{false}, beeping{false};
// BOOT cycles the test tone: off, quiet, medium, loud. Crackle that follows the level is power or a
// damaged speaker; crackle at every level is a bad contact.
static std::atomic<int> tone_level{0};
// Level 4 also silences the microphone: mic and amp share BCLK/WS, so if the tone only cleans up
// here, the fault is that shared bus (or the mic loading it), not the amplifier or the speaker.
static constexpr float kToneLevels[] = {0.0f, 0.02f, 0.1f, 0.4f, 0.4f};
static constexpr int kToneModes = 5, kMicOffLevel = 4;
// Write health, sampled by the slow display loop. Never logged from the audio tasks themselves.
static std::atomic<uint32_t> txWrites{0}, txShort{0}, txErrors{0}, txWorstUs{0};
static TaskHandle_t speakerTask, micTask;

// What the I2S bus carries. The MAX98357A works out the sample rate from the BCLK/LRCLK ratio, so
// a format it cannot lock onto shows up as noise no matter how clean the samples are.
struct AudioFormat {
    int rate;
    i2s_data_bit_width_t bits;
    i2s_slot_mode_t slots;
    const char* name;
};
static constexpr AudioFormat kDefaultFormat{kRate, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO,
                                            "16 kHz 32-bit stereo"};
static AudioFormat format = kDefaultFormat;

static void CloseAudio() {
    if (rx) { i2s_channel_disable(rx); i2s_del_channel(rx); rx = nullptr; }
    if (tx) { i2s_channel_disable(tx); i2s_del_channel(tx); tx = nullptr; }
}
static bool InitAudio(const AudioFormat& fmt = kDefaultFormat, bool with_mic = true) {
    format = fmt;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.auto_clear = true;  // an underrun is silence, not the last buffer repeated as a buzz
    chan.dma_desc_num = kDmaDescs;  // 8 x 240 frames: 120 ms at 16 kHz, same as the firmware
    if (i2s_new_channel(&chan, &tx, with_mic ? &rx : nullptr) != ESP_OK) return false;
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(fmt.rate)),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(fmt.bits, fmt.slots),
        .gpio_cfg = {.mclk = GPIO_NUM_NC, .bclk = GPIO_NUM_43, .ws = GPIO_NUM_44, .dout = GPIO_NUM_1,
                     .din = with_mic ? GPIO_NUM_8 : GPIO_NUM_NC, .invert_flags = {}},
    };
    if (i2s_channel_init_std_mode(tx, &std) != ESP_OK || i2s_channel_enable(tx) != ESP_OK) return false;
    if (with_mic && (i2s_channel_init_std_mode(rx, &std) != ESP_OK || i2s_channel_enable(rx) != ESP_OK)) return false;
    // What the clock tree actually produced, not what we asked for. The MAX98357A needs 32, 48 or 64
    // BCLK per frame and a 8-96 kHz LRCLK; anything else and it will not lock.
    i2s_chan_info_t info = {};
    if (i2s_channel_get_info(tx, &info) == ESP_OK)
        ESP_LOGI(TAG, "I2S %s: sclk %lu Hz, mclk %lu Hz, bclk %lu Hz = %lu per frame, DMA %lu bytes", fmt.name,
                 info.sclk_hz, info.mclk_hz, info.bclk_hz, info.bclk_hz / fmt.rate, info.total_dma_buf_size);
    return true;
}

// The waveform itself lives in tone.h, checked on a PC by tools/tone_check.cpp.
static size_t FillTone(void* buffer, int frames, const AudioFormat& fmt, float amplitude, float hz, float& phase) {
    return FillTone(buffer, frames, fmt.rate, fmt.bits == I2S_DATA_BIT_WIDTH_16BIT ? 16 : 32,
                    fmt.slots == I2S_SLOT_MODE_STEREO ? 2 : 1, amplitude, hz, phase);
}
// Writes and accounts for one buffer. Returns the time the write took, so a caller can see a stall.
static uint32_t WriteTone(const void* buffer, size_t bytes) {
    const int64_t started = esp_timer_get_time();
    size_t written = 0;
    const esp_err_t err = i2s_channel_write(tx, buffer, bytes, &written, pdMS_TO_TICKS(500));
    const uint32_t took = static_cast<uint32_t>(esp_timer_get_time() - started);
    ++txWrites;
    if (err != ESP_OK) ++txErrors;
    else if (written != bytes) ++txShort;
    return took;
}

// Always writing - silence when there is no tone - keeps the DMA fed and the amplifier locked, so
// the noise floor between tones is as informative as the tone itself.
static void SpeakerTask(void*) {
    static uint8_t buffer[kFrames * 2 * sizeof(int32_t)];  // worst case: stereo, 32-bit slots
    float phase = 0;
    int pip = 0;
    int64_t loop_started = esp_timer_get_time();
    for (;;) {
        if (beep.exchange(false)) pip = kRate / 2;  // 0.5 s startup pip
        const int level = tone_level.load();
        float amplitude = 0, hz = kToneHz;
        if (level > 0) {
            amplitude = kToneLevels[level];
        } else if (pip > 0) {
            amplitude = 0.1f; hz = pip > kRate / 4 ? 660 : 880;  // two-tone pip
            pip -= kFrames;
        }
        beeping = level > 0 || pip > 0;
        const size_t bytes = FillTone(buffer, kFrames, format, amplitude, hz, phase);
        WriteTone(buffer, bytes);
        // How long one turn of this loop took. Longer than the DMA holds (120 ms) means the speaker
        // ran dry and the gap was audible.
        const int64_t now = esp_timer_get_time();
        const uint32_t took = static_cast<uint32_t>(now - loop_started);
        loop_started = now;
        if (took > txWorstUs.load()) txWorstUs = took;
    }
}

static void MicTask(void*) {
    static int32_t in[kFrames * 2];
    bool running = true;
    for (;;) {
        // The last tone level switches the microphone off, so the transmitter owns the shared
        // BCLK/WS alone. A tone that only cleans up here points at that shared bus, not the amp.
        const bool want = tone_level != kMicOffLevel;
        if (want != running) {
            if (want) i2s_channel_enable(rx); else i2s_channel_disable(rx);
            running = want;
            micAlive = false;
        }
        if (!running) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        size_t got = 0;
        if (i2s_channel_read(rx, in, sizeof(in), &got, pdMS_TO_TICKS(500)) != ESP_OK || got == 0) { micAlive = false; continue; }
        // L/R to GND puts the mic in the left slot; check both in case it is tied high.
        // An undriven SD line reads stuck 0 / -1, with the odd glitch bit.
        const int frames = got / 8;
        int stuck[2] = {};
        int32_t peaks[2] = {};
        for (int i = 0; i < frames * 2; ++i) {
            stuck[i % 2] += (in[i] >> 8) == 0 || (in[i] >> 8) == -1;
            peaks[i % 2] = std::max(peaks[i % 2], std::abs(in[i] >> 8));  // 24-bit, left-aligned
        }
        const int slot = stuck[1] < stuck[0];
        micAlive = stuck[slot] < frames / 8;  // a live mic is almost never exactly 0 / -1
        micSlot = slot;
        micDb = peaks[slot] > 0 ? static_cast<int>(20 * log10f(peaks[slot] / 8388608.0f)) : -120;
    }
}

// ---------- Audio-only diagnostic ----------
// Press BOOT in the first seconds after reset to get here: no display, no camera, no battery, no
// microphone, no extra task - just app_main writing a 440 Hz sine into I2S. If the tone is still not
// clean here, nothing in the rest of the firmware is to blame. Each further BOOT press steps to the
// next format; clean in one format and scratchy in another means the amplifier cannot lock onto that
// format, which is not a wiring fault.
static constexpr AudioFormat kFormats[] = {
    {16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, "16 kHz 16-bit stereo"},
    {16000, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO, "16 kHz 32-bit stereo (app default)"},
    {16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, "16 kHz 16-bit mono"},
    {44100, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, "44.1 kHz 16-bit stereo"},
    {48000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, "48 kHz 16-bit stereo"},
};
static constexpr int kDiagnosticWindowMs = 3000;
static void AudioOnlyDiagnostic() {
    ESP_LOGW(TAG, "AUDIO-ONLY DIAGNOSTIC: display, camera, battery and microphone are all off.");
    ESP_LOGW(TAG, "440 Hz at 10%% of full scale. Press BOOT to step to the next I2S format.");
    static uint8_t buffer[kFrames * 2 * sizeof(int32_t)];
    for (size_t i = 0;; i = (i + 1) % std::size(kFormats)) {
        const AudioFormat& fmt = kFormats[i];
        if (!InitAudio(fmt, false)) {
            ESP_LOGE(TAG, "format %u (%s): I2S init FAILED", static_cast<unsigned>(i), fmt.name);
            CloseAudio(); vTaskDelay(pdMS_TO_TICKS(2000)); continue;
        }
        txWrites = txShort = txErrors = txWorstUs = 0;
        float phase = 0;
        int64_t next_report = 0, loop_started = esp_timer_get_time();
        bool was_pressed = true;  // still held from the press that got us here, or the last step
        for (;;) {
            const size_t bytes = FillTone(buffer, kFrames, fmt, 0.1f, kToneHz, phase);
            WriteTone(buffer, bytes);
            const int64_t now = esp_timer_get_time();
            if (static_cast<uint32_t>(now - loop_started) > txWorstUs.load()) txWorstUs = static_cast<uint32_t>(now - loop_started);
            loop_started = now;
            const bool pressed = gpio_get_level(GPIO_NUM_0) == 0;
            if (pressed && !was_pressed) break;
            was_pressed = pressed;
            if (now > next_report) {  // the write blocks ~16 ms, so this costs nothing in the gaps
                next_report = now + 2000000;
                ESP_LOGI(TAG, "%s: %lu writes, %lu short, %lu errors, worst gap %lu us (DMA holds %d), heap %u min %u, stack %u",
                         fmt.name, txWrites.load(), txShort.load(), txErrors.load(), txWorstUs.load(),
                         kDmaDescs * kDmaFrames * 1000000 / fmt.rate,
                         static_cast<unsigned>(esp_get_free_heap_size()), static_cast<unsigned>(esp_get_minimum_free_heap_size()),
                         static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
            }
        }
        CloseAudio();
    }
}

// ---------- Battery divider on GPIO6 / ADC1 ch5 ----------
static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t cali;
static bool InitBattery() {
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1, .clk_src = {}, .ulp_mode = ADC_ULP_MODE_DISABLE};
    adc_oneshot_chan_cfg_t ch = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    adc_cali_curve_fitting_config_t cc = {.unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_5, .atten = ADC_ATTEN_DB_12,
                                          .bitwidth = ADC_BITWIDTH_DEFAULT};
    return adc_oneshot_new_unit(&unit, &adc) == ESP_OK && adc_oneshot_config_channel(adc, ADC_CHANNEL_5, &ch) == ESP_OK &&
           adc_cali_create_scheme_curve_fitting(&cc, &cali) == ESP_OK;
}
// Returns volts; spread = max-min over the burst. A floating pin (divider not
// wired) wanders by volts, a real divider with its 100 nF cap stays within ~50 mV.
static float BatteryVolts(float& spread) {
    int sum = 0, lo = 99999, hi = 0;
    for (int i = 0; i < 16; ++i) {
        int raw = 0, mv = 0;
        adc_oneshot_read(adc, ADC_CHANNEL_5, &raw);
        adc_cali_raw_to_voltage(cali, raw, &mv);
        sum += mv; lo = std::min(lo, mv); hi = std::max(hi, mv);
        vTaskDelay(1);
    }
    spread = (hi - lo) / 1000.0f * kDividerRatio;
    return sum / 16 / 1000.0f * kDividerRatio;
}

// ---------- Camera (XIAO Sense connector) ----------
static const char* InitCamera() {
    camera_config_t c = {};
    c.pin_pwdn = -1; c.pin_reset = -1; c.pin_xclk = 10;
    c.pin_sccb_sda = 40; c.pin_sccb_scl = 39;
    c.pin_d7 = 48; c.pin_d6 = 11; c.pin_d5 = 12; c.pin_d4 = 14;
    c.pin_d3 = 16; c.pin_d2 = 18; c.pin_d1 = 17; c.pin_d0 = 15;
    c.pin_vsync = 38; c.pin_href = 47; c.pin_pclk = 13;
    c.xclk_freq_hz = 20000000;
    c.ledc_timer = LEDC_TIMER_0; c.ledc_channel = LEDC_CHANNEL_0;
    c.pixel_format = PIXFORMAT_RGB565; c.frame_size = FRAMESIZE_QQVGA;  // 160x120
    c.fb_count = 2; c.fb_location = CAMERA_FB_IN_PSRAM; c.grab_mode = CAMERA_GRAB_LATEST;
    if (esp_camera_init(&c) != ESP_OK) return nullptr;
    sensor_t* s = esp_camera_sensor_get();
    camera_sensor_info_t* info = s ? esp_camera_sensor_get_info(&s->id) : nullptr;
    return info ? info->name : "UNKNOWN";
}
static bool DrawCameraFrame() {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) return false;
    if (fb->format == PIXFORMAT_RGB565 && fb->width <= W) {
        int x = (W - fb->width) / 2, maxRows = std::min<int>(fb->height, 120);
        for (int top = 0; top < maxRows; top += kRows) {
            int rows = std::min(kRows, maxRows - top);
            const uint8_t* src = fb->buf + top * fb->width * 2;
            for (int i = 0; i < rows * static_cast<int>(fb->width); ++i) px[i] = src[i * 2] << 8 | src[i * 2 + 1];  // camera is big-endian
            Blit(x, 120 + top, fb->width, rows);
        }
    }
    esp_camera_fb_return(fb);
    return true;
}

extern "C" void app_main() {
    gpio_config_t boot = {.pin_bit_mask = 1ULL << GPIO_NUM_0, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
                          .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&boot);
    // BOOT pressed in the first seconds after reset: nothing but I2S from here on. It cannot be held
    // through the reset itself - GPIO0 low at reset is the ROM download mode strap, and app_main
    // would never run - so the window opens once we are already up.
    ESP_LOGI(TAG, "Press BOOT within %d s for the audio-only diagnostic", kDiagnosticWindowMs / 1000);
    for (int64_t until = esp_timer_get_time() + kDiagnosticWindowMs * 1000; esp_timer_get_time() < until;) {
        if (gpio_get_level(GPIO_NUM_0) == 0) AudioOnlyDiagnostic();  // never returns
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // Wiring probe: a pin that stays high with the internal pull-down has an external
    // pull-up (module RES/BLK) or a supply on it; one that stays low with pull-up is grounded.
    for (int pin : {1, 2, 4, 5, 6, 7, 8, 9, 43, 44}) {
        gpio_num_t g = static_cast<gpio_num_t>(pin);
        gpio_set_direction(g, GPIO_MODE_INPUT);
        gpio_set_pull_mode(g, GPIO_PULLDOWN_ONLY); vTaskDelay(2); int down = gpio_get_level(g);
        gpio_set_pull_mode(g, GPIO_PULLUP_ONLY); vTaskDelay(2); int up = gpio_get_level(g);
        gpio_set_pull_mode(g, GPIO_FLOATING);
        ESP_LOGI(TAG, "pin GPIO%-2d %s", pin, down ? "drives HIGH" : !up ? "tied LOW" : "open/high-Z");
    }
    bool display = InitDisplay();
    ESP_LOGI(TAG, "display %s", display ? "OK" : "FAIL");
    if (display) {
        Fill(0, 0, W, H, kBg);
        Line(0, "BITBOT HW-TEST", kOrange);
        Line(120, "Starting camera...", kGrey);
    }
    bool audio = InitAudio();
    ESP_LOGI(TAG, "i2s %s", audio ? "OK" : "FAIL");
    if (audio) {
        // The speaker outranks the microphone: it is the one with a hard deadline.
        xTaskCreatePinnedToCore(SpeakerTask, "speaker", 4096, nullptr, 6, &speakerTask, 1);
        xTaskCreatePinnedToCore(MicTask, "mic", 4096, nullptr, 5, &micTask, 1);
        beep = true;  // startup pip
    }
    bool battery = InitBattery();
    const char* camera = InitCamera();
    ESP_LOGI(TAG, "camera %s", camera ? camera : "not found");

    if (display) {
        Fill(0, 120, W, 20, kBg);
        if (!camera) {
            // Color-order check: these must read red, green, blue.
            Fill(30, 150, 50, 50, kRed); Fill(95, 150, 50, 50, kGreen); Fill(160, 150, 50, 50, kBlue);
            Line(210, "  R      G      B", kWhite);
        }
    }

    char buf[32], last[6][32] = {};
    int frames = 0;
    int64_t fpsStart = esp_timer_get_time();
    float fps = 0;
    bool wasPressed = false;
    for (int tick = 0;; ++tick) {
        bool pressed = gpio_get_level(GPIO_NUM_0) == 0;
        if (pressed && !wasPressed) {
            tone_level = (tone_level + 1) % kToneModes;
            ESP_LOGI(TAG, "BOOT pressed: tone level %d (%.0f%% of full scale)", tone_level.load(), kToneLevels[tone_level] * 100);
        }
        wasPressed = pressed;

        if (camera && display && DrawCameraFrame()) ++frames;
        int64_t now = esp_timer_get_time();
        if (now - fpsStart > 1000000) { fps = frames * 1e6f / (now - fpsStart); frames = 0; fpsStart = now; }

        if (!display) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        if (!camera) vTaskDelay(pdMS_TO_TICKS(50));

        auto row = [&](int i, uint16_t color, int bar = 0) {
            if (strcmp(buf, last[i]) == 0 && bar == 0) return;  // unchanged text, no live bar
            strcpy(last[i], buf);
            Line(20 + i * 20, buf, color, bar);
        };
        size_t psram = esp_psram_get_size();
        snprintf(buf, sizeof(buf), "CPU OK PSRAM %uMB", static_cast<unsigned>(psram >> 20));
        row(0, psram ? kGreen : kRed);

        if (!audio) { snprintf(buf, sizeof(buf), "MIC I2S FAIL"); row(1, kRed); }
        else if (!micAlive) { snprintf(buf, sizeof(buf), "MIC NO SIGNAL"); row(1, kRed); }
        else if (tick % 2 == 0) {  // live meter, ~10 Hz
            int db = micDb;
            snprintf(buf, sizeof(buf), "MIC%s %4ddB", micSlot ? " R" : "", db);
            row(1, kGreen, std::clamp((db + 90) * 2, 1, 120));  // -90..-30 dBFS
        }

        if (!audio) snprintf(buf, sizeof(buf), "SPEAKER I2S FAIL");
        else if (tone_level == 0) snprintf(buf, sizeof(buf), "SPEAKER BOOT=TONE");
        else if (tone_level == kMicOffLevel) snprintf(buf, sizeof(buf), "TONE 4: MIC OFF");
        else snprintf(buf, sizeof(buf), "TONE %d: %.0f%%", tone_level.load(), kToneLevels[tone_level] * 100);
        row(2, !audio ? kRed : tone_level > 0 ? kYellow : kWhite);

        if (tick % 10 == 0) {
            float spread = 0, v = battery ? BatteryVolts(spread) : 0;
            bool floating = spread > 0.3f;
            bool ok = battery && !floating && v > 3.0f && v < 4.35f;
            if (!battery) snprintf(buf, sizeof(buf), "BATT ADC FAIL");
            else if (floating) snprintf(buf, sizeof(buf), "BATT FLOATING %.1fV", v);
            else snprintf(buf, sizeof(buf), ok ? "BATT %.2fV" : "BATT %.2fV ?", v);
            row(3, ok ? kGreen : kRed);
            ESP_LOGI(TAG, "tone %d batt %.2fV spread %.2f mic %s (%s) %ddB fps %.1f", tone_level.load(), v, spread, micAlive ? "ok" : "none", micSlot ? "R" : "L", micDb.load(), fps);
            // Speaker health. A worst gap past the DMA depth (120000 us here) means it ran dry,
            // and that gap is the scratch.
            if (audio)
                ESP_LOGI(TAG, "i2s %lu writes, %lu short, %lu errors, worst gap %lu us, heap %u min %u, stack spk %u mic %u",
                         txWrites.load(), txShort.load(), txErrors.load(), txWorstUs.exchange(0),
                         static_cast<unsigned>(esp_get_free_heap_size()), static_cast<unsigned>(esp_get_minimum_free_heap_size()),
                         static_cast<unsigned>(uxTaskGetStackHighWaterMark(speakerTask)), static_cast<unsigned>(uxTaskGetStackHighWaterMark(micTask)));
        }

        if (camera) snprintf(buf, sizeof(buf), "CAMERA %s %.0ffps", camera, fps);
        else snprintf(buf, sizeof(buf), "CAMERA NOT FOUND");
        row(4, camera ? kGreen : kRed);
    }
}
