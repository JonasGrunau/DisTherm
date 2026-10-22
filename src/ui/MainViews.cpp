#include "MainViews.h"

#include "plugin/PluginProcessor.h"

using namespace theme;

namespace
{
    // The preset name as a button: click for the menu of DisTitten and Thermal presets.
    struct PresetButton : juce::Component
    {
        std::function<juce::String()> name;
        std::function<void()> onClick;
        PresetButton() { setMouseCursor (juce::MouseCursor::PointingHandCursor); setRepaintsOnMouseActivity (true); }
        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            caption (g, name ? name() : juce::String(), b.reduced (kMinSidePad, 0.f), juce::Justification::centredLeft,
                     isMouseOver() ? col::ink : col::ink.withAlpha (0.9f), 11.f);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (getLocalBounds().contains (e.getPosition()) && onClick) onClick();
        }
    };

} // namespace

void wordmark (juce::Graphics& g, juce::Rectangle<float> r, float height)
{
    g.setFont (title (height));
    const float w1 = textWidth (title (height), "DIS"), ws = textWidth (title (height), "/");
    g.setColour (col::ink);
    g.drawText ("DIS", r, juce::Justification::centredLeft, false);
    g.setColour (col::accent);
    g.drawText ("/", r.withTrimmedLeft (w1), juce::Justification::centredLeft, false);
    g.setColour (col::ink);
    g.drawText ("TITTEN", r.withTrimmedLeft (w1 + ws + 1.f), juce::Justification::centredLeft, false);
}

// ---- XYPad ----------------------------------------------------------------------------

XYPad::XYPad (DisTittenProcessor& p, bool l)
    : proc (p), large (l), m1 (p.param (dt::pid::macro1)), m2 (p.param (dt::pid::macro2))
{
    m1.addListener (this);
    m2.addListener (this);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    refreshNames();
}

XYPad::~XYPad()
{
    m1.removeListener (this);
    m2.removeListener (this);
}

void XYPad::refreshNames()
{
    name1 = proc.getMacroName (0);
    name2 = proc.getMacroName (1);
    repaint();
}

void XYPad::parameterValueChanged (int, float)
{
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<XYPad> (this)] {
        if (safe != nullptr) safe->repaint();
    });
}

juce::Rectangle<float> XYPad::field() const
{
    return getLocalBounds().toFloat().reduced (large ? 36.f : 18.f);
}

