// Structure.h - DisTherm reconstruction of Output Thermal's routing / utility nodes.
// Header-only, JUCE-free, C++20. Measured black-box against Thermal v1.2.1 (see FINDINGS.md).
//
//   dt::InputBus      MKUInputBus_MainInput      InputGain -60..+6 dB (5 ms linear ramp, fades in from 0)
//   dt::Crossover3    MKUThreeBandSplitterNode + MKUThreeBandSummerNode (LR4 3-band split, solo, refilter,
//                     band coupling on the 855-px band axis, 50 ms crossover glides in 5-sample ticks)
//   dt::MidSideWidth  MKUMidSideGainNode         SideGain -36..+12 dB, TimeSpreadAmount 0..1 (15 ms L-delay spread)
//   dt::GainStage     MKUGainBus                 Gain -60..+6 dB (5 ms linear ramp, fades in from 0)
//   dt::ToneControl   MKUToneControl             RBJ low shelf 500 Hz / high shelf 5 kHz, Q 1/sqrt2, +-18 dB
//   dt::OutputBus     MKUOutputBus_MainOutput    dry/wet (x^(5/7) law), dry delay alignment, output gain, LP/HP 12 dB/oct
//   dt::StructureChain the whole graph with the distortion / effects / compressor bypassed (31-sample stage latency)
//
// Every class: prepare (sampleRate, maxBlockSize) / reset() / setParams (const XxxParams&) once per block /
// process (left, right, numSamples) in place. No allocation in process().
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Exact comparisons of values are deliberate in this file (a host value that did not move, as Thermal tells);
// keeps -Wfloat-equal quiet.
#if defined (__clang__) || defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

namespace dt
{
namespace detail
{
    constexpr double kPi = 3.14159265358979323846;
    inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }

    // juce::SmoothedValue<..., ValueSmoothingTypes::Linear> semantics (double precision)
    struct LinearSmoothed
    {
        double cur = 0, tgt = 0, step = 0;
        int countdown = 0, steps = 0;
        void reset (double sampleRate, double seconds) { steps = (int) std::floor (seconds * sampleRate); setCurrentAndTarget (tgt); }
        void setCurrentAndTarget (double v) { cur = tgt = v; countdown = 0; step = 0; }
        void setTarget (double v)
        {
            if (v == tgt) return;
            if (steps <= 0) { setCurrentAndTarget (v); return; }
            tgt = v; countdown = steps; step = (tgt - cur) / (double) countdown;
        }
        bool smoothing() const { return countdown > 0; }
        double next()
        {
            if (countdown <= 0) return tgt;
            --countdown;
            if (countdown > 0) cur += step; else cur = tgt;
            return cur;
        }
        double skip (int n)
        {
            if (n >= countdown) { setCurrentAndTarget (tgt); return tgt; }
            cur += step * (double) n; countdown -= n; return cur;
        }
    };

    // Thermal's "slow" parameters (crossover frequencies, tone shelf gains, refilter frequencies):
    // a 50 ms linear ramp that is advanced 5 steps per tick, one tick per started 5-sample chunk of a block
    // (ticks = ceil(n / 5)); the coefficients of a chunk use the value after that tick's advance.
    // Consequence (copied from Thermal): the glide lasts 2400*B/(5*ceil(B/5)) samples at 48 kHz for host block B.
    constexpr int kTick = 5;
    constexpr double kSlowRampSeconds = 0.05;

    struct BiquadCoeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

