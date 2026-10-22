#include "EditViews.h"

#include "fx/Structure.h"
#include "model/Names.h"
#include "plugin/PluginProcessor.h"

#include <random>

using namespace theme;

namespace
{
    constexpr int kStageStride = dt::pid::s2_lo - dt::pid::s1_lo;
    constexpr int kSwitchStride = dt::pid::s2_on - dt::pid::s1_on;
    int stageParam (int stage, int s1Pid) { return s1Pid + stage * kStageStride; }
    int stageSwitch (int stage, int s1Pid) { return s1Pid + stage * kSwitchStride; }

    float plainOf (DisTittenProcessor& p, int id)
    {
        auto& prm = p.param (id);
        return prm.convertFrom0to1 (prm.getValue());
    }

    std::unique_ptr<Dial> compactDial (DisTittenProcessor& p, int id, const char* name, const char* help, Dial::Ring ring)
    {
        auto d = std::make_unique<Dial> (p.param (id), name, help);
        d->setCompact (true);
        d->setRing (ring);
        return d;
    }
} // namespace

// ---- BandStrip ----------------------------------------------------------------------------

BandStrip::BandStrip (DisTittenProcessor& p) : proc (p)
{
    setBounds (layout::bandStrip.i());
    setRepaintsOnMouseActivity (false);
    for (int s = 0; s < 3; ++s)
    {
        power[s] = std::make_unique<ParamToggle> (proc.param (stageSwitch (s, dt::pid::s1_on)), ParamToggle::Style::power);
        power[s]->help = "Stage " + juce::String (s + 1) + " on or off.";
        power[s]->onHover = [this] (const juce::String& t, bool on) { if (onHelp) onHelp (t, on); };
        addChildComponent (*power[s]);
    }
    placePower();
}

namespace
{
    // Thermal's strip, in the strip's own pixels: the selected column, its power and name, the puck, the handles,
    // the LOW / HIGH readouts under it.
    constexpr float kColumnTop = 2.f, kColumnBottom = 132.f, kPowerY = 20.f, kNameY = 33.f, kPuckY = 62.f;
    constexpr float kHandleY = 76.f, kHandleH = 38.f, kReadoutY = 143.f;
}

float BandStrip::xOf (double hz) const
{
    return (float) (dt::bandaxis::pos (juce::jlimit (30.0, 22000.0, hz)) / dt::bandaxis::kAxisPx) * (float) getWidth();
}

double BandStrip::hzAt (float x) const
{
    return dt::bandaxis::freq (juce::jlimit (0.0, (double) dt::bandaxis::kAxisPx, (double) x / getWidth() * dt::bandaxis::kAxisPx));
}

float BandStrip::plain (int id) const { return plainOf (proc, id); }

juce::Range<float> BandStrip::bandOf (int s) const
{
    if (plain (stageParam (s, dt::pid::s1_split)) < 0.5f) return { 0.f, (float) getWidth() };
    return { xOf (plain (stageParam (s, dt::pid::s1_lo))), xOf (plain (stageParam (s, dt::pid::s1_hi))) };
}

float BandStrip::trackY (int s) const
{
    int k = 0;   // the other stages in order, one track under the other
    for (int i = 0; i < s; ++i) if (i != stage) ++k;
    return kHandleY + 14.f * (float) k;
}

int BandStrip::hit (juce::Point<float> p) const
{
    const auto band = bandOf (stage);
    const bool split = plain (stageParam (stage, dt::pid::s1_split)) >= 0.5f;
    if (p.y >= kColumnTop && p.y <= kColumnBottom)
    {
        if (split && std::abs (p.x - band.getStart()) < 6.f) return 0;
        if (split && std::abs (p.x - band.getEnd()) < 6.f) return 1;
        if (band.contains (p.x) && split) return 2;
    }
    for (int s = 0; s < 3; ++s)
        if (s != stage && std::abs (p.y - trackY (s)) < 6.f && bandOf (s).expanded (4.f).contains (p.x)) return 10 + s;
    return -1;
}

void BandStrip::placePower()
{
    const auto band = bandOf (stage);
    for (int s = 0; s < 3; ++s)
    {
        power[s]->setVisible (s == stage);
        if (s == stage) power[s]->setBounds (juce::Rectangle<int> (17, 17).withCentre ({ juce::roundToInt (band.getStart() + band.getLength() * 0.5f), (int) kPowerY }));
    }
}

void BandStrip::tick()
{
    bool changed = false;
    for (int s = 0; s < 3; ++s)
    {
        const float v[4] = { plain (stageParam (s, dt::pid::s1_lo)), plain (stageParam (s, dt::pid::s1_hi)),
                             plain (stageSwitch (s, dt::pid::s1_on)), plain (stageParam (s, dt::pid::s1_split)) };
        for (int k = 0; k < 4; ++k)
            if (std::abs (v[k] - shown[s][k]) > 1e-4f) { shown[s][k] = v[k]; changed = true; }
    }
    if (changed || shownStage != stage)
    {
        shownStage = stage;
        placePower();
        repaint();
    }
}

