#pragma once
#include "bitbot.h"
namespace bitbot {
// 240x240 ST7789, 7-pin module without CS. No audio/network dependencies.
bool InitDisplay();
void TickDisplay(State state);
bool DisplayReady();
}
