// dt::Filter -- black-box reconstruction of Output Thermal's MKUFilter effect.
// Header-only, JUCE-free, C++20. See FINDINGS.md.
//
// The core is (measured to -86..-140 dB null) the juce::dsp::LadderFilter algorithm:
//   * 4 identical one-pole stages  y = b1*s[i-1] + a1*s[i] + b0*x  with a1 = exp(-2 pi fc / fs),
//     g = 1 - a1, b0 = g/1.3, b1 = 0.3 g/1.3  (zero at z = -0.3), unit-delay global feedback from stage 4,
//   * tanh saturation through a 128-point lookup table on [-5, 5] (linear interpolation, clamped),
//     applied to the driven input and to the feedback,
//   * resonance k = 4 * (0.1 + 0.9 * Resonance), "comp" (= 0.5 for low-pass modes, 0 for high-pass) adds
//     dx * comp back into the feedback term (gain compensation),
//   * output = 1.2 * tap mix: LP12 = s2, LP24 = s4, HP12 = s0 - 2 s1 + s2, HP24 = s0 - 4 s1 + 6 s2 - 4 s3 + s4,
//   * drive: gain = drive^-2.642 * 0.6103 + 0.3903, drive2 = 0.04 drive + 0.96, gain2 likewise,
//   * cutoff (as a1) and resonance smoothed by 50 ms linear ramps (juce::LinearSmoothedValue, restarted
//     whenever a new target arrives), drive unsmoothed, a mode change resets the filter state.
// Thermal's additions around it:
//   * Mode = floor(mapped value) in {0: LP12, 1: LP24, 2: HP12, 3: HP24}.
//   * Envelope follower on the RAW effect input (before InputGain): d = |L + R| / 2, peak one-pole with
//     attack 4 ms / release 100 ms, coefficient exp(ln(0.01) / (T fs)). Once per processing block (after
//     running the follower over the whole block) the cutoff target is set to
//         u  = clamp(u_cutoff + 2 * FollowAmount * env, 0, 1),  fc = 30 + 21970 * u^4
//     where u_cutoff = ((Cutoff - 30) / 21970)^(1/4) is the knob's normalised position. The ladder's
//     50 ms a1 smoother then ramps toward it (so the follower depends on the host block size, as in Thermal).
//   * Gain staging: x_in = InputGain * x, y = OutputGain * (WetDry * ladder(x_in) + (1 - WetDry) * x_in).
// Precision: ladder state in double; the follower, the a1 / resonance smoothers and the a1 targets are float32 like
// Thermal's (JUCE float) code, which is needed to null below -100 dB when the cutoff moves at low frequencies.
#pragma once

#include "FxdfCommon.h"
#include <array>

namespace dt
{
struct FilterParams
{
    float inputGainDb  = 0.0f;      // -60..+6 dB
    float outputGainDb = 0.0f;      // -60..+6 dB
    float cutoffHz     = 22000.0f;  // 30..22000 Hz (taper u^4)
    float resonance    = 0.1f;      // 0..0.9
    float drive        = 1.0f;      // 1..10
    float mode         = 1.0f;      // 0..3, floor -> LP12, LP24, HP12, HP24
    float followAmount = 0.0f;      // -3..3 (display -300..300 %)
    float wetDry       = 1.0f;      // 0..1
};

class Filter
{
public:
    Filter() { initLut(); }

    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sr = sampleRate;
        gains.prepare (sr);
        gains.snap (params.inputGainDb, params.outputGainDb, params.wetDry);
        a1Smooth.reset (sr, 0.05);
        resSmooth.reset (sr, 0.05);
        // follower ballistics: exp(ln(0.01) / (T * fs)), attack 4 ms, release 100 ms
        envAtt = std::exp (std::log (0.01) / (0.004 * sr));
        envRel = std::exp (std::log (0.01) / (0.100 * sr));
        applyStaticParams (params);
        a1Smooth.setCurrentAndTarget (cutoffToA1 (params.cutoffHz));
        resSmooth.setCurrentAndTarget (0.1 + 0.9 * (double) params.resonance);
        reset();
    }

    void reset()
    {
        for (auto& s : state) s.fill (0.0);
        envF = 0.0f;
        a1Smooth.setCurrentAndTarget (a1Smooth.target);
        resSmooth.setCurrentAndTarget (resSmooth.target);
    }