void BandStrip::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (b, kRadius);

    // The selected stage's column, under the grid.
    const bool on = plain (stageSwitch (stage, dt::pid::s1_on)) >= 0.5f;
    const bool split = plain (stageParam (stage, dt::pid::s1_split)) >= 0.5f;
    const auto band = bandOf (stage);
    const auto column = juce::Rectangle<float> (band.getStart(), kColumnTop, std::max (1.f, band.getLength()), kColumnBottom - kColumnTop);
    g.setColour (col::raised);
    g.fillRoundedRectangle (column, kRadius);
    g.setColour (col::hairline);
    g.drawRoundedRectangle (column.reduced (0.5f), kRadius, stroke::hairline);

    // Frequency grid and labels, on Thermal's axis.
    juce::Path grid;
    g.setFont (mono (size::axis));
    for (double f : { 40.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = xOf (f);
        grid.addRectangle (x, 20.f, stroke::hairline, b.getHeight() - 24.f);
        g.setColour (col::inkFaint);
        g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "K" : juce::String ((int) f), juce::Rectangle<float> (x - 20.f, 4.f, 40.f, 12.f),
                    juce::Justification::centred, false);
    }
    g.setColour (col::hairline.withAlpha (0.6f));
    g.fillPath (grid);

    // The other stages: a thin track over their band, a tick at each edge.
    for (int s = 0; s < 3; ++s)
    {
        if (s == stage) continue;
        const bool otherOn = plain (stageSwitch (s, dt::pid::s1_on)) >= 0.5f;
        const auto track = bandOf (s);
        const float y = trackY (s);
        g.setColour (hover == 10 + s ? col::ink : otherOn ? col::inkSoft : col::inkFaint);
        g.fillRect (track.getStart(), y - 0.5f, track.getLength(), 1.f);
        g.fillRect (track.getStart(), y - 5.f, 1.f, 10.f);
        g.fillRect (track.getEnd() - 1.f, y - 5.f, 1.f, 10.f);
        g.setFont (mono (size::axis));
        g.drawText (juce::String (s + 1), juce::Rectangle<float> (track.getStart() + 4.f, y - 13.f, 12.f, 11.f), juce::Justification::centredLeft, false);
    }

    // The selected stage: name, puck and handles.
    const auto heat = on ? col::accent : col::inkFaint;
    caption (g, "STAGE " + juce::String (stage + 1), column.withY (kNameY - 7.f).withHeight (14.f).expanded (30.f, 0.f), juce::Justification::centred,
             heat, size::section);

    const float cx = column.getCentreX();
    g.setColour (col::hairline);
    g.fillRect (column.getX() + 4.f, kPuckY - 0.5f, column.getWidth() - 8.f, 1.f);
    if (split)
    {
        g.setColour (dragWhat == 2 || hover == 2 ? col::ink : heat);
        g.drawEllipse (juce::Rectangle<float> (20.f, 20.f).withCentre ({ cx, kPuckY }), 1.2f);
        g.fillEllipse (juce::Rectangle<float> (10.f, 10.f).withCentre ({ cx, kPuckY }));
        for (int h = 0; h < 2; ++h)
        {
            const float x = h == 0 ? band.getStart() : band.getEnd();
            const auto handle = juce::Rectangle<float> (8.f, kHandleH).withCentre ({ x, kHandleY });
            g.setColour (col::sheet);
            g.fillRoundedRectangle (handle, kRadius);
            g.setColour (dragWhat == h || hover == h ? col::ink : col::inkSoft);
            g.drawRoundedRectangle (handle.reduced (0.5f), kRadius, stroke::hairline);
            g.setColour (heat);
            g.fillRect (handle.reduced (3.f, 6.f));
        }
    }

    // LOW and HIGH under the column (Thermal's readouts), pushed apart where the band is narrow.
    if (split)
    {
        g.setFont (mono (11.f));
        const auto lo = proc.param (stageParam (stage, dt::pid::s1_lo)).getCurrentValueAsText();
        const auto hi = proc.param (stageParam (stage, dt::pid::s1_hi)).getCurrentValueAsText();
        const float wl = theme::textWidth (g.getCurrentFont(), lo) + 8.f, wh = theme::textWidth (g.getCurrentFont(), hi) + 8.f;
        float xl = band.getStart() - wl * 0.5f, xh = band.getEnd() - wh * 0.5f;
        if (xl + wl + 10.f > xh) { const float mid = (xl + wl + xh) * 0.5f; xl = mid - 5.f - wl; xh = mid + 5.f; }
        xl = juce::jlimit (2.f, b.getWidth() - wl - wh - 12.f, xl);
        xh = juce::jlimit (xl + wl + 10.f, b.getWidth() - wh - 2.f, xh);
        g.setColour (heat);
        g.drawText (lo, juce::Rectangle<float> (xl, kReadoutY - 7.f, wl, 14.f), juce::Justification::centred, false);
        g.drawText (hi, juce::Rectangle<float> (xh, kReadoutY - 7.f, wh, 14.f), juce::Justification::centred, false);
    }
    else
        caption (g, "BAND SPLIT OFF: THE WHOLE SIGNAL", column.withY (kReadoutY - 7.f).withHeight (14.f), juce::Justification::centred, col::inkFaint, size::axis);
}

