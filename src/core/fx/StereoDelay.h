// dt::StereoDelay -- clone of Output Thermal's "Delay" effect (MKUStereoDelay_*), header-only, JUCE-free, C++20.
//
// Measured behaviour (see FINDINGS.md, section "Stereo Delay"):
//  * per channel: integer-sample (non-interpolated) delay line, read BEFORE write:
//        y = HP(LP(line[w - d]))                 (2nd-order Butterworth LP then HP, DF1, in the loop)
//        STEREO   : line_c[w] = x_c + sat(fb * y_c)
//        PING PONG: lineL[w] = xL + sat(fb * yR[n-1]);  lineR[w] = sat(yL)   (input R is ignored!)
//        sat(x) = x (27 + x^2) / (27 + 9 x^2)    (Pade tanh approximation, unclamped)
//  * delay time: a juce::SmoothedValue<float, Linear> in SECONDS with a 0.15 s ramp (floor(0.15 sr) steps).
//    Synced mode: every block the smoother is re-targeted twice (first to the raw mapped Time value =
//    the division INDEX, then to the target in seconds), which restarts the ramp each block:
//    step = (T - s) / (0.15 sr) per block -> an exponential (one-pole, tau = 0.15 s) approach whose float
//    rounding makes s stall a few samples away from T (1/8 @120 bpm reads 12006 samples instead of 12000).
//    Free mode: a plain linear 0.15 s ramp to the new value (no stall).
//    Read offset d = ceil(s * sr) (double).  Synced target = floor(beats*60/bpm * sr)/sr; free = seconds.
//    The smoother starts at 10.0 (the default mapped Time value, read as seconds) after prepare(): in
//    synced mode the wet signal therefore fades in from a 10 s delay over ~0.5..1.5 s after loading.
//    In free mode it starts on the target (snaps when the mapping mode changes).
//  * in/out gain, dry/wet and feedback: linear ramps of 5 ms; LP/HP cutoffs: linear ramps (in Hz) of
//    10 ms with per-sample coefficient updates.
//  * out = (x_in * (1 - mix) + y * mix) * outGain,  x_in = x * inGain.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace dt {

struct StereoDelayParams {
    float  inputGainDb  = 0.0f;     // -60..+6 dB
    float  outputGainDb = 0.0f;     // -60..+6 dB
    float  type         = 1.0f;     // mapped Type (0..1); PING PONG only when (int) type == 1, i.e. exactly 1.0
    bool   tempoSync    = true;     // node property TimeCurrentMapping (1 = synced, the default)
    float  time         = 10.0f;    // synced: mapped Time 0..18 (division index, rounded half-up);
                                    // free: seconds, 0.001..2.5 (= 0.001 + 2.499 * u^(10/3))
    double bpm          = 120.0;    // host tempo (only used when tempoSync)
    float  feedback     = 0.25f;    // 0..1 (1 = no loss apart from the filters and sat)
    float  lowPassHz    = 20000.0f; // 20..20000 (u^4 taper)
    float  highPassHz   = 60.0f;    // 20..20000 (u^4 taper)
    float  dryWet       = 0.33f;    // 0..1 linear
};

namespace sdetail {
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

// 2nd-order Butterworth (Q = 1/sqrt2, bilinear) in direct form 1; coefficients in double, float state.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void reset() { x1 = x2 = y1 = y2 = 0; }
    float process (float x) noexcept {
        const float y = (float) (b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2);
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
    void setLowPass (double sr, double f) {
        const double n = 1.0 / std::tan (3.14159265358979323846 * f / sr), n2 = n * n, invQ = 1.4142135623730951;
        const double c1 = 1.0 / (1.0 + invQ * n + n2);
        b0 = c1; b1 = c1 * 2.0; b2 = c1; a1 = c1 * 2.0 * (1.0 - n2); a2 = c1 * (1.0 - invQ * n + n2);
    }
    void setHighPass (double sr, double f) {
        const double n = std::tan (3.14159265358979323846 * f / sr), n2 = n * n, invQ = 1.4142135623730951;
        const double c1 = 1.0 / (1.0 + invQ * n + n2);
        b0 = c1; b1 = c1 * -2.0; b2 = c1; a1 = c1 * 2.0 * (n2 - 1.0); a2 = c1 * (1.0 - invQ * n + n2);
    }
};
inline float sat (float x) { return x * (27.0f + x * x) / (27.0f + 9.0f * x * x); }
} // namespace sdetail