    void setParams (const FilterParams& p)
    {
        params = p;
        gains.set (p.inputGainDb, p.outputGainDb, p.wetDry);
        applyStaticParams (p);
        resSmooth.setTarget (0.1 + 0.9 * (double) std::clamp (p.resonance, 0.0f, 1.0f));
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        if (numSamples <= 0) return;
        const int numCh = right != nullptr ? 2 : 1;
        float* ch[2] = { left, right };

        // 1) envelope follower over the whole block (raw input, before InputGain). float32 on purpose:
        //    Thermal's follower and cutoff smoother run in float and the rounding is audible in the null test
        {
            float e = envF;
            const float ea = (float) envAtt, er = (float) envRel;
            for (int i = 0; i < numSamples; ++i)
            {
                const float d = std::abs (left[i] + (right != nullptr ? right[i] : left[i])) * 0.5f;
                const float c = d > e ? ea : er;
                e = d + c * (e - d);
            }
            envF = e;
        }
        // 2) one cutoff target per block
        double fc = params.cutoffHz;
        if (params.followAmount != 0.0f)
        {
            const float u = std::clamp ((float) uCut + 2.0f * params.followAmount * envF, 0.0f, 1.0f);
            fc = (double) (30.0f + 21970.0f * (u * u) * (u * u));
        }
        a1Smooth.setTarget (cutoffToA1 (fc));

        // 3) ladder, per-sample smoothers (JUCE order: update smoothers, then all channels)
        for (int i = 0; i < numSamples; ++i)
        {
            const double a1 = a1Smooth.next();
            const double r  = resSmooth.next();
            const double ig = gains.in.next(), og = gains.out.next(), w = gains.wet.next();
            const double g = 1.0 - a1, b0 = g * 0.76923076923, b1 = g * 0.23076923076;
            for (int c = 0; c < numCh; ++c)
            {
                auto& s = state[(size_t) c];
                const double xin = (double) ch[c][i] * ig;
                const double dx = gain * sat (drive * xin);
                const double a = dx + r * -4.0 * (gain2 * sat (drive2 * s[4]) - dx * comp);
                const double b = b1 * s[0] + a1 * s[1] + b0 * a;
                const double cc = b1 * s[1] + a1 * s[2] + b0 * b;
                const double d = b1 * s[2] + a1 * s[3] + b0 * cc;
                const double e = b1 * s[3] + a1 * s[4] + b0 * d;
                s = { a, b, cc, d, e };
                const double wet = A[0] * a + A[1] * b + A[2] * cc + A[3] * d + A[4] * e;
                ch[c][i] = (float) (og * (w * wet + (1.0 - w) * xin));
            }
        }
    }

private:
    static constexpr int lutPoints = 128;

    void initLut()
    {
        // juce::dsp::LookupTableTransform<float> { tanh, -5, 5, 128 }
        for (int i = 0; i < lutPoints; ++i)
            lut[(size_t) i] = std::tanh (-5.0 + 10.0 * (double) i / (double) (lutPoints - 1));
        lut[(size_t) lutPoints] = lut[(size_t) lutPoints - 1];
    }

    double sat (double x) const noexcept
    {
        constexpr double scaler = (double) (lutPoints - 1) / 10.0, offset = 5.0 * scaler;
        x = std::clamp (x, -5.0, 5.0);
        const double idx = scaler * x + offset;
        const int i = std::min ((int) idx, lutPoints - 1);
        const double f = idx - (double) i;
        return lut[(size_t) i] + f * (lut[(size_t) i + 1] - lut[(size_t) i]);
    }

    double cutoffToA1 (double fc) const noexcept { return (double) std::exp ((float) fc * (float) (-2.0 * 3.14159265358979323846 / sr)); }

    void applyStaticParams (const FilterParams& p)
    {
        // Mode: Thermal floors the mapped value (tiny epsilon guards our own float rounding)
        const int m = std::clamp ((int) std::floor ((double) p.mode + 1e-4), 0, 3);
        if (m != mode)
        {
            mode = m;
            static constexpr double taps[4][5] = { { 0, 0, 1, 0, 0 }, { 0, 0, 0, 0, 1 }, { 1, -2, 1, 0, 0 }, { 1, -4, 6, -4, 1 } };
            for (int i = 0; i < 5; ++i) A[(size_t) i] = 1.2 * taps[m][i];
            comp = (m <= 1) ? 0.5 : 0.0;
            if (prepared) reset();   // juce::dsp::LadderFilter::setMode() calls reset(): state cleared (audible click)
        }
        prepared = true;
        drive = std::clamp ((double) p.drive, 1.0, 10.0);
        gain = std::pow (drive, -2.642) * 0.6103 + 0.3903;
        drive2 = drive * 0.04 + 0.96;
        gain2 = std::pow (drive2, -2.642) * 0.6103 + 0.3903;
        const double fc = std::clamp ((double) p.cutoffHz, 30.0, 22000.0);
        uCut = std::pow ((fc - 30.0) / 21970.0, 0.25);
        params.cutoffHz = (float) fc;
    }

    double sr = 48000.0;
    FilterParams params;
    fxdf::GainStage gains;
    // float on purpose: juce::dsp::LadderFilter<float> accumulates `current += step` in float; at low cutoffs
    // (a1 close to 1) the rounding shifts the cutoff by up to a few percent while it moves (measured)
    fxdf::LinearSmoothedT<float> a1Smooth, resSmooth;
    std::array<double, lutPoints + 1> lut {};
    std::array<std::array<double, 5>, 2> state {};
    std::array<double, 5> A { 0, 0, 0, 0, 1.2 };
    int mode = 1;
    bool prepared = false;
    double comp = 0.5, drive = 1.0, gain = 1.0006, drive2 = 1.0, gain2 = 1.0006;
    double uCut = 1.0, envAtt = 0.0, envRel = 0.0;
    float envF = 0.0f;
};
} // namespace dt