void BandStrip::mouseMove (const juce::MouseEvent& e)
{
    const int h = hit (e.position);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h == 0 || h == 1 ? juce::MouseCursor::LeftRightResizeCursor : h == 2 ? juce::MouseCursor::DraggingHandCursor
                                                                                             : juce::MouseCursor::NormalCursor);
        if (onHelp) onHelp (h >= 10 ? "Stage " + juce::String (h - 9) + ": click to edit it."
                                    : "Drag the edges to set the band this stage distorts, the puck to move the band.", h >= 0);
        repaint();
    }
}

void BandStrip::mouseDown (const juce::MouseEvent& e)
{
    // A click on another stage's track selects that stage.
    if (const int h = hit (e.position); h >= 10)
    {
        if (onSelectStage) onSelectStage (h - 10);
        return;
    }
    dragWhat = hit (e.position);
    if (dragWhat < 0) return;
    dragStartX = e.position.x;
    dragLo = plain (stageParam (stage, dt::pid::s1_lo));
    dragHi = plain (stageParam (stage, dt::pid::s1_hi));
    for (int id : { dt::pid::s1_lo, dt::pid::s1_hi }) proc.param (stageParam (stage, id)).beginChangeGesture();
}

void BandStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragWhat < 0) return;
    auto set = [&] (int id, double hz) { auto& prm = proc.param (stageParam (stage, id)); prm.setValueNotifyingHost (prm.convertTo0to1 ((float) hz)); };
    if (dragWhat == 0) set (dt::pid::s1_lo, hzAt (e.position.x));
    else if (dragWhat == 1) set (dt::pid::s1_hi, hzAt (e.position.x));
    else
    {
        const float dx = e.position.x - dragStartX;
        set (dt::pid::s1_lo, hzAt (xOf (dragLo) + dx));
        set (dt::pid::s1_hi, hzAt (xOf (dragHi) + dx));
    }
    placePower();
    repaint();
}

void BandStrip::mouseUp (const juce::MouseEvent&)
{
    if (dragWhat < 0) return;
    for (int id : { dt::pid::s1_lo, dt::pid::s1_hi }) proc.param (stageParam (stage, id)).endChangeGesture();
    dragWhat = -1;
    repaint();
}

void BandStrip::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    if (onHelp) onHelp ({}, false);
    repaint();
}

// ---- ModPanel ----------------------------------------------------------------------------

ModPanel::ModPanel (DisTittenProcessor& p, int e) : proc (p), env (e)
{
    namespace L = layout;
    setBounds (L::modPanel.moved (0.f, L::modStep * (float) env).i());
    const auto place = [&] (juce::Component& c, layout::R r) { c.setBounds (local (r.moved (0.f, L::modStep * (float) env))); addAndMakeVisible (c); };
    const auto hook = [this] (Helpful& h) { h.onHover = [this] (const juce::String& t, bool on) { if (onHelp) onHelp (t, on); }; };

    plus = std::make_unique<IconButton> (IconButton::Icon::plus);
    plus->help = "Assign: then drag any knob to set how far this envelope moves it.";
    plus->onClick = [this] { if (onAssign) onAssign (env); };
    place (*plus, L::modPlus);
    save = std::make_unique<IconButton> (IconButton::Icon::save);
    save->help = "Save this shape.";
    save->onClick = [this] { savePreset(); };
    place (*save, L::modSave);
    prev = std::make_unique<IconButton> (IconButton::Icon::prev);
    prev->help = "Previous shape (yours and Thermal's modulation presets).";
    prev->onClick = [this] { stepPreset (-1); };
    place (*prev, L::modPrev);
    next = std::make_unique<IconButton> (IconButton::Icon::next);
    next->help = "Next shape.";
    next->onClick = [this] { stepPreset (1); };
    place (*next, L::modNext);
    rndm = std::make_unique<IconButton> (IconButton::Icon::none, "RNDM");
    rndm->help = "A random shape.";
    rndm->onClick = [this] { randomise(); };
    place (*rndm, L::modRandom);
    clr = std::make_unique<IconButton> (IconButton::Icon::none, "CLEAR");
    clr->help = "Back to a flat line.";
    clr->onClick = [this] { clear(); };
    place (*clr, L::modClear);
    sync = std::make_unique<ParamToggle> (proc.param (env == 0 ? dt::pid::mod1_sync : dt::pid::mod2_sync), ParamToggle::Style::key, "SYNC");
    sync->help = "Rate in note values locked to the host's position, or free in Hz.";
    place (*sync, L::modSync);
    power = std::make_unique<ParamToggle> (proc.param (env == 0 ? dt::pid::mod1_on : dt::pid::mod2_on), ParamToggle::Style::power);
    power->help = "Envelope on or off (off outputs 0).";
    place (*power, L::modPower);
    for (Helpful* h : std::initializer_list<Helpful*> { plus.get(), save.get(), prev.get(), next.get(), rndm.get(), clr.get(), sync.get(), power.get() })
        hook (*h);

    rate = compactDial (proc, env == 0 ? dt::pid::mod1_rate : dt::pid::mod2_rate, "RATE", "One cycle: 8 bars to 1/64 T (SYNC) or 0.005 to 30 Hz.", Dial::Ring::position);
    // Thermal plays round (u * 25) of the note values (its display floors), or 0.005 + 29.995 u^(10/3) Hz free.
    rate->textOverride = [this] (float plain) {
        const float u = plain / 25.f;
        if (proc.param (env == 0 ? dt::pid::mod1_sync : dt::pid::mod2_sync).getValue() >= 0.5f)
            return juce::String (dt::names::kSyncMod[juce::jlimit (0, 25, juce::roundToInt (u * 25.f))]);
        const double hz = 0.005 + 29.995 * std::pow ((double) u, 10.0 / 3.0);
        return hz < 9.995 ? fmt::fixed (hz, 2) + " Hz" : fmt::fixed (hz, 1) + " Hz";
    };
    place (*rate, L::dial (L::modRate.x, L::modRate.y));
    humanize = compactDial (proc, env == 0 ? dt::pid::mod1_humanize : dt::pid::mod2_humanize, "HUMANIZE",
                            "Every cycle each point moves by a new random amount, up to this much.", Dial::Ring::plain);
    place (*humanize, L::dial (L::modHumanize.x, L::modHumanize.y));
    for (auto* d : { rate.get(), humanize.get() })
        d->onHover = [this] (Dial* dd, bool on) { if (onHelp) onHelp (dd->getHelp(), on); };
}

