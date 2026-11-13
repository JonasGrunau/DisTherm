// Shared helpers for the fx-modulation module (Chorus / Flanger / Phaser).
// Header-only, JUCE-free, C++20. Part of the DisTherm re-implementation of Output Thermal.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace dt::fxmod {

// Linear ramp with exactly the semantics of juce::SmoothedValue<float, ValueSmoothingTypes::Linear>
// (measured in Thermal: the first value returned after a target change is already one step along,
// the target is reached after floor(rampSeconds * sampleRate) samples).
struct LinearSmoothed
{
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int countdown = 0, stepsToTarget = 0;

    void reset (double sampleRate, double rampSeconds) noexcept
    {
        stepsToTarget = (int) std::floor (rampSeconds * sampleRate);
        setCurrentAndTarget (target);
    }
    void setCurrentAndTarget (float v) noexcept { current = target = v; countdown = 0; }
    void setTarget (float v) noexcept
    {
        if (v == target) return;
        if (stepsToTarget <= 0) { setCurrentAndTarget (v); return; }
        target = v;
        countdown = stepsToTarget;
        step = (target - current) / (float) countdown;
    }
    bool isSmoothing() const noexcept { return countdown > 0; }
    float next() noexcept
    {
        if (countdown <= 0) return target;
        --countdown;
        if (countdown > 0) current += step; else current = target;
        return current;
    }
};

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

// Thermal's LFO phase: a float32 accumulator in cycles [0, 1), incremented by float(rate / sr) per sample.
// The float rounding of the increment is audible as a +-0.1 % rate error that depends on the phase binade;
// keeping it float reproduces Thermal's trajectory to ~1e-5 samples.
struct CyclePhase
{
    float p = 0.0f;
    float advance (float inc) noexcept   // returns the phase used for this sample, then advances
    {
        const float out = p;
        p = p + inc;
        if (p >= 1.0f) p -= 1.0f;
        return out;
    }
};

// 4-point Catmull-Rom (cubic Hermite) interpolation, t in [0,1) between p1 and p2.
inline float catmullRom (float p0, float p1, float p2, float p3, float t) noexcept
{
    const float t2 = t * t, t3 = t2 * t;
    const float w0 = 0.5f * (-t3 + 2.0f * t2 - t);
    const float w1 = 0.5f * (3.0f * t3 - 5.0f * t2 + 2.0f);
    const float w2 = 0.5f * (-3.0f * t3 + 4.0f * t2 + t);
    const float w3 = 0.5f * (t3 - t2);
    return w0 * p0 + w1 * p1 + w2 * p2 + w3 * p3;
}

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;

} // namespace dt::fxmod