    // RBJ cookbook (bilinear, prewarped at f0), normalised by a0
    inline BiquadCoeffs rbj (int kind, double f, double sr, double Q = 0.70710678118654752, double gainDb = 0.0)
    {
        // kind: 0 lp, 1 hp, 2 ap, 3 lowshelf, 4 highshelf
        const double w0 = 2.0 * kPi * f / sr, c = std::cos (w0), s = std::sin (w0), alpha = s / (2.0 * Q);
        double b0, b1, b2, a0, a1, a2;
        if (kind == 0) { b0 = (1 - c) * 0.5; b1 = 1 - c; b2 = b0; a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha; }
        else if (kind == 1) { b0 = (1 + c) * 0.5; b1 = -(1 + c); b2 = b0; a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha; }
        else if (kind == 2) { b0 = 1 - alpha; b1 = -2 * c; b2 = 1 + alpha; a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha; }
        else
        {
            const double A = std::pow (10.0, gainDb / 40.0), sa = 2.0 * std::sqrt (A) * alpha;
            if (kind == 3)
            {
                b0 = A * ((A + 1) - (A - 1) * c + sa); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - sa);
                a0 = (A + 1) + (A - 1) * c + sa; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - sa;
            }
            else
            {
                b0 = A * ((A + 1) + (A - 1) * c + sa); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sa);
                a0 = (A + 1) - (A - 1) * c + sa; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - sa;
            }
        }
        const double ia = 1.0 / a0;
        return { b0 * ia, b1 * ia, b2 * ia, a1 * ia, a2 * ia };
    }

    // transposed direct form II, double state
    struct BiquadTDF2
    {
        BiquadCoeffs k;
        double s1 = 0, s2 = 0;
        void clear() { s1 = s2 = 0; }
        inline double tick (double x)
        {
            const double y = k.b0 * x + s1;
            s1 = k.b1 * x - k.a1 * y + s2;
            s2 = k.b2 * x - k.a2 * y;
            return y;
        }
    };
    // direct form I, double state (Thermal's output LP/HP: matches its per-sample cutoff sweeps, TDF2 does not)
    struct BiquadDF1
    {
        BiquadCoeffs k;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void clear() { x1 = x2 = y1 = y2 = 0; }
        inline double tick (double x)
        {
            const double y = k.b0 * x + k.b1 * x1 + k.b2 * x2 - k.a1 * y1 - k.a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };
#ifndef DT_XO_BIQUAD
 #define DT_XO_BIQUAD BiquadTDF2
#endif
    using Biquad = DT_XO_BIQUAD;   // crossover / refilter / tone / dry-compensation sections (TDF2 matches their glides; DF1 does not)

    template <typename BQ = BiquadDF1>
    struct StereoBiquadT
    {
        BQ l, r;
        void set (const BiquadCoeffs& c) { l.k = c; r.k = c; }
        void clear() { l.clear(); r.clear(); }
    };
    using StereoBiquad = StereoBiquadT<Biquad>;

    // integer delay line (prepare-time allocation)
    struct Delay
    {
        std::vector<float> buf; int len = 0, w = 0;
        void prepare (int samples) { len = samples; buf.assign ((size_t) std::max (1, samples), 0.0f); w = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        inline float tick (float x)
        {
            if (len <= 0) return x;
            const float y = buf[(size_t) w]; buf[(size_t) w] = x; if (++w >= len) w = 0; return y;
        }
    };
} // namespace detail

//==================================================================================================
// Band-split axis and LOW/HIGH coupling (exact reproduction of Thermal, measured with 13k probes)
//==================================================================================================
// Thermal keeps the two crossover handles on an 855-pixel axis made of 4 segments of 213.75 px
// (30-100, 100-500, 500-5000, 5000-22000 Hz). Inside a segment [fa, fb]: f = A + (fa - A)(1 + (sqrt10 - 1) t)^2,
// A = (10 fa - fb) / 9, t in [0, 1]. The handles must stay >= 30 px apart; the processor snaps and pushes the
// other band parameter and writes the result back into the parameters (a DisTherm processor must do the same).
namespace bandaxis
{
    constexpr double kSegPx = 213.75;
    constexpr int kAxisPx = 855, kMinGapPx = 30;
    inline const double* breaks() { static const double b[5] = { 30.0, 100.0, 500.0, 5000.0, 22000.0 }; return b; }
    inline double pos (double f)
    {
        const double* B = breaks();
        int k = 0;
        while (k < 3 && f >= B[k + 1]) ++k;
        const double fa = B[k], fb = B[k + 1], A = (10.0 * fa - fb) / 9.0;
        const double t = (std::sqrt (std::max ((f - A) / (fa - A), 0.0)) - 1.0) / (std::sqrt (10.0) - 1.0);
        return ((double) k + t) * kSegPx;
    }
    inline double freq (double x)
    {
        const double* B = breaks();
        const int k = (int) std::min (std::max (std::floor (x / kSegPx), 0.0), 3.0);
        const double t = x / kSegPx - (double) k;
        const double fa = B[k], fb = B[k + 1], A = (10.0 * fa - fb) / 9.0, g = 1.0 + (std::sqrt (10.0) - 1.0) * t;
        return A + (fa - A) * g * g;
    }
    // x2 pixels for which Thermal's float re-read of freq(x2) floors to x2 - 1 (measured; makes the snapped
    // LOW value land one pixel lower after a LOW move pushed HIGH)
    inline bool rereadFloorsDown (int x2)
    {
        static const short dev[] = { 32, 34, 36, 40, 41, 43, 46, 51, 52, 55, 56, 59, 60, 62, 65, 67, 70, 74, 77, 80, 85, 89, 90, 93, 95,
            100, 105, 106, 109, 113, 114, 115, 117, 119, 120, 123, 124, 126, 128, 129, 136, 137, 141, 145, 151, 159, 161, 163, 164,
            166, 171, 172, 173, 174, 176, 177, 179, 181, 183, 193, 194, 196, 197, 198, 199, 204, 209, 210, 212, 213, 215, 216, 217,
            218, 225, 226, 229, 232, 233, 234, 243, 248, 254, 255, 259, 265, 270, 273, 276, 277, 282, 283, 288, 293, 298, 310, 311,
            315, 316, 320, 325, 327, 329, 344, 348, 356, 358, 364, 369, 371, 374, 377, 379, 381, 383, 384, 388, 398, 399, 400, 407,
            409, 415, 417, 419, 504, 506, 628, 634, 640, 815, 832, 839, 840 };
        return std::binary_search (std::begin (dev), std::end (dev), (short) x2);
    }
} // namespace bandaxis

class BandSplitCoupler
{
public:
    void reset() { last1 = c1 = 30.0; last2 = c2 = 22000.0; }
    // host values (Hz) for this block -> coupled values. LOW wins when both moved in the same block.
    void apply (double host1, double host2)
    {
        using namespace bandaxis;
        const bool ch1 = host1 != last1, ch2 = host2 != last2;
        last1 = host1; last2 = host2;
        double f1 = ch1 ? host1 : c1, f2 = ch2 ? host2 : c2;
        if (ch1)
        {
            int x1 = (int) std::floor (pos (f1));
            if (x1 > kAxisPx - kMinGapPx) { x1 = kAxisPx - kMinGapPx; f1 = freq (x1); }
            if (pos (f2) < x1 + kMinGapPx)
            {
                const double nf2 = freq (x1 + kMinGapPx);
                if (nf2 != f2)
                {
                    f2 = nf2;
                    int x2 = x1 + kMinGapPx;
                    if (rereadFloorsDown (x2)) --x2;
                    if (pos (f1) > x2 - kMinGapPx) f1 = freq (std::max (x2 - kMinGapPx, 0));
                }
            }
        }
        else if (ch2)
        {
            int x2 = (int) std::floor (pos (f2));
            if (x2 < kMinGapPx) { x2 = kMinGapPx; f2 = freq (x2); }
            if (pos (f1) > x2 - kMinGapPx) f1 = freq (x2 - kMinGapPx);
        }
        c1 = f1; c2 = f2;
    }
    double band1() const { return c1; }
    double band2() const { return c2; }
private:
    double last1 = 30.0, last2 = 22000.0, c1 = 30.0, c2 = 22000.0;
};

//==================================================================================================
// Crossover3: ThreeBandSplitter + ThreeBandSummer of one stage
//==================================================================================================
struct Crossover3Params
{
    double band1Hz = 30.0;     // LOW  (MKUThreeBandSplitterNode_Stage_N_Band1Hz, 30..22000, mapped = 30 + 21970 u^4)
    double band2Hz = 22000.0;  // HIGH (Band2Hz)
    bool multiBand = true;     // "Band Split" (on when u >= 1, i.e. only the top value 1.0 switches it on)
    bool soloBand = false;     // summer "Solo Band": output = processed mids only
    bool refilter = false;     // summer "Refilter": mids x HP4(f1) LP4(f2) again, lows x AP(f1), highs x AP(f2)
    bool applyCoupling = true; // apply Thermal's LOW/HIGH coupling to the host values
};

class Crossover3
{
public:
    // midPathLatency: latency of whatever runs on the mids between split() and sum() (Thermal: 31 samples,
    // the DustDisto node's latency, present even when the distortion is bypassed). Lows and highs are delayed by it.
    void prepare (double sampleRate, int maxBlockSize, int midPathLatency = 31)
    {
        sr = sampleRate; maxBlock = std::max (1, maxBlockSize); latency = midPathLatency;
        for (auto* v : { &lowL, &lowR, &midL, &midR, &highL, &highR }) v->assign ((size_t) maxBlock, 0.0f);
        for (auto* d : { &dLowL, &dLowR, &dHighL, &dHighR, &dMidL, &dMidR }) d->prepare (latency);
        for (auto* s : { &f1s, &f2s, &r1s, &r2s }) s->reset (sr, detail::kSlowRampSeconds);
        reset();
    }
    // Thermal start-up: crossover smoothers sit at the parameter defaults (30 Hz / 22 kHz) and glide to the
    // loaded values over 50 ms after load (masked by the 5 ms output fade-in only partly).
    void reset()
    {
        coupler.reset();
        f1s.setCurrentAndTarget (30.0); f2s.setCurrentAndTarget (22000.0);
        r1s.setCurrentAndTarget (30.0); r2s.setCurrentAndTarget (22000.0);
        clearSplitState(); clearRefilterState();
        for (auto* d : { &dLowL, &dLowR, &dHighL, &dHighR, &dMidL, &dMidR }) d->clear();
        wasMulti = false; wasRefilter = false; coeffsValid = false; refCoeffsValid = false;
    }
    void setParams (const Crossover3Params& p)
    {
        prm = p;
        double h1 = std::clamp (p.band1Hz, 30.0, 22000.0), h2 = std::clamp (p.band2Hz, 30.0, 22000.0);
        if (p.applyCoupling && p.multiBand) { coupler.apply (h1, h2); h1 = coupler.band1(); h2 = coupler.band2(); } // Thermal couples only while splitting
        cb1 = h1; cb2 = h2;
        if (p.multiBand)
        {
            if (! wasMulti) { clearSplitState(); coeffsValid = false; }
            f1s.setTarget (h1); f2s.setTarget (h2);
        }
        else { f1s.setTarget (30.0); f2s.setTarget (22000.0); }
        const bool ref = p.multiBand && p.refilter;
        if (ref)
        {
            if (! wasRefilter) { clearRefilterState(); refCoeffsValid = false; }
            r1s.setTarget (h1); r2s.setTarget (h2);
        }
        else { r1s.setTarget (30.0); r2s.setTarget (22000.0); }
        wasMulti = p.multiBand; wasRefilter = ref;
    }
    double coupledBand1() const { return cb1; } // write these back into the host parameters (Thermal does)
    double coupledBand2() const { return cb2; }

    // 1) split: mids (undelayed) are left in midsL()/midsR(); lows/highs are kept internally (delayed by latency)
    void split (const float* inL, const float* inR, int n)
    {
        if (! prm.multiBand)   // full band to the mids (bit exact), no lows/highs (zeros enter the alignment delays)
        {
            for (int c = 0; c < kTicks (n); ++c) { f1s.skip (detail::kTick); f2s.skip (detail::kTick); }
            for (int i = 0; i < n; ++i)
            {
                midL[(size_t) i] = inL[i]; midR[(size_t) i] = inR[i];
                lowL[(size_t) i] = dLowL.tick (0.0f); lowR[(size_t) i] = dLowR.tick (0.0f);
                highL[(size_t) i] = dHighL.tick (0.0f); highR[(size_t) i] = dHighR.tick (0.0f);
            }
            return;
        }
        for (int i0 = 0; i0 < n; i0 += detail::kTick)
        {
            const bool moving = f1s.smoothing() || f2s.smoothing();
            const double a = f1s.skip (detail::kTick), b = f2s.skip (detail::kTick);
            if (moving || ! coeffsValid) updateSplitCoeffs (a, b);
            const int i1 = std::min (n, i0 + detail::kTick);
            for (int i = i0; i < i1; ++i)
            {
                splitSample (inL[i], lp1L, hp1L, lp2L, hp2L, apL, lowL[(size_t) i], midL[(size_t) i], highL[(size_t) i]);
                splitSample (inR[i], lp1R, hp1R, lp2R, hp2R, apR, lowR[(size_t) i], midR[(size_t) i], highR[(size_t) i]);
                lowL[(size_t) i] = dLowL.tick (lowL[(size_t) i]); lowR[(size_t) i] = dLowR.tick (lowR[(size_t) i]);
                highL[(size_t) i] = dHighL.tick (highL[(size_t) i]); highR[(size_t) i] = dHighR.tick (highR[(size_t) i]);
            }
        }
    }
    float* midsL() { return midL.data(); }
    float* midsR() { return midR.data(); }

    // 2) sum: processed mids (same length) + delayed lows/highs -> out
    void sum (const float* mL, const float* mR, float* outL, float* outR, int n)
    {
        const bool ref = prm.multiBand && prm.refilter;
        if (! ref) for (int c = 0; c < kTicks (n); ++c) { r1s.skip (detail::kTick); r2s.skip (detail::kTick); }
        for (int i0 = 0; i0 < n; i0 += detail::kTick)
        {
            if (ref)
            {
                const bool moving = r1s.smoothing() || r2s.smoothing();
                const double a = r1s.skip (detail::kTick), b = r2s.skip (detail::kTick);
                if (moving || ! refCoeffsValid) updateRefCoeffs (a, b);
            }
            const int i1 = std::min (n, i0 + detail::kTick);
            for (int i = i0; i < i1; ++i)
            {
                double lo[2] = { lowL[(size_t) i], lowR[(size_t) i] }, mi[2] = { mL[i], mR[i] }, hi[2] = { highL[(size_t) i], highR[(size_t) i] };
                if (ref)
                {
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        auto& R = ch == 0 ? refL : refR;
                        lo[ch] = R.ap1.tick (lo[ch]);
                        hi[ch] = R.ap2.tick (hi[ch]);
                        mi[ch] = R.lp2b.tick (R.lp2a.tick (R.hp1b.tick (R.hp1a.tick (mi[ch]))));
                    }
                }
                if (prm.soloBand) { outL[i] = (float) mi[0]; outR[i] = (float) mi[1]; }
                else { outL[i] = (float) (lo[0] + mi[0] + hi[0]); outR[i] = (float) (lo[1] + mi[1] + hi[1]); }
            }
        }
    }

    // stage with an empty mid path (mids delayed by the latency): split + sum, in place
    void process (float* left, float* right, int n) noexcept
    {
        split (left, right, n);
        for (int i = 0; i < n; ++i) { midL[(size_t) i] = dMidL.tick (midL[(size_t) i]); midR[(size_t) i] = dMidR.tick (midR[(size_t) i]); }
        sum (midL.data(), midR.data(), left, right, n);
    }

private:
    struct Lr4 { detail::Biquad a, b; double tick (double x) { return b.tick (a.tick (x)); } void set (const detail::BiquadCoeffs& c) { a.k = c; b.k = c; } void clear() { a.clear(); b.clear(); } };
    struct Ref { detail::Biquad ap1, ap2, hp1a, hp1b, lp2a, lp2b; };
    static int kTicks (int n) { return (n + detail::kTick - 1) / detail::kTick; }

    static inline void splitSample (float x, Lr4& lp1, Lr4& hp1, Lr4& lp2, Lr4& hp2, detail::Biquad& ap, float& lo, float& mi, float& hi)
    {
        const double l = ap.tick (lp1.tick (x));   // lows = LP4(f1) . AP(f2)
        const double rest = hp1.tick (x);          // rest = HP4(f1)
        lo = (float) l; mi = (float) lp2.tick (rest); hi = (float) hp2.tick (rest);
    }
    void updateSplitCoeffs (double a, double b)
    {
        const auto cl1 = detail::rbj (0, a, sr), ch1 = detail::rbj (1, a, sr), cl2 = detail::rbj (0, b, sr), ch2 = detail::rbj (1, b, sr), ca2 = detail::rbj (2, b, sr);
        lp1L.set (cl1); lp1R.set (cl1); hp1L.set (ch1); hp1R.set (ch1);
        lp2L.set (cl2); lp2R.set (cl2); hp2L.set (ch2); hp2R.set (ch2);
        apL.k = ca2; apR.k = ca2; coeffsValid = true;
    }
    void updateRefCoeffs (double a, double b)
    {
        const auto ca1 = detail::rbj (2, a, sr), ca2 = detail::rbj (2, b, sr), ch1 = detail::rbj (1, a, sr), cl2 = detail::rbj (0, b, sr);
        for (auto* R : { &refL, &refR }) { R->ap1.k = ca1; R->ap2.k = ca2; R->hp1a.k = ch1; R->hp1b.k = ch1; R->lp2a.k = cl2; R->lp2b.k = cl2; }
        refCoeffsValid = true;
    }
    void clearSplitState() { for (auto* f : { &lp1L, &lp1R, &hp1L, &hp1R, &lp2L, &lp2R, &hp2L, &hp2R }) f->clear(); apL.clear(); apR.clear(); }
    void clearRefilterState() { for (auto* R : { &refL, &refR }) { R->ap1.clear(); R->ap2.clear(); R->hp1a.clear(); R->hp1b.clear(); R->lp2a.clear(); R->lp2b.clear(); } }

    double sr = 48000; int maxBlock = 512, latency = 31;
    Crossover3Params prm;
    BandSplitCoupler coupler;
    double cb1 = 30, cb2 = 22000;
    detail::LinearSmoothed f1s, f2s, r1s, r2s;
    Lr4 lp1L, lp1R, hp1L, hp1R, lp2L, lp2R, hp2L, hp2R;
    detail::Biquad apL, apR;
    Ref refL, refR;
    bool wasMulti = false, wasRefilter = false, coeffsValid = false, refCoeffsValid = false;
    std::vector<float> lowL, lowR, midL, midR, highL, highR;
    detail::Delay dLowL, dLowR, dHighL, dHighR, dMidL, dMidR;
};