juce::Rectangle<float> ModPanel::editor() const
{
    return local (layout::modEditor.moved (0.f, layout::modStep * (float) env)).toFloat().reduced (6.f, 8.f);
}

juce::Point<float> ModPanel::toScreen (float x, float y) const
{
    const auto r = editor();
    return { r.getX() + x * r.getWidth(), r.getY() + y * r.getHeight() };   // y is Thermal's screen y: 0 = top = 1.0
}

juce::Point<float> ModPanel::fromScreen (juce::Point<float> p) const
{
    const auto r = editor();
    return { juce::jlimit (0.f, 1.f, (p.x - r.getX()) / r.getWidth()), juce::jlimit (0.f, 1.f, (p.y - r.getY()) / r.getHeight()) };
}

void ModPanel::publish (const dt::ModState::Shape& shape)
{
    auto m = proc.modState();
    m.envelopes[(size_t) env] = shape;
    proc.setModState (m);
    repaint();
}

void ModPanel::randomise()
{
    std::mt19937 rng { std::random_device {}() };
    std::uniform_real_distribution<float> u (0.05f, 0.95f);
    dt::ModState::Shape s;
    const int steps = 8;
    s.numPoints = steps + 1;
    for (int i = 0; i <= steps; ++i)
    {
        auto& p = s.points[(size_t) i];
        p.x = (float) i / steps;
        p.y = i == steps ? s.points[0].y : u (rng);
        p.cx = i == 0 ? 0.f : (p.x + s.points[(size_t) i - 1].x) * 0.5f;
        p.cy = i == 0 ? 0.f : (p.y + s.points[(size_t) i - 1].y) * 0.5f;
    }
    proc.setEnvelopeName (env, "RANDOM");
    publish (s);
}

void ModPanel::clear()
{
    dt::ModState::Shape s;
    proc.setEnvelopeName (env, "MOD " + juce::String (env + 1));
    publish (s);
}

void ModPanel::loadShapeFile (const juce::File& f)
{
    auto xml = juce::parseXML (f);
    if (xml == nullptr) return;
    dt::ModState::Shape s;
    s.numPoints = juce::jlimit (0, dt::mod::kMaxPoints, xml->getIntAttribute ("NumPoints", 2));
    for (int i = 0; i < s.numPoints; ++i)
    {
        const auto n = juce::String (i);
        auto& p = s.points[(size_t) i];
        p.x = (float) juce::jlimit (0.0, 1.0, xml->getDoubleAttribute ("PointX" + n));
        p.y = (float) juce::jlimit (0.0, 1.0, xml->getDoubleAttribute ("PointY" + n));
        p.cx = (float) xml->getDoubleAttribute ("ControlPointX" + n);
        p.cy = (float) xml->getDoubleAttribute ("ControlPointY" + n);
    }
    shapeFile = f;
    proc.setEnvelopeName (env, f.getFileNameWithoutExtension());
    publish (s);
}

