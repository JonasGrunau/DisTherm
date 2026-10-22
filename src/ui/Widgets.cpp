#include "Widgets.h"

using namespace theme;

void uprightCaption (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Colour c, float height)
{
    juce::Graphics::ScopedSaveState save (g);
    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, r.getCentreX(), r.getCentreY()));
    const auto turned = juce::Rectangle<float> (r.getHeight(), r.getWidth()).withCentre (r.getCentre());
    caption (g, s, turned, juce::Justification::centred, c, height);
}

namespace
{
    void hoverHelp (Helpful& h, bool entering)
    {
        if (h.onHover) h.onHover (h.help, entering);
    }
} // namespace

// ---- ParamToggle -------------------------------------------------------------------

ParamToggle::ParamToggle (juce::RangedAudioParameter& p, Style s, juce::String t, float th)
    : param (p), style (s), text (std::move (t)), threshold (th),
      attachment (p, [this] (float v) { value = v; repaint(); if (onChange) onChange(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setRepaintsOnMouseActivity (true);
}

void ParamToggle::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const bool on = isOn(), hot = isMouseOver();
    switch (style)
    {
        case Style::power:
        {
            // A power symbol: an open ring and a stroke through its gap.
            const float d = std::min (b.getWidth(), b.getHeight()) - 6.f;
            const auto r = juce::Rectangle<float> (d, d).withCentre (b.getCentre());
            juce::Path p;
            p.addCentredArc (r.getCentreX(), r.getCentreY(), d * 0.5f, d * 0.5f, 0.f, 0.7f, juce::MathConstants<float>::twoPi - 0.7f, true);
            p.startNewSubPath (r.getCentreX(), r.getY() - 1.f);
            p.lineTo (r.getCentreX(), r.getCentreY());
            g.setColour (on ? col::ink : hot ? col::inkSoft : col::inkFaint);
            g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
        case Style::check:
        {
            const auto r = b.reduced (0.5f);
            g.setColour (hot ? col::ink : col::inkSoft);
            g.drawRoundedRectangle (r, kRadius, stroke::hairline);
            if (on)
            {
                g.setColour (col::ink);
                g.fillRoundedRectangle (r.reduced (2.5f), 1.f);
            }
            break;
        }
        case Style::vertical:
        case Style::key:
        case Style::solo:
        {
            const auto r = b.reduced (0.5f);
            if (on)
            {
                g.setColour (col::ink);
                g.fillRoundedRectangle (r, kRadius);
            }
            else
            {
                g.setColour (hot ? col::inkSoft : col::hairline);
                g.drawRoundedRectangle (r, kRadius, stroke::hairline);
            }
            const auto c = on ? col::sheet : hot ? col::ink : col::inkSoft;
            if (style == Style::vertical) uprightCaption (g, text, r, c, 8.5f);
            else caption (g, text, r, juce::Justification::centred, c, style == Style::solo ? 8.5f : 9.f);
            break;
        }
    }
}

void ParamToggle::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    attachment.setValueAsCompleteGesture (isOn() ? param.convertFrom0to1 (0.f) : param.convertFrom0to1 (1.f));
}

void ParamToggle::mouseEnter (const juce::MouseEvent&) { hoverHelp (*this, true); }
void ParamToggle::mouseExit (const juce::MouseEvent&) { hoverHelp (*this, false); }

// ---- SegmentSwitch -------------------------------------------------------------------

SegmentSwitch::SegmentSwitch (juce::RangedAudioParameter& p, juce::StringArray l, std::vector<float> v, std::function<int (float)> f)
    : labels (std::move (l)), values (std::move (v)), indexOf (std::move (f)),
      attachment (p, [this] (float x) { value = x; repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setRepaintsOnMouseActivity (true);
}

void SegmentSwitch::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const int n = labels.size();
    const int sel = indexOf (value);
    const float w = b.getWidth() / (float) n;
    g.setColour (col::hairline);
    g.drawRoundedRectangle (b, kRadius, stroke::hairline);
    for (int i = 0; i < n; ++i)
    {
        const auto r = juce::Rectangle<float> (b.getX() + w * (float) i, b.getY(), w, b.getHeight());
        if (i == sel)
        {
            g.setColour (col::ink);
            g.fillRoundedRectangle (r.reduced (1.f), kRadius);
        }
        else if (i > 0 && i != sel + 1)
        {
            g.setColour (col::hairline);
            g.fillRect (r.getX(), r.getY() + 3.f, stroke::hairline, r.getHeight() - 6.f);
        }
        caption (g, labels[i], r, juce::Justification::centred, i == sel ? col::sheet : col::inkSoft, 8.5f);
    }
}

void SegmentSwitch::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    const int i = juce::jlimit (0, labels.size() - 1, (int) (e.position.x / ((float) getWidth() / (float) labels.size())));
    attachment.setValueAsCompleteGesture (values[(size_t) i]);
}

