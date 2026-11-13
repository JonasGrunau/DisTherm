#include "StageView.h"

#include "fx/Shapes.h"
#include "model/Names.h"
#include "plugin/PluginProcessor.h"

using namespace theme;

namespace
{
    constexpr int kStageStride = dt::pid::s2_lo - dt::pid::s1_lo;
    constexpr int kSwitchStride = dt::pid::s2_on - dt::pid::s1_on;
    constexpr int kSlotStride = dt::pid::s2_bit_in - dt::pid::s1_bit_in;

    int stageParam (int stage, int s1Pid) { return s1Pid + stage * kStageStride; }
    int stageSwitch (int stage, int s1Pid) { return s1Pid + stage * kSwitchStride; }
    // Effect parameters: the five slots (three stages, FX 1, FX 2) repeat with one stride.
    int slotParam (int slot, int s1Pid) { return s1Pid + slot * kSlotStride; }
    int delaySync (int slot) { return slot < 3 ? dt::pid::s1_dly_sync + slot * (dt::pid::s2_dly_sync - dt::pid::s1_dly_sync)
                                               : (slot == 3 ? dt::pid::fx1_dly_sync : dt::pid::fx2_dly_sync); }

    // Which of the four shape controls each distortion type uses (measured: the rest only lower the level).
    constexpr bool kUsesControl[34][4] = {
        { 1, 0, 0, 0 }, { 1, 0, 0, 0 }, { 1, 1, 1, 1 }, { 1, 0, 0, 0 }, { 1, 0, 0, 0 }, { 1, 0, 0, 0 }, { 1, 1, 0, 0 },
        { 1, 1, 1, 0 }, { 1, 0, 0, 0 }, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
        {}, {}, {}, { 1, 0, 0, 0 }, { 1, 0, 0, 0 }
    };

    int floorIndex (float plain, int count) { return dt::names::stepIndex (plain, count); }

    std::unique_ptr<Dial> makeDial (DisThermProcessor& p, int id, const juce::String& name, const juce::String& help,
                                    Dial::Ring ring = Dial::Ring::position)
    {
        auto d = std::make_unique<Dial> (p.param (id), name, help);
        d->setCompact (true);
        d->setRing (ring);
        return d;
    }
} // namespace

// ---- EffectView ----------------------------------------------------------------------------

EffectView::EffectView (DisThermProcessor& p, int s, EffectGeometry g, juce::Rectangle<int> area)
    : proc (p), slot (s), geo (g), origin (area.getPosition())
{
    setBounds (area);
    setInterceptsMouseClicks (false, true);
}

juce::Rectangle<int> EffectView::local (layout::R r) const { return r.i() - origin; }

