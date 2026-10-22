#pragma once

#include "Controls.h"
#include "Layout.h"
#include "Widgets.h"

#include <memory>

class DisTittenProcessor;

// DIS/TITTEN, the slash in the accent, left-aligned in r.
void wordmark (juce::Graphics&, juce::Rectangle<float> r, float height);

// Thermal's XY pad: X moves MACRO 2, Y moves MACRO 1. The field glows in the accent around the puck: the macros
// are what turns the heat up (DisTitten's accent means the heat the distortion adds).
class XYPad : public juce::Component, private juce::AudioProcessorParameter::Listener
{
public:
    XYPad (DisTittenProcessor&, bool large);
    ~XYPad() override;

    void refreshNames();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void parameterValueChanged (int, float) override;
    void parameterGestureChanged (int, bool) override {}
    juce::Rectangle<float> field() const;
    void setFrom (juce::Point<float>);

    DisTittenProcessor& proc;
    bool large;
    juce::RangedAudioParameter &m1, &m2;
    bool dragging = false;
    juce::String name1, name2;
};

// The right-hand column of both pages: preset bar, the two macros, the page switch, master dry/wet with its
// lock, the wordmark, help, the XY readout and the input / output faders with their meters.
class RightColumn : public juce::Component
{
public:
    RightColumn (DisTittenProcessor&);

    std::function<void (int page)> onPage;                      // 0 main, 1 edit
    std::function<void (int source)> onAssign;                  // a macro's "+": mod source 2 or 3
    std::function<void (const juce::String&, bool)> onHelp;
    void setPage (int page);
    void setAssignLit (int source);                             // -1: none
    void tick();                                                // meters, names, readout
    void showHelpText (const juce::String& t);                  // help mode: the readout shows what the hovered control does
    bool helpMode() const noexcept { return helpOn; }

    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<int> local (layout::R r) const { return r.i().translated (-838, 0); }
    void showPresetMenu();
    void stepPreset (int delta);
    void savePresetAs();

    DisTittenProcessor& proc;
    std::unique_ptr<Dial> macro[2];
    std::unique_ptr<juce::Label> macroName[2];
    std::unique_ptr<IconButton> plus[2], pageMain, pageEdit, lock, helpButton, save, prev, next;
    std::unique_ptr<juce::Component> presetButton;
    std::unique_ptr<Fader> dryWet, inFader, outFader;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String shownPreset, helpText;
    bool helpOn = false;
    float m1 = -1.f, m2 = -1.f;
};