void SegmentSwitch::mouseEnter (const juce::MouseEvent&) { hoverHelp (*this, true); }
void SegmentSwitch::mouseExit (const juce::MouseEvent&) { hoverHelp (*this, false); }

// ---- Fader -------------------------------------------------------------------

Fader::Fader (juce::RangedAudioParameter& p, bool v)
    : param (p), vertical (v), attachment (p, [this] (float x) { value = x; repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (vertical ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::LeftRightResizeCursor);
    setRepaintsOnMouseActivity (true);
}

juce::Rectangle<float> Fader::track() const
{
    auto b = getLocalBounds().toFloat();
    if (vertical)
        return b.removeFromLeft (std::min (b.getWidth(), 8.f)).reduced (0.f, 2.f);
    if (boxed)
        return b.reduced (22.f, 1.f);
    const float labelW = startLabel.isEmpty() ? 0.f : 30.f;
    return b.withTrimmedLeft (labelW).withTrimmedRight (endLabel.isEmpty() ? 0.f : 30.f).withSizeKeepingCentre (b.getWidth() - 2.f * labelW, 4.f);
}

float Fader::normAt (juce::Point<float> p) const
{
    const auto t = track();
    return vertical ? juce::jlimit (0.f, 1.f, (t.getBottom() - p.y) / t.getHeight())
                    : juce::jlimit (0.f, 1.f, (p.x - t.getX()) / t.getWidth());
}

void Fader::paint (juce::Graphics& g)
{
    const auto t = track();
    const float norm = param.convertTo0to1 (value);
    const bool hot = isMouseOverOrDragging() && ! dimmed;
    if (boxed) { paintBoxed (g, norm, hot); return; }
    g.setColour (col::well);
    g.fillRoundedRectangle (t, kRadius);
    g.setColour (col::hairline);
    g.drawRoundedRectangle (t, kRadius, stroke::hairline);

    if (vertical)
    {
        if (meterDb > -60.f)   // the level under the fader: -60..+6 dBFS over the track
        {
            const float m = juce::jlimit (0.f, 1.f, (meterDb + 60.f) / 66.f);
            g.setColour (col::inkFaint);
            g.fillRect (t.withTop (t.getBottom() - m * t.getHeight()).reduced (2.f, 0.f));
        }
        const float y = t.getBottom() - norm * t.getHeight();
        g.setColour (dimmed ? col::inkFaint : hot ? col::ink : col::inkSoft);
        g.fillRoundedRectangle (juce::Rectangle<float> (t.getWidth() + 6.f, 3.f).withCentre ({ t.getCentreX(), y }), 1.f);
        if (faderName.isNotEmpty())
            uprightCaption (g, faderName, getLocalBounds().toFloat().withTrimmedLeft (t.getWidth() + 2.f), col::inkSoft, 8.5f);
        if (showsValue && hot)
        {
            g.setColour (col::ink);
            g.setFont (mono (9.f));
            g.drawText (param.getText (norm, 0), getLocalBounds().toFloat().withHeight (11.f).translated (0.f, -12.f).expanded (20.f, 0.f),
                        juce::Justification::centred, false);
        }
        return;
    }

    const float x = t.getX() + norm * t.getWidth();
    g.setColour (dimmed ? col::inkFaint : col::inkSoft);
    g.fillRect (t.withRight (x).reduced (0.f, 1.f));
    g.setColour (dimmed ? col::inkFaint : hot ? col::ink : col::inkSoft);
    g.fillRoundedRectangle (juce::Rectangle<float> (3.f, t.getHeight() + 10.f).withCentre ({ x, t.getCentreY() }), 1.f);
    const auto b = getLocalBounds().toFloat();
    if (startLabel.isNotEmpty())
        caption (g, startLabel, b.withWidth (28.f), juce::Justification::centredLeft, col::inkSoft, 8.5f);
    if (endLabel.isNotEmpty())
        caption (g, hot && showsValue ? param.getText (norm, 0) : endLabel, b.withTrimmedLeft (b.getWidth() - 40.f),
                 juce::Justification::centredRight, hot && showsValue ? col::ink : col::inkSoft, 8.5f);
}

void Fader::paintBoxed (juce::Graphics& g, float norm, bool hot)
{
    const auto t = track();
    g.setColour (col::well);
    g.fillRoundedRectangle (t, kRadius);
    const float x = t.getX() + norm * t.getWidth();
    g.setColour (col::hairline);
    g.fillRoundedRectangle (t.withRight (std::max (t.getX() + 1.f, x)).reduced (1.f), kRadius);
    g.setColour (col::hairline);
    g.drawRoundedRectangle (t.reduced (0.5f), kRadius, stroke::hairline);

    // The handle: a key standing in the box, a grip line down its middle.
    const auto handle = juce::Rectangle<float> (14.f, t.getHeight() + 4.f).withCentre ({ juce::jlimit (t.getX() + 7.f, t.getRight() - 7.f, x), t.getCentreY() });
    g.setColour (col::sheet);
    g.fillRoundedRectangle (handle, kRadius);
    g.setColour (dimmed ? col::inkFaint : hot ? col::ink : col::inkSoft);
    g.drawRoundedRectangle (handle.reduced (0.5f), kRadius, stroke::hairline);
    g.fillRect (handle.getCentreX() - 0.75f, handle.getY() + 8.f, 1.5f, handle.getHeight() - 16.f);

    const auto b = getLocalBounds().toFloat();
    if (startLabel.isNotEmpty()) uprightCaption (g, startLabel, b.withWidth (t.getX() - 2.f), col::inkSoft, 8.5f);
    if (endLabel.isNotEmpty()) uprightCaption (g, endLabel, b.withLeft (t.getRight() + 2.f), col::inkSoft, 8.5f);
    if (hot && showsValue)
    {
        g.setColour (col::ink);
        g.setFont (mono (10.f));
        const bool left = norm > 0.5f;   // the value sits on the side of the box away from the handle
        g.drawText (param.getText (norm, 0), t.reduced (10.f, 0.f), left ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
    }
}

void Fader::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    attachment.beginGesture();
    if (! e.mods.isShiftDown())
        attachment.setValueAsPartOfGesture (param.convertFrom0to1 (normAt (e.position)));
}

void Fader::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    if (e.mods.isShiftDown())
    {
        const float delta = (vertical ? -(float) e.getDistanceFromDragStartY() : (float) e.getDistanceFromDragStartX()) * 0.0005f;
        attachment.setValueAsPartOfGesture (param.convertFrom0to1 (juce::jlimit (0.f, 1.f, param.convertTo0to1 (value) + delta)));
        return;
    }
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (normAt (e.position)));
}