//==================================================================================================
// MidSideWidth: MKUMidSideGainNode (node prop Bypassed=1 by default -> identity)
//==================================================================================================
struct MidSideWidthParams
{
    double sideGainDb = 0.0; // SIDES -36..+12 dB (mapped = -36 + 48 u)
    double width = 0.0;      // WIDTH (TimeSpreadAmount) 0..1
    bool enabled = false;    // node property Bypassed = 0
};

class MidSideWidth
{
public:
    void prepare (double sampleRate, int)
    {
        sr = sampleRate;
        g.reset (sr, 0.010); w.reset (sr, 0.010);
        dl.prepare ((int) std::floor (0.015 * sr)); // 720 samples at 48 kHz (661 at 44.1 kHz)
        reset();
    }
    void reset() { g.setCurrentAndTarget (1.0); w.setCurrentAndTarget (1.0); dl.clear(); } // Thermal quirk: WIDTH glides down from 1.0 after load
    void setParams (const MidSideWidthParams& p) { prm = p; g.setTarget (detail::dbToGain (p.sideGainDb)); w.setTarget (std::clamp (p.width, 0.0, 1.0)); }
    void setEnabled (bool e) { prm.enabled = e; }
    void process (float* left, float* right, int n) noexcept
    {
        if (! prm.enabled) { for (int i = 0; i < n; ++i) { g.next(); w.next(); dl.tick (left[i]); } return; }
        for (int i = 0; i < n; ++i)
        {
            const double gs = g.next(), ws = w.next();
            const double th = detail::kPi * 0.25 - 0.5 * ws;
            const double a = std::cos (th), d = std::sin (th), k = std::min (2.0 * ws, 1.0);
            const double L = left[i], R = right[i], Ld = dl.tick (left[i]);
            const double l2 = a * L + k * d * Ld, r2 = d * R + k * a * Ld;   // time spread: delayed L into both sides
            const double m = 0.5 * (l2 + r2), s = 0.5 * gs * (l2 - r2);      // then mid/side gain
            left[i] = (float) (m + s); right[i] = (float) (m - s);
        }
    }
private:
    double sr = 48000;
    MidSideWidthParams prm;
    detail::LinearSmoothed g, w;
    detail::Delay dl;
};