void EffectView::build (int effect)
{
    namespace pid = dt::pid;
    auto& L = layouts[(size_t) effect];
    if (! L.parts.empty()) return;

    struct Knob { int s1Pid; const char* name; const char* help; Dial::Ring ring; };
    std::vector<Knob> knobs;
    int base = 0;   // the effect's In / Out / Dry/Wet parameters
    switch (effect)
    {
        case dt::names::bitReducer:
            base = pid::s1_bit_in;
            knobs = { { pid::s1_bit_bits, "BITS", "Coarser steps: the level is truncated to fewer and fewer values (500 down to 5 per unit).", Dial::Ring::build },
                      { pid::s1_bit_redux, "REDUX", "Sample-and-hold: every Nth sample is held, 1 to 300.", Dial::Ring::build } };
            break;
        case dt::names::chorus:
            base = pid::s1_cho_in;
            knobs = { { pid::s1_cho_rate, "RATE", "LFO rate, 0.001 to 50 Hz.", Dial::Ring::position },
                      { pid::s1_cho_depth, "DEPTH", "Delay swing around 3 ms, up to 2 ms either way.", Dial::Ring::plain },
                      { pid::s1_cho_width, "WIDTH", "Phase offset of the right channel's LFO, up to half a cycle.", Dial::Ring::plain },
                      { pid::s1_cho_fb, "FEEDBACK", "Delayed signal fed back, up to 90 %.", Dial::Ring::plain } };
            break;
        case dt::names::compressor:
            base = pid::s1_cmp_in;
            knobs = { { pid::s1_cmp_thresh, "THRESHOLD", "Level above which it compresses, -30 to 0 dB.", Dial::Ring::position },
                      { pid::s1_cmp_ratio, "RATIO", "2:1 to 20:1, hard knee.", Dial::Ring::plain },
                      { pid::s1_cmp_attack, "ATTACK", "10-90 % attack time, 0.1 to 100 ms.", Dial::Ring::position },
                      { pid::s1_cmp_release, "RELEASE", "10-90 % release time, 50 ms to 2 s.", Dial::Ring::position },
                      { pid::s1_cmp_gain, "GAIN", "Make-up gain, 0 to 24 dB.", Dial::Ring::plain } };
            break;
        case dt::names::delay:
            base = pid::s1_dly_in;
            knobs = { { pid::s1_dly_time, "TIME", "Delay time: a note value (SYNC) or 1 ms to 2.5 s.", Dial::Ring::position },
                      { pid::s1_dly_fb, "FEEDBACK", "Echoes fed back through the filters and a soft saturator.", Dial::Ring::plain },
                      { pid::s1_dly_hp, "HP", "High-pass inside the feedback loop, 20 Hz to 20 kHz.", Dial::Ring::position },
                      { pid::s1_dly_lp, "LP", "Low-pass inside the feedback loop, 20 Hz to 20 kHz.", Dial::Ring::position } };
            break;
        case dt::names::filter:
            base = pid::s1_flt_in;
            knobs = { { pid::s1_flt_cutoff, "CUTOFF", "Ladder filter cutoff, 30 Hz to 22 kHz.", Dial::Ring::position },
                      { pid::s1_flt_res, "RESONANCE", "Feedback around the ladder; it never self-oscillates.", Dial::Ring::plain },
                      { pid::s1_flt_drive, "DRIVE", "Saturation into the ladder and its feedback.", Dial::Ring::build },
                      { pid::s1_flt_follow, "ENV FOL", "The input's envelope moves the cutoff up (+) or down (-).", Dial::Ring::bipolar } };
            break;
        case dt::names::flanger:
            base = pid::s1_fla_in;
            knobs = { { pid::s1_fla_time, "TIME", "Base delay, 10 ms (left) to 0.2 ms (right).", Dial::Ring::position },
                      { pid::s1_fla_fb, "FEEDBACK", "Loop gain 0.5 to 0.99; MODERN inverts it.", Dial::Ring::plain },
                      { pid::s1_fla_rate, "FREQ MOD", "LFO rate, 0.1 to 5 Hz.", Dial::Ring::position },
                      { pid::s1_fla_range, "MOD RANGE", "How far the LFO moves the delay.", Dial::Ring::plain } };
            break;
        case dt::names::freqShifter:
            base = pid::s1_fsh_in;
            knobs = { { pid::s1_fsh_freq, "FREQUENCY", "Shift upwards, 0 to 10 kHz (cubic).", Dial::Ring::plain },
                      { pid::s1_fsh_offset, "OFFSET", "Extra shift of the right channel only.", Dial::Ring::bipolar } };
            break;
        case dt::names::phaser:
            base = pid::s1_pha_in;
            knobs = { { pid::s1_pha_stages, "STAGES", "Number of allpass stages, 1 to 32 (MULTI).", Dial::Ring::position },
                      { pid::s1_pha_fb, "FEEDBACK", "Signal fed back around the stages.", Dial::Ring::plain },
                      { pid::s1_pha_freq, "FREQ", "Where the sweep starts, 70 Hz to 16 kHz (MULTI).", Dial::Ring::position },
                      { pid::s1_pha_range, "FREQ MOD", "Sweep depth (MULTI).", Dial::Ring::plain },
                      { pid::s1_pha_rate, "MOD RATE", "LFO rate, 0.01 to 50 Hz.", Dial::Ring::position } };
            break;
        case dt::names::reverb:
            base = pid::s1_rev_in;
            knobs = { { pid::s1_rev_size, "SIZE", "Room size: the tail's feedback.", Dial::Ring::plain },
                      { pid::s1_rev_damp, "DAMPING", "Darker tail.", Dial::Ring::plain },
                      { pid::s1_rev_width, "WIDTH", "Wet level, +0 to +6 dB.", Dial::Ring::plain } };
            break;
        default: return;
    }

    const auto add = [&] (std::unique_ptr<juce::Component> c) -> juce::Component* {
        auto* raw = c.get();
        addChildComponent (raw);
        L.parts.push_back (std::move (c));
        return raw;
    };
    const auto hookHelp = [this] (Helpful& h) { h.onHover = [this] (const juce::String& t, bool on) { if (onHelp) onHelp (t, on); }; };

    const int n = (int) knobs.size();
    for (int i = 0; i < n; ++i)
    {
        const float cx = geo.knobCentre + geo.knobStep * ((float) i - (float) (n - 1) * 0.5f);
        auto d = makeDial (proc, slotParam (slot, knobs[(size_t) i].s1Pid), knobs[(size_t) i].name, knobs[(size_t) i].help, knobs[(size_t) i].ring);
        d->setBounds (local (layout::dial (cx - layout::kDisc * 0.5f, geo.knobY)));
        d->onHover = [this] (Dial* dd, bool on) { if (onHelp) onHelp (dd->getHelp(), on); };
        L.dials.push_back (d.get());
        add (std::move (d));
    }

    auto in = std::make_unique<Fader> (proc.param (slotParam (slot, base)), true);
    in->setCaption ("INPUT");
    in->help = "Into the effect, -60 to +6 dB (the dry part of its mix too).";
    in->setBounds (local (geo.in));
    hookHelp (*in);
    add (std::move (in));
    auto outF = std::make_unique<Fader> (proc.param (slotParam (slot, base + 1)), true);
    outF->setCaption ("OUTPUT");
    outF->help = "Out of the effect, -60 to +6 dB.";
    outF->setBounds (local (geo.out));
    hookHelp (*outF);
    add (std::move (outF));
    // Dry/wet is the last parameter of most effects; the flanger, shifter and reverb keep it elsewhere.
    const int wetPid = effect == dt::names::flanger ? pid::s1_fla_mix : effect == dt::names::freqShifter ? pid::s1_fsh_mix
                     : effect == dt::names::reverb ? pid::s1_rev_mix : effect == dt::names::bitReducer ? pid::s1_bit_mix
                     : effect == dt::names::chorus ? pid::s1_cho_mix : effect == dt::names::compressor ? pid::s1_cmp_mix
                     : effect == dt::names::delay ? pid::s1_dly_mix : effect == dt::names::filter ? pid::s1_flt_mix : pid::s1_pha_mix;
    auto wet = std::make_unique<Fader> (proc.param (slotParam (slot, wetPid)), false);
    wet->setEndLabels ("DRY", "WET");
    wet->help = "The effect against its input (linear mix).";
    wet->setBounds (local ({ geo.wet.x - 30.f, geo.wet.y, geo.wet.w + 60.f, geo.wet.h }));
    hookHelp (*wet);
    add (std::move (wet));

    // The type switches between the knobs and the wet slider.
    const auto switchAt = [&] (float width) { return local ({ geo.knobCentre - width * 0.5f, geo.switchY, width, 16.f }); };
    const auto exact = [] (float v) { return v >= 1.f ? 1 : 0; };
    if (effect == dt::names::filter)
    {
        auto s = std::make_unique<SegmentSwitch> (proc.param (slotParam (slot, pid::s1_flt_mode)), juce::StringArray { "LP12", "LP24", "HP12", "HP24" },
                                                  std::vector<float> { 0.f, 1.f, 2.f, 3.f }, [] (float v) { return floorIndex (v, 4); });
        s->help = "Ladder filter mode: 2 or 4 poles, low- or high-pass.";
        s->setBounds (switchAt (160.f));
        hookHelp (*s);
        add (std::move (s));
    }
    else if (effect == dt::names::flanger)
    {
        auto s = std::make_unique<SegmentSwitch> (proc.param (slotParam (slot, pid::s1_fla_type)), juce::StringArray { "VINTAGE", "MODERN" },
                                                  std::vector<float> { 0.f, 1.f }, exact);
        s->help = "VINTAGE feeds back positive, MODERN negative.";
        s->setBounds (switchAt (119.f));
        hookHelp (*s);
        add (std::move (s));
    }
    else if (effect == dt::names::phaser)
    {
        auto s = std::make_unique<SegmentSwitch> (proc.param (slotParam (slot, pid::s1_pha_type)), juce::StringArray { "MULTI", "BARBER" },
                                                  std::vector<float> { 0.f, 1.f }, exact);
        s->help = "MULTI: a sweeping allpass chain. BARBER: an endlessly rising shift.";
        s->setBounds (switchAt (104.f));
        hookHelp (*s);
        add (std::move (s));
        phaserDims[0] = L.dials[0]; phaserDims[1] = L.dials[2]; phaserDims[2] = L.dials[3];
    }
    else if (effect == dt::names::delay)
    {
        auto sync = std::make_unique<ParamToggle> (proc.param (delaySync (slot)), ParamToggle::Style::key, "SYNC");
        sync->help = "Delay time in note values at the host tempo, or free in ms.";
        sync->setBounds (local ({ geo.knobCentre - 118.f, geo.switchY, 44.f, 16.f }));
        hookHelp (*sync);
        add (std::move (sync));
        auto s = std::make_unique<SegmentSwitch> (proc.param (slotParam (slot, pid::s1_dly_type)), juce::StringArray { "STEREO", "PING PONG" },
                                                  std::vector<float> { 0.f, 1.f }, exact);
        s->help = "STEREO: two echoes. PING PONG: the left input bounces left, right, left...";
        s->setBounds (local ({ geo.knobCentre - 65.f, geo.switchY, 132.f, 16.f }));
        hookHelp (*s);
        add (std::move (s));
    }
    else if (effect == dt::names::reverb)
    {
        auto f = std::make_unique<ParamToggle> (proc.param (slotParam (slot, pid::s1_rev_freeze)), ParamToggle::Style::key, "FREEZE");
        f->help = "Holds the tail forever; the input still passes dry.";
        f->setBounds (switchAt (60.f));
        hookHelp (*f);
        add (std::move (f));
    }
}

