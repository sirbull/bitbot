#pragma once
#include <cstddef>
#include <cstdint>
namespace bitbot {
// INMP441 mic + MAX98357A amp on one full-duplex I2S bus (shared BCLK/WS, see docs/wiring.md).
// Both directions run at 16 kHz mono int16 as far as callers are concerned.
constexpr int kAudioRate = 16000;
bool InitAudio();
// Blocks until `count` mic samples are read. Returns false on an I2S error.
bool ReadMic(int16_t* samples, size_t count);
// Blocks until `count` samples are queued to the speaker.
void WriteSpeaker(const int16_t* samples, size_t count);
void SetVolume(int percent);  // 0-100
}