//==================================================================================================
// GainStage: MKUGainBus (and the gain of Input/Output bus). dB -> linear, 5 ms linear ramp in linear gain.
//==================================================================================================
struct GainStageParams { double gainDb = 0.0; bool enabled = true; };

class GainStage
{
public:
    // fadeIn = true: the smoother starts at 0 after reset (Thermal's Input/Output bus gains: 5 ms fade-in after load);
    // false: it starts at unity (the GainBus default, 0 dB) and ramps to the loaded value.
    explicit GainStage (bool fadeIn = false) : fadeInOnReset (fadeIn) {}
    void prepare (double sampleRate, int) { sm.reset (sampleRate, 0.005); reset(); }
    void reset() { sm.setCurrentAndTarget (fadeInOnReset ? 0.0 : 1.0); }
    void setParams (const GainStageParams& p) { prm = p; sm.setTarget (detail::dbToGain (std::clamp (p.gainDb, -60.0, 6.0))); }
    float nextGain() { return (float) sm.next(); }
    void process (float* left, float* right, int n) noexcept
    {
        if (! prm.enabled) { sm.skip (n); return; }
        for (int i = 0; i < n; ++i) { const double k = sm.next(); left[i] = (float) (left[i] * k); right[i] = (float) (right[i] * k); }
    }
private:
    bool fadeInOnReset = false;
    GainStageParams prm;
    detail::LinearSmoothed sm;
};

