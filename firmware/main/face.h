#pragma once
#include <cstdint>
// Face drawing: pure maths, no ESP-IDF, so tools/face_preview.cpp renders the very same pixels to PNG.
namespace bitbot {
enum class Expression { Neutral, Happy, Surprised, Scared, Angry, Cute, Love, PutOff, Laughing, Winking, Exasperated, Sceptical };
constexpr int kExpressions = static_cast<int>(Expression::Sceptical) + 1;
constexpr uint16_t kBackground = 0x0862;
constexpr int kScreenW = 240, kFaceBottom = 168;  // rows from kFaceBottom down are for text
// The face fills this whole area, always redrawn as one fixed rectangle: a rectangle that moved with
// the eyes would show up on the panel as a box, because a refreshed area looks slightly different
// from one that stands still. Looking around shifts the drawing inside it instead, up to kLookX px
// sideways and kLookUp px up (never down, into the text), at most kStepX/kStepY px per frame.
constexpr int kLookX = 38, kLookUp = 14, kStepX = 10, kStepY = 4;
constexpr int kFaceW = kScreenW, kFaceH = kFaceBottom;
// Neutral blinks through these poses; the other expressions have one pose.
enum Pose { kOpen, kHalf, kThin, kClosed, kPoses };
// Renders into out, kFaceW * kFaceH RGB565 pixels. pose only matters for Neutral.
void RenderFace(Expression expression, Pose pose, uint16_t* out);
}
