#pragma once

#include "Controls.h"
#include "Layout.h"
#include "MainViews.h"
#include "StageView.h"
#include "Widgets.h"
#include "model/ModState.h"

#include <memory>

class DisThermProcessor;

// The band strip over the edit page, drawn like Thermal's: the frequency axis (four segments: 30-100-500-5k-22k Hz),
// the selected stage as a column over its band (power, name, the puck that moves the band, a handle on each edge,
// LOW and HIGH under it), the other stages as thin tracks over theirs (click one to select it). The selected
// column is marked in the accent: it is where that stage adds its heat.
class BandStrip : public juce::Component
{
public:
    explicit BandStrip (DisThermProcessor&);

    std::function<void (int)> onSelectStage;
    std::function<void (const juce::String&, bool)> onHelp;
    void setStage (int s) { if (s != stage) { stage = s; placePower(); repaint(); } }
    void tick();

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    float xOf (double hz) const;
    double hzAt (float x) const;
    float plain (int id) const;
    juce::Range<float> bandOf (int s) const;   // x extent of a stage's band (the whole axis while BAND SPLIT is off)
    float trackY (int s) const;                // where an unselected stage's track runs
    int hit (juce::Point<float>) const;        // 0 LOW handle, 1 HIGH handle, 2 band, 10 + s another stage, -1 nothing
    void placePower();

    DisThermProcessor& proc;
    std::unique_ptr<ParamToggle> power[3];
    int stage = 0, dragWhat = -1, hover = -1;
    float dragStartX = 0.f;
    double dragLo = 0, dragHi = 0;
    float shown[3][4] {};
    int shownStage = -1;
};

// One of the two mod envelopes: header (assign, name, presets, SYNC, RNDM, CLEAR, power), RATE and HUMANIZE,
// and the drawable shape.
class ModPanel : public juce::Component
{
public:
    ModPanel (DisThermProcessor&, int envelope);

    std::function<void (int source)> onAssign;
    std::function<void (const juce::String&, bool)> onHelp;
    void setAssignLit (bool lit) { plus->setLit (lit); }
    void tick();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> local (layout::R r) const { return r.i().translated (0, -(int) (350 + layout::modStep * (float) env)); }
    juce::Rectangle<float> editor() const;
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> fromScreen (juce::Point<float>) const;
    void publish (const dt::ModState::Shape& shape);
    void randomise();
    void clear();
    void stepPreset (int delta);
    void savePreset();
    void loadShapeFile (const juce::File&);

    DisThermProcessor& proc;
    int env;
    std::unique_ptr<IconButton> plus, save, prev, next, rndm, clr;
    std::unique_ptr<ParamToggle> sync, power;
    std::unique_ptr<Dial> rate, humanize;
    std::unique_ptr<juce::FileChooser> chooser;
    int dragPoint = -1, dragControl = -1;
    float lastValue = -1.f;
    juce::File shapeFile;
};

// The master section (MASTER tab): output filters, the master compressor with its gain-reduction gauge, its
// input / output faders and dry/wet.
class MasterView : public juce::Component
{
public:
    explicit MasterView (DisThermProcessor&);
    std::function<void (const juce::String&, bool)> onHelp;
    void tick();
    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<int> local (layout::R r) const { return r.i().translated (-419, -350); }
    DisThermProcessor& proc;
    std::unique_ptr<Dial> hp, lp, thresh, ratio, attack, release, gain;
    std::unique_ptr<Fader> in, out, wet;
    std::unique_ptr<ParamToggle> power;
    float gr = 0.f;
};

// The lower right panel of the edit page: XY / EFFECTS / MASTER, its tabs standing on the right edge.
class LowerPanel : public juce::Component
{
public:
    explicit LowerPanel (DisThermProcessor&);
    std::function<void (const juce::String&, bool)> onHelp;
    void showTab (int t);
    int tab() const noexcept { return current; }
    void tick();
    void refreshNames() { xy->refreshNames(); }
    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<int> local (layout::R r) const { return r.i().translated (-419, -350); }
    DisThermProcessor& proc;
    int current = 0;
    std::unique_ptr<TabKey> tabs[3];
    std::unique_ptr<XYPad> xy;
    std::unique_ptr<ChoiceMenu> slotType[2];
    std::unique_ptr<ParamToggle> slotOn[2];
    std::unique_ptr<EffectView> slot[2];
    std::unique_ptr<MasterView> master;
};
