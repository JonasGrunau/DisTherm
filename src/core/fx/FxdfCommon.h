// Shared helpers for the fx-dynamics-filter module (Compressor, Filter, BitReducer).
// Header-only, JUCE-free, C++20. Helpers live in dt::fxdf to avoid clashing with other modules.
#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace dt::fxdf
{
inline double dbToGain (double db) noexcept { return std::pow (10.0, db * 0.05); }

// Exact behaviour of juce::LinearSmoothedValue<float/double> (Thermal uses it for every smoothed
// parameter): reset(sr, seconds) -> stepsToTarget = floor(seconds * sr); setTargetValue() restarts the
// countdown only when the new target differs from the current target; getNextValue() returns
// current + step, landing exactly on the target after `steps` calls.
template <typename T = double>
struct LinearSmoothedT
{
    T current = 0, target = 0, step = 0;
    int countdown = 0, steps = 0;

    void reset (double sampleRate, double seconds) noexcept
    {
        steps = (int) std::floor (seconds * sampleRate);
        setCurrentAndTarget (target);
    }
    void setCurrentAndTarget (double v) noexcept { current = target = (T) v; countdown = 0; step = 0; }
    void setTarget (double vd) noexcept
    {
        const T v = (T) vd;
        if (v == target) return;
        if (steps <= 0) { setCurrentAndTarget (v); return; }
        target = v;
        countdown = steps;
        step = (target - current) / (T) countdown;
    }
    bool isSmoothing() const noexcept { return countdown > 0; }
    double next() noexcept
    {
        if (countdown <= 0) return target;
        --countdown;
        if (countdown > 0) current += step; else current = target;
        return current;
    }
};
using LinearSmoothed = LinearSmoothedT<double>;

// Thermal's effect gain staging (measured, identical for Compressor, Filter and BitReducer):
//   x_in  = InputGain * x
//   y     = OutputGain * ( WetDry * effect(x_in) + (1 - WetDry) * x_in )
// InputGain / OutputGain (linear amplitude) and WetDry are linear 5 ms ramps (juce::LinearSmoothedValue).
struct GainStage
{
    LinearSmoothed in, out, wet;
    void prepare (double sr) noexcept { in.reset (sr, 0.005); out.reset (sr, 0.005); wet.reset (sr, 0.005); }
    void snap (double inDb, double outDb, double w) noexcept
    {
        in.setCurrentAndTarget (dbToGain (inDb)); out.setCurrentAndTarget (dbToGain (outDb));
        wet.setCurrentAndTarget (std::clamp (w, 0.0, 1.0));
    }
    void set (double inDb, double outDb, double w) noexcept
    {
        in.setTarget (dbToGain (inDb)); out.setTarget (dbToGain (outDb)); wet.setTarget (std::clamp (w, 0.0, 1.0));
    }
};
} // namespace dt::fxdf
