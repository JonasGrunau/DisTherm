// dt::Chorus - black-box reconstruction of Output Thermal's "Chorus" effect (MKUChorus_*).
// Header-only, JUCE-free, C++20. See FINDINGS.md (fx-modulation) for the measurements behind every constant.
//
// Signal flow (per channel, identical code for L and R):
//   x *= inGain                                         (linear ramp, 5 ms)
//   s  = x + feedback * yPrev                           (positive feedback, taken from the delay output)
//   write s into a circular buffer of N = 5 ms samples  (240 @ 48 kHz) - NOT padded: see quirk below
//   y  = CatmullRom read at D = (3 + 2 * depth * sin(2*pi*(phase + chOffset))) ms
//   out = (x * (1 - mix) + y * mix) * outGain           (linear dry/wet law, mix ramp 5 ms)
// One LFO (float32 phase accumulator in cycles, starts at 0, delay rises first); the right channel reads
// the same LFO shifted by +width cycles (Width 0..0.5 -> 0..180 degrees).
// Quirk copied from Thermal: at depth ~1 the read position reaches 239..240 samples, the 4-point kernel then
// reads past the end of the 240-sample buffer and wraps onto the newest samples (delay ~0).
#pragma once
#include "FxModCommon.h"
#include <vector>

namespace dt {

struct ChorusParams
{
    float inputGainDb  = 0.0f;   // -60..+6 dB
    float outputGainDb = 0.0f;   // -60..+6 dB
    float rateHz       = 1.0f;   // 0.001..50 Hz
    float depth        = 0.5f;   // 0..1  (modulation = +-2 ms * depth around 3 ms)
    float feedback     = 0.0f;   // 0..0.9
    float width        = 0.0f;   // 0..0.5 cycles of LFO phase offset on the right channel (display 0..100 %)
    float wetDry       = 1.0f;   // 0..1  (linear: out = dry*(1-w) + wet*w)
};

class Chorus
{
public:
    static constexpr double kBaseMs = 3.0, kModMs = 2.0, kBufferMs = 5.0;

    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sr = sampleRate;
        bufLen = (int) std::ceil (kBufferMs * 0.001 * sampleRate);   // 240 @ 48 kHz (no guard samples, see quirk)
        if (bufLen < 4) bufLen = 4;
        for (auto& b : buf) b.assign ((size_t) bufLen, 0.0f);
        for (auto* s : { &inGain, &outGain, &mix, &rate, &fb }) s->reset (sr, 0.005);   // 5 ms ramps
        for (auto* s : { &depth, &width }) s->reset (sr, 0.05);                         // 50 ms ramps
        reset();
    }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        writePos = 0; yPrev[0] = yPrev[1] = 0.0f; phase = {};
        first = true;
    }

    // Optional: start the smoothers from these values instead of snapping to the first setParams().
    // Thermal is prepared with its default parameters and then ramps to the loaded state; pass ChorusParams{}
    // (= Thermal defaults) to reproduce a freshly loaded instance sample-exactly.
    void startFrom (const ChorusParams& p) { apply (p, true); first = false; }

    void setParams (const ChorusParams& p) { apply (p, first); first = false; }

    void process (float* left, float* right, int numSamples) noexcept
    {
        float* io[2] = { left, right };
        const float srf = (float) sr;
        for (int i = 0; i < numSamples; ++i)
        {
            const float gIn = inGain.next(), gOut = outGain.next(), w = mix.next();
            const float r = rate.next(), d = depth.next(), wd = width.next(), f = fb.next();
            const float ph = phase.advance (r / srf);
            for (int c = 0; c < 2; ++c)
            {
                if (io[c] == nullptr) continue;
                const float x = io[c][i] * gIn;
                float* b = buf[c].data();
                b[writePos] = x + f * yPrev[c];
                double pc = (double) ph + (c == 1 ? (double) wd : 0.0);
                const double lfo = std::sin (fxmod::kTwoPi * pc);
                const float delay = (float) ((kBaseMs + kModMs * (double) d * lfo) * 0.001 * sr);
                const int di = (int) std::floor (delay);
                const float t = delay - (float) di;
                auto at = [&] (int k) { int idx = (writePos - di + k) % bufLen; if (idx < 0) idx += bufLen; return b[idx]; };
                const float y = fxmod::catmullRom (at (1), at (0), at (-1), at (-2), t);
                yPrev[c] = y;
                io[c][i] = (x * (1.0f - w) + y * w) * gOut;
            }
            if (++writePos >= bufLen) writePos = 0;
        }
    }

private:
    void apply (const ChorusParams& p, bool snap)
    {
        auto set = [snap] (fxmod::LinearSmoothed& s, float v) { if (snap) s.setCurrentAndTarget (v); else s.setTarget (v); };
        set (inGain, fxmod::dbToGain (p.inputGainDb));
        set (outGain, fxmod::dbToGain (p.outputGainDb));
        set (mix, std::clamp (p.wetDry, 0.0f, 1.0f));
        set (rate, std::clamp (p.rateHz, 0.001f, 50.0f));
        set (depth, std::clamp (p.depth, 0.0f, 1.0f));
        set (width, std::clamp (p.width, 0.0f, 0.5f));
        set (fb, std::clamp (p.feedback, 0.0f, 0.9f));
    }

    double sr = 48000.0;
    int bufLen = 240, writePos = 0;
    std::vector<float> buf[2];
    float yPrev[2] {};
    fxmod::CyclePhase phase;
    fxmod::LinearSmoothed inGain, outGain, mix, rate, depth, width, fb;
    bool first = true;
};

} // namespace dt
