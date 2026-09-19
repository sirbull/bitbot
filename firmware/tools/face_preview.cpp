// Renders every expression with the firmware's own face.cpp to PNG, and fails if a face reaches the
// sprite border (moving eyes would leave trails). From firmware/:
//   c++ -std=c++17 -O2 -Imain tools/face_preview.cpp main/face.cpp -lz -o /tmp/face_preview && /tmp/face_preview ../docs/faces
#include "face.h"
#include <cstdio>
#include <string>
#include <vector>
#include <zlib.h>

using namespace bitbot;
static const char* kNames[kExpressions] = {"neutral", "happy", "surprised", "scared", "angry", "cute", "love", "put-off", "laughing", "winking", "exasperated", "sceptical"};

static void Chunk(FILE* f, const char* type, const std::vector<unsigned char>& data) {
    auto be32 = [f](uint32_t v) { unsigned char b[4] = {(unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v}; fwrite(b, 1, 4, f); };
    be32(data.size());
    uLong crc = crc32(crc32(0, nullptr, 0), (const Bytef*)type, 4);
    if (!data.empty()) crc = crc32(crc, data.data(), data.size());
    fwrite(type, 1, 4, f); fwrite(data.data(), 1, data.size(), f); be32(crc);
}
// RGB565 image to an 8-bit RGB PNG.
static bool WritePng(const std::string& path, const std::vector<uint16_t>& pixels, int w, int h) {
    std::vector<unsigned char> raw;
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);  // filter: none
        for (int x = 0; x < w; ++x) {
            uint16_t c = pixels[y * w + x];
            raw.insert(raw.end(), {(unsigned char)((c >> 11) * 255 / 31), (unsigned char)((c >> 5 & 63) * 255 / 63), (unsigned char)((c & 31) * 255 / 31)});
        }
    }
    uLongf size = compressBound(raw.size());
    std::vector<unsigned char> packed(size);
    if (compress2(packed.data(), &size, raw.data(), raw.size(), 9) != Z_OK) return false;
    packed.resize(size);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    Chunk(f, "IHDR", {(unsigned char)(w >> 24), (unsigned char)(w >> 16), (unsigned char)(w >> 8), (unsigned char)w,
                      (unsigned char)(h >> 24), (unsigned char)(h >> 16), (unsigned char)(h >> 8), (unsigned char)h, 8, 2, 0, 0, 0});
    Chunk(f, "IDAT", packed);
    Chunk(f, "IEND", {});
    return fclose(f) == 0;
}
// The whole 240x240 screen with the face at rest and the text area left empty.
static std::vector<uint16_t> Screen(Expression expression, Pose pose, bool& border_ok) {
    std::vector<uint16_t> face(kFaceW * kFaceH), screen(kScreenW * kScreenW, kBackground);
    RenderFace(expression, pose, face.data());
    for (int y = 0; y < kFaceH; ++y)
        for (int x = 0; x < kFaceW; ++x) {
            bool border = y < kPadY || y >= kFaceH - kPadY || x < kPadX || x >= kFaceW - kPadX;
            if (border && face[y * kFaceW + x] != kBackground) border_ok = false;
            screen[(kFaceY + y) * kScreenW + kFaceX + x] = face[y * kFaceW + x];
        }
    return screen;
}
int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : ".";
    bool ok = true;
    // Overview: the face area of every expression, 6 per row, in the order of the sketch.
    constexpr int kCols = 6, kRowsOut = (kExpressions + kCols - 1) / kCols;
    std::vector<uint16_t> sheet(kCols * kScreenW * kRowsOut * kFaceBottom, kBackground);
    for (int e = 0; e < kExpressions; ++e) {
        bool border_ok = true;
        auto screen = Screen(static_cast<Expression>(e), kOpen, border_ok);
        if (!border_ok) { fprintf(stderr, "%s reaches the sprite border: moving eyes would leave trails\n", kNames[e]); ok = false; }
        char name[64]; snprintf(name, sizeof(name), "/%02d-%s.png", e + 1, kNames[e]);
        ok &= WritePng(dir + name, screen, kScreenW, kScreenW);
        for (int y = 0; y < kFaceBottom; ++y)
            for (int x = 0; x < kScreenW; ++x)
                sheet[(e / kCols * kFaceBottom + y) * kCols * kScreenW + e % kCols * kScreenW + x] = screen[y * kScreenW + x];
    }
    ok &= WritePng(dir + "/all.png", sheet, kCols * kScreenW, kRowsOut * kFaceBottom);
    // Neutral's blink, left to right: open, half, thin, closed.
    std::vector<uint16_t> blink(kPoses * kScreenW * kFaceBottom);
    for (int p = 0; p < kPoses; ++p) {
        bool border_ok = true;
        auto screen = Screen(Expression::Neutral, static_cast<Pose>(p), border_ok);
        ok &= border_ok;
        for (int y = 0; y < kFaceBottom; ++y)
            for (int x = 0; x < kScreenW; ++x) blink[y * kPoses * kScreenW + p * kScreenW + x] = screen[y * kScreenW + x];
    }
    ok &= WritePng(dir + "/neutral-blink.png", blink, kPoses * kScreenW, kFaceBottom);
    printf(ok ? "Wrote %d expressions to %s\n" : "FAILED (%d expressions, %s)\n", kExpressions, dir.c_str());
    return ok ? 0 : 1;
}