void EffectView::showEffect (int effect)
{
    effect = juce::jlimit (0, 8, effect);
    if (effect == shown) return;
    build (effect);
    for (int e = 0; e < 9; ++e)
        for (auto& c : layouts[(size_t) e].parts)
            c->setVisible (e == effect);
    shown = effect;
    repaint();
}

void EffectView::setGainReduction (float db)
{
    if (shown == dt::names::compressor && std::abs (db - grDb) > 0.05f)
    {
        grDb = db;
        repaint();
    }
    if (shown == dt::names::phaser)
    {
        const bool barber = proc.param (slotParam (slot, dt::pid::s1_pha_type)).convertFrom0to1 (proc.param (slotParam (slot, dt::pid::s1_pha_type)).getValue()) >= 1.f;
        for (auto* d : phaserDims) if (d != nullptr) d->setDimmed (barber);
    }
}

void EffectView::paint (juce::Graphics& g)
{
    if (shown != dt::names::compressor) return;
    // The compressor's gain reduction: GR, a bar growing leftwards from 0 dB to -30 dB, the number.
    const auto row = local ({ geo.knobCentre - 72.f, geo.switchY, 144.f, 16.f }).toFloat();
    caption (g, "GR", row.withWidth (20.f), juce::Justification::centredLeft, col::inkSoft, 8.5f);
    const auto bar = row.withTrimmedLeft (24.f).withTrimmedRight (40.f).withSizeKeepingCentre (row.getWidth() - 64.f, 4.f);
    g.setColour (col::well);
    g.fillRoundedRectangle (bar, kRadius);
    const float m = juce::jlimit (0.f, 1.f, -grDb / 30.f);
    g.setColour (col::inkSoft);
    g.fillRect (bar.withLeft (bar.getRight() - m * bar.getWidth()));
    g.setColour (col::ink);
    g.setFont (mono (9.f));
    g.drawText (fmt::signedNumber (grDb, 2), row.withTrimmedLeft (row.getWidth() - 38.f), juce::Justification::centredRight, false);
}

