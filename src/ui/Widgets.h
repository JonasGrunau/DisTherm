#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <vector>

// The small controls DisTherm's Thermal layout needs, in the house style (flat, hairlines, radius 2, Plex type,
// ink for state, the accent only for heat). Each value control is bound to one parameter through
// juce::ParameterAttachment and writes host gestures; each reports hover for the help line.

// Base for everything that shows a help sentence while hovered.
struct Helpful
{
    juce::String help;
    std::function<void (const juce::String&, bool)> onHover;   // (help, entering)
};

// An on/off control on a parameter. Thermal keeps several switches as floats with their own "on" rule (AUTO is on
// only at exactly 1, FOLLOW at anything above 0): `threshold` is the plain value from which it reads as on; it
// writes 1 or 0.
class ParamToggle : public juce::Component, public Helpful
{
public:
    enum class Style { power, check, vertical, key, solo };

    ParamToggle (juce::RangedAudioParameter& p, Style s, juce::String text = {}, float threshold = 0.5f);

    bool isOn() const noexcept { return value >= threshold; }
    std::function<void()> onChange;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter& param;
    Style style;
    juce::String text;
    float threshold, value = 0.f;
    juce::ParameterAttachment attachment;
};

// A row of segments, each standing for one plain value of a parameter (filter modes, VINTAGE / MODERN, ...).
// `indexOf` turns the parameter's plain value into the lit segment, the way the DSP reads it.
class SegmentSwitch : public juce::Component, public Helpful
{
public:
    SegmentSwitch (juce::RangedAudioParameter& p, juce::StringArray labels, std::vector<float> values,
                   std::function<int (float)> indexOf);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::StringArray labels;
    std::vector<float> values;
    std::function<int (float)> indexOf;
    float value = 0.f;
    juce::ParameterAttachment attachment;
};

// A fader on a parameter, horizontal or vertical. Drag anywhere, Shift = fine, wheel; double-click resets.
// Optional end labels ("DRY" / "WET") and, for the level faders, a meter drawn under the track.
class Fader : public juce::Component, public Helpful
{
public:
    Fader (juce::RangedAudioParameter& p, bool vertical);

    void setEndLabels (juce::String start, juce::String end) { startLabel = std::move (start); endLabel = std::move (end); repaint(); }
    void setCaption (juce::String c) { faderName = std::move (c); repaint(); }   // vertical faders: the rotated name
    // Once per UI tick: the peak since the last one. Rises at once, falls 1.5 dB a tick (45 dB/s at 30 Hz).
    void setMeter (float dbfs)
    {
        const float next = std::max (dbfs, std::max (-100.f, meterDb - 1.5f));
        if (std::abs (next - meterDb) > 0.25f) { meterDb = next; repaint(); }
    }
    void setShowsValue (bool s) { showsValue = s; repaint(); }
    void setDimmed (bool d) { if (d != dimmed) { dimmed = d; repaint(); } }
    // Thermal's master dry/wet: a tall box filled up to a handle, the end labels standing upright beside it.
    void setBoxed (bool b) { boxed = b; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> track() const;
    void paintBoxed (juce::Graphics&, float norm, bool hot);
    float normAt (juce::Point<float>) const;

    juce::RangedAudioParameter& param;
    bool vertical, dragging = false, showsValue = true, dimmed = false, boxed = false;
    juce::String startLabel, endLabel, faderName;
    float value = 0.f, meterDb = -100.f;
    juce::ParameterAttachment attachment;
};

// A tiny knob without text (Thermal's feedback-release "diamond" under FEEDBACK).
class MiniKnob : public juce::Component, public Helpful
{
public:
    explicit MiniKnob (juce::RangedAudioParameter& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter& param;
    float value = 0.f, lastY = 0.f;
    bool dragging = false;
    juce::ParameterAttachment attachment;
};

// A stepped parameter shown as its current name and a chevron (distortion type, effect type). Click: a menu of
// the names; wheel and step() go to the neighbours. `values[i]` is the plain value of name i.
class ChoiceMenu : public juce::Component, public Helpful
{
public:
    ChoiceMenu (juce::RangedAudioParameter& p, juce::StringArray names, std::function<int (float)> indexOf);

    void step (int delta);
    void setDimmed (bool d) { if (d != dimmed) { dimmed = d; repaint(); } }
    std::function<bool()> onBeforeOpen;   // false: the click did something else (brought a panel forward)
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void choose (int i);

    juce::StringArray names;
    std::function<int (float)> indexOf;
    float value = 0.f, wheel = 0.f;
    bool dimmed = false;
    juce::ParameterAttachment attachment;
};

// A button that runs an action, drawn as one of the house icons or as a caption.
class IconButton : public juce::Component, public Helpful
{
public:
    enum class Icon { none, save, prev, next, help, lock, plus, pageMain, pageEdit };

    explicit IconButton (Icon i, juce::String caption = {}) : icon (i), text (std::move (caption))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setRepaintsOnMouseActivity (true);
    }

    void setLit (bool l) { if (l != lit) { lit = l; repaint(); } }   // a toggle-like state (lock on, page shown)
    bool isLit() const noexcept { return lit; }
    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    Icon icon;
    juce::String text;
    bool lit = false;
};

// A tab of a tab row (stage 01/02/03, WIDTH/TONE, XY/EFFECTS/MASTER standing upright). The selected tab is
// inked and underlined in ink (never the accent).
class TabKey : public juce::Component, public Helpful
{
public:
    TabKey (juce::String caption, bool upright = false) : text (std::move (caption)), vertical (upright)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setRepaintsOnMouseActivity (true);
    }

    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }
    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::String text;
    bool vertical, selected = false;
};

// Draws a caption rotated a quarter turn counter-clockwise inside r (vertical faders, upright tabs).
void uprightCaption (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Colour c, float height);