void Fader::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    attachment.endGesture();
}

void Fader::mouseDoubleClick (const juce::MouseEvent&)
{
    if (dragging) { dragging = false; attachment.endGesture(); }
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}

void Fader::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float d = (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.f : 1.f);
    const float n = juce::jlimit (0.f, 1.f, param.convertTo0to1 (value) + d * 0.05f * (e.mods.isShiftDown() ? 0.1f : 1.f));
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (n));
}

void Fader::mouseEnter (const juce::MouseEvent&) { hoverHelp (*this, true); }
void Fader::mouseExit (const juce::MouseEvent&) { hoverHelp (*this, false); }

// ---- MiniKnob -------------------------------------------------------------------

MiniKnob::MiniKnob (juce::RangedAudioParameter& p)
    : param (p), attachment (p, [this] (float x) { value = x; repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity (true);
}

void MiniKnob::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (1.f);
    const float d = std::min (b.getWidth(), b.getHeight());
    const auto r = juce::Rectangle<float> (d, d).withCentre (b.getCentre());
    const bool hot = isMouseOverOrDragging();
    g.setColour (col::raised);
    g.fillEllipse (r);
    g.setColour (hot ? col::rimHot : col::rim);
    g.drawEllipse (r, stroke::hairline);
    const float a = juce::MathConstants<float>::pi * (-0.75f + 1.5f * param.convertTo0to1 (value));
    const auto c = r.getCentre();
    g.setColour (col::ink);
    g.drawLine (c.x, c.y, c.x + d * 0.42f * std::sin (a), c.y - d * 0.42f * std::cos (a), 1.4f);
}

void MiniKnob::mouseDown (const juce::MouseEvent& e) { dragging = true; lastY = e.position.y; attachment.beginGesture(); }

void MiniKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float dy = lastY - e.position.y;
    lastY = e.position.y;
    const float n = juce::jlimit (0.f, 1.f, param.convertTo0to1 (value) + dy / 220.f * (e.mods.isShiftDown() ? 0.08f : 1.f));
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (n));
    if (onHover) onHover (help + " " + param.getText (n, 0), true);
}

void MiniKnob::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    attachment.endGesture();
}

void MiniKnob::mouseDoubleClick (const juce::MouseEvent&)
{
    if (dragging) { dragging = false; attachment.endGesture(); }
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}

void MiniKnob::mouseEnter (const juce::MouseEvent&)
{
    if (onHover) onHover (help + " " + param.getText (param.convertTo0to1 (value), 0), true);
}

void MiniKnob::mouseExit (const juce::MouseEvent&) { hoverHelp (*this, false); }

// ---- ChoiceMenu -------------------------------------------------------------------

ChoiceMenu::ChoiceMenu (juce::RangedAudioParameter& p, juce::StringArray n, std::function<int (float)> f)
    : names (std::move (n)), indexOf (std::move (f)),
      attachment (p, [this] (float x) { value = x; repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setRepaintsOnMouseActivity (true);
}

void ChoiceMenu::choose (int i)
{
    i = juce::jlimit (0, names.size() - 1, i);
    attachment.setValueAsCompleteGesture ((float) i);
}

void ChoiceMenu::step (int delta)
{
    const int n = names.size();
    choose (((indexOf (value) + delta) % n + n) % n);
}

void ChoiceMenu::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const bool hot = isMouseOver();
    g.setColour (hot ? col::inkSoft : col::hairline);
    g.drawRoundedRectangle (b, kRadius, stroke::hairline);
    auto r = b.reduced (kMinSidePad, 0.f);
    // the chevron
    const auto chev = r.removeFromRight (8.f).withSizeKeepingCentre (8.f, 4.f);
    juce::Path p;
    p.startNewSubPath (chev.getX(), chev.getY());
    p.lineTo (chev.getCentreX(), chev.getBottom());
    p.lineTo (chev.getRight(), chev.getY());
    g.setColour (col::inkSoft);
    g.strokePath (p, juce::PathStrokeType (1.2f));
    caption (g, names[juce::jlimit (0, names.size() - 1, indexOf (value))], r, juce::Justification::centredLeft,
             dimmed ? col::inkFaint : col::ink, 9.5f);
}

void ChoiceMenu::mouseDown (const juce::MouseEvent&)
{
    if (onBeforeOpen && ! onBeforeOpen()) return;
    juce::PopupMenu m;
    const int cur = indexOf (value);
    for (int i = 0; i < names.size(); ++i)
        m.addItem (i + 1, names[i], true, i == cur);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                     [safe = juce::Component::SafePointer<ChoiceMenu> (this)] (int r) {
                         if (safe != nullptr && r > 0) safe->choose (r - 1);
                     });
}

void ChoiceMenu::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    wheel += w.deltaY * (w.isReversed ? -1.f : 1.f);
    if (std::abs (wheel) >= 0.15f)
    {
        step (wheel > 0.f ? -1 : 1);
        wheel = 0.f;
    }
}

void ChoiceMenu::mouseEnter (const juce::MouseEvent&) { hoverHelp (*this, true); }
void ChoiceMenu::mouseExit (const juce::MouseEvent&) { hoverHelp (*this, false); }

// ---- IconButton -------------------------------------------------------------------