//==================================================================================================
// ToneControl: MKUToneControl (node prop Bypassed=1 by default -> identity). Stays in the chain even when
// the stage's splitter is bypassed.
//==================================================================================================
struct ToneControlParams { double lowShelfDb = 0.0, highShelfDb = 0.0; bool enabled = false; };

class ToneControl
{
public:
    static constexpr double kLowHz = 500.0, kHighHz = 5000.0;
    void prepare (double sampleRate, int) { sr = sampleRate; ls.reset (sr, detail::kSlowRampSeconds); hs.reset (sr, detail::kSlowRampSeconds); reset(); }
    void reset() { ls.setCurrentAndTarget (1.0); hs.setCurrentAndTarget (1.0); lsF.clear(); hsF.clear(); update (1.0, 1.0); }
    // shelf gains are smoothed as linear amplitude (50 ms, 5-sample ticks)
    void setParams (const ToneControlParams& p) { prm = p; ls.setTarget (detail::dbToGain (std::clamp (p.lowShelfDb, -18.0, 18.0))); hs.setTarget (detail::dbToGain (std::clamp (p.highShelfDb, -18.0, 18.0))); }
    void setEnabled (bool e) { prm.enabled = e; }
    void process (float* left, float* right, int n) noexcept
    {
        for (int i0 = 0; i0 < n; i0 += detail::kTick)
        {
            const bool moving = ls.smoothing() || hs.smoothing();
            const double a = ls.skip (detail::kTick), b = hs.skip (detail::kTick);
            if (moving) update (a, b);
            if (! prm.enabled) continue;
            const int i1 = std::min (n, i0 + detail::kTick);
            for (int i = i0; i < i1; ++i)
            {
                left[i] = (float) hsF.l.tick (lsF.l.tick (left[i]));
                right[i] = (float) hsF.r.tick (lsF.r.tick (right[i]));
            }
        }
    }
private:
    void update (double lsLin, double hsLin)
    {
        lsF.set (detail::rbj (3, kLowHz, sr, 0.70710678118654752, 20.0 * std::log10 (lsLin)));
        hsF.set (detail::rbj (4, kHighHz, sr, 0.70710678118654752, 20.0 * std::log10 (hsLin)));
    }
    double sr = 48000;
    ToneControlParams prm;
    detail::LinearSmoothed ls, hs;
    detail::StereoBiquad lsF, hsF;
};