// ---- ShapeView ----------------------------------------------------------------------------

ShapeView::ShapeView (DisThermProcessor& p, int s) : proc (p), stage (s)
{
    namespace pid = dt::pid;
    for (int id : { pid::s1_type, pid::s1_drive, pid::s1_out, pid::s1_shape, pid::s1_depth, pid::s1_freq, pid::s1_clip })
    {
        auto* prm = &proc.param (stageParam (stage, id));
        prm->addListener (this);
        watched.push_back (prm);
    }
    auto* on = &proc.param (stageSwitch (stage, pid::s1_disto_on));
    on->addListener (this);
    watched.push_back (on);
    setInterceptsMouseClicks (false, false);
}

ShapeView::~ShapeView()
{
    for (auto* w : watched) w->removeListener (this);
}

void ShapeView::triggerRepaint()
{
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ShapeView> (this)] {
        if (safe != nullptr) safe->repaint();
    });
}

void ShapeView::paint (juce::Graphics& g)
{
    namespace pid = dt::pid;
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (col::well);
    g.fillRoundedRectangle (b, kRadius);
    g.setColour (col::hairline);
    g.drawRoundedRectangle (b, kRadius, stroke::hairline);
    g.fillRect (b.getX() + 1.f, b.getCentreY(), b.getWidth() - 2.f, stroke::hairline);
    g.fillRect (b.getCentreX(), b.getY() + 1.f, stroke::hairline, b.getHeight() - 2.f);

    const auto plain = [&] (int id) { auto& prm = proc.param (stageParam (stage, id)); return prm.convertFrom0to1 (prm.getValue()); };
    int type = dt::names::stepIndex (plain (pid::s1_type), 34);
    if (type >= dt::names::kNumDistortionTypes && type < 32) type = dt::names::kNumDistortionTypes - 1;
    const double c[4] = { plain (pid::s1_shape), plain (pid::s1_depth), plain (pid::s1_freq), plain (pid::s1_clip) };
    const double drive = std::pow (10.0, plain (pid::s1_drive) / 20.0), outG = std::pow (10.0, plain (pid::s1_out) / 20.0);
    const bool on = proc.param (stageSwitch (stage, pid::s1_disto_on)).getValue() >= 0.5f;

    // The curve the input (-1..1) goes through: drive, the shape, output; 1.5 full scale vertically.
    juce::Path p;
    const int n = (int) b.getWidth() * 2;
    for (int i = 0; i <= n; ++i)
    {
        const double x = -1.0 + 2.0 * i / n;
        const double y = std::clamp (outG * dt::shapeEval (type, drive * x, c), -1.5, 1.5);
        const float px = b.getX() + (float) (x + 1.0) * 0.5f * b.getWidth();
        const float py = b.getCentreY() - (float) (y / 1.5) * b.getHeight() * 0.5f;
        if (i == 0) p.startNewSubPath (px, py);
        else p.lineTo (px, py);
    }
    g.saveState();
    g.reduceClipRegion (b.toNearestInt());
    g.setColour (on ? col::accent : col::accentDim);
    g.strokePath (p, juce::PathStrokeType (on ? stroke::traceStrong : stroke::trace));
    g.restoreState();
}

