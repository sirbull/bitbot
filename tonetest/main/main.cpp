// BitBot tone test: the smallest thing that can make a sound. One I2S transmitter, one sine wave,
// and nothing else - no display, no camera, no microphone, no PSRAM, no Wi-Fi, and no second task
// except the one that prints. If the tone is not clean here, no amount of firmware work will fix it.
//
// Sweeps 100-3000 Hz continuously, up and back down, so a few bad frequencies stand out against
// a clean sweep. Ground D2 / GPIO3 to mute, leave it open to play.
// BCLK is on D1/GPIO2 here (see below) - D6/GPIO43 tested faulty as an I2S clock and is left
// disconnected. This build also toggles D6/GPIO43 slowly as a plain GPIO, independent of the audio
// path, to check whether that pin can still drive a clean, slow signal (e.g. a reset line) even
// though it cannot hold up as a fast I2S clock. Put a multimeter (DC volts) on D6 vs GND.
//
//   cd tonetest && idf.py -p <port> flash monitor
#include <atomic>
#include <iterator>
#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "../../hwtest/main/tone.h"

static const char* TAG = "tonetest";

// The only knobs. 16-bit stereo at 44.1 kHz gives 32 BCLK per frame, the ratio a MAX98357A is
// least fussy about, and avoids the fractional clock divider that 16 kHz needs on the ESP32-S3.
// TEMP: BCLK dropped from 1.41 MHz to 256 kHz (5.5x) to test whether the amp is more tolerant of a
// slower, gentler clock edge over the same wiring - a ringing/signal-integrity test, not a format fix.
static constexpr int kRate = 8000, kBits = 16;
static constexpr auto kSlotBits = kBits == 16 ? I2S_DATA_BIT_WIDTH_16BIT : I2S_DATA_BIT_WIDTH_32BIT;
// A continuous triangle sweep, low to high and back, at a fixed moderate level: one long listen
// finds resonances or clock-division artefacts at specific frequencies, which a single 440 Hz tone
// cannot show. Log the Hz alongside what you hear to pin down where it happens.
static constexpr float kFreqLow = 100, kFreqHigh = 3000, kAmplitude = 0.9f;  // capped under 4 kHz Nyquist
static constexpr int kSweepMs = 8000;  // one direction; 16 s for a full up-down cycle
// TEMP: BCLK moved off D6/GPIO43 onto D1/GPIO2 - an otherwise-unused pin - to test whether GPIO43
// itself (or its routing on this board) is the fault. Move ONLY the physical BCLK wire to D1;
// leave WS on D7 and DIN on D0 untouched.
static constexpr gpio_num_t kBclk = GPIO_NUM_2, kWs = GPIO_NUM_44, kDout = GPIO_NUM_1;  // D1, D7, D0
static constexpr gpio_num_t kMute = GPIO_NUM_3;  // D2, to GND
static constexpr int kFrames = 256, kDmaDescs = 8, kDmaFrames = 240;

static constexpr gpio_num_t kGpio43Test = GPIO_NUM_43;  // D6 - suspect pin, driven independently
static i2s_chan_handle_t tx;
static std::atomic<uint32_t> writes{0}, shorts{0}, errors{0}, worstUs{0};
static std::atomic<float> currentHz{kFreqLow};
static std::atomic<bool> muted{false};

// Toggles D6/GPIO43 at 1 Hz as a plain output, nothing to do with I2S or the audio path. Answers
// one question: can this pin still drive a clean, slow digital level (what a reset line needs),
// even though it cannot hold up as a multi-hundred-kHz I2S clock. Watch D6 vs GND on a multimeter
// (DC volts) - it should read close to 0V and close to 3.3V, in sync with the log below.
static void Gpio43CheckTask(void*) {
    gpio_config_t cfg = {.pin_bit_mask = 1ULL << kGpio43Test, .mode = GPIO_MODE_OUTPUT,
                         .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
                         .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&cfg);
    for (bool level = false;; level = !level) {
        gpio_set_level(kGpio43Test, level);
        ESP_LOGI(TAG, "D6/GPIO43 (unrelated to audio): driving %s", level ? "HIGH (~3.3V)" : "LOW (~0V)");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Printing lives out here, never in the write loop: a log line over USB-CDC can block for longer
// than the DMA holds, and then the report is itself the gap it was meant to measure.
static void Reporter(void*) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP_LOGI(TAG, "%4.0f Hz%s | %lu writes, %lu short, %lu errors | worst gap %lu us (DMA holds %d us)",
                 currentHz.load(), muted ? " MUTED" : "", writes.load(), shorts.load(),
                 errors.load(), worstUs.exchange(0), kDmaDescs * kDmaFrames * 1000000 / kRate);
    }
}

