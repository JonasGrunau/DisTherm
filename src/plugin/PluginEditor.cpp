#include "PluginEditor.h"

using namespace theme;

namespace
{
    constexpr int kStageOn[3] { dt::pid::s1_on, dt::pid::s2_on, dt::pid::s3_on };

    template <typename Fn>
    void forEachChild (juce::Component& c, Fn&& fn)
    {
        for (auto* child : c.getChildren())
        {
            fn (*child);
            forEachChild (*child, fn);
        }
    }
} // namespace

DisTittenEditor::DisTittenEditor (DisTittenProcessor& p)
    : AudioProcessorEditor (p), proc (p), column (p), bigXY (p, true), bandStrip (p), lower (p)
{
    namespace L = layout;
    setLookAndFeel (&lnf);
    addAndMakeVisible (sheet);
    const auto help = [this] (const juce::String& t, bool on) { showHelp (t, on); };

    // Main page.
    sheet.addChildComponent (bigXY);
    bigXY.setBounds (L::mainXY.i());

    // Edit page: band strip, the three stages (one shown), their tabs over them, mod envelopes, lower panel.
    sheet.addChildComponent (bandStrip);
    bandStrip.setBounds (L::bandStrip.i());
    bandStrip.onSelectStage = [this] (int s) { selectStage (s); };
    bandStrip.onHelp = help;

    for (int s = 0; s < 3; ++s)
    {
        stages[(size_t) s] = std::make_unique<StageView> (p, s);
        sheet.addChildComponent (*stages[(size_t) s]);
        stages[(size_t) s]->setBounds (0, 155, L::column.i().getX() + 1, 195);
        stages[(size_t) s]->setHelpCallback (help);
    }
    for (int s = 0; s < 3; ++s)
    {
        auto& tab = stageTabs[(size_t) s];
        tab = std::make_unique<TabKey> ("0" + juce::String (s + 1));
        tab->help = "Shows stage " + juce::String (s + 1) + ": its band, distortion, effect, width and tone.";
        tab->onHover = help;
        tab->onClick = [this, s] { selectStage (s); };
        sheet.addChildComponent (*tab);
        tab->setBounds (L::stageTab[s].i());

        auto& power = stagePower[(size_t) s];
        power = std::make_unique<ParamToggle> (p.param (kStageOn[s]), ParamToggle::Style::power);
        power->help = "Stage " + juce::String (s + 1) + " on or off. Off, its band passes through untouched (delayed like the others).";
        power->onHover = help;
        sheet.addChildComponent (*power);
        power->setBounds (L::stagePower[s].i());
    }

    for (int e = 0; e < 2; ++e)
    {
        auto& m = mods[(size_t) e];
        m = std::make_unique<ModPanel> (p, e);
        m->onAssign = [this] (int source) { setAssign (source); };
        m->onHelp = help;
        sheet.addChildComponent (*m);
        m->setBounds (L::modPanel.moved (0, L::modStep * (float) e).i());
    }

    sheet.addChildComponent (lower);
    lower.setBounds (L::lowerPanel.i());
    lower.onHelp = help;

    // Both pages.
    sheet.addAndMakeVisible (column);
    column.setBounds (L::column.i());
    column.onPage = [this] (int pg) { showPage (pg); };
    column.onAssign = [this] (int source) { setAssign (source); };
    column.onHelp = help;

    collectDials (sheet);
    selectStage (0);
    showPage (0);

    setResizable (true, true);
    setResizeLimits (kWidth * 3 / 4, kHeight * 3 / 4, kWidth * 2, kHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) kWidth / kHeight);
    setSize (kWidth, kHeight);
    tick();
    startTimerHz (30);
}

DisTittenEditor::~DisTittenEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DisTittenEditor::resized()
{
    sheet.setBounds (0, 0, kWidth, kHeight);
    sheet.setTransform (juce::AffineTransform::scale ((float) getWidth() / kWidth));
}

void DisTittenEditor::Sheet::paint (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    if (editor.page == 0) wordmark (g, { 130.f, 48.f, 240.f, 30.f }, 28.f);   // where Thermal shows its logo, clear of the pad grid
}

void DisTittenEditor::showPage (int p)
{
    page = juce::jlimit (0, 1, p);
    const bool edit = page == 1;
    bigXY.setVisible (! edit);
    sheet.repaint (0, 0, layout::column.i().getX(), 120);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &bandStrip, &lower, mods[0].get(), mods[1].get() })
        c->setVisible (edit);
    for (int s = 0; s < 3; ++s)
    {
        stages[(size_t) s]->setVisible (edit && s == stage);
        stageTabs[(size_t) s]->setVisible (edit);
        stagePower[(size_t) s]->setVisible (edit);
    }
    column.setPage (page);
}

