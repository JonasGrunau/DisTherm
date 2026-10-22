// dt::Phaser - black-box reconstruction of Output Thermal's "Phaser" effect (MKUPhaser_*).
// Header-only, JUCE-free, C++20. See FINDINGS.md (fx-modulation) for the measurements behind every constant.
//
// Two completely different engines, selected by PhaserType (only the exact values 0 / 1 select an engine; any
// value in between makes the effect a pure gain stage). Each engine keeps its own LFO phase and filter state;
// only the selected engine runs (its LFO pauses and its state freezes while the other one is selected).
//
// MULTI (type 0): classic swept allpass phaser.
//   f0   = Centre/2 + sr/(8*pi) * (1 + Range * sin(phase))      (linear in Hz! sweep fc/2 .. fc/2 + sr/(4*pi))
//   s    = x + 0.98*Feedback * padeTanh(v[n-1])                 (Pade tanh x(27+x^2)/(27+9x^2), |x|<=3)
//   v    = 2*floor(Stages) first-order bilinear allpasses at f0 (one "stage" = two identical sections)
//   fx   = x + 0.5 * v
// BARBER (type 1): frequency-shifter ("barber pole") phaser; Stages, Centre and Range are ignored.
//   s    = x + 0.65*Feedback * o[n-1]                           (linear, from the effect output)
//   I,Q  = Hilbert pair of s: two chains of 6 first-order allpasses (Hutchins/Csound "hilbert" poles x15)
//   o    = x + I*cos(phase) + Q*sin(phase)                      (phase = 2*pi*Rate*t)
//   fx   = o
// Both: x = in * inGain;  out = (mix * fx + (1 - mix) * x) * outGain;  one LFO, float32 radians accumulator.
#pragma once
#include "FxModCommon.h"
#include <array>

namespace dt {

struct PhaserParams
{
    float inputGainDb  = 0.0f;    // -60..+6 dB
    float outputGainDb = 0.0f;    // -60..+6 dB
    float type         = 1.0f;    // exactly 0 = MULTI, exactly 1 = BARBER, anything else = bypass (gains only)
    float centreHz     = 100.0f;  // 70..16000 Hz (MULTI only)
    float feedback     = 0.25f;   // 0..1
    float stages       = 16.0f;   // 1..32, floored (MULTI only); each stage = 2 first-order allpasses
    float rateHz       = 1.0f;    // 0.01..50 Hz
    float modRange     = 0.0f;    // 0..1 (MULTI only)
    float wetDry       = 1.0f;    // 0..1
};

class Phaser
{
public:
    static constexpr int kMaxSections = 64;
    static constexpr float kTwoPiF = 6.283185307179586f;
    int startupPreRun = -1;          // fresh instance: LFO runs 2 samples (@48k) ahead (MULTI); -1 = auto
    bool tptAllpass = true;          // allpass topology for the swept sections (see FINDINGS)
    bool useRightQuirk = true;       // reproduce the BARBER right-channel quirk (48 kHz only)
    bool resetOnSwitch = false;      // (rejected hypothesis, kept for experiments)
    bool multiRestart = true;
    bool pauseInactive = true;       // measured: only the selected engine's LFO runs; inactive state is frozen
    int rescaleMode = 1;             // bit0: MULTI, bit1: BARBER phase*rate preserved on rate changes

    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sr = sampleRate;
        static constexpr double hutchins[12] = { 0.3609, 2.7412, 11.1573, 44.7581, 179.6242, 798.4578,
                                                 1.2524, 5.5671, 22.3423, 89.6271, 364.7914, 2770.1114 };
        for (int j = 0; j < 12; ++j)
        {
            const double alpha = fxmod::kPi * hutchins[j] * 15.0 / sr;
            hilbC[j] = -(1.0 - alpha) / (1.0 + alpha);
        }
        for (auto* s : { &inGain, &outGain, &mix, &centreS }) s->reset (sr, 0.005);   // 5 ms linear ramps
        for (auto* s : { &rateS, &rangeS, &fbS }) s->reset (sr, 0.0);                 // NOT smoothed (measured)
        quirkR.b = kRb; quirkR.a = kRa; quirkK.b = kKb; quirkK.a = kKa;
        reset();
    }

