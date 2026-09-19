#include "face.h"
#include <algorithm>
#include <cmath>

namespace bitbot {
static constexpr uint16_t kEye = 0x2b77, kGlowColor = 0x129f, kHighlight = 0xffff;  // eye = RGB(40,110,190), glow = RGB(20,80,255)
// Eye centres at rest; the neutral pill is 2*kEyeHalfW x 2*kEyeHalfH.
static constexpr int kEyeY = 104, kEyeLeft = 82, kEyeRight = 158, kEyeHalfW = 22, kEyeHalfH = 48;
static constexpr int kPoseHalfH[kPoses] = {kEyeHalfH, 21, 10, 4};
// Soft glow outside every edge: width in px and strength at the edge (0-1).
static constexpr int kGlow = 12;
static constexpr float kGlowStrength = 0.5f;
static constexpr float kFar = 1e6f;

// Signed distances in px, negative inside. y points down, as on screen.
// Pill: a rounded rectangle centred on 0,0 with the largest possible corner radius.
static float Pill(float x, float y, float half_w, float half_h) {
    float r = std::min(half_w, half_h);
    float qx = std::abs(x) - (half_w - r), qy = std::abs(y) - (half_h - r);
    return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
}
static float Circle(float x, float y, float r) { return std::hypot(x, y) - r; }
// Round-capped stroke from a to b, r px thick on each side.
static float Stroke(float x, float y, float ax, float ay, float bx, float by, float r) {
    float vx = bx - ax, vy = by - ay;
    float t = std::clamp(((x - ax) * vx + (y - ay) * vy) / (vx * vx + vy * vy), 0.0f, 1.0f);
    return std::hypot(x - ax - t * vx, y - ay - t * vy) - r;
}
// Arc bulging up around 0,0: radius ra, reaching half_angle each side of the top. Thickness tapers
// from 2*rb at the top to 2*rb_end at the round ends.
static float Arc(float x, float y, float half_angle, float ra, float rb, float rb_end) {
    float sn = std::sin(half_angle), cs = std::cos(half_angle);
    x = std::abs(x); y = -y;
    if (cs * x > sn * y) return std::hypot(x - sn * ra, y - cs * ra) - rb_end;
    float t = std::atan2(x, y) / half_angle;
    return std::abs(std::hypot(x, y) - ra) - (rb + (rb_end - rb) * t);
}
// Heart with its tip at 0,0, size px tall-ish (Inigo Quilez's heart, scaled).
static float Heart(float x, float y, float size) {
    x = std::abs(x) / size; y = -y / size;
    if (x + y > 1) return (std::hypot(x - 0.25f, y - 0.75f) - std::sqrt(2.0f) / 4) * size;
    float m = 0.5f * std::max(x + y, 0.0f);
    return std::min(std::hypot(x, y - 1), std::hypot(x - m, y - m)) * (x > y ? 1 : -1) * size;
}
// ">" pointing at the nose (inner is + towards the nose): "> <" on the two eyes.
static float Chevron(float inner, float y) {
    return std::min(Stroke(inner, y, -9, -11, 9, 0, 4.5f), Stroke(inner, y, -9, 11, 9, 0, 4.5f));
}
// Neutral's two catch-lights (big upper-left, small lower-right), scaled to an eye half_w wide.
static float CatchLights(float x, float y, float half_w, float half_h) {
    float k = half_w / kEyeHalfW;
    return std::min(Circle(x + 8 * k, y + half_h - 24 * k, 8 * k), Circle(x - 7 * k, y + half_h - 45 * k, 4 * k));
}

// One eye: side 0 is the left one on screen. x is relative to the eye's centre column, y is the
// screen row. Returns the shape distance and sets lights to the catch-light distance.
static float Eye(Expression expression, Pose pose, int side, float x, float y, float& lights) {
    float inner = side ? -x : x;
    lights = kFar;
    switch (expression) {
        case Expression::Neutral: {
            // Blinks keep the bottom edge put and bring the top down, like an eyelid.
            int half_h = kPoseHalfH[pose];
            float dy = y - (kEyeY + kEyeHalfH - half_h);
            if (pose == kOpen) lights = CatchLights(x, dy, kEyeHalfW, kEyeHalfH);
            return Pill(x, dy, kEyeHalfW, half_h);
        }
        case Expression::Happy: return Arc(x, y - (kEyeY + 6), 1.15f, 22, 7, 3.5f);
        case Expression::Surprised: {
            // Small wide-open eyes under raised "/ \" marks.
            float dy = y - (kEyeY + 8);
            lights = CatchLights(x, dy, 16, 30);
            return std::min(Pill(x, dy, 16, 30), Stroke(inner, dy, -14, -44, 0, -54, 3.5f));
        }
        case Expression::Scared: {
            // "(. .)": tiny eyes with brackets on the outer sides.
            lights = Circle(x + 3, y - kEyeY + 3, 2.5f);
            return std::min(Circle(x, y - kEyeY, 8), Arc(y - kEyeY, inner - 2, 0.8f, 22, 3.5f, 3.5f));
        }
        case Expression::Angry: {
            // Pill with the top cut away steeply towards the nose.
            float dy = y - (kEyeY + 4);
            return std::max(Pill(x, dy, 20, 40), (-34 + inner - dy) / std::sqrt(2.0f));
        }
        case Expression::Cute: {
            float dy = y - kEyeY;
            lights = std::min(Circle(x + 8, dy + 9, 8), Circle(x - 9, dy - 8, 4));
            return Circle(x, dy, 24);
        }
        case Expression::Love: {
            float dy = y - (kEyeY + 22);
            lights = Circle(x + 11, dy + 30, 4);
            return Heart(x, dy, 38);
        }
        case Expression::PutOff: {
            float dy = y - kEyeY;
            return std::min(Stroke(x, dy, -13, -13, 13, 13, 4.5f), Stroke(x, dy, -13, 13, 13, -13, 4.5f));
        }
        case Expression::Laughing: return Chevron(inner, y - kEyeY);
        case Expression::Winking:
            return side ? Chevron(inner, y - kEyeY) : Eye(Expression::Neutral, kOpen, side, x, y, lights);
        case Expression::Exasperated: return Pill(x, y - (kEyeY + 4), kEyeHalfW, 6);
        case Expression::Sceptical: {
            if (!side) return Pill(x - 4, y - (kEyeY + 30), 13, 5);  // low, flat dash
            // Oval tilted with its top towards the right.
            constexpr float kTurn = 0.3f;
            float dy = y - (kEyeY - 2), u = x * std::cos(kTurn) + dy * std::sin(kTurn), v = -x * std::sin(kTurn) + dy * std::cos(kTurn);
            lights = CatchLights(u, v, 17, 30);
            return Pill(u, v, 17, 30);
        }
    }
    return kFar;
}
// Anti-aliasing: a pixel's coverage is how much of its 1px footprint lies inside the edge.
static float Coverage(float distance) { return std::clamp(0.5f - distance, 0.0f, 1.0f); }
static uint16_t Mix(uint16_t a, uint16_t b, float t) {
    auto lerp = [t](int x, int y) { return static_cast<int>(x + (y - x) * t + 0.5f); };
    return lerp(a >> 11, b >> 11) << 11 | lerp(a >> 5 & 63, b >> 5 & 63) << 5 | lerp(a & 31, b & 31);
}
void RenderFace(Expression expression, Pose pose, uint16_t* out) {
    for (int y = kFaceY; y < kFaceY + kFaceH; ++y) {
        for (int x = kFaceX; x < kFaceX + kFaceW; ++x) {
            uint16_t color = kBackground;
            for (int side = 0; side < 2; ++side) {
                float dx = x - (side ? kEyeRight : kEyeLeft), lights;
                if (std::abs(dx) > 30 + kGlow) continue;  // no eye reaches further out
                float d = Eye(expression, pose, side, dx, y, lights);
                if (d >= kGlow) continue;
                float glow = kGlowStrength * (1 - std::max(d, 0.0f) / kGlow);
                color = Mix(kBackground, kGlowColor, glow * glow / kGlowStrength);  // squared falloff: soft outer edge
                if (d >= 0.5f) continue;
                color = Mix(color, kEye, Coverage(d));
                color = Mix(color, kHighlight, Coverage(d) * Coverage(lights));
            }
            out[(y - kFaceY) * kFaceW + x - kFaceX] = color;
        }
    }
}
}