void IconButton::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const bool hot = isMouseOver();
    const auto c = lit ? col::ink : hot ? col::ink : col::inkSoft;
    g.setColour (c);
    const auto s = std::min (b.getWidth(), b.getHeight());
    const auto r = juce::Rectangle<float> (s, s).withCentre (b.getCentre()).reduced (s * 0.25f);
    juce::Path p;
    const juce::PathStrokeType st (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    switch (icon)
    {
        case Icon::none:
        {
            const auto rr = b.reduced (0.5f);
            if (lit) { g.fillRoundedRectangle (rr, kRadius); }
            else { g.setColour (hot ? col::inkSoft : col::hairline); g.drawRoundedRectangle (rr, kRadius, stroke::hairline); }
            caption (g, text, rr, juce::Justification::centred, lit ? col::sheet : hot ? col::ink : col::inkSoft, 8.5f);
            return;
        }
        case Icon::save:
            p.addRoundedRectangle (r, 1.5f);
            p.startNewSubPath (r.getX() + r.getWidth() * 0.25f, r.getY());
            p.lineTo (r.getX() + r.getWidth() * 0.25f, r.getY() + r.getHeight() * 0.35f);
            p.lineTo (r.getRight() - r.getWidth() * 0.25f, r.getY() + r.getHeight() * 0.35f);
            p.lineTo (r.getRight() - r.getWidth() * 0.25f, r.getY());
            g.strokePath (p, st);
            return;
        case Icon::prev:
        case Icon::next:
        {
            const bool left = icon == Icon::prev;
            const auto t = r.withSizeKeepingCentre (r.getWidth() * 0.6f, r.getHeight() * 0.9f);
            p.startNewSubPath (left ? t.getRight() : t.getX(), t.getY());
            p.lineTo (left ? t.getX() : t.getRight(), t.getCentreY());
            p.lineTo (left ? t.getRight() : t.getX(), t.getBottom());
            p.closeSubPath();
            g.strokePath (p, st);
            return;
        }
        case Icon::help:
            g.drawEllipse (r.expanded (2.f), 1.2f);
            caption (g, "?", r.expanded (2.f), juce::Justification::centred, c, 9.5f);
            return;
        case Icon::lock:
        {
            const auto body = r.withTrimmedTop (r.getHeight() * 0.45f);
            if (lit) g.fillRoundedRectangle (body, 1.5f);
            else g.drawRoundedRectangle (body, 1.5f, 1.2f);
            p.addCentredArc (body.getCentreX(), body.getY(), body.getWidth() * 0.3f, r.getHeight() * 0.32f, 0.f,
                             -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.strokePath (p, st);
            return;
        }
        case Icon::plus:
        {
            const auto cr = r.expanded (3.f);
            if (lit) g.fillEllipse (cr);
            else g.drawEllipse (cr, 1.2f);
            g.setColour (lit ? col::sheet : c);
            const auto pr = cr.reduced (cr.getWidth() * 0.28f);
            g.drawLine (pr.getCentreX(), pr.getY(), pr.getCentreX(), pr.getBottom(), 1.4f);
            g.drawLine (pr.getX(), pr.getCentreY(), pr.getRight(), pr.getCentreY(), 1.4f);
            return;
        }
        case Icon::pageMain:
        {
            const auto cr = r.expanded (4.f);
            g.drawEllipse (cr, 1.2f);
            g.fillEllipse (cr.withSizeKeepingCentre (4.f, 4.f));
            return;
        }
        case Icon::pageEdit:
        {
            const auto rr = r.expanded (4.f);
            for (int i = 0; i < 3; ++i)
            {
                const float y = rr.getY() + rr.getHeight() * (0.2f + 0.3f * (float) i);
                g.drawLine (rr.getX(), y, rr.getRight(), y, 1.2f);
                const float kx = rr.getX() + rr.getWidth() * (i == 1 ? 0.3f : 0.65f);
                g.fillEllipse (juce::Rectangle<float> (4.f, 4.f).withCentre ({ kx, y }));
            }
            return;
        }
    }
}

void IconButton::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()) && onClick) onClick();
}

void IconButton::mouseEnter (const juce::MouseEvent&) { hoverHelp (*this, true); }
void IconButton::mouseExit (const juce::MouseEvent&) { hoverHelp (*this, false); }

// ---- TabKey -------------------------------------------------------------------

void TabKey::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto c = selected ? col::ink : isMouseOver() ? col::inkSoft : col::inkFaint;
    if (vertical)
    {
        uprightCaption (g, text, b, c, 9.f);
        if (selected)
        {
            g.setColour (col::ink);
            g.fillRect (b.getRight() - 1.5f, b.getY() + 6.f, 1.5f, b.getHeight() - 12.f);
        }
        return;
    }
    caption (g, text, b, juce::Justification::centred, c, 9.5f);
    if (selected)
    {
        const auto ink = inkRange (label (9.5f), text.toUpperCase());
        const float x0 = b.getCentreX() - (ink.getEnd() - ink.getStart()) * 0.5f;
        g.setColour (col::ink);
        g.fillRect (x0, b.getBottom() - 1.5f, ink.getLength(), 1.5f);
    }
}

void TabKey::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()) && onClick) onClick();
}

void TabKey::mouseEnter (const juce::MouseEvent&) { hoverHelp (*this, true); }
void TabKey::mouseExit (const juce::MouseEvent&) { hoverHelp (*this, false); }
