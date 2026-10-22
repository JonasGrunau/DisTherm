#pragma once

#include "PluginProcessor.h"
#include "ui/Controls.h"
#include "ui/EditViews.h"
#include "ui/Layout.h"
#include "ui/MainViews.h"
#include "ui/StageView.h"

#include <array>
#include <memory>
#include <vector>

// The editor: Output Thermal's layout (src/ui/Layout.h, measured from Thermal itself) drawn in the house style,
// on one 1123 x 701 sheet scaled as a whole. Two pages like Thermal's: the XY page (the heat pad and the right
// column) and the edit page (band strip, the selected stage, the two mod envelopes, XY / EFFECTS / MASTER).
//
// Modulation is assigned like in Thermal: a source's "+" (a macro's, or an envelope's) lights, and then dragging
// any knob sets how far that source moves it. Every knob with modulation shows where its modulated value is now.
//
// Nothing repaints on the timer unless what it shows changed.
class DisTittenEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kWidth = layout::kWidth, kHeight = layout::kHeight;

    explicit DisTittenEditor (DisTittenProcessor&);
    ~DisTittenEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override {}

    void tick();   // one UI update (timer; the snapshot tool calls it directly)
    // UI state for the snapshot tool and tests: page (0 XY, 1 edit), stage (0..2), lower (0 XY, 1 effects, 2 master),
    // stagefx (0 distortion, 1 effect), tone (0 width, 1 tone), assign (-1..3).
    void setUiState (const juce::StringPairArray&);

private:
    struct Sheet : juce::Component
    {
        explicit Sheet (DisTittenEditor& e) : editor (e) { setOpaque (true); }
        void paint (juce::Graphics&) override;
        DisTittenEditor& editor;
    };

    void timerCallback() override { tick(); }
    void showPage (int p);
    void selectStage (int s);
    void setAssign (int source);
    void showHelp (const juce::String& text, bool entering);
    void collectDials (juce::Component&);
    void wireModulation (Dial&);

    DisTittenProcessor& proc;
    SheetLookAndFeel lnf;
    Sheet sheet { *this };

    RightColumn column;
    XYPad bigXY;
    BandStrip bandStrip;
    std::array<std::unique_ptr<StageView>, 3> stages;
    std::array<std::unique_ptr<TabKey>, 3> stageTabs;
    std::array<std::unique_ptr<ParamToggle>, 3> stagePower;
    std::array<std::unique_ptr<ModPanel>, 2> mods;
    LowerPanel lower;

    std::vector<Dial*> dials;
    std::unique_ptr<dt::ModState> editState;   // the routing being edited while a knob is dragged in assign mode
    juce::String shownNames;
    int page = 0, stage = 0, assign = -1, ticks = 0;
};
