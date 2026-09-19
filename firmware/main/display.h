#pragma once
#include "bitbot.h"
#include "face.h"
#include <string>
#include <utility>
#include <vector>
namespace bitbot {
// 240x240 ST7789, 8-pin module with CS on GPIO4. No audio/network dependencies.
bool InitDisplay();
void TickDisplay(State state);
bool DisplayReady();
// Facial expression, safe from any task. Hold one while the bot says something that fits, then set
// Neutral again. Changes close the eyes and open them on the new face; only Neutral blinks.
void SetExpression(Expression expression);
// Text under the eyes: each page is {small line, big line}; pages rotate every 4 s.
using Page = std::pair<std::string, std::string>;
void SetDisplayPages(std::vector<Page> pages);
// Conversation text (what was heard, what BitBot says), word-wrapped over up to four lines.
// While not empty it replaces the pages; "" hands the text area back to them.
void SetDisplayCaption(std::string text);
// Dim everything (idle/sleep). The backlight is wired to 3V3, so this darkens the pixels.
void SetDisplayDim(bool dim);
}
