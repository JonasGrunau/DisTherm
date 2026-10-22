// dt::FrequencyShifter -- clone of Output Thermal's "Frequency Shifter" effect (MKUFrequencyShifter_*).
// Header-only, JUCE-free, C++20.
//
// Measured behaviour (see FINDINGS.md, section "Frequency Shifter"):
//  * single-sideband UP-shifter per channel: an IIR Hilbert pair of two 6th-order allpass chains
//    (first-order sections, analog prototype poles below, bilinear transform without prewarp at the
//    host rate), I/Q mixed with a free-running quadrature oscillator:
//        wet = I(x) * sin(phi) + Q(x) * cos(phi);   phi += 2 pi f / sr  (after use; phi(0) = 0 at prepare)
//    Sideband rejection ~56 dB from 20 Hz to 10 kHz (quadrature error +-0.19 deg), falling off below 20 Hz
//    and above 15 kHz.  No latency (the dry path is not delayed).
//  * f_L = 10000 * F^3 Hz, f_R = 10000 * clamp(F + Offset, 0, 1)^3 Hz  (F = Frequency 0..1, Offset -0.25..0.25)
//    F is smoothed linearly over 10 ms in the normalised domain (starting from the default 0.5 after
//    prepare(), so a loaded value glides in); Offset is NOT smoothed.
//  * out = (x_in (1 - mix) + wet * mix) * outGain, x_in = x * inGain; gains and mix: 5 ms linear ramps.
#pragma once
#include <algorithm>
#include <cmath>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace dt {

struct FrequencyShifterParams {
    float inputGainDb  = 0.0f;    // -60..+6 dB
    float outputGainDb = 0.0f;    // -60..+6 dB
    float frequency    = 0.5f;    // 0..1 (shift = 10000 * frequency^3 Hz, upward only; 0.5 = 1250 Hz)
    float offset       = 0.0f;    // -0.25..0.25, added to `frequency` for the RIGHT channel only (clamped 0..1)
    float dryWet       = 0.15f;   // 0..1 linear
};

class FrequencyShifter {
public:
    // analog prototype poles (rad/s) of the two allpass branches (fitted; reciprocal pairs around 2980 rad/s)
    static constexpr double kPolesI[6] = { 261088.7, 34382.05, 8447.308, 2105.787, 524.694, 118.042 };
    static constexpr double kPolesQ[6] = { 75257.75, 16929.07, 4218.596, 1051.546, 258.366, 34.015 };
    static constexpr float kDefaultFrequency = 0.5f;

    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sr = sampleRate;
        for (int k = 0; k < 6; ++k) {
            cI[k] = (float) ((2.0 * sr - kPolesI[k]) / (2.0 * sr + kPolesI[k]));
            cQ[k] = (float) ((2.0 * sr - kPolesQ[k]) / (2.0 * sr + kPolesQ[k]));
        }
        const int s5 = (int) std::floor (0.005 * sr), s10 = (int) std::floor (0.01 * sr);
        inG.reset (s5); outG.reset (s5); mix.reset (s5);
        freqS.reset (s10);
        freqS.setNow (kDefaultFrequency);       // Thermal: a loaded value glides in from the default
        offset = 0.0f;
        first = true;
        reset();
    }

    void reset()
    {
        for (auto& c : ch) {
            for (auto& s : c.sI) s = {};
            for (auto& s : c.sQ) s = {};
            c.phase = 0.0;
        }
    }

    void setParams (const FrequencyShifterParams& p)
    {
        const float ig = dbToGain (p.inputGainDb), og = dbToGain (p.outputGainDb);
        if (first) { inG.setNow (ig); outG.setNow (og); mix.setNow (p.dryWet); first = false; }
        else { inG.setTarget (ig); outG.setTarget (og); mix.setTarget (p.dryWet); }
        freqS.setTarget (p.frequency);
        offset = p.offset;                       // not smoothed
    }

    void process (float* left, float* right, int n) noexcept
    {
        float* io[2] = { left, right };
        const double twoPiOverSr = 2.0 * 3.14159265358979323846 / sr;
        constexpr double twoPi = 6.283185307179586;
        for (int i = 0; i < n; ++i) {
            const float ig = inG.next(), og = outG.next(), m = mix.next();
            const float u = freqS.next();
            const float uc[2] = { u, std::clamp (u + offset, 0.0f, 1.0f) };
            for (int c = 0; c < 2; ++c) {
                auto& st = ch[c];
                const float x = io[c][i] * ig;
                float vi = x, vq = x;
                for (int k = 0; k < 6; ++k) vi = st.sI[k].process (vi, cI[k]);
                for (int k = 0; k < 6; ++k) vq = st.sQ[k].process (vq, cQ[k]);
                const double f = (double) (10000.0f * (uc[c] * uc[c] * uc[c]));
                const float wet = (float) ((double) vi * std::sin (st.phase) + (double) vq * std::cos (st.phase));
                st.phase += f * twoPiOverSr;
                if (st.phase >= twoPi) st.phase -= twoPi;
                io[c][i] = (x * (1.0f - m) + wet * m) * og;
            }
        }
    }

private:
    static float dbToGain (float db) { return db > -100.0f ? std::pow (10.0f, db * 0.05f) : 0.0f; }
    struct LinSmooth {   // == juce::SmoothedValue<float, Linear>
        float cur = 0, tgt = 0, step = 0; int steps = 0, cd = 0;
        void reset (int s) { steps = s; cur = tgt; cd = 0; }
        void setNow (float v) { cur = tgt = v; cd = 0; }
        void setTarget (float v) {
            if (v == tgt) return;
            if (steps <= 0) { setNow (v); return; }
            tgt = v; cd = steps; step = (tgt - cur) / (float) cd;
        }
        float next() {
            if (cd <= 0) return tgt;
            --cd;
            if (cd > 0) cur += step; else cur = tgt;
            return cur;
        }
    };
    struct AP1 {   // first-order allpass (-c + z^-1) / (1 - c z^-1)
        float x1 = 0, y1 = 0;
        float process (float x, float c) noexcept { const float y = -c * x + x1 + c * y1; x1 = x; y1 = y; return y; }
    };
    struct Chan { AP1 sI[6], sQ[6]; double phase = 0.0; };

    double sr = 48000;
    float cI[6] {}, cQ[6] {};
    float offset = 0.0f;
    Chan ch[2];
    LinSmooth inG, outG, mix, freqS;
    bool first = true;
};

} // namespace dt
