// Shapes.h - Output Thermal distortion-node (MKUDustDistoNode) static waveshaper curves, measured black-box.
// Header-only, JUCE-free C++20.  See FINDINGS.md for how every constant was measured and the residuals.
//
//   y = shapeEval(type, x, c)        x = signal AFTER the DRIVE gain (InputGain, linear), c = Control1..4 in 0..1
//                                    (host names SHAPE, DEPTH, FREQUENCY, CLIP), y = signal BEFORE OutputGain.
//
// All 19 public types (and the hidden indices 32/33) are memoryless: y depends only on the current x and c.
// The curve already contains Thermal's per-type "control gain" (each control lowers the output by a fixed,
// type-specific number of dB, see kGain below); nothing else (no DC blocker, no dry/wet, no post filter) is
// inside the shaper.  Thermal runs the shaper oversampled with a linear-phase anti-alias filter; that belongs
// to the distortion-node model (it calls shapeApply at the oversampled rate).
//
// Usage for real-time code: compute ShapeCoeffs once per parameter change (pow/exp are in there), then call
// shapeApply per (oversampled) sample.  shapeEval() is the convenience one-shot version.
#pragma once

#include <algorithm>
#include <cmath>

namespace dt
{
namespace shape_detail
{
    constexpr double kPi = 3.14159265358979323846;

    // JUCE NormalisableRange-style skew: value = lo + (hi - lo) * c^(1/skew)
    inline double sk (double c, double skew) noexcept
    {
        c = std::clamp (c, 0.0, 1.0);
        return skew == 1.0 ? c : std::pow (c, 1.0 / skew);
    }

    // Triangle wavefolder: identity on [-1, 1], reflects at +-1 (period 4).
    inline double fold (double u) noexcept
    {
        double p = std::fmod (u + 1.0, 4.0);
        if (p < 0.0) p += 4.0;
        return p < 2.0 ? p - 1.0 : 3.0 - p;
    }

    // Triangle wave, period 1 in th, sine-like phase: tw(0)=0, tw(0.25)=1, tw(0.5)=0, tw(0.75)=-1.
    inline double triWave (double th) noexcept { return fold (4.0 * th); }

    inline double clip1 (double v) noexcept { return std::clamp (v, -1.0, 1.0); }

    inline double sgn (double v) noexcept { return (v > 0.0) - (v < 0.0); }

