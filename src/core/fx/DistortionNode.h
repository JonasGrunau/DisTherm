// DistortionNode.h -- Output Thermal "MKUDustDistoNode_Stage_N" (one distortion stage), reproduced by black-box
// measurement.  Header-only, JUCE-free C++20.  Every constant and how it was measured: see FINDINGS (distortion-node).
//
// Signal flow (per channel; the two detectors are stereo-linked):
//
//   x --*drive (20 ms lin ramp)--> 2x up (JUCE FIR equiripple half-band, 75 taps, odd phase = delayed input) = u
//        env  = peak detector on (|uL|+|uR|)/2   attack 12.5 ms, release RELEASE   (tau = 1.8205 * time)
//        gate = env > 1e-4  (both FOLLOW modes)
//        w    = delay line (2x rate, linear interpolation, T * 2fs samples, 30 ms linear glide on T)
//        pre  = fb (10 ms ramp) * w
//        lim  = peak detector on (|preL|+|preR|)/2, attack 100 ms, release 1 s (tau = time * fs / 2pi);
//               gr = min(1, 0.3162 / lim)                                      (feedback limited to -10 dB)
//        s    = u + gate * gr * B * pre       B = FOLLOW ? min(1, 5 env) : 1
//        v    = shape(type, s)  (Shapes.h, coefficients ramped linearly over 10 ms)
//        delay line <- A * v                  A = FOLLOW ? 1 : per-type gain (kTypeFbDb)
//   y = 2x down (51 taps)(v * typeFade) * out (20 ms ramp) * autoComp (20 ms ramp)
//
// Latency: exactly 31 samples at the base rate at any sample rate; a bypassed node is a pure 31-sample delay.
// Thermal's constant 93-sample chain delay is these 31 samples times the three stages.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "Shapes.h"

namespace dt
{

struct DistortionNodeParams
{
    int    type        = 0;              // floor(DistortionType mapped value): 0..18 (32/33 hidden), see shapeTypeFromNormalised
    double controls[4] = { 0, 0, 0, 0 }; // Control1..4 (SHAPE, DEPTH, FREQUENCY, CLIP), 0..1
    double driveDb     = 0.0;            // InputGain  mapped, -36..+36 dB
    double outDb       = 0.0;            // OutputGain mapped, -36..+36 dB
    bool   autoComp    = false;          // InputGainComp: ON only when the normalised value is >= 1.0
    double feedback    = 0.0;            // Feedback 0..1 (linear)
    double timeSec     = 0.03;           // Time mapped (0.0002..0.3 s), used when sync == false
    bool   sync        = false;          // node property TimeCurrentMapping == 1
    int    syncIndex   = 0;              // round(Time_normalised * 18): 1/64t .. 1 Bar (kSyncBeats)
    double bpm         = 120.0;          // host tempo, used when sync == true
    bool   follow      = true;           // FeedbackEnvFollow: ON for any normalised value > 0
    double release     = 0.1;            // FeedbackEnvFollowRelease mapped, 0.01..1 (tau = 1.8205 * release seconds)
    bool   bypassed    = false;          // node property Bypassed
};

namespace dn_detail
{
    inline constexpr double kPi = 3.14159265358979323846;   // (dn_detail::kPi is not portable to MSVC)

