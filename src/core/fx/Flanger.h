// dt::Flanger - black-box reconstruction of Output Thermal's "Flanger" effect (MKUFlanger_*).
// Header-only, JUCE-free, C++20. See FINDINGS.md (fx-modulation) for the measurements behind every constant.
//
// Signal flow (per channel, identical code for L and R, one shared mono LFO):
//   x *= inGain
//   Teff = min(1, Time - ModRange * sin(phase))             (LFO acts on the normalised Time knob)
//   D    = (0.2 + 9.8 * (1 - Teff)^3) ms                      (Time 0 -> 10 ms, Time 1 -> 0.2 ms; Teff < 0 allowed)
//   d    = CatmullRom(buffer, D)                              (read before write: loop delay is exactly D)
//   y    = R * (y1 + d - d1),  R = 1/(1 + 2*pi*20/sr)         (backward-Euler 20 Hz high-pass INSIDE the loop)
//   buffer <- x + g * y,  g = (Type==1 ? -1 : +1) * 0.495 * (1 + Feedback)  (Feedback 0 -> |g| 0.495, 1 -> 0.99)
//   out  = (x * (1 - mix) + y * mix) * outGain
// UI type VINTAGE (Type 0) = positive feedback, MODERN (Type 1) = negative feedback; nothing else differs.
// Type is not smoothed and only the exact value 1 selects MODERN. All other parameters (incl. the LFO rate)
// are smoothed with a 5 ms linear ramp.
// LFO quirk copied from Thermal: the phase (float32 radians) is rescaled by f_prev/f_new whenever the smoothed
// rate changes (so phase*rate is preserved, not the phase), which makes the LFO jump/sweep quickly during a
// ModFreq ramp, and the rate smoother of a freshly prepared instance starts at 5 Hz.
#pragma once
#include "FxModCommon.h"
#include <vector>

namespace dt {

struct FlangerParams
{
    float inputGainDb  = 0.0f;   // -60..+6 dB
    float outputGainDb = 0.0f;   // -60..+6 dB
    float time         = 0.1f;   // 0..1  (0 = 10 ms, 1 = 0.2 ms, cubic)
    float feedback     = 0.0f;   // 0..1  (loop gain 0.495 .. 0.99)
    float type         = 0.0f;   // 0..1  (VINTAGE = +feedback unless exactly 1 = MODERN = -feedback)
    float modFreq      = 0.1f;   // 0..1  (LFO rate = 0.1 + 4.9 * modFreq Hz)
    float modRange     = 0.0f;   // 0..1  (LFO depth in units of the Time knob)
    float dryWet       = 0.33f;  // 0..1  (linear)
};

class Flanger
{
public:
    static constexpr double kMinMs = 0.2, kSpanMs = 9.8, kMaxMs = 0.2 + 9.8 * 8.0;   // Teff in [-1, 1]
    static constexpr double kHpHz = 20.0;
    static constexpr float kLoop = 0.495f;
    float initialRateHz = 5.0f;                      // rate smoother start value of a fresh Thermal instance
    static constexpr float kTwoPiF = 6.283185307179586f;
    int startupHold = -1;                            // fresh instance: rate ramp starts 2 samples (@48k) late; -1 = auto
    int preRun = -1;                                 // fresh instance: LFO runs 4 samples (@48k) ahead of audio; -1 = auto
    float* debugDelay = nullptr;                     // optional: per-sample delay (samples) for analysis
    bool wrapToZero = false;                         // phase wrap rule (false: ph -= 2*pi), see FINDINGS

    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sr = sampleRate;
        bufLen = (int) std::ceil (kMaxMs * 0.001 * sr) + 8;
        for (auto& b : buf) b.assign ((size_t) bufLen, 0.0f);
        hpR = 1.0 / (1.0 + fxmod::kTwoPi * kHpHz / sr);
        for (auto* s : { &inGain, &outGain, &mix, &timeS, &fbS, &signS, &rateS, &rangeS }) s->reset (sr, 0.005);
        reset();
    }

