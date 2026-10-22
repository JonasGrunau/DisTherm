#pragma once

#include "model/Params.h"

#include <cmath>

// Thermal computes its parameter mapping in float, the way juce::NormalisableRange<float>::convertFrom0to1 does
// (start + (end - start) * exp (log (u) / skew)). Several measured behaviours depend on that exact float
// (the bit reducer's ceil of the hold length, the distortion feedback delay), so modulated values are mapped here
// the same way. Unmodulated values come straight from the host parameter, which uses the same range.
namespace dt::juce32
{
    inline float from0to1 (float lo, float hi, float skew, float u) noexcept
    {
       #if defined (__clang__)
        #pragma clang fp contract (off)
       #endif
        u = u < 0.f ? 0.f : (u > 1.f ? 1.f : u);
        if (skew != 1.f && u > 0.f)
            u = std::exp (std::log (u) / skew);
        return lo + (hi - lo) * u;
    }

    inline float plain (const ParamInfo& p, float u) noexcept { return toPlain (p, u); }
    inline float norm (const ParamInfo& p, float plainValue) noexcept { return toNorm (p, plainValue); }
} // namespace dt::juce32