//==================================================================================================
// InputBus: MKUInputBus_MainInput
//==================================================================================================
struct InputBusParams { double inputGainDb = 0.0; };

class InputBus
{
public:
    void prepare (double sampleRate, int maxBlockSize) { gain.prepare (sampleRate, maxBlockSize); }
    void reset() { gain.reset(); }
    void setParams (const InputBusParams& p) { gain.setParams ({ p.inputGainDb, true }); }
    void process (float* left, float* right, int n) noexcept { gain.process (left, right, n); }
private:
    GainStage gain { true };
};

//==================================================================================================
// DryCompensation: the OutputBus runs the dry signal through the crossover phase of every active band split, so
// dry/wet mixing does not comb: per stage with splitter on and MultiBand on: AP(f1) AP(f2) (2nd-order RBJ allpasses,
// Q 1/sqrt2, the coupled band frequencies), squared when that stage's Refilter is on. Same 50 ms tick glides as the
// crossover; a stage's allpasses start from zero state and glide up from 30 Hz / 22 kHz when it becomes active;
// the extra Refilter pair switches in hard (zero state, current frequencies).
//==================================================================================================
struct DryCompStageParams { bool active = false; double band1Hz = 30.0, band2Hz = 22000.0; bool refilter = false; };

class DryCompensation
{
public:
    void prepare (double sampleRate) { sr = sampleRate; for (auto& s : st) { s.a.reset (sr, detail::kSlowRampSeconds); s.b.reset (sr, detail::kSlowRampSeconds); } reset(); }
    void reset()
    {
        for (auto& s : st)
        {
            s.a.setCurrentAndTarget (30.0); s.b.setCurrentAndTarget (22000.0);
            s.wasActive = s.wasRef = false; s.valid = false; s.prm = {};
            for (auto& f : s.f) f.clear();
        }
    }
    void setParams (int stage, const DryCompStageParams& p)
    {
        auto& s = st[(size_t) stage];
        const bool ref = p.active && p.refilter;
        if (p.active)
        {
            if (! s.wasActive) { s.f[0].clear(); s.f[1].clear(); s.valid = false; }
            s.a.setTarget (p.band1Hz); s.b.setTarget (p.band2Hz);
        }
        else { s.a.setTarget (30.0); s.b.setTarget (22000.0); }
        if (ref && ! s.wasRef) { s.f[2].clear(); s.f[3].clear(); }   // refilter pair: hard switch-in from zero state
        s.wasActive = p.active; s.wasRef = ref; s.prm = p;
    }
    void process (float* left, float* right, int n) noexcept
    {
        for (auto& s : st)
        {
            if (! s.prm.active) { for (int i0 = 0; i0 < n; i0 += detail::kTick) { s.a.skip (detail::kTick); s.b.skip (detail::kTick); } continue; }
            const bool ref = s.prm.refilter;
            for (int i0 = 0; i0 < n; i0 += detail::kTick)
            {
                const bool moving = s.a.smoothing() || s.b.smoothing();
                const double fa = s.a.skip (detail::kTick), fb = s.b.skip (detail::kTick);
                if (moving || ! s.valid)
                {
                    const auto c1 = detail::rbj (2, fa, sr), c2 = detail::rbj (2, fb, sr);
                    s.f[0].set (c1); s.f[2].set (c1); s.f[1].set (c2); s.f[3].set (c2); s.valid = true;
                }
                const int i1 = std::min (n, i0 + detail::kTick);
                for (int i = i0; i < i1; ++i)
                {
                    // order (measured at a refilter switch): AP(f1), [AP(f1)], AP(f2), [AP(f2)]
                    double l = s.f[0].l.tick (left[i]), r = s.f[0].r.tick (right[i]);
                    if (ref) { l = s.f[2].l.tick (l); r = s.f[2].r.tick (r); }
                    l = s.f[1].l.tick (l); r = s.f[1].r.tick (r);
                    if (ref) { l = s.f[3].l.tick (l); r = s.f[3].r.tick (r); }
                    left[i] = (float) l; right[i] = (float) r;
                }
            }
        }
    }
private:
    struct Stage { detail::LinearSmoothed a, b; std::array<detail::StereoBiquad, 4> f; bool wasActive = false, wasRef = false, valid = false; DryCompStageParams prm; };
    double sr = 48000;
    std::array<Stage, 3> st;
};