void XYPad::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto f = field();

    // Graph paper behind the field.
    juce::Path minor, major;
    const float step = f.getWidth() / 20.f;
    for (int i = 0; i <= 20; ++i)
    {
        auto& p = i % 5 == 0 ? major : minor;
        const float x = f.getX() + step * (float) i, y = f.getY() + step * (float) i;
        p.addRectangle (x, f.getY(), stroke::hairline, f.getHeight());
        p.addRectangle (f.getX(), y, f.getWidth(), stroke::hairline);
    }
    g.setColour (col::grid);
    g.fillPath (minor);
    g.setColour (col::hairline);
    g.fillPath (major);

    // The heat: flat accent rings around the puck, clipped to the circle; the outer rings fade out.
    const float x = m2.getValue(), y = m1.getValue();
    const auto puck = juce::Point<float> (f.getX() + x * f.getWidth(), f.getBottom() - y * f.getHeight());
    const auto disc = f.reduced (f.getWidth() * 0.02f);
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addEllipse (disc);
        g.reduceClipRegion (clip);
        g.setColour (col::well);
        g.fillEllipse (disc);
        const float heat = 0.35f + 0.65f * 0.5f * (x + y);   // more macro, more heat
        const int rings = 9;
        const float maxR = disc.getWidth() * 1.1f;
        for (int i = 0; i < rings; ++i)
        {
            const float t = (float) i / (float) (rings - 1);              // 0 outer .. 1 inner
            const float r = maxR * (1.f - t * 0.9f);
            g.setColour (col::accent.withAlpha (juce::jlimit (0.f, 1.f, heat * (0.05f + 0.11f * t))));
            g.fillEllipse (juce::Rectangle<float> (2.f * r, 2.f * r).withCentre (puck));
        }
    }
    g.setColour (col::hairline);
    g.drawEllipse (disc, stroke::hairline);
    g.drawEllipse (disc.expanded (large ? 14.f : 7.f), stroke::hairline);

    // The puck.
    g.setColour (col::sheet);
    g.fillEllipse (juce::Rectangle<float> (large ? 26.f : 18.f, large ? 26.f : 18.f).withCentre (puck));
    g.setColour (dragging ? col::ink : col::accent);
    g.drawEllipse (juce::Rectangle<float> (large ? 22.f : 15.f, large ? 22.f : 15.f).withCentre (puck), 1.6f);
    g.drawEllipse (juce::Rectangle<float> (large ? 12.f : 8.f, large ? 12.f : 8.f).withCentre (puck), 1.2f);

    // Axis names: MACRO 1 up the left, MACRO 2 along the bottom; corner brackets like Thermal's frame.
    const float pad = large ? 18.f : 6.f;
    g.setColour (col::inkFaint);
    g.fillRect (b.getX() + pad, b.getBottom() - pad - 90.f, stroke::hairline, 90.f);
    g.fillRect (b.getX() + pad, b.getBottom() - pad, 90.f, stroke::hairline);
    g.fillRect (b.getRight() - pad, b.getY() + pad, stroke::hairline, 90.f);
    g.fillRect (b.getRight() - pad - 90.f, b.getY() + pad, 90.f, stroke::hairline);
    if (large)
    {
        uprightCaption (g, name1, { b.getX() + pad - 16.f, b.getBottom() - pad - 230.f, 14.f, 130.f }, col::ink, 11.f);
        caption (g, name2, { b.getX() + pad + 100.f, b.getBottom() - pad - 7.f, 240.f, 14.f }, juce::Justification::centredLeft, col::ink, 11.f);
    }
}

void XYPad::setFrom (juce::Point<float> p)
{
    const auto f = field();
    m2.setValueNotifyingHost (juce::jlimit (0.f, 1.f, (p.x - f.getX()) / f.getWidth()));
    m1.setValueNotifyingHost (juce::jlimit (0.f, 1.f, (f.getBottom() - p.y) / f.getHeight()));
}

void XYPad::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    m1.beginChangeGesture();
    m2.beginChangeGesture();
    setFrom (e.position);
}

void XYPad::mouseDrag (const juce::MouseEvent& e) { if (dragging) setFrom (e.position); }

void XYPad::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    m1.endChangeGesture();
    m2.endChangeGesture();
    repaint();
}

void XYPad::mouseDoubleClick (const juce::MouseEvent&)
{
    for (auto* m : { &m1, &m2 })
    {
        m->beginChangeGesture();
        m->setValueNotifyingHost (m->getDefaultValue());
        m->endChangeGesture();
    }
}

// ---- RightColumn ----------------------------------------------------------------------------

