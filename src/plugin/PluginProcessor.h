#pragma once

#include "analysis/Tap.h"
#include "engine/Engine.h"
#include "model/ModState.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>

// DisTherm: a recreation of Output Thermal's multistage distortion. Three band-split stages (distortion on
// the mid band, one of nine effects, width, gain, tone), two master effect slots, a master compressor and an
// output bus, driven by two drawable mod envelopes and two macros. Every parameter, range and taper is
// Thermal's (src/core/model/Params.h, measured from the original); the switches Thermal keeps in its
// node properties are parameters here too. The editor taps the input and output.
class DisThermProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    DisThermProcessor();
    ~DisThermProcessor() override;

    // ---- AudioProcessor -------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return engine.getTailSeconds(); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::RangedAudioParameter& param (int id) const { return *params[(size_t) id]; }

    // ---- Modulation and presets (message thread) -------------------------------
    const dt::ModState& modState() const noexcept { return *modEdit; }
    void setModState (const dt::ModState&);
    juce::String getMacroName (int macro) const { return macroNames[macro & 1]; }
    void setMacroName (int macro, const juce::String& name) { macroNames[macro & 1] = name; }
    juce::String getEnvelopeName (int e) const { return envelopeNames[e & 1]; }
    void setEnvelopeName (int e, const juce::String& name) { envelopeNames[e & 1] = name; }
    // Loads an Output Thermal preset or state (its XML) into every parameter, the modulation and the macro names.
    bool loadThermalPreset (const juce::File& xmlFile);
    bool loadThermalState (const juce::XmlElement& thermalRoot);

    // Presets: DisTherm's own (.dtpreset, its state XML) in the user folder, and Thermal's factory presets read
    // from the user's Thermal installation. The dry/wet lock keeps the current dry/wet across preset loads.
    struct PresetEntry { juce::File file; juce::String name, folder; bool thermal; };
    static juce::File userPresetFolder();
    std::vector<PresetEntry> listPresets() const;
    bool loadPreset (const juce::File&);
    bool savePreset (const juce::File&);
    juce::String getPresetName() const { return presetName; }
    juce::File getPresetFile() const { return presetFile; }
    bool dryWetLocked = false;

    // ---- For the editor -------------------------------------------------------
    const dt::EngineMeters& meters() const noexcept { return engine.meters(); }
    const dt::Tap& inputTap() const noexcept { return inTap; }
    const dt::Tap& outputTap() const noexcept { return outTap; }
    double getRate() const noexcept { return currentRate.load (std::memory_order_relaxed); }
    // Peak level since the last call, linear.
    float takePeak (bool output) noexcept { return (output ? outPeak : inPeak).exchange (0.f, std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void readParams() noexcept;
    dt::Transport transport() const noexcept;
    void timerCallback() override;   // writes Thermal's LOW/HIGH coupling back into the parameters
    std::unique_ptr<juce::XmlElement> modulationXml() const;
    void readModulationXml (const juce::XmlElement&);

    juce::String presetName { "DEFAULT" };
    juce::File presetFile;
    std::unique_ptr<dt::ModState> modEdit = std::make_unique<dt::ModState>();
    std::unique_ptr<dt::ModStateChannel> modChannel = std::make_unique<dt::ModStateChannel>();
    juce::String macroNames[2] { "MACRO 1", "MACRO 2" }, envelopeNames[2] { "MOD 1", "MOD 2" };

    std::array<juce::RangedAudioParameter*, dt::kNumParams> params {};
    std::array<std::atomic<float>*, dt::kNumParams> rawParams {};
    dt::Engine engine;
    dt::EngineParams block;   // the parameters copied once per block, audio thread only
    dt::Tap inTap, outTap;
    juce::AudioBuffer<float> scratch;   // [0] right channel for a mono input, [1] mono sum for the taps
    std::atomic<double> currentRate { 48000.0 };
    std::atomic<float> inPeak { 0.f }, outPeak { 0.f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DisThermProcessor)
};