//==================================================================================================
// OutputBus: MKUOutputBus_MainOutput
//   out = OutputGain * ( (1-v)^(5/7) * dry  +  v^(5/7) * HP(LP(wet)) )
//   dry = InputBus output delayed by the chain latency (93 samples in Thermal), unfiltered.
//==================================================================================================
struct OutputBusParams
{
    double dryWet = 1.0;          // 0..1
    double outputGainDb = 0.0;    // -60..+6
    double lowpassHz = 22000.0;   // 30..22000 (u^4 taper)
    double highpassHz = 30.0;     // 30..22000
    bool filtersBypassed = false; // node property FiltersBypassed
};

class OutputBus
{
public:
    void prepare (double sampleRate, int maxBlockSize, int dryDelaySamples = 93)
    {
        sr = sampleRate;
        dryL.prepare (dryDelaySamples); dryR.prepare (dryDelaySamples);
        comp.prepare (sr);
        for (auto* v : { &dryBufL, &dryBufR, &tmpL, &tmpR }) v->assign ((size_t) std::max (1, maxBlockSize), 0.0f);
        gain.reset (sr, 0.005); mix.reset (sr, 0.005); lpS.reset (sr, 0.005); hpS.reset (sr, 0.005);
        reset();
    }
    void reset()
    {
        gain.setCurrentAndTarget (0.0);      // fades in from 0
        mix.setCurrentAndTarget (1.0);       // dry/wet starts at its default (1) and ramps to the loaded value
        lpS.setCurrentAndTarget (22000.0); hpS.setCurrentAndTarget (30.0);
        lp.clear(); hp.clear(); dryL.clear(); dryR.clear(); lastV = -1.0; comp.reset();
        updateFilters (22000.0, 30.0);
    }
    void setParams (const OutputBusParams& p)
    {
        prm = p;
        gain.setTarget (detail::dbToGain (std::clamp (p.outputGainDb, -60.0, 6.0)));
        mix.setTarget (std::clamp (p.dryWet, 0.0, 1.0));
        lpS.setTarget (std::clamp (p.lowpassHz, 30.0, 22000.0)); hpS.setTarget (std::clamp (p.highpassHz, 30.0, 22000.0));
    }
    // dry-path crossover phase compensation, per stage (call once per block, like setParams)
    void setDryCompensation (int stage, const DryCompStageParams& p) { comp.setParams (stage, p); }
    // wet in place, dry = undelayed InputBus output (the bus delays and phase-compensates it). n <= maxBlockSize
    void process (float* wetL, float* wetR, const float* dryInL, const float* dryInR, int n) noexcept
    {
        for (int i = 0; i < n; ++i) { dryBufL[(size_t) i] = dryL.tick (dryInL[i]); dryBufR[(size_t) i] = dryR.tick (dryInR[i]); }
        comp.process (dryBufL.data(), dryBufR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            const bool moving = lpS.smoothing() || hpS.smoothing();
            const double fl = lpS.next(), fh = hpS.next();
            if (moving) updateFilters (fl, fh);
            double wl = wetL[i], wr = wetR[i];
            if (! prm.filtersBypassed) { wl = hp.l.tick (lp.l.tick (wl)); wr = hp.r.tick (lp.r.tick (wr)); }
            const bool mixMoving = mix.smoothing();
            const double v = mix.next();
            if (mixMoving || v != lastV) { lastV = v; gw = std::pow (v, 5.0 / 7.0); gd = std::pow (1.0 - v, 5.0 / 7.0); }
            const double go = gain.next();
            const double dl = dryBufL[(size_t) i], dr = dryBufR[(size_t) i];
            wetL[i] = (float) (go * (gd * dl + gw * wl));
            wetR[i] = (float) (go * (gd * dr + gw * wr));
        }
    }
    void process (float* left, float* right, int n) noexcept   // dry = wet input (n <= maxBlockSize)
    {
        std::copy (left, left + n, tmpL.begin()); std::copy (right, right + n, tmpR.begin());
        process (left, right, tmpL.data(), tmpR.data(), n);
    }
private:
    void updateFilters (double fl, double fh) { lp.set (detail::rbj (0, fl, sr)); hp.set (detail::rbj (1, fh, sr)); }
    double sr = 48000, lastV = -1.0, gw = 1.0, gd = 0.0;
    OutputBusParams prm;
    detail::LinearSmoothed gain, mix, lpS, hpS;
    detail::StereoBiquadT<detail::BiquadDF1> lp, hp;
    detail::Delay dryL, dryR;
    DryCompensation comp;
    std::vector<float> dryBufL, dryBufR, tmpL, tmpR;
};

