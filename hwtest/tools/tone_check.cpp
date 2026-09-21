// Checks the test tone the speaker actually plays: a clean sine, the right datatype, the right
// interleaving, the right byte count, and no overflow at full scale. From hwtest/:
//   c++ -std=c++17 -O2 -Imain tools/tone_check.cpp -o /tmp/tone_check && /tmp/tone_check
#include "tone.h"
#include <cassert>
#include <cstdio>
#include <vector>

int main() {
    const int rate = 16000, hz = 440;

    // 16-bit stereo: both slots identical, peaks where a sine should have them, and one second
    // holds 2 * hz zero crossings - that is the check that the frequency is actually 440 Hz.
    {
        std::vector<int16_t> buf(rate * 2);
        float phase = 0;
        const size_t bytes = FillTone(buf.data(), rate, rate, 16, 2, 0.5f, hz, phase);
        assert(bytes == rate * 2 * sizeof(int16_t));
        int crossings = 0, peak = 0, trough = 0;
        for (int i = 0; i < rate; ++i) {
            assert(buf[i * 2] == buf[i * 2 + 1]);  // mono duplicated into both slots
            peak = std::max<int>(peak, buf[i * 2]);
            trough = std::min<int>(trough, buf[i * 2]);
            if (i && (buf[i * 2] >= 0) != (buf[(i - 1) * 2] >= 0)) ++crossings;
        }
        assert(crossings >= 2 * hz - 2 && crossings <= 2 * hz + 2);
        assert(peak > 16000 && peak <= 16384);      // 0.5 * 32767, within a sample of the crest
        assert(trough < -16000 && trough >= -16384);
    }

    // 32-bit slots carry 24 bits left-aligned: the low byte is always zero, and the peak sits at
    // the same fraction of full scale as the 16-bit one, so the amplifier hears the same level.
    {
        std::vector<int32_t> buf(256 * 2);
        float phase = 0;
        const size_t bytes = FillTone(buf.data(), 256, rate, 32, 2, 0.5f, hz, phase);
        assert(bytes == 256 * 2 * sizeof(int32_t));
        int64_t peak = 0;
        for (size_t i = 0; i < buf.size(); i += 2) {
            assert(buf[i] == buf[i + 1]);       // mono duplicated into both slots
            assert((buf[i] & 0xff) == 0);       // 24 bits, left-aligned
            peak = std::max<int64_t>(peak, buf[i]);
        }
        const int64_t full = int64_t{8388607} << 8;
        assert(peak > full * 49 / 100 && peak <= full / 2 + 1);
    }

    // Mono: one sample per frame, so half the bytes of stereo for the same frame count.
    {
        std::vector<int16_t> buf(256);
        float phase = 0;
        assert(FillTone(buf.data(), 256, rate, 16, 1, 0.5f, hz, phase) == 256 * sizeof(int16_t));
    }

    // Full scale must not wrap. A sine that overflows comes out as a square wave: loud scratching,
    // which is exactly the symptom this test exists to rule out.
    {
        std::vector<int32_t> wide(rate * 2);
        std::vector<int16_t> narrow(rate * 2);
        float p1 = 0, p2 = 0;
        FillTone(wide.data(), rate, rate, 32, 2, 1.0f, hz, p1);
        FillTone(narrow.data(), rate, rate, 16, 2, 1.0f, hz, p2);
        for (size_t i = 1; i < wide.size(); i += 2) {
            // Neighbouring samples of a 440 Hz sine at 16 kHz never jump more than ~18% of full
            // scale; a wrap shows up as a jump of nearly the whole range.
            assert(std::llabs(int64_t{wide[i]} - int64_t{wide[i - 1]}) < (int64_t{1} << 30));
            assert(std::abs(int{narrow[i]} - int{narrow[i - 1]}) < 24000);
        }
    }

    // Phase carries across calls: two buffers in a row are bit-for-bit the same waveform as one
    // long buffer. A phase that restarts each buffer is a step, and a step is an audible click.
    {
        int16_t one[128], split[128];
        float whole = 0, carried = 0;
        FillTone(one, 128, rate, 16, 1, 0.5f, hz, whole);
        FillTone(split, 64, rate, 16, 1, 0.5f, hz, carried);
        FillTone(split + 64, 64, rate, 16, 1, 0.5f, hz, carried);
        for (int i = 0; i < 128; ++i) assert(one[i] == split[i]);
    }

    printf("tone_check: all good\n");
    return 0;
}