    // ---------------- exact port of JUCE 8 FilterDesign::designFIRLowpassHalfBandEquirippleMethod ---------------
    inline std::vector<double> partialHn (int n, double kp)
    {
        std::vector<double> alpha ((size_t) (2 * n + 1), 0.0);
        alpha[(size_t) (2 * n)] = 1.0 / std::pow (1.0 - kp * kp, n);
        if (n > 0)
            alpha[(size_t) (2 * n - 2)] = -(2 * n * kp * kp + 1) * alpha[(size_t) (2 * n)];
        if (n > 1)
            alpha[(size_t) (2 * n - 4)] = -(4 * n + 1 + (n - 1) * (2 * n - 1) * kp * kp) / (2.0 * n) * alpha[(size_t) (2 * n - 2)]
                                          - (2 * n + 1) * ((n + 1) * kp * kp + 1) / (2.0 * n) * alpha[(size_t) (2 * n)];
        for (int k = n; k >= 3; --k)
        {
            const double c1 = (3 * (n * (n + 2) - k * (k - 2)) + 2 * k - 3 + 2 * (k - 2) * (2 * k - 3) * kp * kp) * alpha[(size_t) (2 * k - 4)];
            const double c2 = (3 * (n * (n + 2) - (k - 1) * (k + 1)) + 2 * (2 * k - 1) + 2 * k * (2 * k - 1) * kp * kp) * alpha[(size_t) (2 * k - 2)];
            const double c3 = (n * (n + 2) - (k - 1) * (k + 1)) * alpha[(size_t) (2 * k)];
            const double c4 = (double) (n * (n + 2) - (k - 3) * (k - 1));
            alpha[(size_t) (2 * k - 6)] = -(c1 + c2 + c3) / c4;
        }
        std::vector<double> ai ((size_t) (2 * n + 2), 0.0);
        for (int k = 0; k <= n; ++k)
            ai[(size_t) (2 * k + 1)] = alpha[(size_t) (2 * k)] / (2.0 * k + 1.0);
        std::vector<double> hn ((size_t) (4 * n + 3), 0.0);
        for (int k = 0; k <= n; ++k)
        {
            hn[(size_t) (2 * n + 1 + (2 * k + 1))] = 0.5 * ai[(size_t) (2 * k + 1)];
            hn[(size_t) (2 * n + 1 - (2 * k + 1))] = 0.5 * ai[(size_t) (2 * k + 1)];
        }
        return hn;
    }

    inline double firMagnitude (const std::vector<float>& c, double fNorm)
    {
        double re = 0, im = 0;
        for (size_t n = 0; n < c.size(); ++n)
        {
            re += (double) c[n] * std::cos (-2.0 * dn_detail::kPi * fNorm * (double) n);
            im += (double) c[n] * std::sin (-2.0 * dn_detail::kPi * fNorm * (double) n);
        }
        return std::sqrt (re * re + im * im);
    }

    // float coefficients exactly like juce::dsp::FIR::Coefficients<float> from the JUCE design
    inline std::vector<float> designHalfBand (float normalisedTransitionWidth, float amplitudedB)
    {
        const double tw = normalisedTransitionWidth, adb = amplitudedB;
        const double wpT = (0.5 - tw) * dn_detail::kPi;
        const int n = (int) std::lround (std::ceil ((adb - 18.18840664 * wpT + 33.64775300) / (18.54155181 * wpT - 29.13196871)));
        const double kp = (n * wpT - 1.57111377 * n + 0.00665857) / (-1.01927560 * n + 0.37221484);
        const double A = (0.01525753 * n + 0.03682344 + 9.24760314 / (double) n) * kp + 1.01701407 + 0.73512298 / (double) n;
        const double B = (0.00233667 * n - 1.35418408 + 5.75145813 / (double) n) * kp + 1.02999650 - 0.72759508 / (double) n;
        const auto hn = partialHn (n, kp);
        const auto hnm = partialHn (n - 1, kp);
        const size_t diff = (hn.size() - hnm.size()) / 2;
        std::vector<double> hnm2 (hn.size(), 0.0);
        for (size_t i = 0; i < hnm.size(); ++i) hnm2[i + diff] = hnm[i];
        std::vector<float> c (hn.size());
        for (size_t i = 0; i < hn.size(); ++i) c[i] = (float) (A * hn[i] + B * hnm2[i]);
        double NN;
        if (n % 2 == 0)
            NN = 2.0 * firMagnitude (c, 0.5);
        else
        {
            const double w01 = std::sqrt (kp * kp + (1 - kp * kp) * std::pow (std::cos (dn_detail::kPi / (2.0 * n + 1.0)), 2.0));
            NN = std::abs (w01) > 1.0 ? 2.0 * firMagnitude (c, 0.5)
                                      : -2.0 * firMagnitude (c, std::acos (-w01) / (2.0 * dn_detail::kPi));
        }
        for (size_t i = 0; i < hn.size(); ++i) c[i] = (float) ((A * hn[i] + B * hnm2[i]) / NN);
        c[(size_t) (2 * n + 1)] = 0.5f;
        return c;
    }

