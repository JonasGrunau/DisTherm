// dt::BitReducer -- black-box reconstruction of Output Thermal's MKUBitReducer effect.
// Header-only, JUCE-free, C++20. See FINDINGS.md for measurements (bit exact against Thermal).
//
//   x_in = InputGain * x                                   (5 ms linear ramp)
//   held = sample-and-hold of x_in, new sample every H = ceil(SampleReduction) samples
//          (one counter shared by both channels, free running since prepare/reset)
//   q    = trunc(held * N) / N  with N = 505 - Bits   (float32, truncation toward zero, no clipping, no dither)
//   y    = OutputGain * (WetDry * q + (1 - WetDry) * x_in)
// Bits (5..500) and SampleReduction (1..300) are applied immediately (no smoothing).
#pragma once

#include "FxdfCommon.h"

namespace dt
{
struct BitReducerParams
{
    float inputGainDb     = 0.0f;   // -60..+6 dB
    float outputGainDb    = 0.0f;   // -60..+6 dB
    float bits            = 5.0f;   // 5..500  (display 0..100 % = (bits-5)/495); levels per unit = 505 - bits
    float sampleReduction = 1.0f;   // 1..300  (hold length = ceil(value) samples)
    float wetDry          = 1.0f;   // 0..1
};

class BitReducer
{
public:
    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sr = sampleRate;
        gains.prepare (sr);
        gains.snap (params.inputGainDb, params.outputGainDb, params.wetDry);
        reset();
    }

    void reset()
    {
        counter = 0;
        held[0] = held[1] = 0.0f;
    }

    void setParams (const BitReducerParams& p)
    {
        params = p;
        gains.set (p.inputGainDb, p.outputGainDb, p.wetDry);
        // Thermal computes these in float32; N is continuous (non-integer Bits give non-integer N).
        levels = 505.0f - std::clamp (p.bits, 5.0f, 500.0f);
        hold = std::max (1, (int) std::ceil (std::clamp (p.sampleReduction, 1.0f, 300.0f)));
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        float* ch[2] = { left, right };
        const int numCh = right != nullptr ? 2 : 1;
        for (int i = 0; i < numSamples; ++i)
        {
            const float ig = (float) gains.in.next();
            const float og = (float) gains.out.next();
            const float w  = (float) gains.wet.next();
            const bool take = (counter == 0);
            if (++counter >= hold) counter = 0;
            for (int c = 0; c < numCh; ++c)
            {
                const float xin = ch[c][i] * ig;
                if (take) held[c] = xin;
                const float q = std::trunc (held[c] * levels) / levels;
                ch[c][i] = og * (w * q + (1.0f - w) * xin);
            }
        }
    }

private:
    double sr = 48000.0;
    BitReducerParams params;
    fxdf::GainStage gains;
    float levels = 500.0f;
    int hold = 1, counter = 0;
    float held[2] {};
};
} // namespace dt