    void setRateRampSteps (int n) { rateS.stepsToTarget = n; }
    void setRampSteps (int which, int n) { fxmod::LinearSmoothed* t[] = { &centreS, &rangeS, &fbS, &mix }; t[which]->stepsToTarget = n; }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            for (auto& s : ap[c]) s = {};
            for (auto& s : hb[c]) s = {};
            last[c] = 0.0;
        }
        quirkR.clear(); quirkK.clear(); inL = 0.0;
        phM = 0.0f; phB = 0.0; preRunLeft = 0; first = true; snapRate = false;
    }

    void startFrom (const PhaserParams& init)
    {
        apply (init, true);
        preRunLeft = startupPreRun >= 0 ? startupPreRun : (int) std::lround (2.0 * sr / 48000.0);
        snapRate = true;   // a fresh Thermal instance starts the LFO directly at the loaded rate
        first = false;
    }
    void setParams (const PhaserParams& p)
    {
        rightQuirk = useRightQuirk && std::abs (sr - 48000.0) < 1.0;
        apply (p, first);
        if (snapRate) { rateS.setCurrentAndTarget (rateS.target); snapRate = false; }
        first = false;
        for (; preRunLeft > 0; --preRunLeft) (void) lfoStep (phM, rateS.target);   // MULTI LFO leads by 2 samples
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        float* io[2] = { left, right };
        const double k = sr / (8.0 * fxmod::kPi);
        for (int i = 0; i < numSamples; ++i)
        {
            const float gIn = inGain.next(), gOut = outGain.next(), w = mix.next();
            const float fc = centreS.next(), fb = fbS.next(), mr = rangeS.next(), rate = rateS.next();
            if (rescaleMode & 1) { if (fPrevM > 0.0f && rate != fPrevM) phM = phM * (fPrevM / rate); }
            if (rescaleMode & 2) { if (fPrevB > 0.0f && rate != fPrevB) phB = phB * ((double) fPrevB / rate); }
            fPrevM = fPrevB = rate;
            // the two engines keep separate LFO phases; with pauseInactive only the active engine's LFO runs
            const bool runM = ! pauseInactive || engine == 0, runB = ! pauseInactive || engine == 1;
            const double sM = runM ? lfoStep (phM, rate) : 0.0;
            const double phaseB = phB;                // BARBER: double-precision phase (matches to -100 dB)
            const double sB = std::sin (phaseB);
            if (runB) { phB += fxmod::kTwoPi * (double) rate / sr; if (phB >= fxmod::kTwoPi) phB -= fxmod::kTwoPi; }
            if (engine == 2)
            {
                for (int c = 0; c < 2; ++c) if (io[c] != nullptr) io[c][i] = io[c][i] * gIn * gOut;   // fractional type: no effect
            }
            else if (! barber)
            {
                const double f0 = 0.5 * fc + k * (1.0 + (double) mr * sM);
                const double t = std::tan (fxmod::kPi * std::min (f0, 0.49 * sr) / sr);
                const double a = (t - 1.0) / (t + 1.0);      // direct form coefficient
                const double G = t / (1.0 + t);              // TPT coefficient
                const double g = 0.98 * (double) fb;
                for (int c = 0; c < 2; ++c)
                {
                    if (io[c] == nullptr) continue;
                    const double x = (double) io[c][i] * gIn;
                    double v = x + g * padeTanh (last[c]);
                    for (int n = 0; n < 2 * nStages; ++n)
                    {
                        auto& st = ap[c][(size_t) n];
                        if (tptAllpass)
                        {
                            const double vv = G * (v - st.s);
                            const double lp = vv + st.s;
                            st.s = lp + vv;
                            v = 2.0 * lp - v;
                        }
                        else
                        {
                            const double y = a * v + st.x1 - a * st.y1;
                            st.x1 = v; st.y1 = y; v = y;
                        }
                    }
                    last[c] = v;
                    const double fx = x + 0.5 * v;
                    io[c][i] = (float) ((w * fx + (1.0 - w) * x) * gOut);
                }
            }
            else
            {
                const double cs = std::cos (phaseB), sn = sB;
                const double g = 0.65 * (double) fb;
                for (int c = 0; c < 2; ++c)
                {
                    if (io[c] == nullptr) continue;
                    const double x = (double) io[c][i] * gIn;
                    const double in = x + g * last[c];
                    double v1 = in, v2 = in;
                    for (int j = 0; j < 6; ++j) v1 = hilbStep (hb[c][(size_t) j], hilbC[j], v1);
                    for (int j = 6; j < 12; ++j) v2 = hilbStep (hb[c][(size_t) j], hilbC[j], v2);
                    if (c == 0) inL = in;
                    if (c == 1 && rightQuirk)   // measured Thermal quirk, see FINDINGS (fitted at 48 kHz)
                        v1 = quirkR.step (in) + quirkK.step (inL);
                    const double o = x + v1 * cs + v2 * sn;
                    last[c] = o;
                    io[c][i] = (float) ((w * o + (1.0 - w) * x) * gOut);
                }
            }
        }
    }

