#include "audio.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <esp_timer.h>

namespace bitbot {
static constexpr const char* kTag = "bitbot_audio";
static i2s_chan_handle_t tx = nullptr, rx = nullptr;
static std::atomic<int> write_errors{0}, short_writes{0};
static std::atomic<float> gain{0.5f};
// INMP441 gives 24-bit samples left-aligned in 32-bit slots and is quiet; >> 12 keeps
// 4 bits of headroom above int16, the same digital gain XiaoZhi uses for this mic.
static constexpr int kMicShift = 13;  // ponytail: calibration knob; lower = louder, clips sooner.
                                      // 12 clipped at 0 dBFS on this mic and wrecked the transcription.

bool InitAudio() {
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.auto_clear = true;  // silence on the speaker whenever nothing is written
    chan.dma_desc_num = 8; chan.dma_frame_num = 240;  // 120 ms of DMA buffer: room for network jitter
    if (i2s_new_channel(&chan, &tx, &rx) != ESP_OK) return false;
    // Stereo 32-bit slots: the mic answers in the left slot (L/R to GND), the amp mixes L+R.
    // BCLK is on D5/GPIO6, not the D6/GPIO43 the schematic originally called for: GPIO43 proved
    // physically faulty on the prototype board, confirmed by moving BCLK alone to
    // a spare pin and getting a clean signal. GPIO6 was already unused (battery sensing was never
    // wired). WS/DIN/mic-SD are unaffected.
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kAudioRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = GPIO_NUM_NC, .bclk = GPIO_NUM_6, .ws = GPIO_NUM_44, .dout = GPIO_NUM_1, .din = GPIO_NUM_8,
                     .invert_flags = {}},
    };
    bool ok = i2s_channel_init_std_mode(tx, &std) == ESP_OK && i2s_channel_init_std_mode(rx, &std) == ESP_OK &&
              i2s_channel_enable(tx) == ESP_OK && i2s_channel_enable(rx) == ESP_OK;
    ESP_LOGI(kTag, "I2S %s: 16 kHz, mic left slot, amp on both slots", ok ? "ready" : "FAILED");
    // What the clock tree actually produced. The MAX98357A works the sample rate out from the
    // BCLK/LRCLK ratio and only locks onto 32, 48 or 64 BCLK per frame at 8-96 kHz.
    i2s_chan_info_t info = {};
    if (ok && i2s_channel_get_info(tx, &info) == ESP_OK)
        ESP_LOGI(kTag, "I2S clocks: sclk %lu Hz, mclk %lu Hz, bclk %lu Hz = %lu per frame, DMA %lu bytes",
                 info.sclk_hz, info.mclk_hz, info.bclk_hz, info.bclk_hz / kAudioRate, info.total_dma_buf_size);
    return ok;
}
bool ReadMic(int16_t* samples, size_t count) {
    int32_t frames[160 * 2];
    while (count > 0) {
        size_t want = std::min<size_t>(count, 160), got = 0;
        if (i2s_channel_read(rx, frames, want * 8, &got, pdMS_TO_TICKS(1000)) != ESP_OK || got != want * 8) return false;
        for (size_t i = 0; i < want; ++i) samples[i] = std::clamp<int32_t>(frames[i * 2] >> kMicShift, INT16_MIN, INT16_MAX);
        samples += want; count -= want;
    }
    return true;
}
void WriteSpeaker(const int16_t* samples, size_t count) {
    int32_t frames[160 * 2];
    const float g = gain.load();
    const int64_t started = esp_timer_get_time();
    while (count > 0) {
        size_t n = std::min<size_t>(count, 160), written = 0;
        for (size_t i = 0; i < n; ++i) frames[i * 2] = frames[i * 2 + 1] = static_cast<int32_t>(samples[i] * g * 65536.0f);
        const esp_err_t err = i2s_channel_write(tx, frames, n * 8, &written, portMAX_DELAY);
        if (err != ESP_OK) ++write_errors;
        else if (written != n * 8) ++short_writes;
        samples += n; count -= n;
    }
    // Only ever logged when something is wrong, and at most every 5 s: a log line in here is itself
    // enough of a stall to empty the DMA and put a scratch on the speaker.
    if (write_errors || short_writes) {
        static int64_t next = 0;
        if (esp_timer_get_time() > next) {
            next = esp_timer_get_time() + 5000000;
            ESP_LOGW(kTag, "I2S writes: %d failed, %d short; last call took %d ms", write_errors.load(),
                     short_writes.load(), static_cast<int>((esp_timer_get_time() - started) / 1000));
        }
    }
}
// Perceived loudness is roughly logarithmic, so the slider maps through a square.
void SetVolume(int percent) {
    float v = std::clamp(percent, 0, 100) / 100.0f;
    gain.store(v * v);
}
}