    // Per-type output gain law: gain_dB = -sum_k w_k * c_k^(1/s_k).  Controls a type does not use cost 1 dB per unit.
    struct GainTerm { double w, s; };
    constexpr GainTerm kGain[19][4] = {
        { { 10.0, 1.5  }, { 1.0, 1.0 }, { 1.0, 1.0 }, { 1.0, 1.0 } },   //  0 Light Shaper
        { { 12.0, 1.65 }, { 1.0, 1.0 }, { 1.0, 1.0 }, { 1.0, 1.0 } },   //  1 Heavy Shaper
        { {  2.0, 1.0  }, { 3.0, 1.9 }, { 0.1, 1.0 }, { 1.0, 1.0 } },   //  2 AM Shaper
        { {  5.0, 1.9  }, { 1.0, 1.0 }, { 1.0, 1.0 }, { 1.0, 1.0 } },   //  3 Wave Fold
        { {  6.0, 1.9  }, { 1.0, 1.0 }, { 1.0, 1.0 }, { 1.0, 1.0 } },   //  4 Sine Drive
        { {  9.0, 1.0  }, { 1.0, 1.0 }, { 1.0, 1.0 }, { 1.0, 1.0 } },   //  5 Spring Drive
        { {  1.0, 1.0  }, { 0.1, 1.9 }, { 1.0, 1.0 }, { 1.0, 1.0 } },   //  6 Linear Sine
        { {  3.0, 1.9  }, { 0.1, 1.9 }, { 3.0, 1.9 }, { 1.0, 1.0 } },   //  7 Asym Destroy
        { {  8.0, 1.4  }, { 1.0, 1.0 }, { 1.0, 1.0 }, { 1.0, 1.0 } },   //  8 Invert Fold
        { {  1.0, 1.0  }, { 1.0, 1.0 }, { 1.0, 1.0 }, { 1.0, 1.0 } },   //  9 Exponential
        { {  1.0, 1.0  }, { 1.0, 1.0 }, { 1.0, 1.0 }, { 1.0, 1.0 } },   // 10 Soft Clip
        { {  1.0, 1.0  }, { 1.0, 1.0 }, { 1.0, 1.0 }, { 1.0, 1.0 } },   // 11 Triangle Clip
        { {  1.0, 1.0  }, { 1.0, 1.0 }, { 1.0, 1.0 }, { 1.0, 1.0 } },   // 12 Triangle Fold
        { {  1.0, 1.0  }, { 1.0, 1.0 }, { 1.0, 1.0 }, { 1.0, 1.0 } },   // 13 Single Fold
        { {  1.0, 1.0  }, { 1.0, 1.0 }, { 1.0, 1.0 }, { 1.0, 1.0 } },   // 14 Multi Fold
        { {  1.0, 1.0  }, { 1.0, 1.0 }, { 1.0, 1.0 }, { 1.0, 1.0 } },   // 15 Sine Fold
        { {  1.0, 1.0  }, { 1.0, 1.0 }, { 1.0, 1.0 }, { 1.0, 1.0 } },   // 16 Sine Shred
        { {  1.0, 1.0  }, { 1.0, 1.0 }, { 1.0, 1.0 }, { 1.0, 1.0 } },   // 17 Binary Shred
        { {  1.0, 1.0  }, { 1.0, 1.0 }, { 1.0, 1.0 }, { 1.0, 1.0 } },   // 18 S Curve
    };
} // namespace shape_detail

enum ShapeType : int
{
    kLightShaper = 0, kHeavyShaper, kAMShaper, kWaveFold, kSineDrive, kSpringDrive, kLinearSine, kAsymDestroy,
    kInvertFold, kExponential, kSoftClip, kTriangleClip, kTriangleFold, kSingleFold, kMultiFold, kSineFold,
    kSineShred, kBinaryShred, kSCurve,
    kNumPublicShapes = 19,
    kHidden32 = 32,   // 0.5*tanh(c1*x): Light Shaper with unmapped SHAPE (see FINDINGS.md)
    kHidden33 = 33    // tanh(c1*x):     Heavy Shaper with unmapped SHAPE
};

// Thermal's DistortionType parameter: normalised u in 0..1 maps to 0..33 and is FLOORED (measured).
// Indices 19..31 crash the original plugin (segfault); shapeApply returns silence for them.
inline int shapeTypeFromNormalised (double u) noexcept
{
    const float mapped = 33.0f * static_cast<float> (std::clamp (u, 0.0, 1.0));
    return std::clamp (static_cast<int> (std::floor (mapped)), 0, 33);
}

// Precomputed per-parameter coefficients (everything that needs pow/exp).
struct ShapeCoeffs
{
    int type = 0;
    double gain = 1.0;          // linear control-gain (already includes all four controls)
    double a = 0, b = 0, d = 0, e = 0, f = 0, g = 0;   // type-specific, see shapePrepare
};

inline ShapeCoeffs shapePrepare (int type, const double c[4]) noexcept
{
    using namespace shape_detail;
    ShapeCoeffs k;
    k.type = type;
    const double c1 = std::clamp (c[0], 0.0, 1.0), c2 = std::clamp (c[1], 0.0, 1.0),
                 c3 = std::clamp (c[2], 0.0, 1.0), c4 = std::clamp (c[3], 0.0, 1.0);
    const double cc[4] = { c1, c2, c3, c4 };

    double gdb = 0.0;
    if (type >= 0 && type < kNumPublicShapes)
        for (int i = 0; i < 4; ++i)
            gdb -= kGain[type][i].w * sk (cc[i], kGain[type][i].s);
    else
        gdb = -(c1 + c2 + c3 + c4);   // hidden 32/33: plain 1 dB per unit on every control
    k.gain = std::pow (10.0, gdb / 20.0);

    switch (type)
    {
        case kLightShaper:  k.a = 1.5 + 48.5 * sk (c1, 0.6); break;                       // tanh slope
        case kHeavyShaper:  k.a = 1.5 + 664.5 * sk (c1, 0.3); break;                      // tanh slope
        case kAMShaper:
            k.a = 2.5 + 22.5 * sk (c1, 0.7);                // tanh slope        (SHAPE)
            k.b = 0.1 + 0.9 * sk (c2, 0.7);                 // AM depth          (DEPTH)
            k.d = kPi * (0.9 + 29.1 * sk (c3, 0.7));        // AM rate in rad/x  (FREQUENCY)
            k.e = 0.9 * sk (c4, 0.7);                       // gate threshold    (CLIP)
            break;
        case kWaveFold:     k.a = 1.0 / (1.0 - 0.88 * sk (c1, 1.25)); break;              // fold gain on sin(4x)
        case kSineDrive:    k.a = kPi * (1.32 + 28.68 * sk (c1, 0.4)); break;             // rad per unit x
        case kSpringDrive:  k.a = 1.0 / (0.64 - 0.6 * sk (c1, 1.9)); break;               // cycles per x^2
        case kLinearSine:
            k.a = c1;                                       // AM depth on x     (SHAPE)
            k.b = 2.0 * kPi * (1.0 + 29.0 * sk (c2, 0.4));  // rad per unit |x|  (DEPTH)
            break;
        case kAsymDestroy:
            k.a = 2.65 + 47.35 * c1;                        // x>0 sine rate     (SHAPE)
            k.b = 666.0 * sk (c2, 1.4);                     // x>0 ripple rate   (DEPTH)
            k.d = 1.0 / (1.0 - 0.96 * sk (c3, 1.99));       // x<0 fold gain     (FREQUENCY)
            break;
        case kInvertFold:   k.a = 2.0 + 18.0 * c1 * c1; break;
        case kHidden32:
        case kHidden33:     k.a = c1; break;
        default: break;
    }
    return k;
}

inline double shapeApply (const ShapeCoeffs& k, double x) noexcept
{
    using namespace shape_detail;
    double y = 0.0;
    switch (k.type)
    {
        case kLightShaper: y = 0.5 * std::tanh (k.a * x); break;
        case kHeavyShaper: y = std::tanh (k.a * x); break;
        case kAMShaper:
        {
            const double u = std::tanh (k.a * x) + k.b * std::sin (k.d * x);
            y = std::fabs (u) > k.e ? clip1 (u) : 0.0;
            break;
        }
        case kWaveFold:  y = fold (k.a * std::sin (4.0 * x)); break;
        case kSineDrive: y = std::sin (k.a * x); break;
        case kSpringDrive:
        {
            double xw = x;
            const double ax = std::fabs (x);
            if (ax > 3.0) xw = sgn (x) * (1.0 + std::fmod (ax - 1.0, 2.0));   // input wraps into (-3, 3)
            y = triWave (xw * std::fabs (xw) * k.a);
            break;
        }
        case kLinearSine: y = clip1 (x * (1.0 + k.a * std::sin (k.b * std::fabs (x)))); break;
        case kAsymDestroy:
            if (x < 0.0) y = fold (k.d * std::sin (4.0 * x));
            else         y = clip1 ((16.0 / 15.0) * std::sin (k.a * x) - (1.0 / 15.0) * std::sin (k.b * x));
            break;
        case kInvertFold:
        {
            const double s = std::tanh (k.a * x);
            y = s - std::tanh (k.a * s);
            break;
        }
        case kExponential: y = sgn (x) * (1.0 - std::exp (-std::fabs (x))); break;
        case kSoftClip:    y = std::fabs (x) < 1.0 ? x - x * x * x / 3.0 : sgn (x) * 0.666666; break;
        case kTriangleClip:
        {
            const double a = std::fabs (x);
            y = sgn (x) * (a < 0.5 ? 2.0 * a : (a < 1.5 ? 2.0 - 2.0 * a : -1.0));
            break;
        }
        case kTriangleFold:
        {
            const double a = std::fabs (x);
            y = sgn (x) * (1.0 - std::fabs (1.0 - 2.0 * (a - std::floor (a))));
            break;
        }
        case kSingleFold:  y = 2.0 * x / (1.0 + x * x); break;
        case kMultiFold:   y = fold (x); break;
        case kSineFold:    y = sgn (x) * std::fabs (std::sin (2.0 * x)); break;
        case kSineShred:   y = 1.2 * std::sin (std::fmod (x, 1.0)); break;
        case kBinaryShred: y = std::fabs (x) > 0.2 ? 0.8 * sgn (x) : 0.0; break;
        case kSCurve:      y = std::tanh (2.718281828459045 * x); break;
        case kHidden32:    y = 0.5 * std::tanh (k.a * x); break;
        case kHidden33:    y = std::tanh (k.a * x); break;
        default:           y = 0.0; break;   // 19..31: the original crashes; we output silence
    }
    return y * k.gain;
}

// One-shot evaluation (computes the coefficients every call; use shapePrepare/shapeApply in real-time loops).
inline double shapeEval (int type, double x, const double c[4]) noexcept
{
    return shapeApply (shapePrepare (type, c), x);
}

} // namespace dt
