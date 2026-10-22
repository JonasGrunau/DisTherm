// dt::Compressor -- black-box reconstruction of Output Thermal's MKUCompressor (used for the effect
// slots AND for the always-on MasterCompressor node: both measured bit identical, no latency).
// Header-only, JUCE-free, C++20. See FINDINGS.md.
//
//   x_in  = InputGain * x                       (both channels)
//   d     = (|x_in,L| + |x_in,R|) / 2           (stereo-linked: mean of the rectified channels)
//   env   = d + c * (env - d),  c = exp(-ln(9) / (T * fs)),  T = Attack if d > env else Release
//           (peak detector, linear domain, branching one-pole; T is the 10 %..90 % time of a step;
//            the effective attack is max(Attack, 0.1 ms): the bottom 24 % of the knob (0.02..0.1 ms) do nothing;
//            QUIRK: when the previous sample's env is below -30 dBFS (0.031623) the release uses 5x the release
//            time, EXCEPT on the first sample of every processing block (a per-block flag that starts false), so the
//            effective slow decay depends on the host block size B: rate = (1 + (B-1)/5) / B of the normal one
//            (0.2016 at B=512, 0.2125 at B=64, 0.25 at B=16) -- measured exactly that way in Thermal)
//   L     = 20 log10(env)
//   GR    = max( min(0, (Threshold - L) * (1 - 1/Ratio)), -30 dB )   (hard knee, max 30 dB reduction)
//   wet   = x_in * 10^(GR/20) * Makeup
//   y     = OutputGain * (WetDry * wet + (1 - WetDry) * x_in)
// Threshold (dB), Ratio, Makeup (linear amplitude), In/Out gains (linear amplitude) and WetDry are per-sample 5 ms
// linear ramps. Attack and Release times (seconds) are 5 ms linear ramps too, but advanced once per processing
// block (SmoothedValue::skip(numSamples)): the coefficients of a block use the smoothed time at the block's end.
// No lookahead, no sidechain filter, no clipping.
#pragma once

#include "FxdfCommon.h"

namespace dt
{
struct CompressorParams
{
    float inputGainDb  = 0.0f;    // -60..+6 dB
    float outputGainDb = 0.0f;    // -60..+6 dB
    float thresholdDb  = 0.0f;    // -30..0 dB
    float ratio        = 4.0f;    // 2..20
    float attackSec    = 0.012f;  // 2e-5..0.1 s   (Thermal default 12 ms)
    float releaseSec   = 0.05f;   // 0.05..2 s
    float makeupDb     = 0.0f;    // 0..24 dB
    float wetDry       = 1.0f;    // 0..1
};

class Compressor
{
public:
    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sr = sampleRate;
        gains.prepare (sr);
        thr.reset (sr, 0.005); rat.reset (sr, 0.005); mk.reset (sr, 0.005);
        gains.snap (params.inputGainDb, params.outputGainDb, params.wetDry);
        thr.setCurrentAndTarget (params.thresholdDb);
        rat.setCurrentAndTarget (params.ratio);
        mk.setCurrentAndTarget (fxdf::dbToGain (params.makeupDb));
        attSm.reset (sr, 0.005); relSm.reset (sr, 0.005);
        attSm.setCurrentAndTarget (params.attackSec);
        relSm.setCurrentAndTarget (params.releaseSec);
        updateCoeffs (attSm.current, relSm.current);
        reset();
    }

    void reset() { env = 0.0; lastGrDb = 0.0; }

    void setParams (const CompressorParams& p)
    {
        params = p;
        gains.set (p.inputGainDb, p.outputGainDb, p.wetDry);
        thr.setTarget (std::clamp (p.thresholdDb, -30.0f, 0.0f));
        rat.setTarget (std::clamp (p.ratio, 1.0f, 20.0f));
        mk.setTarget (fxdf::dbToGain (std::clamp (p.makeupDb, 0.0f, 24.0f)));
        attSm.setTarget (std::clamp (p.attackSec, 2e-5f, 0.1f));
        relSm.setTarget (std::clamp (p.releaseSec, 0.05f, 2.0f));
    }

    // gain reduction (dB, <= 0) of the last processed sample, for a meter
    double getGainReductionDb() const noexcept { return lastGrDb; }

    void process (float* left, float* right, int numSamples) noexcept
    {
        process (left, right, numSamples, nullptr, nullptr, 0);
    }

    // The same, with the input/output gain targets (dB) re-read every `chunk` samples from per-chunk arrays.
    // Thermal reads modulation of the master compressor's gains every 32 samples while everything else about the
    // compressor stays per block (the slow-release flag, the attack/release smoothing).
    void process (float* left, float* right, int numSamples, const float* inDbPerChunk, const float* outDbPerChunk,
                  int chunk) noexcept
    {
        if (numSamples <= 0) return;
        updateCoeffs (skip (attSm, numSamples), skip (relSm, numSamples));
        bool slow = false;
        for (int i = 0; i < numSamples; ++i)
        {
            if (chunk > 0 && i % chunk == 0)
            {
                gains.in.setTarget (fxdf::dbToGain (inDbPerChunk[i / chunk]));
                gains.out.setTarget (fxdf::dbToGain (outDbPerChunk[i / chunk]));
            }
            const double ig = gains.in.next(), og = gains.out.next(), w = gains.wet.next();
            const double t = thr.next(), r = rat.next(), m = mk.next();
            const double xl = (double) left[i] * ig;
            const double xr = right != nullptr ? (double) right[i] * ig : xl;
            const double d = 0.5 * (std::abs (xl) + std::abs (xr));
            const double c = d > env ? ca : (slow ? crSlow : cr);
            env = d + c * (env - d);
            slow = env < kSlowKnee;
            double gr = 0.0;
            if (env > 0.0)
            {
                const double lev = 20.0 * std::log10 (env);
                gr = std::max (std::min (0.0, (t - lev) * (1.0 - 1.0 / r)), -30.0);
            }
            lastGrDb = gr;
            const double g = std::pow (10.0, gr * 0.05) * m;
            left[i] = (float) (og * (w * g * xl + (1.0 - w) * xl));
            if (right != nullptr)
                right[i] = (float) (og * (w * g * xr + (1.0 - w) * xr));
        }
    }

private:
    // juce::SmoothedValue::skip(): advance numSamples steps at once and return the new current value
    static double skip (fxdf::LinearSmoothed& s, int numSamples) noexcept
    {
        if (numSamples >= s.countdown) { s.setCurrentAndTarget (s.target); return s.target; }
        s.current += s.step * (double) numSamples;
        s.countdown -= numSamples;
        return s.current;
    }

    void updateCoeffs (double attackSec, double releaseSec) noexcept
    {
        const double ln9 = std::log (9.0);
        ca = std::exp (-ln9 / (std::max (attackSec, 1.0e-4) * sr));   // attack clamped at 0.1 ms (measured)
        cr = std::exp (-ln9 / (std::max (releaseSec, 1.0e-4) * sr));
        crSlow = std::exp (-ln9 / (5.0 * std::max (releaseSec, 1.0e-4) * sr));
    }

    static constexpr double kSlowKnee = 0.031622776601683794;   // -30 dBFS

    double sr = 48000.0;
    CompressorParams params;
    fxdf::GainStage gains;
    fxdf::LinearSmoothed thr, rat, mk, attSm, relSm;
    double ca = 0.0, cr = 0.0, crSlow = 0.0, env = 0.0, lastGrDb = 0.0;
};
} // namespace dt
