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
static constexpr uint16_t kTextSmall = 0xa534, kTextBig = 0xffff;
static std::mutex text_mutex;
static std::vector<Page> pages;
static bool pages_changed = false;
static std::string caption;
static std::atomic<bool> dim{false};
static std::mutex image_mutex;
static uint16_t* image = nullptr;        // PSRAM copy of the last photo
static int image_w = 0, image_h = 0;
static int64_t image_until = 0;
static bool image_fresh = false;    // not drawn yet: a picture is blitted once, not on every tick
static bool image_cleared = false;  // borders around the current picture are already black
static bool dim_drawn = false;

// Idle dimming. There is no backlight pin, so this scales the pixels themselves; each RGB565
// channel keeps its own bits (a shift would bleed red's LSB into green).
static constexpr uint16_t Dimmed(uint16_t c, int percent) {
    return static_cast<uint16_t>((((c >> 11) * percent / 100) << 11) | (((c >> 5 & 0x3f) * percent / 100) << 5) | ((c & 0x1f) * percent / 100));
}
static_assert(Dimmed(0xffff, 100) == 0xffff && Dimmed(0xffff, 0) == 0 && Dimmed(0xffff, 50) == (15 << 11 | 31 << 5 | 15), "dimming must scale each channel without bleeding into the next");
static uint16_t Dim(uint16_t c) { return dim.load() ? Dimmed(c, CONFIG_BITBOT_DISPLAY_DIM_PERCENT) : c; }

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
// The same rectangle every time, with the drawing shifted by ox/oy inside it: a copy into the DMA
// buffer and the SPI transfer, a few ms at 40 MHz.
static bool ShowFrame(Frame frame, int ox, int oy) {
    const uint16_t* face = Pixels(frame);
    for (int top = 0; top < kFaceH; top += kRows) {
        int rows = std::min(kRows, kFaceH - top);
        for (int row = 0; row < rows; ++row) {
            uint16_t* out = pixels + row * kFaceW;
            int source = top + row - oy;
            if (source < 0 || source >= kFaceH) { std::fill_n(out, kFaceW, kBackground); continue; }
            const uint16_t* in = face + source * kFaceW;
            std::fill_n(ox >= 0 ? out : out + kFaceW + ox, std::abs(ox), kBackground);
            std::copy_n(ox >= 0 ? in : in - ox, kFaceW - std::abs(ox), ox >= 0 ? out + ox : out);
        }
        // A sleeping face, background included, without a backlight pin to turn down.
        if (dim.load())
            for (int i = 0; i < rows * kFaceW; ++i) pixels[i] = Dimmed(pixels[i], CONFIG_BITBOT_DISPLAY_DIM_PERCENT);
        if (!Blit(0, top, kFaceW, rows)) return false;
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
void SetDisplayImage(const uint16_t* source, int width, int height, int ms) {
    if (!ready.load() || width > kWidth || height > kHeight) return;
    std::lock_guard<std::mutex> lock(image_mutex);
    if (!image || image_w != width || image_h != height) {  // a live view keeps the same buffer
        uint16_t* copy = static_cast<uint16_t*>(heap_caps_malloc(width * height * 2, MALLOC_CAP_SPIRAM));
        if (!copy) return;
        heap_caps_free(image);
        image = copy; image_w = width; image_h = height;
    }
    std::copy_n(source, width * height, image);
    image_until = NowMs() + ms; image_fresh = true;
}
void ClearDisplayImage() {
    std::lock_guard<std::mutex> lock(image_mutex);
    image_until = 0;
}
bool DisplayShowingImage() {
    std::lock_guard<std::mutex> lock(image_mutex);
    return image && NowMs() < image_until;
}
// Centred on the whole screen; the bars around a picture shorter than the screen are cleared once.
static bool ShowImage() {
    std::lock_guard<std::mutex> lock(image_mutex);
    if (!image) return false;
    if (!image_fresh) return true;
    image_fresh = false;
    const int x = (kWidth - image_w) / 2, y = (kHeight - image_h) / 2;
    if (!image_cleared) {  // clearing every frame would flicker a live view
        if (y > 0 && !Fill(0, 0, kWidth, y, kBackground)) return false;
        if (y + image_h < kHeight && !Fill(0, y + image_h, kWidth, kHeight - y - image_h, kBackground)) return false;
        image_cleared = true;
    }
    for (int row = 0; row < image_h; row += kRows) {
        int rows = std::min(kRows, image_h - row);
        std::copy_n(image + row * image_w, rows * image_w, pixels);
        if (!Blit(x, y + row, image_w, rows)) return false;
    }
    return true;
}
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
// Scale is counted in half-pixels, so 3 is 1.5x: a font pixel becomes a 1 or 2 px block.
struct Run { std::vector<uint8_t> glyphs; int x, y, halves; uint16_t color; };
static constexpr int Scaled(int font_px, int halves) { return font_px * halves / 2; }
// Centred, at the largest scale up to max_halves that fits the width.
static Run Layout(const std::string& text, int y, int max_halves, uint16_t color) {
    Run run{Glyphs(text), 0, y, max_halves, Dim(color)};
    int n = static_cast<int>(run.glyphs.size());
    auto width = [&] { return Scaled(n * 6 - 1, run.halves); };
    while (run.halves > 2 && width() > kWidth - 8) --run.halves;
    run.x = std::max(0, (kWidth - width()) / 2);
    return run;
}
// Overlap between output pixel `out` and font pixel `k` on one axis, measured in 1/halves of a font
// pixel: an output pixel is 2 of those units wide, a font pixel is `halves`.
static constexpr int Overlap(int out, int k, int halves) {
    int lo = std::max(out * 2, k * halves), hi = std::min(out * 2 + 2, (k + 1) * halves);
    return hi > lo ? hi - lo : 0;
}
// A font pixel must hand out exactly its own area, no more: otherwise strokes gain or lose weight.
static constexpr bool CoverageAddsUp(int halves) {
    for (int k = 0; k < 7; ++k) {
        int sum = 0;
        for (int out = 0; out <= (k + 1) * halves; ++out) sum += Overlap(out, k, halves);
        if (sum != halves) return false;
    }
    return true;
}
static_assert(CoverageAddsUp(2) && CoverageAddsUp(3) && CoverageAddsUp(4), "antialiasing must conserve stroke weight");
// `over` at num/den coverage on top of `under`, per RGB565 channel.
static uint16_t Mix(uint16_t under, uint16_t over, int num, int den) {
    int r = ((over >> 11 & 0x1f) * num + (under >> 11 & 0x1f) * (den - num)) / den;
    int g = ((over >> 5 & 0x3f) * num + (under >> 5 & 0x3f) * (den - num)) / den;
    int b = ((over & 0x1f) * num + (under & 0x1f) * (den - num)) / den;
    return static_cast<uint16_t>(r << 11 | g << 5 | b);
}
// Paints the part of a run that falls inside the stripe starting at row top. Each output pixel takes
// the share of the font pixels it covers, so half-covered edges come out half-bright. At a whole
// scale every pixel lands fully inside one font pixel and this is the old on/off block fill; at 1.5x
// it antialiases the halves instead of rounding them up or down to a whole pixel.
static void Paint(const Run& run, int top) {
    const int halves = run.halves, n = static_cast<int>(run.glyphs.size());
    const int width = Scaled(n * 6 - 1, halves) + 1, height = Scaled(7, halves) + 1;
    for (int y = std::max(run.y, top); y < std::min({run.y + height, top + kRows, kHeight}); ++y)
        for (int x = run.x; x < std::min(run.x + width, kWidth); ++x) {
            const int lx = x - run.x, ly = y - run.y;
            int covered = 0;
            for (int fy = ly * 2 / halves; fy <= (ly * 2 + 1) / halves && fy < 7; ++fy)
                for (int fx = lx * 2 / halves; fx <= (lx * 2 + 1) / halves; ++fx) {
                    const int glyph = fx / 6, col = fx % 6;
                    if (col == 5 || glyph >= n) continue;  // the blank column between glyphs
                    if (!(kFont5x7[run.glyphs[glyph]][col] >> fy & 1)) continue;
                    covered += Overlap(ly, fy, halves) * Overlap(lx, fx, halves);
                }
            uint16_t& out = pixels[(y - top) * kWidth + x];
            if (covered) out = covered >= 4 ? run.color : Mix(out, run.color, covered, 4);
        }
}
static bool DrawPage(const Page& page) {
    const Run runs[] = {Layout(page.first, kTextTop + 10, 4, kTextSmall), Layout(page.second, kTextTop + 30, 4, kTextBig)};
    for (int top = kTextTop; top < kHeight; top += kRows) {
        std::fill_n(pixels, kWidth * kRows, Dim(kBackground));
        for (const auto& run : runs) Paint(run, top);
        if (!Blit(0, top, kWidth, std::min(kRows, kHeight - top))) return false;
    }
    return true;
}
// Word-wraps; if it runs long, the last kLines lines are the ones that matter.
// ponytail: bigger text means fewer characters fit (5 x 25); lower kHalves if captions get clipped.
static bool DrawCaption(const std::string& text) {
    constexpr int kHalves = 3, kGlyphH = Scaled(7, kHalves) + 1;  // 1.5x, plus the antialiased edge
    constexpr int kPerLine = (kWidth - 8) / Scaled(6, kHalves), kLines = 5, kLineH = kGlyphH + 3;
    static_assert(kHeight - 4 - kGlyphH - (kLines - 1) * kLineH >= kTextTop, "caption block must not reach into the face");
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
    // Anchored to the bottom edge: the text grows upwards, so the last line never moves.
    const int first = kHeight - 4 - kGlyphH - (static_cast<int>(lines.size()) - 1) * kLineH;
    for (size_t i = 0; i < lines.size(); ++i) runs.push_back(Layout(lines[i], first + static_cast<int>(i) * kLineH, kHalves, kTextBig));
    for (int top = kTextTop; top < kHeight; top += kRows) {
        std::fill_n(pixels, kWidth * kRows, Dim(kBackground));
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
    if (DisplayShowingImage()) return;
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
    // A photo takes over the face area while it is on screen.
    static bool showed_image = false;
    bool showing_image;
    { std::lock_guard<std::mutex> lock(image_mutex); showing_image = image && now < image_until; }
    if (showing_image) {
        showed_image = true;
        ShowImage();
        return;
    }
    if (showed_image) {  // the picture covered the text rows as well as the face
        image_cleared = false;
        std::lock_guard<std::mutex> lock(text_mutex);
        pages_changed = true;
    }
    bool dirty = blink_at < 0 || dim_drawn != dim.load() || showed_image;
    showed_image = false;
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
        x += Step(target_x - x, kStepX);
        y += Step(target_y - y, kStepY);
        dirty = true;
    }
    if (dirty) ShowFrame(current, x, y);
}
}