void ModPanel::stepPreset (int delta)
{
    juce::Array<juce::File> files;
    for (auto root : { DisTittenProcessor::userPresetFolder().getSiblingFile ("Shapes"),
                       juce::File ("/Library/Application Support/Output/Thermal/SubPresets/Modulation Presets") })
        if (root.isDirectory())
        {
            auto f = root.findChildFiles (juce::File::findFiles, true, "*.xml");
            f.sort();
            files.addArray (f);
        }
    if (files.isEmpty()) return;
    const int cur = files.indexOf (shapeFile);
    const int n = files.size();
    loadShapeFile (files[cur < 0 ? (delta > 0 ? 0 : n - 1) : ((cur + delta) % n + n) % n]);
}

void ModPanel::savePreset()
{
    auto folder = DisTittenProcessor::userPresetFolder().getSiblingFile ("Shapes");
    folder.createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Save shape", folder.getChildFile (proc.getEnvelopeName (env) + ".xml"), "*.xml");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& c) {
        auto f = c.getResult();
        if (f == juce::File()) return;
        const auto& s = proc.modState().envelopes[(size_t) env];
        juce::XmlElement xml ("MKUClickableEnvelope");   // the same format as Thermal's modulation presets
        xml.setAttribute ("NumPoints", s.numPoints);
        for (int i = 0; i < s.numPoints; ++i)
        {
            const auto n = juce::String (i);
            xml.setAttribute ("PointX" + n, s.points[(size_t) i].x);
            xml.setAttribute ("PointY" + n, s.points[(size_t) i].y);
            xml.setAttribute ("ControlPointX" + n, s.points[(size_t) i].cx);
            xml.setAttribute ("ControlPointY" + n, s.points[(size_t) i].cy);
        }
        xml.writeTo (f.withFileExtension ("xml"));
        shapeFile = f.withFileExtension ("xml");
        proc.setEnvelopeName (env, shapeFile.getFileNameWithoutExtension());
        repaint();
    });
}

void ModPanel::tick()
{
    const float v = proc.meters().envelope[(size_t) env].load (std::memory_order_relaxed);
    if (std::abs (v - lastValue) > 0.004f)
    {
        lastValue = v;
        repaint (editor().withTrimmedLeft (editor().getWidth() - 14.f).expanded (4.f).toNearestInt());
    }
}

void ModPanel::paint (juce::Graphics& g)
{
    namespace L = layout;
    g.setColour (col::hairline);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f, 0.5f), kRadius, stroke::hairline);
    caption (g, proc.getEnvelopeName (env), local (L::modName.moved (0.f, L::modStep * (float) env)).toFloat(), juce::Justification::centredLeft,
             col::ink, size::section);

    // The editor: graph paper (16 columns), the shape, its points.
    const auto r = editor();
    g.setColour (col::well);
    g.fillRoundedRectangle (r.expanded (6.f, 8.f), kRadius);
    juce::Path minor, major;
    for (int i = 0; i <= 16; ++i)
        (i % 4 == 0 ? major : minor).addRectangle (r.getX() + r.getWidth() * (float) i / 16.f, r.getY(), stroke::hairline, r.getHeight());
    major.addRectangle (r.getX(), r.getCentreY(), r.getWidth(), stroke::hairline);
    g.setColour (col::grid);
    g.fillPath (minor);
    g.setColour (col::hairline);
    g.fillPath (major);

    const auto& s = proc.modState().envelopes[(size_t) env];
    const bool on = power->isOn();
    if (s.numPoints >= 1)
    {
        juce::Path curve;
        curve.startNewSubPath (toScreen (s.points[0].x, s.points[0].y));
        for (int i = 1; i < s.numPoints; ++i)
            curve.quadraticTo (toScreen (s.points[(size_t) i].cx, juce::jlimit (-2.f, 3.f, s.points[(size_t) i].cy)),
                               toScreen (s.points[(size_t) i].x, s.points[(size_t) i].y));
        g.saveState();
        g.reduceClipRegion (r.expanded (1.f).toNearestInt());
        g.setColour (on ? col::ink : col::inkFaint);
        g.strokePath (curve, juce::PathStrokeType (stroke::traceRef));
        g.restoreState();
        for (int i = 0; i < s.numPoints; ++i)
        {
            const auto pt = toScreen (s.points[(size_t) i].x, s.points[(size_t) i].y);
            g.setColour (col::sheet);
            g.fillEllipse (juce::Rectangle<float> (8.f, 8.f).withCentre (pt));
            g.setColour (i == dragPoint ? col::ink : col::inkSoft);
            g.drawEllipse (juce::Rectangle<float> (8.f, 8.f).withCentre (pt), 1.2f);
            if (i > 0)   // the curve handle sits on the segment's midpoint
            {
                const auto a = toScreen (s.points[(size_t) i - 1].x, s.points[(size_t) i - 1].y);
                const auto c = toScreen (s.points[(size_t) i].cx, juce::jlimit (-2.f, 3.f, s.points[(size_t) i].cy));
                const auto mid = (a + pt) * 0.25f + c * 0.5f;
                g.setColour (i == dragControl ? col::ink : col::inkFaint);
                g.fillEllipse (juce::Rectangle<float> (4.f, 4.f).withCentre (mid));
            }
        }
    }
    // Where the envelope is now: a mark on the right edge.
    if (on && lastValue >= 0.f)
    {
        const float y = r.getBottom() - lastValue * r.getHeight();
        g.setColour (col::ink);
        g.fillRect (r.getRight() - 8.f, y - 1.f, 10.f, 2.f);
    }
}

void ModPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto& s = proc.modState().envelopes[(size_t) env];
    dragPoint = dragControl = -1;
    for (int i = 0; i < s.numPoints; ++i)
    {
        const auto pt = toScreen (s.points[(size_t) i].x, s.points[(size_t) i].y);
        if (pt.getDistanceFrom (e.position) < 7.f) { dragPoint = i; break; }
        if (i > 0)
        {
            const auto a = toScreen (s.points[(size_t) i - 1].x, s.points[(size_t) i - 1].y);
            const auto c = toScreen (s.points[(size_t) i].cx, s.points[(size_t) i].cy);
            if (((a + pt) * 0.25f + c * 0.5f).getDistanceFrom (e.position) < 6.f) dragControl = i;
        }
    }
    repaint();
}

void ModPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragPoint < 0 && dragControl < 0) return;
    auto s = proc.modState().envelopes[(size_t) env];
    const auto p = fromScreen (e.position);
    if (dragPoint >= 0)
    {
        auto& pt = s.points[(size_t) dragPoint];
        const float lo = dragPoint == 0 ? 0.f : s.points[(size_t) dragPoint - 1].x;
        const float hi = dragPoint == s.numPoints - 1 ? 1.f : s.points[(size_t) dragPoint + 1].x;
        const float ox = pt.x, oy = pt.y;
        pt.x = (dragPoint == 0 || dragPoint == s.numPoints - 1) ? pt.x : juce::jlimit (lo, hi, p.x);
        pt.y = p.y;
        // keep the neighbouring curve handles where they were relative to the segment
        for (int seg : { dragPoint, dragPoint + 1 })
            if (seg >= 1 && seg < s.numPoints)
            {
                auto& c = s.points[(size_t) seg];
                c.cx += (pt.x - ox) * 0.5f;
                c.cy += (pt.y - oy) * 0.5f;
            }
    }
    else
    {
        // drag the handle by its midpoint: the control point moves twice as far
        const auto& a = s.points[(size_t) dragControl - 1];
        auto& b = s.points[(size_t) dragControl];
        const auto r = editor();
        const float my = (e.position.y - r.getY()) / r.getHeight();
        b.cy = juce::jlimit (-2.f, 3.f, 2.f * my - 0.5f * (a.y + b.y));
        b.cx = juce::jlimit (a.x, b.x, 2.f * p.x - 0.5f * (a.x + b.x));
    }
    publish (s);
}

void ModPanel::mouseUp (const juce::MouseEvent&)
{
    dragPoint = dragControl = -1;
    repaint();
}

void ModPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! editor().expanded (6.f).contains (e.position)) return;
    auto s = proc.modState().envelopes[(size_t) env];
    // On a point (not the ends): remove it. Elsewhere: add one there.
    for (int i = 1; i < s.numPoints - 1; ++i)
        if (toScreen (s.points[(size_t) i].x, s.points[(size_t) i].y).getDistanceFrom (e.position) < 7.f)
        {
            for (int k = i; k < s.numPoints - 1; ++k) s.points[(size_t) k] = s.points[(size_t) k + 1];
            --s.numPoints;
            s.points[(size_t) i].cx = (s.points[(size_t) i].x + s.points[(size_t) i - 1].x) * 0.5f;
            s.points[(size_t) i].cy = (s.points[(size_t) i].y + s.points[(size_t) i - 1].y) * 0.5f;
            publish (s);
            return;
        }
    if (s.numPoints >= dt::mod::kMaxPoints) return;
    const auto p = fromScreen (e.position);
    int at = 1;
    while (at < s.numPoints && s.points[(size_t) at].x < p.x) ++at;
    for (int k = s.numPoints; k > at; --k) s.points[(size_t) k] = s.points[(size_t) k - 1];
    ++s.numPoints;
    auto& np = s.points[(size_t) at];
    np.x = p.x;
    np.y = p.y;
    for (int seg : { at, at + 1 })
        if (seg < s.numPoints)
        {
            s.points[(size_t) seg].cx = (s.points[(size_t) seg].x + s.points[(size_t) seg - 1].x) * 0.5f;
            s.points[(size_t) seg].cy = (s.points[(size_t) seg].y + s.points[(size_t) seg - 1].y) * 0.5f;
        }
    publish (s);
}

// ---- MasterView ----------------------------------------------------------------------------