RightColumn::RightColumn (DisTittenProcessor& p) : proc (p)
{
    namespace L = layout;
    setBounds (L::column.i());
    setOpaque (false);
    const auto place = [&] (juce::Component& c, layout::R r) { c.setBounds (local (r)); addAndMakeVisible (c); };
    const auto helpHook = [this] (Helpful& h) { h.onHover = [this] (const juce::String& t, bool on) { if (onHelp) onHelp (t, on); }; };

    auto pb = std::make_unique<PresetButton>();
    pb->name = [this] { return proc.getPresetName(); };
    pb->onClick = [this] { showPresetMenu(); };
    presetButton = std::move (pb);
    place (*presetButton, L::presetName);
    save = std::make_unique<IconButton> (IconButton::Icon::save);
    save->help = "Save the current state as a DisTitten preset.";
    save->onClick = [this] { savePresetAs(); };
    place (*save, L::presetSave);
    prev = std::make_unique<IconButton> (IconButton::Icon::prev);
    prev->help = "Previous preset.";
    prev->onClick = [this] { stepPreset (-1); };
    place (*prev, L::presetPrev);
    next = std::make_unique<IconButton> (IconButton::Icon::next);
    next->help = "Next preset.";
    next->onClick = [this] { stepPreset (1); };
    place (*next, L::presetNext);
    for (auto* h : std::initializer_list<Helpful*> { save.get(), prev.get(), next.get() }) helpHook (*h);

    for (int m = 0; m < 2; ++m)
    {
        macro[m] = std::make_unique<Dial> (proc.param (m == 0 ? dt::pid::macro1 : dt::pid::macro2), juce::String ("MACRO ") + juce::String (m + 1),
                                           "A macro: moves every parameter routed to it. X/Y of the pad move MACRO 2 / MACRO 1.");
        macro[m]->setMaxDiameter (124.f);
        macro[m]->setShowsName (false);
        macro[m]->setCompact (true);
        macro[m]->setRing (Dial::Ring::plain);
        macro[m]->onHover = [this] (Dial* d, bool on) { if (onHelp) onHelp (d->getHelp(), on); };
        // Thermal's knob sits centred in its 196 x 195 frame: a 77 px body in a ring of 124.
        const auto frame = (m == 0 ? L::macro1 : L::macro2).f();
        place (*macro[m], { frame.getCentreX() - 66.f, frame.getCentreY() - 64.f, 132.f, 128.f });

        macroName[m] = std::make_unique<juce::Label>();
        macroName[m]->setFont (label (12.f));
        macroName[m]->setJustificationType (juce::Justification::centred);
        macroName[m]->setColour (juce::Label::textColourId, col::ink);
        macroName[m]->setColour (juce::Label::textWhenEditingColourId, col::ink);
        macroName[m]->setColour (juce::TextEditor::highlightColourId, col::accentDim);
        macroName[m]->setEditable (false, true, false);
        macroName[m]->setText (proc.getMacroName (m), juce::dontSendNotification);
        macroName[m]->onTextChange = [this, m] { proc.setMacroName (m, macroName[m]->getText().toUpperCase()); };
        place (*macroName[m], m == 0 ? L::macro1Name : L::macro2Name);

        plus[m] = std::make_unique<IconButton> (IconButton::Icon::plus);
        plus[m]->help = "Assign: then drag any knob to set how far this macro moves it.";
        plus[m]->onClick = [this, m] { if (onAssign) onAssign (2 + m); };
        helpHook (*plus[m]);
        place (*plus[m], m == 0 ? L::macro1Plus : L::macro2Plus);
    }

    pageMain = std::make_unique<IconButton> (IconButton::Icon::pageMain);
    pageMain->help = "The XY page.";
    pageMain->onClick = [this] { if (onPage) onPage (0); };
    place (*pageMain, L::pageMain);
    pageEdit = std::make_unique<IconButton> (IconButton::Icon::pageEdit);
    pageEdit->help = "The edit page: stages, effects, modulation.";
    pageEdit->onClick = [this] { if (onPage) onPage (1); };
    place (*pageEdit, L::pageEdit);
    helpHook (*pageMain);
    helpHook (*pageEdit);

    dryWet = std::make_unique<Fader> (proc.param (dt::pid::mix), false);
    dryWet->setEndLabels ("DRY", "WET");
    dryWet->setBoxed (true);
    dryWet->help = "Master dry/wet (equal-power-ish, as Thermal: v^(5/7) each way).";
    helpHook (*dryWet);
    place (*dryWet, { 846, 440, 274, 52 });
    lock = std::make_unique<IconButton> (IconButton::Icon::lock);
    lock->help = "Lock dry/wet: keep it when loading presets.";
    lock->onClick = [this] { proc.dryWetLocked = ! proc.dryWetLocked; lock->setLit (proc.dryWetLocked); };
    lock->setLit (proc.dryWetLocked);
    helpHook (*lock);
    place (*lock, L::dryWetLock);

    helpButton = std::make_unique<IconButton> (IconButton::Icon::help);
    helpButton->help = "Help: hover a control to read what it does here.";
    helpButton->onClick = [this] { helpOn = ! helpOn; helpButton->setLit (helpOn); helpText.clear(); repaint(); };
    place (*helpButton, L::helpButton);

    inFader = std::make_unique<Fader> (proc.param (dt::pid::in_gain), true);
    inFader->setCaption ("INPUT");
    inFader->help = "Input gain, -60 to +6 dB (the dry signal too).";
    helpHook (*inFader);
    place (*inFader, { 1055, 600, 22, 52 });
    outFader = std::make_unique<Fader> (proc.param (dt::pid::out_gain), true);
    outFader->setCaption ("OUTPUT");
    outFader->help = "Output gain, -60 to +6 dB.";
    helpHook (*outFader);
    place (*outFader, { 1085, 600, 22, 52 });
}