// ---- StageView ----------------------------------------------------------------------------

StageView::StageView (DisThermProcessor& p, int s) : proc (p), stage (s)
{
    namespace pid = dt::pid;
    namespace L = layout;
    setBounds (0, 155, 839, 193);
    setInterceptsMouseClicks (false, true);
    const auto P = [&] (int s1Pid) { return stageParam (stage, s1Pid); };
    const auto S = [&] (int s1Pid) { return stageSwitch (stage, s1Pid); };
    const auto place = [&] (juce::Component& c, layout::R r) { c.setBounds (local (r)); addAndMakeVisible (c); };
    const auto dialAt = [&] (std::unique_ptr<Dial>& d, int id, const char* name, const char* help, Dial::Ring ring, layout::R disc) {
        d = makeDial (proc, id, name, help, ring);
        place (*d, L::dial (disc.x, disc.y));
    };

    // Stage panel
    dialAt (low, P (pid::s1_lo), "LOW", "Lower edge of the band this stage distorts, 30 Hz to 22 kHz (BAND SPLIT on).", Dial::Ring::position, L::stageLow);
    dialAt (high, P (pid::s1_hi), "HIGH", "Upper edge of the band this stage distorts.", Dial::Ring::position, L::stageHigh);
    solo = std::make_unique<ParamToggle> (proc.param (P (pid::s1_solo)), ParamToggle::Style::solo, "S", 1e-6f);
    solo->help = "Solo: hear only the processed band.";
    place (*solo, L::stageSolo);
    split = std::make_unique<ParamToggle> (proc.param (P (pid::s1_split)), ParamToggle::Style::check);
    split->help = "Band split: only the band between LOW and HIGH goes through the distortion and effect.";
    place (*split, L::bandSplit);
    refilter = std::make_unique<ParamToggle> (proc.param (P (pid::s1_refilter)), ParamToggle::Style::check, juce::String(), 1e-6f);
    refilter->help = "Refilter: band-limit the distorted band again, so its new harmonics stay inside it.";
    place (*refilter, L::refilter);

    // Distortion panel
    type = std::make_unique<ChoiceMenu> (proc.param (P (pid::s1_type)), juce::StringArray (dt::names::kDistortionTypes, dt::names::kNumDistortionTypes),
                                         [] (float v) { return dt::names::stepIndex (v, dt::names::kNumDistortionTypes); });
    type->help = "The distortion shape. Click the name to switch to the distortion panel, again for the list.";
    place (*type, L::distoType);
    prev = std::make_unique<IconButton> (IconButton::Icon::prev);
    prev->onClick = [this] { type->step (-1); showDistortion (true); };
    place (*prev, L::distoPrev);
    next = std::make_unique<IconButton> (IconButton::Icon::next);
    next->onClick = [this] { type->step (1); showDistortion (true); };
    place (*next, L::distoNext);
    distoOn = std::make_unique<ParamToggle> (proc.param (S (pid::s1_disto_on)), ParamToggle::Style::power);
    distoOn->help = "Distortion on or off (off passes the band untouched).";
    distoOn->onChange = [this] { updateDimming(); };
    place (*distoOn, L::distoPower);
    fxType = std::make_unique<ChoiceMenu> (proc.param (S (pid::s1_fx_type)), juce::StringArray (dt::names::kEffects, dt::names::kNumEffects),
                                           [] (float v) { return dt::names::stepIndex (v, dt::names::kNumEffects); });
    fxType->help = "The stage's effect, after the distortion. Click the name to show it, again for the list.";
    place (*fxType, L::fxType);
    fxOn = std::make_unique<ParamToggle> (proc.param (S (pid::s1_fx_on)), ParamToggle::Style::power);
    fxOn->help = "The stage effect on or off.";
    place (*fxOn, L::fxPower);

    autoKey = std::make_unique<ParamToggle> (proc.param (P (pid::s1_auto)), ParamToggle::Style::vertical, "AUTO", 1.f);
    autoKey->help = "Auto gain: turns the output down by 0.3 dB per dB of drive.";
    place (*autoKey, L::autoKey);
    dialAt (drive, P (pid::s1_drive), "DRIVE", "Gain into the shape, -36 to +36 dB.", Dial::Ring::bipolar, L::drive);
    drive->setRing (Dial::Ring::build);
    dialAt (out, P (pid::s1_out), "OUTPUT", "Level after the shape, -36 to +36 dB.", Dial::Ring::bipolar, L::distoOut);
    dialAt (c1, P (pid::s1_shape), "SHAPE", "The shape's first control (what it does depends on the shape).", Dial::Ring::build, L::shape1);
    dialAt (c2, P (pid::s1_depth), "DEPTH", "The shape's second control.", Dial::Ring::build, L::shape2);
    dialAt (c3, P (pid::s1_freq), "FREQUENCY", "The shape's third control.", Dial::Ring::build, L::shape3);
    dialAt (c4, P (pid::s1_clip), "CLIP", "The shape's fourth control.", Dial::Ring::build, L::shape4);
    dialAt (time, P (pid::s1_time), "TIME", "Feedback delay, 0.2 to 300 ms or a note value (SYNC).", Dial::Ring::position, L::fbTime);
    syncKey = std::make_unique<ParamToggle> (proc.param (S (pid::s1_time_sync)), ParamToggle::Style::vertical, "SYNC");
    syncKey->help = "Feedback time in note values at the host tempo.";
    place (*syncKey, L::syncKey);
    dialAt (fb, P (pid::s1_fb), "FEEDBACK", "Distorted signal fed back into the shape through the delay.", Dial::Ring::build, L::feedback);
    release = std::make_unique<MiniKnob> (proc.param (P (pid::s1_release)));
    release->help = "Feedback release:";
    place (*release, L::fbRelease);
    followKey = std::make_unique<ParamToggle> (proc.param (P (pid::s1_follow)), ParamToggle::Style::vertical, "FOLLOW", 1e-6f);
    followKey->help = "Follow: the feedback follows the input's envelope and dies with it.";
    place (*followKey, L::followKey);
    shapeView = std::make_unique<ShapeView> (proc, stage);
    place (*shapeView, L::shapeView);

    // The stage effect
    effect = std::make_unique<EffectView> (proc, stage,
                                           EffectGeometry { L::sfxIn, L::sfxOut, L::sfxWet, L::sfxKnobY, L::sfxKnobCentre, L::sfxKnobStep, L::sfxSwitchY },
                                           juce::Rectangle<int> (172, 178, 496, 170));
    effect->setBounds (effect->getBounds().translated (0, -155));
    addChildComponent (*effect);

    // Width / tone
    widthTab = std::make_unique<TabKey> ("WIDTH");
    widthTab->onClick = [this] { showTone (false); };
    place (*widthTab, L::widthTab);
    toneTab = std::make_unique<TabKey> ("TONE");
    toneTab->onClick = [this] { showTone (true); };
    place (*toneTab, L::toneTab);
    widthOn = std::make_unique<ParamToggle> (proc.param (S (pid::s1_width_on)), ParamToggle::Style::power);
    widthOn->help = "Width on or off (on, the stage costs 3 dB, as in Thermal).";
    widthOn->onChange = [this] { updateDimming(); };
    place (*widthOn, L::widthPower);
    toneOn = std::make_unique<ParamToggle> (proc.param (S (pid::s1_tone_on)), ParamToggle::Style::power);
    toneOn->help = "Tone on or off.";
    toneOn->onChange = [this] { updateDimming(); };
    place (*toneOn, L::tonePower);
    dialAt (sides, P (pid::s1_sides), "SIDES", "Level of the side signal, -36 to +12 dB.", Dial::Ring::bipolar, L::wtKnob1);
    dialAt (width, P (pid::s1_width), "WIDTH", "Spreads the left channel, 15 ms late, into both sides.", Dial::Ring::plain, L::wtKnob2);
    dialAt (toneLow, P (pid::s1_tone_lo), "LOW", "Low shelf at 500 Hz, -18 to +18 dB.", Dial::Ring::bipolar, L::wtKnob1);
    dialAt (toneHigh, P (pid::s1_tone_hi), "HIGH", "High shelf at 5 kHz, -18 to +18 dB.", Dial::Ring::bipolar, L::wtKnob2);

    // Clicking the type names: the first click brings the panel to the front, the next opens the list.
    type->onBeforeOpen = [this] { if (distortionInFront) return true; showDistortion (true); return false; };
    fxType->onBeforeOpen = [this] { if (! distortionInFront) return true; showDistortion (false); return false; };
    showDistortion (true);
    showTone (false);
    updateDimming();
}