MasterView::MasterView (DisTittenProcessor& p) : proc (p)
{
    namespace L = layout;
    namespace pid = dt::pid;
    setBounds (L::lowerPanel.i());
    setInterceptsMouseClicks (false, true);
    const auto place = [&] (juce::Component& c, juce::Rectangle<int> r) { c.setBounds (r); addAndMakeVisible (c); };
    hp = compactDial (proc, pid::hp, "HP CUTOFF", "Output high-pass, 12 dB/oct, 30 Hz to 22 kHz (wet signal only).", Dial::Ring::position);
    place (*hp, local (L::dial (L::masterHP.x, L::masterHP.y)));
    lp = compactDial (proc, pid::lp, "LP CUTOFF", "Output low-pass, 12 dB/oct.", Dial::Ring::position);
    place (*lp, local (L::dial (L::masterLP.x, L::masterLP.y)));
    const std::pair<int, const char*> comp[5] { { pid::mc_thresh, "THRESHOLD" }, { pid::mc_ratio, "RATIO" }, { pid::mc_attack, "ATTACK" },
                                                { pid::mc_release, "RELEASE" }, { pid::mc_gain, "GAIN" } };
    std::unique_ptr<Dial>* slots[5] { &thresh, &ratio, &attack, &release, &gain };
    const char* helps[5] { "Master compressor threshold, -30 to 0 dB.", "2:1 to 20:1, hard knee, at most 30 dB of reduction.",
                           "10-90 % attack, 0.1 to 100 ms.", "10-90 % release, 50 ms to 2 s.", "Make-up gain, 0 to 24 dB." };
    for (int i = 0; i < 5; ++i)
    {
        *slots[i] = compactDial (proc, comp[i].first, comp[i].second, helps[i], i == 4 ? Dial::Ring::plain : Dial::Ring::position);
        place (**slots[i], local (L::dial (L::masterKnobX[i], L::masterKnobY)));
    }
    in = std::make_unique<Fader> (proc.param (pid::mc_in), true);
    in->setCaption ("INPUT");
    in->help = "Into the master compressor, -60 to +6 dB.";
    place (*in, local (L::masterIn));
    out = std::make_unique<Fader> (proc.param (pid::mc_out), true);
    out->setCaption ("OUTPUT");
    out->help = "Out of the master compressor, -60 to +6 dB.";
    place (*out, local (L::masterOut));
    wet = std::make_unique<Fader> (proc.param (pid::mc_mix), false);
    wet->setEndLabels ("DRY", "WET");
    wet->help = "The compressor against its input.";
    place (*wet, local ({ L::masterWet.x - 30.f, L::masterWet.y, L::masterWet.w + 60.f, L::masterWet.h }));
    power = std::make_unique<ParamToggle> (proc.param (pid::mc_on), ParamToggle::Style::power);
    power->help = "Master compressor on or off.";
    place (*power, local (L::masterPower));

    const auto hookDial = [this] (Dial* d) { d->onHover = [this] (Dial* dd, bool on) { if (onHelp) onHelp (dd->getHelp(), on); }; };
    for (auto* d : { hp.get(), lp.get(), thresh.get(), ratio.get(), attack.get(), release.get(), gain.get() }) hookDial (d);
    for (Helpful* h : std::initializer_list<Helpful*> { in.get(), out.get(), wet.get(), power.get() })
        h->onHover = [this] (const juce::String& t, bool on) { if (onHelp) onHelp (t, on); };
}

void MasterView::tick()
{
    const float g = proc.meters().masterGainReductionDb.load (std::memory_order_relaxed);
    if (std::abs (g - gr) > 0.05f) { gr = g; repaint (local (layout::masterGR)); }
    for (auto* d : { thresh.get(), ratio.get(), attack.get(), release.get(), gain.get() }) d->setDimmed (! power->isOn());
}

void MasterView::paint (juce::Graphics& g)
{
    namespace L = layout;
    caption (g, "MASTER", local (L::masterTitle).toFloat(), juce::Justification::centredLeft, col::ink, size::section);
    g.setColour (col::hairline);
    g.fillRect (local ({ 436, 377, 330, 1 }).toFloat());

    // Gain reduction: a gauge from 0 (left) to -30 dB (right), the needle at the current reduction.
    const auto box = local (L::masterGR).toFloat();
    const auto c = juce::Point<float> (box.getCentreX(), box.getBottom() - 6.f);
    const float rad = box.getHeight() - 22.f;
    const auto angleOf = [] (float db) { return juce::MathConstants<float>::pi * (-0.42f + 0.84f * juce::jlimit (0.f, 1.f, std::pow (-db / 30.f, 0.6f))); };
    const auto at = [&] (float a, float r) { return juce::Point<float> (c.x + r * std::sin (a), c.y - r * std::cos (a)); };
    g.setFont (mono (size::axis));
    for (float db : { 0.f, -3.f, -6.f, -10.f, -20.f, -30.f })
    {
        const float a = angleOf (db);
        g.setColour (col::hairline);
        g.drawLine ({ at (a, rad - 8.f), at (a, rad) }, stroke::hairline);
        g.setColour (col::inkFaint);
        g.drawText (fmt::signedNumber (db, 0), juce::Rectangle<float> (30.f, 12.f).withCentre (at (a, rad + 9.f)), juce::Justification::centred, false);
    }
    caption (g, "GAIN REDUCTION", juce::Rectangle<float> (160.f, 12.f).withCentre ({ c.x, c.y - rad * 0.45f }), juce::Justification::centred,
             col::inkFaint, size::tagLetter);
    g.setColour (power->isOn() ? col::ink : col::inkFaint);
    g.drawLine ({ c, at (angleOf (gr), rad - 2.f) }, 1.6f);
    g.fillEllipse (juce::Rectangle<float> (8.f, 8.f).withCentre (c));
}