void RightColumn::setPage (int page)
{
    pageMain->setLit (page == 0);
    pageEdit->setLit (page == 1);
}

void RightColumn::setAssignLit (int source)
{
    plus[0]->setLit (source == 2);
    plus[1]->setLit (source == 3);
}

void RightColumn::showHelpText (const juce::String& t)
{
    if (! helpOn || t == helpText) return;
    helpText = t;
    repaint (local (layout::xyReadout));
}

void RightColumn::tick()
{
    inFader->setMeter (juce::Decibels::gainToDecibels (proc.takePeak (false), -100.f));
    outFader->setMeter (juce::Decibels::gainToDecibels (proc.takePeak (true), -100.f));
    for (int m = 0; m < 2; ++m)
        if (! macroName[m]->isBeingEdited() && macroName[m]->getText() != proc.getMacroName (m))
            macroName[m]->setText (proc.getMacroName (m), juce::dontSendNotification);
    const float a = proc.param (dt::pid::macro1).getValue(), b = proc.param (dt::pid::macro2).getValue();
    if (std::abs (a - m1) > 1.0e-6f || std::abs (b - m2) > 1.0e-6f || proc.getPresetName() != shownPreset)
    {
        m1 = a; m2 = b; shownPreset = proc.getPresetName();
        repaint (local (layout::xyReadout));
        presetButton->repaint();
    }
    lock->setLit (proc.dryWetLocked);
}