void StageView::setHelpCallback (std::function<void (const juce::String&, bool)> cb)
{
    for (auto* d : { low.get(), high.get(), drive.get(), out.get(), c1.get(), c2.get(), c3.get(), c4.get(), time.get(), fb.get(),
                     sides.get(), width.get(), toneLow.get(), toneHigh.get() })
        d->onHover = [cb] (Dial* dd, bool on) { cb (dd->getHelp(), on); };
    for (Helpful* h : std::initializer_list<Helpful*> { solo.get(), split.get(), refilter.get(), autoKey.get(), syncKey.get(), followKey.get(),
                                                       distoOn.get(), fxOn.get(), widthOn.get(), toneOn.get(), release.get(), type.get(), fxType.get(),
                                                       prev.get(), next.get(), widthTab.get(), toneTab.get() })
        h->onHover = cb;
    effect->onHelp = cb;
}

void StageView::showDistortion (bool d)
{
    distortionInFront = d;
    for (juce::Component* c : std::initializer_list<juce::Component*> { autoKey.get(), drive.get(), out.get(), c1.get(), c2.get(), c3.get(), c4.get(),
                                                                        time.get(), syncKey.get(), fb.get(), release.get(), followKey.get(), shapeView.get() })
        c->setVisible (d);
    effect->setVisible (! d);
    if (! d) effect->showEffect (dt::names::stepIndex (proc.param (stageSwitch (stage, dt::pid::s1_fx_type)).convertFrom0to1 (
                                                           proc.param (stageSwitch (stage, dt::pid::s1_fx_type)).getValue()), dt::names::kNumEffects));
    repaint();
}