// ---- LowerPanel ----------------------------------------------------------------------------

LowerPanel::LowerPanel (DisTittenProcessor& p) : proc (p)
{
    namespace L = layout;
    setBounds (L::lowerPanel.i());
    const char* names[3] { "XY", "EFFECTS", "MASTER" };
    const L::R rects[3] { L::lowerTabXY, L::lowerTabFx, L::lowerTabMaster };
    for (int t = 0; t < 3; ++t)
    {
        tabs[t] = std::make_unique<TabKey> (names[t], true);
        tabs[t]->onClick = [this, t] { showTab (t); };
        tabs[t]->setBounds (local (rects[t]));
        addAndMakeVisible (*tabs[t]);
    }
    xy = std::make_unique<XYPad> (proc, false);
    xy->setBounds (local (L::lowerXY));
    addChildComponent (*xy);

    for (int s = 0; s < 2; ++s)
    {
        const float dy = L::slotStep * (float) s;
        const int typeId = s == 0 ? dt::pid::fx1_type : dt::pid::fx2_type, onId = s == 0 ? dt::pid::fx1_on : dt::pid::fx2_on;
        slotType[s] = std::make_unique<ChoiceMenu> (proc.param (typeId), juce::StringArray (dt::names::kEffects, dt::names::kNumEffects),
                                                    [] (float v) { return dt::names::stepIndex (v, dt::names::kNumEffects); });
        slotType[s]->help = "Master effect after the three stages.";
        slotType[s]->setBounds (local (L::slotType.moved (0.f, dy)));
        addChildComponent (*slotType[s]);
        slotOn[s] = std::make_unique<ParamToggle> (proc.param (onId), ParamToggle::Style::power);
        slotOn[s]->help = "Master effect on or off.";
        slotOn[s]->setBounds (local (L::slotPower.moved (0.f, dy)));
        addChildComponent (*slotOn[s]);
        slot[s] = std::make_unique<EffectView> (proc, 3 + s,
                                                EffectGeometry { L::slotIn.moved (0.f, dy), L::slotOut.moved (0.f, dy), L::slotWet.moved (0.f, dy),
                                                                 L::slotKnobY + dy, L::slotKnobCentre, L::slotKnobStep, L::slotSwitchY + dy },
                                                juce::Rectangle<int> (419, (int) (376 + dy), 400, 150));
        slot[s]->setBounds (slot[s]->getBounds().translated (-419, -350));
        addChildComponent (*slot[s]);
    }
    master = std::make_unique<MasterView> (proc);
    master->setBounds (master->getBounds().translated (-419, -350));
    addChildComponent (*master);
    showTab (0);
}

void LowerPanel::showTab (int t)
{
    current = juce::jlimit (0, 2, t);
    for (int i = 0; i < 3; ++i) tabs[i]->setSelected (i == current);
    xy->setVisible (current == 0);
    for (int s = 0; s < 2; ++s)
    {
        slotType[s]->setVisible (current == 1);
        slotOn[s]->setVisible (current == 1);
        slot[s]->setVisible (current == 1);
    }
    master->setVisible (current == 2);
    repaint();
}

void LowerPanel::tick()
{
    if (current == 1)
        for (int s = 0; s < 2; ++s)
        {
            auto& prm = proc.param (s == 0 ? dt::pid::fx1_type : dt::pid::fx2_type);
            slot[s]->showEffect (dt::names::stepIndex (prm.convertFrom0to1 (prm.getValue()), dt::names::kNumEffects));
            slot[s]->setGainReduction (proc.meters().slotGainReductionDb[(size_t) (3 + s)].load (std::memory_order_relaxed));
        }
    if (current == 2) master->tick();
}

void LowerPanel::paint (juce::Graphics& g)
{
    g.setColour (col::hairline);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), kRadius, stroke::hairline);
    if (current == 0)
    {
        caption (g, "XY CONTROL", local (layout::masterTitle).toFloat(), juce::Justification::centredLeft, col::ink, size::section);
        g.setColour (col::hairline);
        g.fillRect (local ({ 436, 377, 330, 1 }).toFloat());
    }
    g.fillRect ((float) local (layout::lowerTabXY).getX() - 4.f, 20.f, stroke::hairline, (float) getHeight() - 40.f);
    if (current == 1)
        for (int s = 0; s < 2; ++s)
            g.fillRect (local ({ 436, 528.f + layout::slotStep * (float) s - 6.f, 360, 1 }).toFloat().withY (s == 0 ? 349.f + 174.f - 350.f : 1e6f));
}