void RightColumn::paint (juce::Graphics& g)
{
    namespace L = layout;
    // Preset bar: a well under the name, hairline under the bar.
    g.setColour (col::well);
    g.fillRoundedRectangle (local (L::presetName).toFloat(), kRadius);
    g.setColour (col::hairline);
    g.fillRect (local (L::presetBar).toFloat().removeFromBottom (1.f));
    // Separators: the column's left edge, above dry/wet, above the readout.
    g.fillRect (0.f, 0.f, stroke::hairline, (float) getHeight());
    g.fillRect (local ({ 846, 430, 274, 1 }).toFloat());
    g.fillRect (local ({ 846, 592, 274, 1 }).toFloat());

    // Wordmark and its sub-line.
    const auto brand = local (L::brand).toFloat();
    wordmark (g, brand.withHeight (22.f), 20.f);
    g.setColour (col::inkSoft);
    g.setFont (mono (8.5f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("DT-1 \xc2\xb7 v")) + JucePlugin_VersionString, brand.withTrimmedTop (22.f),
                juce::Justification::centredLeft, false);

    // The XY readout (or, in help mode, what the hovered control does).
    const auto box = local (L::xyReadout).toFloat();
    if (helpOn)
    {
        caption (g, "HELP", box.withHeight (16.f), juce::Justification::centredLeft, col::ink, size::section);
        g.setColour (col::inkSoft);
        g.setFont (text (11.f));
        g.drawFittedText (helpText.isEmpty() ? juce::String ("Hover a control.") : helpText, box.withTrimmedTop (20.f).toNearestInt(),
                          juce::Justification::topLeft, 5, 1.f);
    }
    else
    {
        caption (g, "XY CONTROL", box.withHeight (16.f), juce::Justification::centredLeft, col::inkSoft, size::section);
        for (int m = 0; m < 2; ++m)
        {
            const auto cell = box.withTrimmedTop (22.f).withWidth (box.getWidth() * 0.5f).translated (box.getWidth() * 0.5f * (float) m, 0.f);
            const float v = m == 0 ? m1 : m2;
            g.setColour (col::ink);
            g.setFont (monoStrong (24.f));
            g.drawText (juce::String (juce::roundToInt (v * 100.f)), cell.withHeight (30.f), juce::Justification::centredLeft, false);
            caption (g, "PERCENT", cell.withTrimmedTop (30.f).withHeight (12.f), juce::Justification::centredLeft, col::inkFaint, 8.f);
            const auto bar = cell.withTrimmedTop (50.f).withHeight (3.f).withTrimmedRight (14.f);
            g.setColour (col::well);
            g.fillRect (bar);
            g.setColour (col::inkSoft);
            g.fillRect (bar.withWidth (bar.getWidth() * v));
        }
    }

    // The faders' values under them.
    g.setFont (mono (9.f));
    g.setColour (col::ink);
    for (auto [f, r] : { std::pair { inFader.get(), L::R { 1050, 653, 27, 11 } }, std::pair { outFader.get(), L::R { 1079, 653, 27, 11 } } })
    {
        auto& prm = proc.param (f == inFader.get() ? dt::pid::in_gain : dt::pid::out_gain);
        g.drawText (fmt::fixed (prm.convertFrom0to1 (prm.getValue()), 1), local (r).toFloat().expanded (4.f, 0.f), juce::Justification::centred, false);
    }
}

void RightColumn::showPresetMenu()
{
    const auto presets = proc.listPresets();
    juce::PopupMenu menu, thermalMenu;
    std::map<juce::String, juce::PopupMenu> folders;
    for (size_t i = 0; i < presets.size(); ++i)
    {
        const auto& p = presets[i];
        const bool current = p.file == proc.getPresetFile();
        if (p.thermal) folders[p.folder].addItem ((int) i + 1, p.name, true, current);
        else menu.addItem ((int) i + 1, p.name, true, current);
    }
    if (menu.getNumItems() == 0) menu.addItem (-1, "No DisTitten presets yet", false);
    for (auto& [name, sub] : folders) thermalMenu.addSubMenu (name, sub);
    if (! folders.empty()) menu.addSubMenu ("THERMAL FACTORY", thermalMenu);
    menu.addSeparator();
    menu.addItem (-2, "Load Thermal preset or state file...");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton.get()),
                        [this, presets] (int r) {
                            if (r > 0) { proc.loadPreset (presets[(size_t) r - 1].file); repaint(); }
                            if (r == -2)
                            {
                                chooser = std::make_unique<juce::FileChooser> ("Thermal preset", juce::File(), "*.xml");
                                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                                      [this] (const juce::FileChooser& c) { if (c.getResult().existsAsFile()) proc.loadPreset (c.getResult()); });
                            }
                        });
}

void RightColumn::stepPreset (int delta)
{
    const auto presets = proc.listPresets();
    if (presets.empty()) return;
    int cur = -1;
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].file == proc.getPresetFile()) cur = (int) i;
    const int n = (int) presets.size();
    const int to = cur < 0 ? (delta > 0 ? 0 : n - 1) : ((cur + delta) % n + n) % n;
    proc.loadPreset (presets[(size_t) to].file);
    repaint();
}

void RightColumn::savePresetAs()
{
    auto folder = DisTittenProcessor::userPresetFolder();
    folder.createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Save DisTitten preset", folder.getChildFile (proc.getPresetName() + ".dtpreset"), "*.dtpreset");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& c) {
                              auto f = c.getResult();
                              if (f == juce::File()) return;
                              proc.savePreset (f.withFileExtension ("dtpreset"));
                              repaint();
                          });
}
