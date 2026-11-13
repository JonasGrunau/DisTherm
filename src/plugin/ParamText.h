#pragma once

#include "Format.h"
#include "model/Names.h"
#include "model/Params.h"

#include <cmath>

// Display text and typed-value parsing for every parameter, by its ParamFormat. The numbers mean what Thermal's
// display shows (same units and rounding, measured from its texts); the typography is DisTherm's (fmt::).
namespace ptext
{
    // "0.26 ms", "16.8 ms", "113 ms", "1.49 s": three significant digits.
    inline juce::String time (double seconds)
    {
        const double ms = seconds * 1000.0;
        if (ms < 9.995) return fmt::fixed (ms, 2) + " ms";
        if (ms < 99.95) return fmt::fixed (ms, 1) + " ms";
        if (ms < 999.5) return fmt::fixed (ms, 0) + " ms";
        return fmt::fixed (seconds, 2) + " s";
    }

    // LFO rates: "0.49 Hz", "12.7 Hz", "50.0 Hz".
    inline juce::String rate (double hz)
    {
        if (hz < 9.995) return fmt::fixed (hz, 2) + " Hz";
        return fmt::fixed (hz, 1) + " Hz";
    }

    inline juce::String percentSigned (double pct)
    {
        const auto body = fmt::fixed (std::abs (pct), 0);
        return (pct < 0.0 && body != "0" ? fmt::minus : juce::String()) + body + " %";
    }

    inline juce::String text (const dt::ParamInfo& p, double plain)
    {
        using F = dt::ParamFormat;
        namespace n = dt::names;
        const double norm = dt::toNorm (p, plain);
        switch (p.format)
        {
            case F::decibels:       return fmt::decibels (plain, 1);
            case F::hertz:          return fmt::hertz (plain);
            case F::rateHz:         return rate (plain);
            case F::percentNorm:    return fmt::percent (norm * 100.0);
            case F::percentValue:   return percentSigned (plain * 100.0);
            case F::percentOfRange: return fmt::percent ((plain - p.lo) / (p.hi - p.lo) * 100.0);
            case F::integer:        return juce::String ((int) std::floor (plain + 1e-6));
            case F::ratio:          return juce::String ((int) std::floor (plain + 1e-6)) + ":1";
            case F::time:           return time (plain);
            case F::toggle:
            case F::onOff:          return plain >= 0.5 ? "On" : "Off";
            case F::distortionType: return n::kDistortionTypes[n::stepIndex (plain, n::kNumDistortionTypes)];
            case F::filterMode:     return n::kFilterModes[n::stepIndex (plain, 4)];
            // Thermal switches these only at the exact end values (measured): Ping Pong and Modern need 1.0, and
            // the phaser plays dry anywhere between its two types.
            case F::delayType:      return plain >= 1.0 ? "Ping Pong" : "Stereo";
            case F::flangerType:    return plain >= 1.0 ? "Modern" : "Vintage";
            case F::phaserType:     return plain <= 0.0 ? "Multi" : (plain >= 1.0 ? "Barber" : "Dry");
            case F::syncNote:       return n::kSyncNotes[n::stepIndex (plain, n::kNumSyncNotes)];
            case F::syncMod:        return n::kSyncMod[n::stepIndex (plain, n::kNumSyncMod)];
            case F::effectType:     return n::kEffects[n::stepIndex (plain, n::kNumEffects)];
        }
        return juce::String (plain, 3);
    }

    // What someone typed back to a plain value; names match case-insensitively.
    inline double parse (const dt::ParamInfo& p, const juce::String& typed)
    {
        using F = dt::ParamFormat;
        namespace n = dt::names;
        const auto t = typed.trim();
        const auto byName = [&] (const char* const* list, int count) -> double {
            for (int i = 0; i < count; ++i)
                if (t.equalsIgnoreCase (list[i])) return (double) i;
            return juce::jlimit (0.0, (double) count - 1, fmt::parse (t));
        };
        switch (p.format)
        {
            case F::percentNorm:    return dt::toPlain (p, fmt::parse (t) / 100.0);
            case F::percentValue:   return fmt::parse (t) / 100.0;
            case F::percentOfRange: return p.lo + (p.hi - p.lo) * fmt::parse (t) / 100.0;
            case F::time:           return t.containsIgnoreCase ("ms") || ! t.containsIgnoreCase ("s") ? fmt::parse (t) / 1000.0
                                                                                                     : fmt::parse (t);
            case F::toggle:
            case F::onOff:          return t.equalsIgnoreCase ("on") || fmt::parse (t) >= 0.5 ? 1.0 : 0.0;
            case F::distortionType: return byName (n::kDistortionTypes, n::kNumDistortionTypes);
            case F::filterMode:     return byName (n::kFilterModes, 4);
            case F::syncNote:       return byName (n::kSyncNotes, n::kNumSyncNotes);
            case F::syncMod:        return byName (n::kSyncMod, n::kNumSyncMod);
            case F::effectType:     return byName (n::kEffects, n::kNumEffects);
            case F::delayType:      return t.containsIgnoreCase ("ping") ? 1.0 : 0.0;
            case F::flangerType:    return t.containsIgnoreCase ("mod") ? 1.0 : 0.0;
            case F::phaserType:     return t.containsIgnoreCase ("bar") ? 1.0 : 0.0;
            case F::decibels:
            case F::hertz:
            case F::rateHz:
            case F::integer:
            case F::ratio:          return fmt::parse (t);
        }
        return fmt::parse (t);
    }
} // namespace ptext