void StageView::showTone (bool t)
{
    toneInFront = t;
    widthTab->setSelected (! t);
    toneTab->setSelected (t);
    sides->setVisible (! t);
    width->setVisible (! t);
    toneLow->setVisible (t);
    toneHigh->setVisible (t);
    repaint();
}

void StageView::updateDimming()
{
    namespace pid = dt::pid;
    auto& tp = proc.param (stageParam (stage, pid::s1_type));
    int t = dt::names::stepIndex (tp.convertFrom0to1 (tp.getValue()), 34);
    if (t >= dt::names::kNumDistortionTypes && t < 32) t = dt::names::kNumDistortionTypes - 1;
    const bool on = distoOn->isOn();
    Dial* cs[4] = { c1.get(), c2.get(), c3.get(), c4.get() };
    for (int i = 0; i < 4; ++i) cs[i]->setDimmed (! on || ! kUsesControl[t][i]);
    for (auto* d : { drive.get(), out.get(), time.get(), fb.get() }) d->setDimmed (! on);
    for (auto* d : { sides.get(), width.get() }) d->setDimmed (! widthOn->isOn());
    for (auto* d : { toneLow.get(), toneHigh.get() }) d->setDimmed (! toneOn->isOn());
}

void StageView::tick()
{
    updateDimming();
    if (! distortionInFront)
    {
        auto& fp = proc.param (stageSwitch (stage, dt::pid::s1_fx_type));
        effect->showEffect (dt::names::stepIndex (fp.convertFrom0to1 (fp.getValue()), dt::names::kNumEffects));
        effect->setGainReduction (proc.meters().slotGainReductionDb[(size_t) stage].load (std::memory_order_relaxed));
    }
}

