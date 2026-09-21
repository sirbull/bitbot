#pragma once
// The test tone, deliberately free of ESP-IDF so tools/tone_check.cpp can verify the maths on a PC.
#include <cmath>
#include <cstddef>
#include <cstdint>

// Writes `frames` frames of a `hz` sine at `amplitude` (0..1) into `buffer`: `slots` samples per
// frame, all carrying the same value (the MAX98357A sums L+R), each `bits` wide (16 or 32).
// `phase` carries across calls - restarting it every buffer is an audible click.
// Returns the number of bytes written, which is what must be handed to i2s_channel_write.
//
// A 32-bit slot gets 24 bits left-aligned, not a full-scale int32: the MAX98357A ignores the low
// bits anyway, and `sinf(p) * INT32_MAX` is undefined behaviour the moment amplitude reaches 1.0.
inline size_t FillTone(void* buffer, int frames, int rate, int bits, int slots, float amplitude, float hz, float& phase) {
    const float step = 2 * float(M_PI) * hz / rate;
    for (int i = 0; i < frames; ++i) {
        phase = fmodf(phase + step, 2 * float(M_PI));
        const float s = sinf(phase) * amplitude;
        for (int c = 0; c < slots; ++c) {
            if (bits == 16) static_cast<int16_t*>(buffer)[i * slots + c] = static_cast<int16_t>(s * 32767.0f);
            else static_cast<int32_t*>(buffer)[i * slots + c] = static_cast<int32_t>(s * 8388607.0f) << 8;
        }
    }
    return static_cast<size_t>(frames) * slots * (bits == 16 ? 2 : 4);
}