class StereoDelay {
public:
    static constexpr float  kInitialTime = 10.0f;   // s, synced-mode smoother start value after prepare()
    static constexpr float  kSmoothTime  = 0.15f;   // s, delay-time one-pole time constant
    // s, the read offset is clamped here. Thermal clamps at 48 s (1 Bar below 5 bpm), which costs 18 MB per
    // instance at 48 kHz; DisTitten keeps 16 s (1 Bar down to 15 bpm), the only difference being at slower tempos.
    static constexpr double kMaxDelay    = 16.0;

    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sr = sampleRate;
        maxD = (int) std::floor (kMaxDelay * sampleRate);
        size = maxD + 2;
        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        const int s5 = (int) std::floor (0.005 * sampleRate), s10 = (int) std::floor (0.01 * sampleRate);
        for (auto* s : { &inG, &outG, &mix, &fbS }) s->reset (s5);
        for (auto* s : { &lpS, &hpS }) s->reset (s10);
        tS.reset ((int) std::floor ((double) kSmoothTime * sampleRate));
        first = true;
        reset();
    }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& f : lp) f.reset();
        for (auto& f : hp) f.reset();
        w = 0; prevR = 0.0f;
        tS.setNow (kInitialTime);
        lastSync = true;
    }

    void setParams (const StereoDelayParams& p)
    {
        P = p;
        const float ig = sdetail::dbToGain (p.inputGainDb), og = sdetail::dbToGain (p.outputGainDb);
        if (first) {
            inG.setNow (ig); outG.setNow (og); mix.setNow (p.dryWet); fbS.setNow (p.feedback);
            lpS.setNow (p.lowPassHz); hpS.setNow (p.highPassHz);
            setCoeffs (p.lowPassHz, p.highPassHz);
        } else {
            inG.setTarget (ig); outG.setTarget (og); mix.setTarget (p.dryWet); fbS.setTarget (p.feedback);
            lpS.setTarget (p.lowPassHz); hpS.setTarget (p.highPassHz);
        }
        // free-time mode: the smoother snaps to the target (measured at load; assumed on every mode switch)
        if (p.tempoSync != lastSync || (first && ! p.tempoSync)) tS.setNow (p.tempoSync ? kInitialTime : targetSeconds());
        lastSync = p.tempoSync;
        first = false;
    }

    static double divisionBeats (int idx)   // quarter notes
    {
        static const double b[19] = { 1.0/24, 1.0/16, 3.0/32, 1.0/12, 1.0/8, 3.0/16, 1.0/6, 1.0/4, 3.0/8,
                                      1.0/3, 1.0/2, 3.0/4, 2.0/3, 1.0, 1.5, 4.0/3, 2.0, 3.0, 4.0 };
        return b[std::clamp (idx, 0, 18)];
    }

    float targetSeconds() const
    {
        if (P.tempoSync) {
            const int idx = (int) std::floor (P.time + 0.5f);
            const float secs = (float) (divisionBeats (idx) * 60.0 / P.bpm);
            const float fsr = (float) sr;
            return std::floor (secs * fsr) / fsr;      // whole samples
        }
        return P.time;
    }

    void process (float* left, float* right, int n) noexcept
    {
        const float T = targetSeconds();
        if (P.tempoSync) tS.setTarget (P.time);       // Thermal's quirk: the raw index first ...
        tS.setTarget (T);                              // ... then seconds -> the ramp restarts every block
        const bool ping = (int) P.type == 1;
        for (int i = 0; i < n; ++i) {
            const float ig = inG.next(), og = outG.next(), m = mix.next(), fbk = fbS.next();
            if (lpS.smoothing() || hpS.smoothing()) {
                const float lv = lpS.next(), hv = hpS.next();
                setCoeffs (lv, hv);
            }
            const float sTime = tS.next();
            const int d = std::clamp ((int) std::ceil ((double) sTime * sr), 1, maxD);
            int r = w - d; if (r < 0) r += size;
            const float xL = left[i] * ig, xR = right[i] * ig;
            const float yL = hp[0].process (lp[0].process (buf[0][(size_t) r]));
            const float yR = hp[1].process (lp[1].process (buf[1][(size_t) r]));
            if (! ping) {
                buf[0][(size_t) w] = xL + sdetail::sat (fbk * yL);
                buf[1][(size_t) w] = xR + sdetail::sat (fbk * yR);
            } else {
                buf[0][(size_t) w] = xL + sdetail::sat (fbk * prevR);
                buf[1][(size_t) w] = sdetail::sat (yL);
                prevR = yR;
            }
            left[i]  = (xL * (1.0f - m) + yL * m) * og;
            right[i] = (xR * (1.0f - m) + yR * m) * og;
            if (++w >= size) w = 0;
        }
    }

private:
    void setCoeffs (float lv, float hv)
    {
        if (lv != curLp) { for (auto& f : lp) f.setLowPass (sr, lv); curLp = lv; }
        if (hv != curHp) { for (auto& f : hp) f.setHighPass (sr, hv); curHp = hv; }
    }

    double sr = 48000;
    int size = 0, w = 0, maxD = 0;
    std::vector<float> buf[2];
    sdetail::Biquad lp[2], hp[2];
    float curLp = -1, curHp = -1, prevR = 0;
    sdetail::LinSmooth inG, outG, mix, fbS, lpS, hpS, tS;
    StereoDelayParams P;
    bool first = true, lastSync = true;
};

} // namespace dt
