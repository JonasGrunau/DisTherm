// dt::Reverb -- clone of Output Thermal's "Reverb" effect (MKUReverb_*), header-only, JUCE-free, C++20.
// Bit-exact model (null = 0.0 against Thermal at 22.05..192 kHz, any block size, with automation):
//  * TWO INDEPENDENT juce::Reverb (Freeverb) instances, processMono() on each channel, both using the
//    LEFT tunings (combs 1116..1617, allpasses 556..225 samples @44.1k, scaled by (int)sr/44100) --
//    no stereo spread, and Width only scales the wet gain (wet1 = 1.5 * (1 + width)): no decorrelation.
//  * juce::Reverb::Parameters: roomSize = Size, damping = Damping, width = Width, freezeMode = Freeze
//    (>= 0.5), wetLevel = 1, dryLevel = 0; JUCE's own 10 ms linear smoothing of damping/feedback/wet gain.
//  * Thermal's wrapper: x_in = x * inGain; y = (x_in * (1 - mix) + reverb(x_in) * mix) * outGain with
//    inGain / outGain / mix as juce::SmoothedValue<float> linear, 5 ms ramps.
//  * float arithmetic without FMA contraction (needed for bit exactness).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace dt {

struct ReverbParams {
    float inputGainDb  = 0.0f;   // -60..+6
    float outputGainDb = 0.0f;   // -60..+6
    float size    = 0.5f;        // 0..1
    float damping = 0.5f;        // 0..1
    float width   = 0.5f;        // 0..1
    float freeze  = 0.0f;        // 0/1 (>= 0.5 frozen)
    float dryWet  = 0.5f;        // 0..1
};

namespace detail {
struct LinSmooth {   // == juce::SmoothedValue<float, Linear>
    float cur = 0, tgt = 0, step = 0; int steps = 0, cd = 0;
    void reset (int s) { steps = s; cur = tgt; cd = 0; }
    void setNow (float v) { cur = tgt = v; cd = 0; }
    void setTarget (float v) {
        if (v == tgt) return;
        if (steps <= 0) { setNow (v); return; }
        tgt = v; cd = steps; step = (tgt - cur) / (float) cd;
    }
    bool smoothing() const { return cd > 0; }
    float next() {
        if (cd <= 0) return tgt;
        --cd;
        if (cd > 0) cur += step; else cur = tgt;
        return cur;
    }
};
inline float dbToGain (float db) { return db > -100.0f ? std::pow (10.0f, db * 0.05f) : 0.0f; }
} // namespace detail

class Reverb {
public:
    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sr = sampleRate;
        static const short combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const short apT[4]   = { 556, 441, 341, 225 };
        const int isr = (int) sampleRate;
        for (auto& ch : chans) {
            for (int i = 0; i < 8; ++i) ch.comb[i].setSize ((isr * combT[i]) / 44100);
            for (int i = 0; i < 4; ++i) ch.ap[i].setSize ((isr * apT[i]) / 44100);
        }
        const int s10 = (int) std::floor (0.01 * sampleRate);
        for (auto* s : { &damp, &fb, &wetG }) s->reset (s10);
        const int s5 = (int) std::floor (0.005 * sampleRate);
        for (auto* s : { &inG, &outG, &mix }) s->reset (s5);
        first = true;
        reset();
    }

    void reset()
    {
        for (auto& ch : chans) {
            for (auto& c : ch.comb) c.clear();
            for (auto& a : ch.ap) a.clear();
        }
    }

    void setParams (const ReverbParams& p)
    {
        const bool frozen = p.freeze >= 0.5f;
        const float wet = 1.0f * 3.0f;                     // juce wetLevel = 1
        const float w1 = 0.5f * wet * (1.0f + p.width);
        gain = frozen ? 0.0f : 0.015f;
        const float dTarget = frozen ? 0.0f : p.damping * 0.4f;
        const float fTarget = frozen ? 1.0f : p.size * 0.28f + 0.7f;
        const float ig = detail::dbToGain (p.inputGainDb), og = detail::dbToGain (p.outputGainDb);
        if (first) {
            first = false;
            wetG.setNow (w1); damp.setNow (dTarget); fb.setNow (fTarget);
            inG.setNow (ig); outG.setNow (og); mix.setNow (p.dryWet);
        } else {
            wetG.setTarget (w1); damp.setTarget (dTarget); fb.setTarget (fTarget);
            inG.setTarget (ig); outG.setTarget (og); mix.setTarget (p.dryWet);
        }
    }

    void process (float* left, float* right, int n) noexcept
    {
        float* io[2] = { left, right };
        for (int i = 0; i < n; ++i) {
            const float d = damp.next(), f = fb.next(), wg = wetG.next();
            const float ig = inG.next(), og = outG.next(), m = mix.next();
            for (int c = 0; c < 2; ++c) {
                const float x = io[c][i] * ig;
                const float input = x * gain;
                float out = 0.0f;
                for (auto& cb : chans[c].comb) out += cb.process (input, d, f);
                for (auto& a : chans[c].ap) out = a.process (out);
                const float r = out * wg;
                io[c][i] = (x * (1.0f - m) + r * m) * og;
            }
        }
    }

private:
    struct Comb {
        std::vector<float> buf; int size = 0, idx = 0; float last = 0;
        void setSize (int s) { buf.assign ((size_t) s, 0.0f); size = s; idx = 0; last = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); last = 0; }
        float process (float input, float damp, float fbk) noexcept {
            const float output = buf[(size_t) idx];
            last = (output * (1.0f - damp)) + (last * damp);
            const float temp = input + (last * fbk);
            buf[(size_t) idx] = temp;
            idx = (idx + 1) % size;
            return output;
        }
    };
    struct AllPass {
        std::vector<float> buf; int size = 0, idx = 0;
        void setSize (int s) { buf.assign ((size_t) s, 0.0f); size = s; idx = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
        float process (float input) noexcept {
            const float bv = buf[(size_t) idx];
            const float temp = input + (bv * 0.5f);
            buf[(size_t) idx] = temp;
            idx = (idx + 1) % size;
            return bv - input;
        }
    };
    struct Chan { Comb comb[8]; AllPass ap[4]; };

    double sr = 48000;
    Chan chans[2];
    detail::LinSmooth damp, fb, wetG, inG, outG, mix;
    float gain = 0.015f;
    bool first = true;
};

} // namespace dt
