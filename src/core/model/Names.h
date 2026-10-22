#pragma once

// Names of the stepped values, shared by the DSP (which index means what), the parameter texts and the editor.
// Lists and their order come from Output Thermal 1.2.1 (its strings and display texts).

#include <algorithm>
#include <cmath>

namespace dt::names
{
    inline constexpr const char* kDistortionTypes[] = {
        "Light Shaper", "Heavy Shaper", "AM Shaper", "Wave Fold", "Sine Drive", "Spring Drive", "Linear Sine",
        "Asym Destroy", "Invert Fold", "Exponential", "Soft Clip", "Triangle Clip", "Triangle Fold", "Single Fold",
        "Multi Fold", "Sine Fold", "Sine Shred", "Binary Shred", "S Curve"
    };
    inline constexpr int kNumDistortionTypes = 19;

    // Effect slot order = Thermal's ActiveSlot index.
    inline constexpr const char* kEffects[] = {
        "Bit Reducer", "Chorus", "Compressor", "Delay", "Filter", "Flanger", "Freq Shifter", "Phaser", "Reverb"
    };
    inline constexpr int kNumEffects = 9;
    enum Effect { bitReducer, chorus, compressor, delay, filter, flanger, freqShifter, phaser, reverb };

    inline constexpr const char* kFilterModes[] = { "LP12", "LP24", "HP12", "HP24" };

    // Tempo-synced note values: the delay and the distortion feedback (19), the mod envelopes (26).
    inline constexpr const char* kSyncNotes[] = {
        "1/64 T", "1/64", "1/64 D", "1/32 T", "1/32", "1/32 D", "1/16 T", "1/16", "1/16 D", "1/8 T", "1/8", "1/8 D",
        "1/4 T", "1/4", "1/4 D", "1/2 T", "1/2", "1/2 D", "1 Bar"
    };
    inline constexpr int kNumSyncNotes = 19;
    // Length of each in quarter notes.
    inline constexpr double kSyncNoteBeats[] = {
        1.0 / 24, 1.0 / 16, 3.0 / 32, 1.0 / 12, 1.0 / 8, 3.0 / 16, 1.0 / 6, 1.0 / 4, 3.0 / 8, 1.0 / 3, 1.0 / 2, 3.0 / 4,
        2.0 / 3, 1.0, 3.0 / 2, 4.0 / 3, 2.0, 3.0, 4.0
    };

    inline constexpr const char* kSyncMod[] = {
        "8 Bar", "7 Bar", "6 Bar", "5 Bar", "4 Bar", "3 Bar", "2 Bar", "1 Bar", "1/2 D", "1/2", "1/2 T", "1/4 D", "1/4",
        "1/4 T", "1/8 D", "1/8", "1/8 T", "1/16 D", "1/16", "1/16 T", "1/32 D", "1/32", "1/32 T", "1/64 D", "1/64",
        "1/64 T"
    };
    inline constexpr int kNumSyncMod = 26;
    inline constexpr double kSyncModBeats[] = {
        32, 28, 24, 20, 16, 12, 8, 4, 3, 2, 4.0 / 3, 1.5, 1, 2.0 / 3, 0.75, 0.5, 1.0 / 3, 0.375, 0.25, 1.0 / 6,
        0.1875, 0.125, 1.0 / 12, 0.09375, 0.0625, 1.0 / 24
    };

    // Thermal turns its continuous stepped values into an index by flooring (as its display does).
    inline int stepIndex (double plain, int count) noexcept
    {
        return std::clamp ((int) std::floor (plain + 1e-6), 0, count - 1);
    }
} // namespace dt::names
