#include "display.h"
#include <algorithm>
#include <atomic>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "sdkconfig.h"

namespace bitbot {
static std::atomic<bool> ready{false};
static esp_lcd_panel_handle_t panel = nullptr;
static esp_lcd_panel_io_handle_t io = nullptr;
static SemaphoreHandle_t transferred = nullptr;
static uint16_t* pixels = nullptr;
static constexpr int kWidth = 240, kHeight = 240, kRows = 16;
static constexpr uint16_t kBackground = 0x0862, kOrange = 0xfc83;

static bool TransferDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(transferred, &woken);
    return woken == pdTRUE;
}
static bool Fill(int x, int y, int width, int height, uint16_t color) {
    std::fill_n(pixels, kWidth * kRows, color);
    for (int top = y; top < y + height; top += kRows) {
        int bottom = std::min(top + kRows, y + height);
        if (esp_lcd_panel_draw_bitmap(panel, x, top, x + width, bottom, pixels) != ESP_OK || xSemaphoreTake(transferred, pdMS_TO_TICKS(1000)) != pdTRUE) {
            // Keep the DMA buffer alive on timeout; do not reuse it.
            ready.store(false); ESP_LOGE("bitbot_display", "Display transfer failed; commissioning remains available"); return false;
        }
    }
    return true;
}
bool InitDisplay() {
#ifndef CONFIG_BITBOT_DISPLAY_ENABLED
    return false;
#endif
    spi_bus_config_t bus = {};
    // Verified Seeed header allocation. Camera/USB/strapping pins are excluded.
    bus.sclk_io_num = GPIO_NUM_7; bus.mosi_io_num = GPIO_NUM_9;
    bus.miso_io_num = -1; bus.quadwp_io_num = -1; bus.quadhd_io_num = -1;
    bus.max_transfer_sz = kWidth * kRows * sizeof(uint16_t);
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
    transferred = xSemaphoreCreateBinary();
    pixels = static_cast<uint16_t*>(heap_caps_malloc(bus.max_transfer_sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!transferred || !pixels) return false;
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = GPIO_NUM_NC;  // 7-pin module has no chip select.
    io_config.dc_gpio_num = GPIO_NUM_5;
    io_config.spi_mode = 0; io_config.pclk_hz = 10 * 1000 * 1000;
    io_config.trans_queue_depth = 1; io_config.lcd_cmd_bits = 8; io_config.lcd_param_bits = 8;
    io_config.on_color_trans_done = TransferDone;
    if (esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &io_config, &io) != ESP_OK) return false;
    esp_lcd_panel_dev_config_t config = {};
    config.reset_gpio_num = GPIO_NUM_2;
#ifdef CONFIG_BITBOT_DISPLAY_BGR
    config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
#else
    config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
#endif
    config.bits_per_pixel = 16; config.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE;
    if (esp_lcd_new_panel_st7789(io, &config, &panel) != ESP_OK) return false;
#ifdef CONFIG_BITBOT_DISPLAY_INVERT
    constexpr bool invert = true;
#else
    constexpr bool invert = false;
#endif
    if (esp_lcd_panel_reset(panel) != ESP_OK || esp_lcd_panel_init(panel) != ESP_OK || esp_lcd_panel_set_gap(panel, 0, CONFIG_BITBOT_DISPLAY_Y_GAP) != ESP_OK || esp_lcd_panel_invert_color(panel, invert) != ESP_OK || esp_lcd_panel_disp_on_off(panel, true) != ESP_OK) return false;
    ready.store(true);
    if (!Fill(0, 0, kWidth, kHeight, kBackground)) return false;
    // RGB bars reveal wrong color order, endianness, clipping, and row offset.
    if (!Fill(12, 220, 72, 8, 0xf800) || !Fill(84, 220, 72, 8, 0x07e0) || !Fill(156, 220, 72, 8, 0x001f)) return false;
    ESP_LOGI("bitbot_display", "ST7789 initialized: 240x240 RGB565, SPI without CS");
    return true;
}
bool DisplayReady() { return ready.load(); }
void TickDisplay(State state) {
    if (!ready.load()) return;
    static int last = -1;
    bool blink = NowMs() % 5000 < 140;
    int frame = static_cast<int>(state) * 2 + blink;
    if (frame == last) return;
    last = frame;
    uint16_t color = state == State::Offline || state == State::Error ? 0xf800 : kOrange;
    if (!Fill(40, 60, 160, 80, kBackground)) return;
    if (!Fill(53, blink ? 96 : 76, 30, blink ? 5 : 44, color)) return;
    if (!Fill(157, blink ? 96 : 76, 30, blink ? 5 : 44, color)) return;
    if (!Fill(101, 151, 38, 5, color)) return;
    // Small status stripe; textual setup instructions/captions are next.
    Fill(100, 192, 40, 4, state == State::Setup ? 0xffff : color);
}
}