private:
    struct DF1 { double x1 = 0, y1 = 0, s = 0; };
    template <int N> struct IIR   // direct form II transposed, N = order
    {
        const double* b = nullptr; const double* a = nullptr; double z[N] {};
        double step (double x) noexcept
        {
            const double y = b[0] * x + z[0];
            for (int k = 0; k < N - 1; ++k) z[k] = b[k + 1] * x - a[k + 1] * y + z[k + 1];
            z[N - 1] = b[N] * x - a[N] * y;
            return y;
        }
        void clear() noexcept { for (auto& v : z) v = 0.0; }
    };
    // BARBER right channel, in-phase path: Thermal does not use the clean Hilbert chain 1 here. Measured
    // (48 kHz) as an LTI filter of the right input (kR) plus a leak of the LEFT input (kK), both multiplied by
    // cos(phase). Order-7 / order-8 least-squares fits of the measured 4096-tap responses (rms error < 3e-5).
    static constexpr double kRb[8] = { 0.075540455364658379, -0.89237216461393842, 2.4836209513999146, -2.1399676128823324, -0.5121083839951317, 1.2970556480422397, -0.091023454259247988, -0.22074185981965286 };
    static constexpr double kRa[8] = { 1, -2.7651795188581496, 1.9461861923199251, 0.717608838771157, -1.0962878768031885, 0.064460504993784265, 0.1506627113231051, -0.017447246442426113 };
    static constexpr double kKb[9] = { 1.8318350857071417e-09, 0.0012859973151650855, -0.02075815345525708, 0.11649063177513429, -0.32610041794032146, 0.51171707789413667, -0.45926285772621456, 0.2208754700870818, -0.044247798196486179 };
    static constexpr double kKa[9] = { 1, -4.5291209272863711, 8.0703505780144962, -6.764776188189261, 2.0226414488931157, 0.77433412493117659, -0.74774998712782126, 0.18775386647048084, -0.01343124360378525 };
    IIR<7> quirkR;
    IIR<8> quirkK;
    double inL = 0.0;

    static double padeTanh (double v) noexcept
    {
        v = std::clamp (v, -3.0, 3.0);
        return v * (27.0 + v * v) / (27.0 + 9.0 * v * v);
    }
    static double hilbStep (DF1& st, double c, double x) noexcept
    {
        const double y = c * (x - st.y1) + st.x1;   // H = (c + z^-1) / (1 + c z^-1)
        st.x1 = x; st.y1 = y;
        return y;
    }
    // float32 radians accumulator (Thermal), returns sin(phase) of the phase used for this sample
    double lfoStep (float& ph, float rate) noexcept
    {
        if (ph >= kTwoPiF) ph -= kTwoPiF;
        const double v = std::sin ((double) ph);
        ph = ph + kTwoPiF * rate / (float) sr;
        if (ph >= kTwoPiF) ph -= kTwoPiF;
        return v;
    }

    void apply (const PhaserParams& p, bool snap)
    {
        auto set = [snap] (fxmod::LinearSmoothed& s, float v) { if (snap) s.setCurrentAndTarget (v); else s.setTarget (v); };
        set (inGain, fxmod::dbToGain (p.inputGainDb));
        set (outGain, fxmod::dbToGain (p.outputGainDb));
        set (mix, std::clamp (p.wetDry, 0.0f, 1.0f));
        set (centreS, std::clamp (p.centreHz, 70.0f, 16000.0f));
        set (fbS, std::clamp (p.feedback, 0.0f, 1.0f));
        set (rangeS, std::clamp (p.modRange, 0.0f, 1.0f));
        set (rateS, std::clamp (p.rateHz, 0.01f, 50.0f));
        nStages = std::clamp ((int) std::floor (p.stages), 1, kMaxSections / 2);
        const int nt = p.type == 0.0f ? 0 : (p.type == 1.0f ? 1 : 2);   // Thermal: only exact 0 / 1 select an engine
        if (nt != engine && ! snap && resetOnSwitch)
        {
            // experimental restart-on-switch (rejected by measurement, resetOnSwitch defaults to false)
            if (nt == 1) { phB = 0.0; for (auto& st : hb) for (auto& v : st) v = {}; quirkR.clear(); quirkK.clear(); }
            if (nt == 0 && multiRestart) { phM = 0.0f; for (auto& st : ap) for (auto& v : st) v = {}; }
            last[0] = last[1] = 0.0;
        }
        engine = nt;
        barber = nt == 1;
    }

    double sr = 48000.0;
    double hilbC[12] {};
    std::array<DF1, kMaxSections> ap[2] {};
    std::array<DF1, 12> hb[2] {};
    double last[2] {};
    float phM = 0.0f;
    double phB = 0.0;
    float fPrevM = -1.0f, fPrevB = -1.0f;
    int preRunLeft = 0, nStages = 16;
    int engine = 1;
    bool barber = true, first = true, snapRate = false, rightQuirk = false;
    fxmod::LinearSmoothed inGain, outGain, mix, centreS, fbS, rangeS, rateS;
};

} // namespace dt
