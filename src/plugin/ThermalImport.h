#pragma once

#include "model/ModState.h"
#include "model/Params.h"

#include <juce_core/juce_core.h>

#include <array>

// Reads an Output Thermal state or preset (the XML document Thermal saves: <Thermal><App_State>...) into
// DisTitten's parameters. Thermal's 410 host parameters map 1:1 by their XML tag (same normalised values,
// same tapers); the switches Thermal keeps in node properties and processor-group state map to DisTitten's
// switch parameters (see kParams[].thermalTag: "<node>.<attribute>", "!" = inverted).
//
// DisTitten carries Thermal's factory presets and modulation shapes (presets/thermal in the repo, one zip in
// the binary). They are unpacked into DisTitten's own folder (application data / DisTitten / Factory) the first
// time they are asked for, and again whenever the carried copy differs from what is there.
namespace thermal
{
    juce::File presetFolder();             // .../Factory/Presets/<category>/<NNN name>.xml
    juce::File modulationPresetFolder();   // .../Factory/SubPresets/Modulation Presets/<category>/<name>.xml

    struct Patch
    {
        std::array<float, dt::kNumParams> norm {};   // normalised value of every DisTitten parameter
        juce::String macroNames[2], envelopeNames[2];
        std::unique_ptr<dt::ModState> mod = std::make_unique<dt::ModState>();   // mod slots + envelope shapes
    };

    // False if the document isn't a Thermal state.
    bool read (const juce::XmlElement& root, Patch& out);
    bool read (const juce::File& file, Patch& out);
} // namespace thermal