extern "C" void app_main() {
    gpio_config_t mute = {.pin_bit_mask = 1ULL << kMute, .mode = GPIO_MODE_INPUT,
                          .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
                          .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&mute));

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.auto_clear = true;  // an underrun is silence, not the last buffer repeated as a buzz
    chan.dma_desc_num = kDmaDescs;
    chan.dma_frame_num = kDmaFrames;
    ESP_ERROR_CHECK(i2s_new_channel(&chan, &tx, nullptr));  // transmitter only: no microphone at all
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(kSlotBits, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = GPIO_NUM_NC, .bclk = kBclk, .ws = kWs, .dout = kDout,
                     .din = GPIO_NUM_NC, .invert_flags = {}},
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));

    // What the clock tree actually produced. The MAX98357A works the sample rate out from this
    // ratio and only locks onto 32, 48 or 64 BCLK per frame, at 8-96 kHz.
    i2s_chan_info_t info = {};
    if (i2s_channel_get_info(tx, &info) == ESP_OK)
        ESP_LOGI(TAG, "%d Hz, %d-bit stereo: bclk %lu Hz = %lu per frame, mclk %lu Hz, DMA %lu bytes",
                 kRate, kBits, info.bclk_hz, info.bclk_hz / kRate, info.mclk_hz, info.total_dma_buf_size);
    ESP_LOGI(TAG, "Sweeping %g - %g Hz on D0/GPIO1 at %.0f%%, %d s up and %d s back down, on repeat.",
             static_cast<double>(kFreqLow), static_cast<double>(kFreqHigh), kAmplitude * 100,
             kSweepMs / 1000, kSweepMs / 1000);
    ESP_LOGI(TAG, "Ground D2/GPIO3 to mute. Leave D6 alone: it is the bit clock.");
    xTaskCreate(Reporter, "report", 4096, nullptr, 1, nullptr);
    xTaskCreate(Gpio43CheckTask, "d6-check", 2048, nullptr, 1, nullptr);
    vTaskPrioritySet(nullptr, 6);  // app_main sits at 1, where almost anything can preempt the writer

    // Muting writes zeros rather than stopping, so the DMA stays fed and the amplifier stays locked.
    // That makes the mute switch a real test in its own right: any noise while muted is provably not
    // coming from the samples, because they are all zero.
    static uint8_t buffer[kFrames * 2 * sizeof(int32_t)];  // worst case: stereo, 32-bit slots
    float phase = 0;
    const int64_t sweep_start = esp_timer_get_time();
    int64_t last = sweep_start;
    for (;;) {  // nothing in here may block: fill, write, read the pin, and that is all
        const bool mute = gpio_get_level(kMute) == 0;  // pulled up, so open = play, grounded = mute
        muted = mute;
        // Triangle wave over time: up for kSweepMs, back down for kSweepMs, repeat.
        const int64_t t = (last - sweep_start) / 1000 % (2 * kSweepMs);
        const float frac = t < kSweepMs ? static_cast<float>(t) / kSweepMs
                                        : static_cast<float>(2 * kSweepMs - t) / kSweepMs;
        const float hz = kFreqLow + (kFreqHigh - kFreqLow) * frac;
        currentHz = hz;
        const size_t bytes = FillTone(buffer, kFrames, kRate, kBits, 2, mute ? 0.0f : kAmplitude, hz, phase);
        size_t written = 0;
        const esp_err_t err = i2s_channel_write(tx, buffer, bytes, &written, pdMS_TO_TICKS(500));
        ++writes;
        if (err != ESP_OK) ++errors;
        else if (written != bytes) ++shorts;
        const int64_t now = esp_timer_get_time();
        const uint32_t took = static_cast<uint32_t>(now - last);
        last = now;
        if (took > worstUs.load()) worstUs = took;  // racy against the reporter's reset, and harmless
    }
}