void DisTittenEditor::selectStage (int s)
{
    stage = juce::jlimit (0, 2, s);
    for (int i = 0; i < 3; ++i)
    {
        stages[(size_t) i]->setVisible (page == 1 && i == stage);
        stageTabs[(size_t) i]->setSelected (i == stage);
    }
    bandStrip.setStage (stage);
}

void DisTittenEditor::setAssign (int source)
{
    assign = (source == assign) ? -1 : juce::jlimit (-1, 3, source);
    column.setAssignLit (assign);
    for (int e = 0; e < 2; ++e) mods[(size_t) e]->setAssignLit (assign == e);
    showHelp (assign < 0 ? juce::String()
                         : "Assigning " + juce::String (assign < 2 ? proc.getEnvelopeName (assign) : proc.getMacroName (assign - 2))
                               + ": drag a knob to set how far it moves it. Click + again to finish.",
              assign >= 0);
}

void DisTittenEditor::showHelp (const juce::String& text, bool entering)
{
    column.showHelpText (entering ? text : juce::String());
}

void DisTittenEditor::collectDials (juce::Component& root)
{
    forEachChild (root, [this] (juce::Component& c) {
        if (auto* d = dynamic_cast<Dial*> (&c); d != nullptr && std::find (dials.begin(), dials.end(), d) == dials.end())
        {
            dials.push_back (d);
            wireModulation (*d);
        }
    });
}

// While a source is being assigned, dragging a knob sets that source's amount on it (Thermal's ModAmount, -1..1
// of the normalised range); back to 0 the connection goes away. The macros themselves take no modulation.
void DisTittenEditor::wireModulation (Dial& d)
{
    const int id = d.getParameter().getParameterIndex();
    if (id < 0 || id >= dt::kNumParams || id == dt::pid::macro1 || id == dt::pid::macro2) return;

    d.onModDrag = [this, id] (float delta, bool begin, bool end) {
        if (assign < 0) return false;
        if (begin)
        {
            editState = std::make_unique<dt::ModState> (proc.modState());
            return true;
        }
        if (editState == nullptr) return false;
        auto& slot = editState->targets[(size_t) id].slot[(size_t) assign];
        if (end)
        {
            if (std::abs (slot.amount) < 1.0e-4f && slot.depthSources == 0) slot = {};
            proc.setModState (*editState);
            editState.reset();
            return true;
        }
        slot.connected = true;
        slot.amount = juce::jlimit (-1.f, 1.f, slot.amount + delta);
        proc.setModState (*editState);
        return true;
    };
}

void DisTittenEditor::tick()
{
    column.tick();
    if (page == 1)
    {
        bandStrip.tick();
        stages[(size_t) stage]->tick();
        for (auto& m : mods) m->tick();
        lower.tick();
    }

    // Effect layouts are built when first shown: pick up their knobs.
    if (++ticks % 15 == 0) collectDials (sheet);

    // Names edited in the column reach both XY pads.
    if (const auto n = proc.getMacroName (0) + "\n" + proc.getMacroName (1); n != shownNames)
    {
        shownNames = n;
        bigXY.refreshNames();
        lower.refreshNames();
    }

    // Where each modulated knob is now: the sources as the engine last saw them, through the knob's routing.
    const auto& m = proc.meters();
    float src[4];
    for (int i = 0; i < 4; ++i) src[i] = m.source[(size_t) i].load (std::memory_order_relaxed);
    const auto& state = proc.modState();
    for (auto* d : dials)
    {
        bool shown = true;   // visible up to the editor (isShowing() also wants a window, which the snapshot tool has not)
        for (juce::Component* c = d; c != nullptr && c != this && shown; c = c->getParentComponent()) shown = c->isVisible();
        if (! shown) continue;
        const int id = d->getParameter().getParameterIndex();
        if (id < 0 || id >= dt::kNumParams) continue;
        const auto& t = state.targets[(size_t) id];
        d->setModulated (t.active() ? t.compute (d->getParameter().getValue(), src) : -1.f);
    }
}

void DisTittenEditor::setUiState (const juce::StringPairArray& s)
{
    const auto get = [&s] (const char* key, int fallback) {
        return s.containsKey (key) ? s[key].getIntValue() : fallback;
    };
    showPage (get ("page", page));
    selectStage (get ("stage", stage));
    lower.showTab (get ("lower", lower.tab()));
    if (s.containsKey ("stagefx")) stages[(size_t) stage]->showDistortion (get ("stagefx", 0) == 0);
    if (s.containsKey ("tone")) stages[(size_t) stage]->showTone (get ("tone", 0) == 1);
    if (s.containsKey ("assign")) { assign = -2; setAssign (get ("assign", -1)); }
    collectDials (sheet);
    tick();
}
