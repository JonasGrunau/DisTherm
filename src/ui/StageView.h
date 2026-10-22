#pragma once

#include "Controls.h"
#include "Layout.h"
#include "Widgets.h"

#include <array>
#include <memory>
#include <vector>

class DisTittenProcessor;

// Where an effect slot's controls go (sheet coordinates): Thermal places them differently in a stage
// (knobs 74 px apart) and in the master FX 1 / FX 2 slots (64 px).
struct EffectGeometry
{
    layout::R in, out, wet;
    float knobY, knobCentre, knobStep, switchY;
};

// One effect slot's nine effect layouts; only the selected effect's controls are visible.
// slot 0..2 = stage effects, 3 = FX 1, 4 = FX 2.
class EffectView : public juce::Component
{
public:
    EffectView (DisTittenProcessor&, int slot, EffectGeometry, juce::Rectangle<int> sheetArea);

    void showEffect (int effect);   // 0..8, names::Effect
    int shownEffect() const noexcept { return shown; }
    void setGainReduction (float db);   // the compressor's GR line
    std::function<void (const juce::String&, bool)> onHelp;

    void paint (juce::Graphics&) override;

private:
    struct Layout
    {
        std::vector<std::unique_ptr<juce::Component>> parts;
        std::vector<Dial*> dials;
    };
    juce::Rectangle<int> local (layout::R r) const;
    void build (int effect);

    DisTittenProcessor& proc;
    int slot, shown = -1;
    EffectGeometry geo;
    juce::Point<int> origin;
    std::array<Layout, 9> layouts;
    std::unique_ptr<Fader> inFader, outFader, wetFader;
    float grDb = 0.f;
    Dial* phaserDims[3] {};   // dimmed in BARBER mode
};

// The transfer curve of the stage's distortion as it is set (drive, type, the four shape controls).
class ShapeView : public juce::Component, private juce::AudioProcessorParameter::Listener
{
public:
    ShapeView (DisTittenProcessor&, int stage);
    ~ShapeView() override;
    void paint (juce::Graphics&) override;

private:
    void parameterValueChanged (int, float) override { triggerRepaint(); }
    void parameterGestureChanged (int, bool) override {}
    void triggerRepaint();

    DisTittenProcessor& proc;
    int stage;
    std::vector<juce::AudioProcessorParameter*> watched;
};

// Everything that belongs to one stage between the band strip and the mod panels: the stage panel (LOW / HIGH,
// solo, band split, refilter), the distortion panel or the stage effect (tabs), and WIDTH / TONE (tabs).
class StageView : public juce::Component
{
public:
    StageView (DisTittenProcessor&, int stage);

    void setHelpCallback (std::function<void (const juce::String&, bool)> cb);
    void tick();   // meters (the effect's gain reduction), dimming
    void showDistortion (bool distortion);
    void showTone (bool tone);

    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<int> local (layout::R r) const { return r.i().translated (0, -155); }
    void updateDimming();

    DisTittenProcessor& proc;
    int stage;
    bool distortionInFront = true, toneInFront = false;

    std::unique_ptr<Dial> low, high, drive, out, c1, c2, c3, c4, time, fb, sides, width, toneLow, toneHigh;
    std::unique_ptr<ParamToggle> solo, split, refilter, autoKey, syncKey, followKey, distoOn, fxOn, widthOn, toneOn;
    std::unique_ptr<MiniKnob> release;
    std::unique_ptr<ChoiceMenu> type, fxType;
    std::unique_ptr<IconButton> prev, next;
    std::unique_ptr<TabKey> widthTab, toneTab;
    std::unique_ptr<ShapeView> shapeView;
    std::unique_ptr<EffectView> effect;
};