    // ---------------- juce::SmoothedValue<..., Linear> equivalent ----------------
    struct LinearSmoother
    {
        double cur = 0, tgt = 0, step = 0;
        int steps = 0, countdown = 0;
        void reset (double rate, double seconds) { steps = (int) std::floor (rate * seconds); countdown = 0; cur = tgt; }
        void setCurrentAndTarget (double v) { cur = tgt = v; countdown = 0; }
        void setTarget (double v)
        {
            if (v == tgt) return;
            if (steps <= 0) { setCurrentAndTarget (v); return; }
            tgt = v; countdown = steps; step = (tgt - cur) / (double) countdown;
        }
        double next()
        {
            if (countdown <= 0) return tgt;
            --countdown;
            if (countdown > 0) cur += step; else cur = tgt;
            return cur;
        }
    };

    // juce::SmoothedValue<float, Linear>: same as above in float arithmetic (used for the delay time)
    struct LinearSmootherF
    {
        float cur = 0, tgt = 0, step = 0;
        int steps = 0, countdown = 0;
        void reset (double rate, double seconds) { steps = (int) std::floor (rate * seconds); countdown = 0; cur = tgt; }
        void setCurrentAndTarget (double v) { cur = tgt = (float) v; countdown = 0; }
        void setTarget (double vd)
        {
            const float v = (float) vd;
            if (v == tgt) return;
            if (steps <= 0) { setCurrentAndTarget (v); return; }
            tgt = v; countdown = steps; step = (tgt - cur) / (float) countdown;
        }
        float next()
        {
            if (countdown <= 0) return tgt;
            --countdown;
            if (countdown > 0) cur += step; else cur = tgt;
            return cur;
        }
    };

    inline double dbToGain (double db) { return std::pow (10.0, db * 0.05); }
} // namespace dn_detail

// Measured constants, grouped so tests can probe them.  Defaults = Thermal.
struct DistortionNodeTuning
{
    double gateThreshold    = 1.0e-4;               // feedback gated off while env <= this (both FOLLOW modes)
    double envAttackSec     = 0.0125;               // env detector attack (tau = kTau * time)
    double followScale      = 5.0;                  // FOLLOW: B = min(1, 5 * env)
    double limitThreshold   = 0.31622776601683794;  // feedback limiter threshold, -10 dB
    double limAttackSec     = 0.1;                  // limiter detector attack  (tau = time * fs / (2 pi), JUCE BallisticsFilter style)
    double limReleaseSec    = 1.0;                  // limiter detector release (same convention)
    double fbSmoothSec      = 0.01;                 // Feedback knob linear ramp
    double timeSmoothSec    = 0.03;                 // Time linear ramp (the delay glides -> pitch shift)
    double gainSmoothSec    = 0.02;                 // drive / output / auto-comp linear ramps
    double coefSmoothSec    = 0.01;                 // shape coefficients (from the controls) linear ramps
    double typeFadeSec      = 0.1;                  // type change: fade out, switch, fade in (each this long)
    double maxDelaySec      = 10.0;                 // delays >= this collapse to 1 sample (2x rate)
    int    switchClearDelay = 1;                    // the delay line is cleared this many 2x samples after the type switch
    double bypassFadeSec    = 0.02;                 // NOT measured (node property, not automatable): our click-free bypass
    double typeGainOverrideDb = 1e9;                // test hook: if < 1e8 replaces kTypeFbDb[type]
};

class DistortionNode
{
public:
    static constexpr int kLatency = 31;                         // base-rate samples
    static constexpr double kTau = 1.8204784532536746;          // env detector: tau = kTau * time (= 1/ln(sqrt 3); measured 1.8203)

    // per-type feedback gain A (dB) used when FOLLOW is off; measured to 0.001 dB
    static constexpr double kTypeFbDb[19] = {
        -5.0,   // 0  Light Shaper
        -10.0,  // 1  Heavy Shaper
        -10.0,  // 2  AM Shaper
        -10.0,  // 3  Wave Fold
        -10.0,  // 4  Sine Drive
        -5.0,   // 5  Spring Drive
        -5.0,   // 6  Linear Sine
        -10.0,  // 7  Asym Destroy
        -10.0,  // 8  Invert Fold
        0.5,    // 9  Exponential
        -0.25,  // 10 Soft Clip
        -5.0,   // 11 Triangle Clip
        -5.0,   // 12 Triangle Fold
        -5.0,   // 13 Single Fold
        -0.25,  // 14 Multi Fold
        -5.0,   // 15 Sine Fold
        -1.0,   // 16 Sine Shred
        -5.0,   // 17 Binary Shred
        -5.0    // 18 S Curve
    };
    static double typeFbDb (int type) noexcept
    {
        if (type >= 0 && type < 19) return kTypeFbDb[type];
        return type == 33 ? -10.0 : -5.0;   // hidden 32 (Light-Shaper-like) / 33 (Heavy-Shaper-like): NOT measured
    }