void StageView::paint (juce::Graphics& g)
{
    namespace L = layout;
    // Three panels, as in Thermal: hairline outlines on the sheet, the radius everywhere.
    for (auto r : { L::stagePanel, L::stageFxPanel, L::widthPanel })
    {
        g.setColour (col::hairline);
        g.drawRoundedRectangle (local (r).toFloat().reduced (0.5f), kRadius, stroke::hairline);
    }
    // Stage panel: "STAGE n", the band split / refilter captions.
    caption (g, "STAGE " + juce::String (stage + 1), local ({ 3, 183, 166, 16 }).toFloat(), juce::Justification::centred, col::ink, size::section);
    caption (g, "BAND SPLIT", local ({ 22, 299, 90, 14 }).toFloat(), juce::Justification::centredLeft, col::inkSoft, size::control);
    caption (g, "REFILTER", local ({ 22, 323, 90, 14 }).toFloat(), juce::Justification::centredLeft, col::inkSoft, size::control);
    // The tab row's selected underline under the panel in front.
    g.setColour (col::ink);
    const auto tab = local (distortionInFront ? L::distoType : L::fxType).toFloat();
    g.fillRect (tab.getX(), tab.getBottom() + 1.f, tab.getWidth(), 1.5f);
    if (distortionInFront)
    {
        caption (g, "VOLUME", local ({ 192, 192, 69, 14 }).toFloat(), juce::Justification::centred, col::inkFaint, size::tagLetter);
        caption (g, "DISTORTION SHAPE", local ({ 273, 192, 146, 14 }).toFloat(), juce::Justification::centred, col::inkFaint, size::tagLetter);
        caption (g, "SHAPE CONTROLS", local ({ 431, 192, 142, 14 }).toFloat(), juce::Justification::centred, col::inkFaint, size::tagLetter);
        caption (g, "FEEDBACK", local ({ 579, 192, 80, 14 }).toFloat(), juce::Justification::centred, col::inkFaint, size::tagLetter);
    }
    // Width / tone display: the stereo field (width) or the shelving curve (tone), simple and flat.
    const auto view = local (L::widthView).toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (view, kRadius);
    g.setColour (col::hairline);
    g.drawRoundedRectangle (view, kRadius, stroke::hairline);
    const auto plain = [&] (int id) { auto& prm = proc.param (stageParam (stage, id)); return prm.convertFrom0to1 (prm.getValue()); };
    juce::Path curve;
    if (toneInFront)
    {
        const double lo = plain (dt::pid::s1_tone_lo), hi = plain (dt::pid::s1_tone_hi);
        for (int i = 0; i <= 100; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / 100.0);
            const double lsh = lo / (1.0 + std::pow (f / 500.0, 2.0)), hsh = hi / (1.0 + std::pow (5000.0 / f, 2.0));
            const float x = view.getX() + view.getWidth() * (float) i / 100.f;
            const float y = view.getCentreY() - (float) ((lsh + hsh) / 18.0) * view.getHeight() * 0.42f;
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        g.setColour (toneOn->isOn() ? col::ink : col::inkFaint);
    }
    else
    {
        // a half-disc whose spread follows SIDES and WIDTH
        const double side = std::pow (10.0, plain (dt::pid::s1_sides) / 20.0), w = plain (dt::pid::s1_width);
        const float spread = (float) juce::jlimit (0.05, 1.0, 0.5 * side + 0.5 * w);
        const auto c = juce::Point<float> (view.getCentreX(), view.getBottom() - 6.f);
        const float rad = view.getHeight() - 14.f;
        curve.addCentredArc (c.x, c.y, rad, rad, 0.f, -spread * juce::MathConstants<float>::halfPi, spread * juce::MathConstants<float>::halfPi, true);
        g.setColour (col::hairline);
        g.drawLine (c.x, c.y, c.x, c.y - rad, stroke::hairline);
        g.setColour (widthOn->isOn() ? col::ink : col::inkFaint);
    }
    g.strokePath (curve, juce::PathStrokeType (stroke::trace));
}