//==================================================================================================
// StructureChain: Thermal's graph with distortion, effects and master compressor bypassed.
//   InputBus -> 3 x [ (splitter on ? Crossover3 split -> 31-sample mid latency -> MidSide -> GainBus -> summer
//                     : 31-sample delay) -> ToneControl ] -> OutputBus (dry = InputBus output, delay 93)
//==================================================================================================
struct StageParams
{
    bool splitterOn = true;      // splitter node Bypassed = 0 (else the stage is a pure 31-sample delay, tone stays)
    Crossover3Params xo;
    MidSideWidthParams ms;
    GainStageParams gain;        // enabled = GainBus node not bypassed
    ToneControlParams tone;
};
struct StructureChainParams
{
    InputBusParams in;
    std::array<StageParams, 3> stage;
    OutputBusParams out;
};

class StructureChain
{
public:
    static constexpr int kStageLatency = 31;
    void prepare (double sampleRate, int maxBlockSize)
    {
        maxBlock = std::max (1, maxBlockSize);
        in.prepare (sampleRate, maxBlock);
        for (auto& s : st)
        {
            s.xo.prepare (sampleRate, maxBlock, kStageLatency);
            s.ms.prepare (sampleRate, maxBlock); s.gain.prepare (sampleRate, maxBlock); s.tone.prepare (sampleRate, maxBlock);
            s.dL.prepare (kStageLatency); s.dR.prepare (kStageLatency); s.mL.prepare (kStageLatency); s.mR.prepare (kStageLatency);
        }
        out.prepare (sampleRate, maxBlock, 3 * kStageLatency);
        dryL.assign ((size_t) maxBlock, 0.0f); dryR.assign ((size_t) maxBlock, 0.0f);
        reset();
    }
    void reset()
    {
        in.reset(); out.reset();
        for (auto& s : st) { s.xo.reset(); s.ms.reset(); s.gain.reset(); s.tone.reset(); s.dL.clear(); s.dR.clear(); s.mL.clear(); s.mR.clear(); }
    }
    void setParams (const StructureChainParams& p)
    {
        prm = p;
        in.setParams (p.in); out.setParams (p.out);
        for (int k = 0; k < 3; ++k)
        {
            auto& s = st[(size_t) k]; const auto& q = p.stage[(size_t) k];
            if (q.splitterOn) { s.xo.setParams (q.xo); s.ms.setParams (q.ms); s.gain.setParams (q.gain); }
            s.tone.setParams (q.tone);
            const bool act = q.splitterOn && q.xo.multiBand;
            out.setDryCompensation (k, { act, s.xo.coupledBand1(), s.xo.coupledBand2(), act && q.xo.refilter });
        }
    }
    const Crossover3& crossover (int k) const { return st[(size_t) k].xo; }
    void process (float* left, float* right, int n) noexcept
    {
        for (int i0 = 0; i0 < n; i0 += maxBlock) processBlock (left + i0, right + i0, std::min (maxBlock, n - i0));
    }
private:
    struct Stage { Crossover3 xo; MidSideWidth ms; GainStage gain; ToneControl tone; detail::Delay dL, dR, mL, mR; };
    void processBlock (float* L, float* R, int n)
    {
        in.process (L, R, n);
        std::copy (L, L + n, dryL.begin()); std::copy (R, R + n, dryR.begin());
        for (int k = 0; k < 3; ++k)
        {
            auto& s = st[(size_t) k];
            if (prm.stage[(size_t) k].splitterOn)
            {
                s.xo.split (L, R, n);
                float* mL = s.xo.midsL(); float* mR = s.xo.midsR();
                for (int i = 0; i < n; ++i) { mL[i] = s.mL.tick (mL[i]); mR[i] = s.mR.tick (mR[i]); } // distortion-node latency
                s.ms.process (mL, mR, n);
                s.gain.process (mL, mR, n);
                s.xo.sum (mL, mR, L, R, n);
            }
            else
                for (int i = 0; i < n; ++i) { L[i] = s.dL.tick (L[i]); R[i] = s.dR.tick (R[i]); }
            s.tone.process (L, R, n);
        }
        out.process (L, R, dryL.data(), dryR.data(), n);
    }
    int maxBlock = 512;
    StructureChainParams prm;
    InputBus in;
    std::array<Stage, 3> st;
    OutputBus out;
    std::vector<float> dryL, dryR;
};

} // namespace dt

#if defined (__clang__) || defined (__GNUC__)
 #pragma GCC diagnostic pop
#endif