    // tempo-sync table (beats), index = round(Time_normalised * 18)
    static constexpr double kSyncBeats[19] = {
        1.0 / 16 * 2 / 3, 1.0 / 16, 1.0 / 16 * 1.5,
        1.0 / 8 * 2 / 3,  1.0 / 8,  1.0 / 8 * 1.5,
        1.0 / 4 * 2 / 3,  1.0 / 4,  1.0 / 4 * 1.5,
        1.0 / 2 * 2 / 3,  1.0 / 2,  1.0 / 2 * 1.5,
        1.0 * 2 / 3,      1.0,      1.0 * 1.5,
        2.0 * 2 / 3,      2.0,      2.0 * 1.5,
        4.0 };

    DistortionNodeTuning tuning;

    void prepare (double sampleRate, int maxBlockSize)
    {
        (void) maxBlockSize;
        sr = sampleRate;
        fs2 = 2.0 * sampleRate;
        // juce::dsp::Oversampling<float> (2 ch, 1 stage, filterHalfBandFIREquiripple, isMaxQuality = false)
        const auto cu = dn_detail::designHalfBand (0.06f, -70.0f);    // 75 taps, centre 0.5 at index 37
        const auto cd = dn_detail::designHalfBand (0.075f, -60.0f);   // 51 taps, centre 0.5 at index 25
        for (int k = 0; k < 38; ++k) cuEven[(size_t) k] = (double) cu[(size_t) (2 * k)];
        for (int k = 0; k < 26; ++k) cdEven[(size_t) k] = (double) cd[(size_t) (2 * k)];
        const int maxDelay2x = (int) std::ceil (tuning.maxDelaySec * fs2) + 8;
        int sz = 1;
        while (sz < maxDelay2x + 4) sz <<= 1;
        dlMask = sz - 1;
        for (auto& d : dl) d.assign ((size_t) sz, 0.0f);
        driveS.reset (sr, tuning.gainSmoothSec);
        outS.reset (sr, tuning.gainSmoothSec);
        compS.reset (sr, tuning.gainSmoothSec);
        bypS.reset (sr, tuning.bypassFadeSec);
        fbS.reset (fs2, tuning.fbSmoothSec);
        timeS.reset (fs2, tuning.timeSmoothSec);
        for (auto& c : coefS) c.reset (fs2, tuning.coefSmoothSec);
        envAtt = 1.0 - std::exp (-1.0 / (kTau * tuning.envAttackSec * fs2));
        limAtt = 1.0 - std::exp (-2.0 * dn_detail::kPi / (tuning.limAttackSec * fs2));
        limRel = 1.0 - std::exp (-2.0 * dn_detail::kPi / (tuning.limReleaseSec * fs2));
        fadeStep = 1.0 / std::max (1.0, tuning.typeFadeSec * fs2);
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            xHist[(size_t) c].fill (0.0);
            vHist[(size_t) c].fill (0.0);
            dryHist[(size_t) c].fill (0.0f);
            std::fill (dl[(size_t) c].begin(), dl[(size_t) c].end(), 0.0f);
        }
        xPos = vPos = dryPos = wp = 0;
        validLen = dlMask + 1;
        env = envLim = 0.0;
        driveS.setCurrentAndTarget (dn_detail::dbToGain (p.driveDb));
        outS.setCurrentAndTarget (dn_detail::dbToGain (p.outDb));
        compS.setCurrentAndTarget (0.0);  // Thermal quirk: the comp ramp starts from 0 (fades in the first time AUTO is on)
        fbS.setCurrentAndTarget (std::clamp (p.feedback, 0.0, 1.0));
        timeS.setCurrentAndTarget (targetTime (p));
        activeType = p.type;
        setCoefTargets (true);
        fade = 1.0;
        fadingOut = false;
        clearCountdown = -1;
        bypS.setCurrentAndTarget (p.bypassed ? 1.0 : 0.0);
    }

