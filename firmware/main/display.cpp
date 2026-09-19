#include "display.h"
#include <algorithm>
#include <atomic>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <iterator>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "sdkconfig.h"
#include "font5x7.h"
#include <mutex>

namespace bitbot {
static std::atomic<bool> ready{false};
static esp_lcd_panel_handle_t panel = nullptr;
static esp_lcd_panel_io_handle_t io = nullptr;
static SemaphoreHandle_t transferred = nullptr;
static uint16_t* pixels = nullptr;
static constexpr int kWidth = 240, kHeight = 240, kRows = 16;
// Text pages below the eyes: a small grey line and a big white line, rotating every kPageMs.
static constexpr int kTextTop = kFaceBottom, kPageMs = 4000;
static constexpr uint16_t kTextSmall = 0xa534, kTextBig = 0xffff, kTextDim = 0x52aa;
static std::mutex text_mutex;
static std::vector<Page> pages;
static bool pages_changed = false;
static std::string caption;
static std::atomic<bool> dim{false};
static bool dim_drawn = false;

static bool TransferDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(transferred, &woken);
    return woken == pdTRUE;
}
static bool Blit(int x, int y, int width, int height) {
    if (esp_lcd_panel_draw_bitmap(panel, x, y, x + width, y + height, pixels) != ESP_OK || xSemaphoreTake(transferred, pdMS_TO_TICKS(1000)) != pdTRUE) {
        // Keep the DMA buffer alive on timeout; do not reuse it.
        ready.store(false); ESP_LOGE("bitbot_display", "Display transfer failed; commissioning remains available"); return false;
    }
    return true;
}
static bool Fill(int x, int y, int width, int height, uint16_t color) {
    std::fill_n(pixels, kWidth * kRows, color);
    for (int top = y; top < y + height; top += kRows)
        if (!Blit(x, top, width, std::min(kRows, y + height - top))) return false;
    return true;
}
static std::atomic<Expression> wanted{Expression::Neutral};
void SetExpression(Expression expression) { wanted.store(expression); }
// Faces are rendered into PSRAM ahead of time; the float maths is too slow to run per frame.
// Neutral's blink poses are rendered at boot; another expression when it is first shown.
static uint16_t* neutral[kPoses] = {};
static uint16_t* other = nullptr;
struct Frame { Expression face; Pose pose; };
static const uint16_t* Pixels(Frame frame) {
    if (frame.face == Expression::Neutral) return neutral[frame.pose];
    static Expression rendered = Expression::Neutral;
    if (rendered != frame.face) RenderFace(rendered = frame.face, kOpen, other);
    return other;
}
// Only a copy into the DMA buffer and the SPI transfer: a few ms at 40 MHz.
static bool ShowFrame(Frame frame, int ox, int oy) {
    const uint16_t* face = Pixels(frame);
    for (int top = 0; top < kFaceH; top += kRows) {
        int rows = std::min(kRows, kFaceH - top);
        if (dim.load()) {
            // Halve each RGB565 channel: a sleeping face, without a backlight pin to turn down.
            for (int i = 0; i < rows * kFaceW; ++i) { uint16_t c = face[top * kFaceW + i]; pixels[i] = (c >> 1 & 0x7800) | (c >> 1 & 0x03e0) | (c >> 1 & 0x000f); }
        } else {
            std::copy_n(face + top * kFaceW, rows * kFaceW, pixels);
        }
        if (!Blit(kFaceX + ox, kFaceY + oy + top, kFaceW, rows)) return false;
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
    for (auto* buffer : {&neutral[kOpen], &neutral[kHalf], &neutral[kThin], &neutral[kClosed], &other})
        if (!(*buffer = static_cast<uint16_t*>(heap_caps_malloc(kFaceW * kFaceH * sizeof(uint16_t), MALLOC_CAP_SPIRAM)))) return false;
    for (int pose = 0; pose < kPoses; ++pose) RenderFace(Expression::Neutral, static_cast<Pose>(pose), neutral[pose]);
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = GPIO_NUM_4;  // 8-pin module: CS on D3. Verified 2026-09-19.
    io_config.dc_gpio_num = GPIO_NUM_5;
    io_config.spi_mode = 3; io_config.pclk_hz = CONFIG_BITBOT_DISPLAY_SPI_MHZ * 1000 * 1000;
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
    ESP_LOGI("bitbot_display", "ST7789 initialized: 240x240 RGB565, SPI, CS on GPIO4");
    return true;
}
bool DisplayReady() { return ready.load(); }
// UTF-8 to font indices: ASCII plus æøåÆØÅ; anything else becomes '?'.
static std::vector<uint8_t> Glyphs(const std::string& text) {
    static const char* extra[] = {"æ", "ø", "å", "Æ", "Ø", "Å"};
    std::vector<uint8_t> out;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = text[i];
        if (c >= 32 && c <= 126) { out.push_back(c - 32); continue; }
        uint8_t glyph = '?' - 32;
        for (int k = 0; k < 6; ++k) if (text.compare(i, 2, extra[k]) == 0) { glyph = 95 + k; ++i; break; }
        out.push_back(glyph);
    }
    return out;
}
struct Run { std::vector<uint8_t> glyphs; int x, y, scale; uint16_t color; };
// Centred, at the largest scale up to max_scale that fits the width.
static Run Layout(const std::string& text, int y, int max_scale, uint16_t color) {
    Run run{Glyphs(text), 0, y, max_scale, dim.load() ? kTextDim : color};
    int n = static_cast<int>(run.glyphs.size());
    while (run.scale > 1 && n * 6 * run.scale - run.scale > kWidth - 8) --run.scale;
    run.x = std::max(0, (kWidth - (n * 6 * run.scale - run.scale)) / 2);
    return run;
}
// Paints the part of a run that falls inside the stripe starting at row top.
static void Paint(const Run& run, int top) {
    for (size_t i = 0; i < run.glyphs.size(); ++i)
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row) {
                if (!(kFont5x7[run.glyphs[i]][col] >> row & 1)) continue;
                int x0 = run.x + (static_cast<int>(i) * 6 + col) * run.scale, y0 = run.y + row * run.scale;
                for (int y = std::max(y0, top); y < std::min(y0 + run.scale, top + kRows); ++y)
                    for (int x = x0; x < std::min(x0 + run.scale, kWidth); ++x) pixels[(y - top) * kWidth + x] = run.color;
            }
}
static bool DrawPage(const Page& page) {
    const Run runs[] = {Layout(page.first, kTextTop + 10, 2, kTextSmall), Layout(page.second, kTextTop + 32, 3, kTextBig)};
    for (int top = kTextTop; top < kHeight; top += kRows) {
        std::fill_n(pixels, kWidth * kRows, kBackground);
        for (const auto& run : runs) Paint(run, top);
        if (!Blit(0, top, kWidth, std::min(kRows, kHeight - top))) return false;
    }
    return true;
}
// Word-wraps at the small scale; if it runs long, the last four lines are the ones that matter.
static bool DrawCaption(const std::string& text) {
    constexpr int kScale = 2, kPerLine = (kWidth - 8) / (6 * kScale), kLines = 4, kLineH = 18;
    std::vector<std::string> lines(1);
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        std::string word = text.substr(start, end - start);
        start = end + 1;
        if (word.empty()) continue;
        std::string joined = lines.back().empty() ? word : lines.back() + " " + word;
        if (Glyphs(joined).size() <= kPerLine || lines.back().empty()) lines.back() = joined;  // a too-long word gets its own line
        else lines.push_back(word);
    }
    if (lines.size() > kLines) lines.erase(lines.begin(), lines.end() - kLines);
    std::vector<Run> runs;
    for (size_t i = 0; i < lines.size(); ++i) runs.push_back(Layout(lines[i], kTextTop + 2 + static_cast<int>(i) * kLineH, kScale, kTextBig));
    for (int top = kTextTop; top < kHeight; top += kRows) {
        std::fill_n(pixels, kWidth * kRows, kBackground);
        for (const auto& run : runs) Paint(run, top);
        if (!Blit(0, top, kWidth, std::min(kRows, kHeight - top))) return false;
    }
    return true;
}
void SetDisplayDim(bool value) {
    if (dim.exchange(value) != value) { std::lock_guard<std::mutex> lock(text_mutex); pages_changed = true; }
}
void SetDisplayCaption(std::string text) {
    std::lock_guard<std::mutex> lock(text_mutex);
    if (text == caption) return;
    caption = std::move(text); pages_changed = true;
}
void SetDisplayPages(std::vector<Page> next) {
    std::lock_guard<std::mutex> lock(text_mutex);
    if (next == pages) return;
    pages = std::move(next); pages_changed = true;
}
static void TickText() {
    static int last_page = -1;
    Page page; bool redraw; std::string text;
    {
        std::lock_guard<std::mutex> lock(text_mutex);
        text = caption;
        int index = pages.empty() ? -1 : static_cast<int>(NowMs() / kPageMs % pages.size());
        redraw = pages_changed || index != last_page;
        pages_changed = false; last_page = index;
        if (index >= 0) page = pages[index];
    }
    if (!redraw) return;
    if (text.empty()) DrawPage(page);
    else DrawCaption(text);
}
// Mostly 5-7 s between blinks, sometimes sooner (1-5 s), so it never looks mechanical.
static int64_t NextBlinkDelay() {
    uint32_t r = esp_random();
    return r % 4 ? 5000 + r % 2001 : 1000 + r % 4001;
}
// A new place to look every 2-6 s; a third of the time straight ahead again.
static int64_t NextLookDelay() { return 2000 + esp_random() % 4001; }
static int RandomUpTo(int range) { return static_cast<int>(esp_random() % (range + 1)); }
// Ease-out: cover half the remaining distance each frame (at least 1 px, at most max px).
static int Step(int diff, int max) { return std::clamp((diff + (diff > 0) - (diff < 0)) / 2, -max, max); }
void TickDisplay(State) {
    if (!ready.load()) return;
    TickText();
    // Animation frames at a steady rate, queued: only Neutral blinks; any change of expression
    // closes the eyes (through Neutral's blink poses) and opens them on the new face.
    static constexpr int kFps = 12, kFrameMs = 1000 / kFps;
    static Frame queue[6], current = {Expression::Neutral, kOpen};
    static int queued = 0, next = 0, x = 0, y = 0, target_x = 0, target_y = 0;
    static int64_t next_frame = 0, blink_at = -1, look_at = 0;
    int64_t now = NowMs();
    bool dirty = blink_at < 0 || dim_drawn != dim.load();
    dim_drawn = dim.load();
    if (dirty) { blink_at = now + NextBlinkDelay(); look_at = now + NextLookDelay(); }
    if (now < next_frame) return;
    next_frame = std::max(next_frame + kFrameMs, now);  // from schedule, not draw end: no drift
#ifdef CONFIG_BITBOT_DISPLAY_EXPRESSION_DEMO
    Expression target = static_cast<Expression>(now / 4000 % kExpressions);
#else
    Expression target = wanted.load();
#endif
    if (next == queued) {
        constexpr auto N = Expression::Neutral;
        queued = next = 0;
        auto push = [](Frame frame) { queue[queued++] = frame; };
        if (target != current.face) {
            if (current.face == N) { push({N, kHalf}); push({N, kThin}); }
            push({N, kClosed});
            if (target == N) { push({N, kThin}); push({N, kHalf}); }
            push({target, kOpen});
        } else if (target == N && now >= blink_at) {
            for (Pose pose : {kHalf, kThin, kClosed, kThin, kHalf, kOpen}) push({N, pose});
        }
    }
    if (next < queued) {
        current = queue[next++];
        dirty = true;
        if (next == queued) blink_at = now + NextBlinkDelay();
    }
    if (now >= look_at) {
        bool ahead = esp_random() % 3 == 0;
        target_x = ahead ? 0 : RandomUpTo(2 * kLookX) - kLookX;
        target_y = ahead ? 0 : -RandomUpTo(kLookUp);
        look_at = now + NextLookDelay();
    }
    if (x != target_x || y != target_y) {
        x += Step(target_x - x, kPadX);
        y += Step(target_y - y, kPadY);
        dirty = true;
    }
    if (dirty) ShowFrame(current, x, y);
}
}