    void setRateRampSteps (int n) { rateS.stepsToTarget = n; }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        writePos = 0;
        for (int c = 0; c < 2; ++c) { hpX1[c] = hpY1[c] = 0.0; yPrev[c] = 0.0f; }
        ph = 0.0f; fPrev = -1.0f; holdCount = 0; pendingActive = false; preRunLeft = 0;
        first = true;
    }

    void startFrom (const FlangerParams& init)
    {
        apply (init, true);
        rateS.setCurrentAndTarget (initialRateHz);
        fPrev = initialRateHz;
        holdCount = startupHold >= 0 ? startupHold : (int) std::lround (2.0 * sr / 48000.0);
        pendingActive = true;
        preRunLeft = preRun >= 0 ? preRun : (int) std::lround (4.0 * sr / 48000.0);
        first = false;
    }
    void setParams (const FlangerParams& prm)
    {
        const bool snap = first;
        apply (prm, snap);
        if (snap) fPrev = rateS.target;
        if (holdCount > 0 || pendingActive)
        {
            pendingRate = rateS.target; pendingActive = true;
            rateS.setCurrentAndTarget (initialRateHz);
        }
        first = false;
        for (; preRunLeft > 0; --preRunLeft) (void) lfoStep();
    }

    // LFO step: float32 phase in radians; rescaled by the rate ratio when the smoothed rate moves (Thermal quirk:
    // preserves phase*rate, not the phase), then used, then advanced by float(2*pi)*f/sr and wrapped.
    double lfoStep() noexcept
    {
        if (pendingActive) { if (holdCount == 0) { rateS.setTarget (pendingRate); pendingActive = false; } else --holdCount; }
        const float f = rateS.next();
        if (f != fPrev) ph = ph * (fPrev / f);
        fPrev = f;
        if (ph >= kTwoPiF) ph = wrapToZero ? 0.0f : ph - kTwoPiF * std::floor (ph / kTwoPiF);
        const double v = std::sin ((double) ph);
        ph = ph + kTwoPiF * f / (float) sr;
        if (ph >= kTwoPiF) ph = wrapToZero ? 0.0f : ph - kTwoPiF;
        return v;
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        float* io[2] = { left, right };
        for (int i = 0; i < numSamples; ++i)
        {
            const float gIn = inGain.next(), gOut = outGain.next(), w = mix.next();
            const float tm = timeS.next(), fb = fbS.next(), sg = signS.next(), mr = rangeS.next();
            const double lfo = lfoStep();

            double teff = (double) tm - (double) mr * lfo;
            if (teff > 1.0) teff = 1.0;
            const double om = 1.0 - teff;
            const double delay = (kMinMs + kSpanMs * om * om * om) * 0.001 * sr;
            if (debugDelay) debugDelay[i] = (float) delay;
            const int di = (int) std::floor (delay);
            const float t = (float) (delay - di);
            const float g = sg * kLoop * (1.0f + fb);

            for (int c = 0; c < 2; ++c)
            {
                if (io[c] == nullptr) continue;
                const float x = io[c][i] * gIn;
                const float* b = buf[c].data();
                auto at = [&] (int k) { int idx = (writePos - di + k) % bufLen; if (idx < 0) idx += bufLen; return b[idx]; };
                // read position: newest sample = writePos - 1 (read happens before this sample is written)
                const float d = fxmod::catmullRom (at (1), at (0), at (-1), at (-2), t);
                const double y = hpR * (hpY1[c] + (double) d - hpX1[c]);
                hpX1[c] = d; hpY1[c] = y;
                buf[c][(size_t) writePos] = x + g * (float) y;
                io[c][i] = (x * (1.0f - w) + (float) y * w) * gOut;
            }
            if (++writePos >= bufLen) writePos = 0;
        }
    }

private:
    void apply (const FlangerParams& prm, bool snap)
    {
        auto set = [snap] (fxmod::LinearSmoothed& s, float v) { if (snap) s.setCurrentAndTarget (v); else s.setTarget (v); };
        set (inGain, fxmod::dbToGain (prm.inputGainDb));
        set (outGain, fxmod::dbToGain (prm.outputGainDb));
        set (mix, std::clamp (prm.dryWet, 0.0f, 1.0f));
        set (timeS, std::clamp (prm.time, 0.0f, 1.0f));
        set (fbS, std::clamp (prm.feedback, 0.0f, 1.0f));
        signS.setCurrentAndTarget (prm.type >= 1.0f ? -1.0f : 1.0f);   // floored, switches instantly
        set (rateS, 0.1f + 4.9f * std::clamp (prm.modFreq, 0.0f, 1.0f));
        set (rangeS, std::clamp (prm.modRange, 0.0f, 1.0f));
    }

    double sr = 48000.0, hpR = 1.0;
    int bufLen = 0, writePos = 0;
    std::vector<float> buf[2];
    double hpX1[2] {}, hpY1[2] {};
    float yPrev[2] {};
    float ph = 0.0f, fPrev = -1.0f, pendingRate = 0.0f;
    int holdCount = 0, preRunLeft = 0;
    bool pendingActive = false;
    fxmod::LinearSmoothed inGain, outGain, mix, timeS, fbS, signS, rateS, rangeS;
    bool first = true;
};

} // namespace dt