    // call once per block (or whenever parameters change); everything is smoothed internally
    void setParams (const DistortionNodeParams& np)
    {
        p = np;
        driveS.setTarget (dn_detail::dbToGain (p.driveDb));
        outS.setTarget (dn_detail::dbToGain (p.outDb));
        if (p.autoComp) compS.setTarget (dn_detail::dbToGain (-0.3 * p.driveDb));   // comp target only follows while AUTO is on
        fbS.setTarget (std::clamp (p.feedback, 0.0, 1.0));
        timeS.setTarget (targetTime (p));
        if (p.type != activeType) fadingOut = true;   // switch happens at the bottom of the fade-out
        else setCoefTargets (false);
        envRel = 1.0 - std::exp (-1.0 / (kTau * std::max (1.0e-6, p.release) * fs2));
        bypS.setTarget (p.bypassed ? 1.0 : 0.0);
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        float* io[2] = { left, right };
        for (int i = 0; i < numSamples; ++i)
        {
            const double g = driveS.next();
            const double outG = outS.next();
            const double compG = p.autoComp ? compS.next() : 1.0;   // comp ramp is frozen while AUTO is off
            const double byp = bypS.next();
            const double xin[2] = { (double) io[0][i], (double) io[1][i] };

            // 2x upsampling, JUCE polyphase structure: even phase = half-band, odd phase = 2 * 0.5 * x[i-18]
            xPos = (xPos + 1) & 63;
            double u[2][2];
            for (int c = 0; c < 2; ++c)
            {
                auto& h = xHist[(size_t) c];
                h[(size_t) xPos] = g * xin[c];
                double acc = 0.0;
                for (int k = 0; k < 38; ++k) acc += cuEven[(size_t) k] * h[(size_t) ((xPos - k) & 63)];
                u[c][0] = 2.0 * acc;
                u[c][1] = h[(size_t) ((xPos - 18) & 63)];
            }
            tick2x (u[0][0], u[1][0]);
            tick2x (u[0][1], u[1][1]);

            // 2x downsampling: vHist[vPos-2] = v[2i] (even), vHist[vPos-1] = v[2i+1]
            for (int c = 0; c < 2; ++c)
            {
                const auto& h = vHist[(size_t) c];
                double acc = 0.0;
                for (int k = 0; k < 26; ++k) acc += cdEven[(size_t) k] * h[(size_t) ((vPos - 2 - 2 * k) & 127)];
                acc += 0.5 * h[(size_t) ((vPos - 2 - 25) & 127)];
                const double y = acc * outG * compG;

                // bypass path: the input delayed by the same 31 samples
                auto& dh = dryHist[(size_t) c];
                if (c == 0) dryPos = (dryPos + 1) & 63;
                dh[(size_t) dryPos] = (float) xin[c];
                const double dry = (double) dh[(size_t) ((dryPos - kLatency) & 63)];
                io[c][i] = (float) (byp <= 0.0 ? y : (byp >= 1.0 ? dry : y + byp * (dry - y)));
            }
        }
    }

    static int latencySamples() noexcept { return kLatency; }

private:
    double targetTime (const DistortionNodeParams& q) const noexcept
    {
        if (q.sync)
        {
            // synced delays are truncated to whole base-rate samples (measured at 97 / 120.5 / 130 / 140 bpm)
            const int idx = std::clamp (q.syncIndex, 0, 18);
            return std::floor (kSyncBeats[idx] * 60.0 / std::max (1.0, q.bpm) * sr) / sr;
        }
        return std::clamp (q.timeSec, 0.0002, 0.3);
    }

    void setCoefTargets (bool immediate) noexcept
    {
        const ShapeCoeffs k = shapePrepare (activeType, p.controls);
        const double v[7] = { k.gain, k.a, k.b, k.d, k.e, k.f, k.g };
        for (int j = 0; j < 7; ++j)
        {
            if (immediate) coefS[(size_t) j].setCurrentAndTarget (v[j]);
            else coefS[(size_t) j].setTarget (v[j]);
        }
        coef.type = activeType;
    }

    // A tap written more than `validLen` 2x samples ago predates the last clear and reads as 0: the same as
    // Thermal zeroing the whole line on a type switch, without a 16 MB fill on the audio thread.
    double readDelay (int c, double d) const noexcept
    {
        const int di = (int) d;
        const double fr = d - (double) di;
        const double a = di <= validLen ? (double) dl[(size_t) c][(size_t) ((wp - di) & dlMask)] : 0.0;
        const double b = di + 1 <= validLen ? (double) dl[(size_t) c][(size_t) ((wp - di - 1) & dlMask)] : 0.0;
        return a + fr * (b - a);
    }

    void tick2x (double uL, double uR) noexcept
    {
        // envelope (FOLLOW amount and feedback gate) on the post-drive upsampled input, stereo-linked
        const double xe = 0.5 * (std::abs (uL) + std::abs (uR));
        env += (xe > env ? envAtt : envRel) * (xe - env);
        const bool gate = env > tuning.gateThreshold;

        const double fb = fbS.next();
        coef.gain = coefS[0].next(); coef.a = coefS[1].next(); coef.b = coefS[2].next(); coef.d = coefS[3].next();
        coef.e = coefS[4].next();    coef.f = coefS[5].next(); coef.g = coefS[6].next();

        // delay in 2x samples (glides linearly).  Thermal does this in float: the float rounding of T * 2fs decides
        // whether the linear interpolation sees a tiny fraction (measured: up to 30 dB lower residual for long T)
        double d = (double) (timeS.next() * (float) fs2);
        if (d >= tuning.maxDelaySec * fs2) d = 1.0;
        d = std::max (1.0, d);

        const double A = p.follow ? 1.0 : dn_detail::dbToGain (tuning.typeGainOverrideDb < 1e8 ? tuning.typeGainOverrideDb : typeFbDb (activeType));
        const double B = p.follow ? std::min (1.0, tuning.followScale * env) : 1.0;
        const double preL = fb * readDelay (0, d), preR = fb * readDelay (1, d);
        const double xl = 0.5 * (std::abs (preL) + std::abs (preR));
        envLim += (xl > envLim ? limAtt : limRel) * (xl - envLim);
        const double gr = envLim > tuning.limitThreshold ? tuning.limitThreshold / envLim : 1.0;
        const double k = gate ? gr * B : 0.0;

        // type change: linear fade out, switch type (+ clear the delay line), linear fade in
        if (fadingOut)
        {
            fade -= fadeStep;
            if (fade <= 0.0)
            {
                fade = 0.0;
                fadingOut = false;
                activeType = p.type;
                setCoefTargets (true);
                clearCountdown = tuning.switchClearDelay;
            }
        }
        else if (fade < 1.0)
            fade = std::min (1.0, fade + fadeStep);
        if (clearCountdown >= 0)
        {
            if (clearCountdown == 0)
                validLen = 0;   // the line counts as cleared (see readDelay)
            --clearCountdown;
        }

        const double vL = shapeApply (coef, uL + k * preL);
        const double vR = shapeApply (coef, uR + k * preR);
        dl[0][(size_t) wp] = (float) (A * vL);
        dl[1][(size_t) wp] = (float) (A * vR);
        wp = (wp + 1) & dlMask;
        if (validLen <= dlMask) ++validLen;
        vHist[0][(size_t) vPos] = vL * fade;
        vHist[1][(size_t) vPos] = vR * fade;
        vPos = (vPos + 1) & 127;
    }

    DistortionNodeParams p;
    double sr = 48000.0, fs2 = 96000.0;
    std::array<double, 38> cuEven {};
    std::array<double, 26> cdEven {};
    std::array<std::array<double, 64>, 2> xHist {};
    std::array<std::array<double, 128>, 2> vHist {};
    std::array<std::array<float, 64>, 2> dryHist {};
    int xPos = 0, vPos = 0, dryPos = 0;
    std::array<std::vector<float>, 2> dl;
    int dlMask = 0, wp = 0, validLen = 0;
    double env = 0, envLim = 0, envAtt = 0, envRel = 0, limAtt = 0, limRel = 0;
    dn_detail::LinearSmoother driveS, outS, compS, fbS, bypS;
    dn_detail::LinearSmootherF timeS;
    std::array<dn_detail::LinearSmoother, 7> coefS;
    ShapeCoeffs coef;
    int activeType = 0;
    double fade = 1.0, fadeStep = 1e-5;
    bool fadingOut = false;
    int clearCountdown = -1;
};

} // namespace dt
