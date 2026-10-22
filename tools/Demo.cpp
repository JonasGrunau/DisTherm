// DisTitten without a DAW: loops built-in material (or your file) through
// the plugin and out of the speakers, with the plugin's editor below a small
// transport bar.
//   distitten_demo [file.wav]                  effect
//   distitten_demo [reference.wav track.wav]   sidechain effect (kSidechain)
//
// BYPASS plays the material untouched for comparison (sidechain: SUM plays
// reference + processed track with the reference delayed by the plugin's
// latency, the way a DAW compensates it; TRACK and REFERENCE play one side
// alone). Space plays and stops.
// The built-in material is a psytrance kick and rolling bass: real samples
// from a local kit file when DT_DEMO_KIT names one (DemoKit.h;
// start.sh sets it for demo-kit.txt), otherwise synthesized (PsyLoop.h). The
// plugin gets a matching looping transport (TempoPlayHead.h), so anything
// that follows the host's tempo, bars or loop works as in a DAW.
//
// Template: merged from BuildApp's demo (one signal, BYPASS, open a file) and
// PhaseFucker's (kit, transport, scenarios, sidechain monitor). The places a
// product plugs in are marked PRODUCT HOOK. It needs from the product:
//   DisTittenProcessor / DisTittenEditor (src/plugin), the editor's
//   static constexpr int kWidth, kHeight, and from src/ui/Theme.h (namespace
//   theme): col::sheet, col::ink, col::inkSoft, col::hairline, col::accent,
//   label(), mono(), caption(), kRadius.

#include "DemoKit.h"
#include "PsyLoop.h"
#include "TempoPlayHead.h"
#include "TestUtil.h"
#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

using namespace theme;

namespace
{
    // PRODUCT HOOK: true when the processor has a sidechain input bus
    // (PhaseFucker: main in/out plus .withInput ("Reference", stereo, true)).
    // The material then carries a second signal for input channels 3-4, the
    // command line takes two files (the reference first), and the bar shows
    // SUM / TRACK / REFERENCE instead of BYPASS.
    constexpr bool kSidechain = false;
    constexpr int kChannels = kSidechain ? 4 : 2;
    constexpr int kFiles = 100;   // the source menu's "your file(s)" item

    // One stereo signal (plus, with a sidechain, the reference of the same
    // length) at one sample rate, looped.
    struct Material
    {
        juce::String name, note;
        double rate = 0.0, bpm = 120.0;
        bool hasTempo = false;   // built-in loops: `bpm` from sample 0
        juce::AudioBuffer<float> main, side;
    };

    juce::AudioBuffer<float> stereo (const testutil::Signal& s, float gain)
    {
        juce::AudioBuffer<float> b (2, (int) s.size());
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < (int) s.size(); ++i)
                b.setSample (c, i, gain * s[(size_t) i]);
        return b;
    }

    // PRODUCT HOOK: the built-in scenarios, picked in the bar. The first is
    // made in makeScenario() from the kick + bass loop. (PhaseFucker has four: the bass
    // changing notes under the kick, the same every beat, the kick's sub layer
    // late and flipped, the loop through a phase-turning chain.) DisTitten adds
    // the kit's `loop = ` lines (drums, a vocal, a lead, a guitar: what a
    // distortion gets fed), each played as it is.
    juce::StringArray scenarioNames()
    {
        juce::StringArray names { "Kick + bass loop" };
        if (const auto kit = kit::load())
            for (const auto& l : kit->loops) names.add (l.name);
        return names;
    }

    std::shared_ptr<Material> makeScenario (int which, double fs)
    {
        auto m = std::make_shared<Material>();
        m->rate = fs;
        m->hasTempo = true;
        const auto kit = kit::load();
        if (kit && which >= 1 && which <= (int) kit->loops.size())
        {
            const auto& l = kit->loops[(size_t) which - 1];
            if (kit::readFile (l.file, fs, m->main))
            {
                m->name = l.name;
                m->bpm = l.bpm > 0.0 ? l.bpm : kit->bpm;
                m->note = "Kit loop " + l.file.getFileNameWithoutExtension() + ", " + juce::String (m->bpm, 0) + " bpm.";
                const float peak = std::max (1.0e-6f, m->main.getMagnitude (0, m->main.getNumSamples()));
                m->main.applyGain (0.71f / peak);   // peaks to -3 dBFS, as below
                if constexpr (kSidechain) m->side.makeCopyOf (m->main);
                return m;
            }
        }
        m->name = "Kick + bass loop";
        const bool kitLoop = kit && kit->hasKickAndBass();
        const auto loop = kitLoop ? kit::make (*kit, fs, true) : psy::make (fs, true);
        m->bpm = kitLoop ? kit->bpm : psy::kBpm;
        const juce::String source = kitLoop ? "Kit " + kit->kick.getFileNameWithoutExtension() + " + " + kit->bass[0].getFileNameWithoutExtension() + ". "
                                            : juce::String ("Synthesized (no kick and bass in DT_DEMO_KIT). ");
        testutil::Signal main, side;
        switch (which)
        {
            default:
                // PRODUCT HOOK: what goes where. Sidechain: the kick is the
                // reference, the bass the track. Otherwise both, summed.
                // (BuildApp plays testutil::psyLoop from tests/TestUtil.h
                // instead: kick, bass, offbeat hats and a stereo arp, which
                // covers the whole spectrum for a filter.)
                if constexpr (kSidechain)
                {
                    side = loop.kick;
                    main = loop.bass;
                }
                else
                {
                    main = loop.kick;
                    for (size_t i = 0; i < main.size() && i < loop.bass.size(); ++i)
                        main[i] += loop.bass[i];
                }
                m->note = source + "Kick and bass, four bars.";
                break;
        }
        // Peaks to -3 dBFS, so the plugin's input doesn't clip.
        const auto peak = [] (const testutil::Signal& x) { float p = 1e-6f; for (float v : x) p = std::max (p, std::abs (v)); return p; };
        m->main = stereo (main, 0.71f / peak (main));
        if constexpr (kSidechain)
            m->side = stereo (side, 0.71f / peak (side));
        return m;
    }

    // Your file(s): the track, or with a sidechain the reference first, then the track.
    std::shared_ptr<Material> makeFromFiles (const std::array<juce::File, 2>& files, double fs)
    {
        auto m = std::make_shared<Material>();
        m->rate = fs;
        if constexpr (kSidechain)
        {
            if (! kit::readFile (files[0], fs, m->side) || ! kit::readFile (files[1], fs, m->main)) return nullptr;
            const int n = std::min (m->side.getNumSamples(), m->main.getNumSamples());
            m->side.setSize (2, n, true);
            m->main.setSize (2, n, true);
            m->name = files[0].getFileName() + " + " + files[1].getFileName();
            m->note = "Your files: the first is the reference (sidechain), the second the track.";
        }
        else
        {
            if (! kit::readFile (files[0], fs, m->main)) return nullptr;
            m->name = m->note = files[0].getFileName();
        }
        return m;
    }

    bool filesExist (const std::array<juce::File, 2>& files)
    {
        return files[0].existsAsFile() && (! kSidechain || files[1].existsAsFile());
    }
} // namespace

// Feeds the processor from the device callback, like a host would.
class DemoAudio : public juce::AudioIODeviceCallback
{
public:
    enum Monitor { sum, track, reference };

    explicit DemoAudio (DisTittenProcessor& p) : proc (p) { proc.setPlayHead (&playHead); }

    std::function<void (double)> onRate;   // message thread: the device runs at a new rate
    std::atomic<int> monitor { sum };      // sidechain only
    std::atomic<bool> playing { true }, bypass { false };

    void setMaterial (std::shared_ptr<Material> m)
    {
        {
            const juce::SpinLock::ScopedLockType l (lock);
            material = std::move (m);
            pos = 0;
        }
        // PRODUCT HOOK: forget what the processor learned from the old
        // material, if anything (PhaseFucker: proc.restartListening()).
    }

    double rate() const noexcept { return fs.load(); }

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        start (d->getCurrentSampleRate(), std::max (16, d->getCurrentBufferSizeSamples()));
    }

    void start (double rate, int block)
    {
        proc.setRateAndBufferSizeDetails (rate, block);
        proc.prepareToPlay (rate, block);
        capacity = block;
        work.setSize (kChannels, block);
        dry.setSize (2, block);
        delay.setSize (2, 1 << 15);
        delay.clear();
        delayPos = 0;
        fs = rate;
        juce::MessageManager::callAsync ([this, rate] { if (onRate) onRate (rate); });
    }

    void audioDeviceStopped() override { proc.releaseResources(); }

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int c = 0; c < numOut; ++c)
            if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);

        const juce::SpinLock::ScopedTryLockType l (lock);
        if (! l.isLocked() || material == nullptr || std::abs (material->rate - fs.load()) > 0.5) return;
        if (! playing)
        {
            // Stopped: hosts keep processing, with the transport standing.
            juce::MidiBuffer midi;
            for (int done = 0; done < n; done += capacity)
            {
                work.setSize (kChannels, std::min (capacity, n - done), false, false, true);
                work.clear();
                playHead.playing = false;
                proc.processBlock (work, midi);
            }
            playHead.playing = true;
            return;
        }

        const auto& m = *material;
        const int len = m.main.getNumSamples();
        const int latency = proc.getLatencySamples();
        const int mask = delay.getNumSamples() - 1;
        juce::MidiBuffer midi;

        for (int done = 0; done < n;)
        {
            const int chunk = std::min ({ n - done, capacity, len - (int) pos });
            work.setSize (kChannels, chunk, false, false, true);
            for (int c = 0; c < 2; ++c)
            {
                work.copyFrom (c, 0, m.main, c, (int) pos, chunk);
                if constexpr (kSidechain)
                {
                    work.copyFrom (c + 2, 0, m.side, c, (int) pos, chunk);
                    dry.copyFrom (c, 0, m.side, c, (int) pos, chunk);
                }
            }
            playHead.sampleRate = fs.load();
            playHead.position = pos;
            playHead.hasTempo = m.hasTempo;
            playHead.bpm = m.bpm;
            playHead.loopQuarters = len / fs.load() * m.bpm / 60.0;
            proc.processBlock (work, midi);

            if constexpr (kSidechain)
            {
                const int mon = monitor.load();
                for (int i = 0; i < chunk; ++i)
                {
                    for (int c = 0; c < 2; ++c)
                        delay.setSample (c, (delayPos + i) & mask, dry.getSample (c, i));
                    for (int c = 0; c < std::min (numOut, 2); ++c)
                    {
                        if (out[c] == nullptr) continue;
                        // The reference as the host would play it: late by the plugin's latency.
                        const float r = delay.getSample (c, (delayPos + i - latency) & mask);
                        const float t = work.getSample (c, i);
                        out[c][done + i] = mon == sum ? 0.5f * (r + t) : mon == track ? 0.7f * t : 0.7f * r;
                    }
                }
                delayPos = (delayPos + chunk) & mask;
            }
            else
            {
                // Bypass plays the material as it is (not delayed by the plugin's latency).
                juce::ignoreUnused (latency, mask);
                for (int c = 0; c < std::min (numOut, 2); ++c)
                    if (out[c] != nullptr)
                        juce::FloatVectorOperations::copy (out[c] + done, bypass ? m.main.getReadPointer (c, (int) pos) : work.getReadPointer (c), chunk);
            }
            pos = (pos + chunk) % len;
            done += chunk;
        }
    }

private:
    DisTittenProcessor& proc;
    TempoPlayHead playHead;
    juce::SpinLock lock;
    std::shared_ptr<Material> material;
    juce::int64 pos = 0;
    std::atomic<double> fs { 0.0 };
    juce::AudioBuffer<float> work, dry, delay;
    int capacity = 0, delayPos = 0;
};

// PRODUCT HOOK: derive from the product's own LookAndFeel so the bar matches
// the editor (BuildApp: NightLookAndFeel, PhaseFucker: PaperLookAndFeel; the
// skill's src/ui/Controls.h template: SheetLookAndFeel, include "ui/Controls.h").
class BarLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BarLookAndFeel()
    {
        setColour (juce::ComboBox::backgroundColourId, col::sheet);
        setColour (juce::ComboBox::textColourId, col::ink);
        setColour (juce::ComboBox::outlineColourId, col::ink);
        setColour (juce::ComboBox::arrowColourId, col::ink);
    }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return label (10.f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return mono (11.f); }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f).translated (0.f, down ? 1.f : 0.f);
        if (b.getToggleState())
        {
            g.setColour (col::ink);
            g.fillRoundedRectangle (r, kRadius);
        }
        else
        {
            g.setColour (over ? col::ink : col::inkSoft);
            g.drawRoundedRectangle (r, kRadius, 1.f);
        }
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        g.setFont (label (10.f));
        g.setColour (b.getToggleState() ? col::sheet : col::ink);
        g.drawText (b.getButtonText().toUpperCase(), b.getLocalBounds().translated (0, down ? 1 : 0), juce::Justification::centred, false);
    }
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        g.setColour (col::ink);
        g.drawRoundedRectangle (juce::Rectangle<float> ((float) w, (float) h).reduced (0.5f), kRadius, 1.f);
        juce::Path arrow;
        arrow.addTriangle ((float) w - 16.f, (float) h * 0.5f - 2.f, (float) w - 8.f, (float) h * 0.5f - 2.f, (float) w - 12.f, (float) h * 0.5f + 3.f);
        g.fillPath (arrow);
    }
};

class DemoComponent : public juce::Component
{
public:
    static constexpr int kBar = 52;

    explicit DemoComponent (std::array<juce::File, 2> f) : audio (proc), files (std::move (f))
    {
        if (filesExist (files)) current = kFiles;
        setLookAndFeel (&lnf);
        setOpaque (true);
        editor.reset (dynamic_cast<DisTittenEditor*> (proc.createEditor()));
        addAndMakeVisible (*editor);

        // Space plays and stops from anywhere, so the controls don't take the focus.
        play.setWantsKeyboardFocus (false);
        source.setWantsKeyboardFocus (false);
        play.setClickingTogglesState (true);
        play.setToggleState (true, juce::dontSendNotification);
        play.onClick = [this] { audio.playing = play.getToggleState(); play.setButtonText (play.getToggleState() ? "Stop" : "Play"); };
        play.setButtonText ("Stop");
        addAndMakeVisible (play);

        const auto names = scenarioNames();
        for (int i = 0; i < names.size(); ++i) source.addItem (names[i], i + 1);
        source.addSeparator();
        source.addItem (juce::String (juce::CharPointer_UTF8 (kSidechain ? "Your files\xe2\x80\xa6" : "Your file\xe2\x80\xa6")), kFiles);
        source.onChange = [this] { chooseSource (source.getSelectedId()); };
        addAndMakeVisible (source);

        if constexpr (kSidechain)
        {
            const char* names[] = { "Sum", "Track", "Reference" };
            for (int i = 0; i < 3; ++i)
            {
                auto& b = monitor[(size_t) i];
                b.setButtonText (names[i]);
                b.setClickingTogglesState (true);
                b.setRadioGroupId (1);
                b.setWantsKeyboardFocus (false);
                b.onClick = [this, i] { if (monitor[(size_t) i].getToggleState()) audio.monitor = i; };
                addAndMakeVisible (b);
            }
            monitor[0].setToggleState (true, juce::dontSendNotification);
        }
        else
        {
            bypass.setButtonText ("Bypass");
            bypass.setClickingTogglesState (true);
            bypass.setWantsKeyboardFocus (false);
            bypass.onClick = [this] { audio.bypass = bypass.getToggleState(); };
            addAndMakeVisible (bypass);
        }

        audio.onRate = [this] (double) { reload(); };
        const auto err = devices.initialiseWithDefaultDevices (0, 2);
        if (err.isNotEmpty()) note = "Audio device: " + err;
        devices.addAudioCallback (&audio);

        setWantsKeyboardFocus (true);
        setSize (DisTittenEditor::kWidth, DisTittenEditor::kHeight + kBar);
    }

    ~DemoComponent() override
    {
        devices.removeAudioCallback (&audio);
        editor.reset();
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::sheet);
        auto bar = getLocalBounds().removeFromTop (kBar).toFloat();
        g.setColour (col::hairline);
        g.fillRect (bar.removeFromBottom (1.f));
        caption (g, "Demo host", { 24.f, 6.f, 100.f, 12.f }, juce::Justification::centredLeft, col::accent, 9.f);
        g.setFont (mono (10.f));
        g.setColour (col::inkSoft);
        const float x = kSidechain ? 600.f : 450.f;
        g.drawText (note, juce::Rectangle<float> (x, 8.f, (float) getWidth() - x - 24.f, 36.f), juce::Justification::centredRight, true);
    }

    void resized() override
    {
        play.setBounds (24, 20, 64, 24);
        source.setBounds (98, 20, 250, 24);
        if constexpr (kSidechain)
            for (int i = 0; i < 3; ++i)
                monitor[(size_t) i].setBounds (362 + i * 74, 20, i == 2 ? 86 : 70, 24);
        else
            bypass.setBounds (362, 20, 74, 24);
        editor->setBounds (0, kBar, DisTittenEditor::kWidth, DisTittenEditor::kHeight);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::spaceKey) { play.triggerClick(); return true; }
        return false;
    }

    void visibilityChanged() override
    {
        if (isShowing()) grabKeyboardFocus();
    }

private:
    // (Re)builds the current material at the device's rate.
    void reload()
    {
        const double fs = audio.rate();
        if (fs <= 0.0) return;
        std::shared_ptr<Material> m;
        if (current == kFiles && filesExist (files))
            m = makeFromFiles (files, fs);
        if (m == nullptr)
        {
            if (current == kFiles) current = 1;
            m = makeScenario (current - 1, fs);
        }
        note = m->note;
        source.setSelectedId (current, juce::dontSendNotification);
        audio.setMaterial (std::move (m));
        repaint (0, 0, getWidth(), kBar);
    }

    void chooseSource (int id)
    {
        if (id != kFiles)
        {
            current = id;
            reload();
            return;
        }
        const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        const juce::String types ("*.wav;*.aif;*.aiff;*.flac;*.mp3");
        if constexpr (! kSidechain)
        {
            chooser = std::make_unique<juce::FileChooser> ("Audio to play through DisTitten", files[0], types);
            chooser->launchAsync (flags, [this] (const juce::FileChooser& c) {
                if (c.getResult().existsAsFile()) { files[0] = c.getResult(); current = kFiles; }
                reload();
            });
            return;
        }
        // Two choosers in a row: reference first, then the track.
        chooser = std::make_unique<juce::FileChooser> ("Reference (goes to the sidechain)", files[0], types);
        chooser->launchAsync (flags, [this, types] (const juce::FileChooser& a) {
            const auto ref = a.getResult();
            if (! ref.existsAsFile()) { source.setSelectedId (current, juce::dontSendNotification); return; }
            chooser = std::make_unique<juce::FileChooser> ("Track (the one DisTitten processes)", ref.getParentDirectory(), types);
            chooser->launchAsync (flags, [this, ref] (const juce::FileChooser& b) {
                const auto trk = b.getResult();
                if (trk.existsAsFile()) { files = { ref, trk }; current = kFiles; }
                reload();
            });
        });
    }

    DisTittenProcessor proc;
    DemoAudio audio;
    juce::AudioDeviceManager devices;
    BarLookAndFeel lnf;
    std::unique_ptr<DisTittenEditor> editor;
    juce::TextButton play, bypass;
    juce::ComboBox source;
    std::array<juce::TextButton, 3> monitor;
    std::array<juce::File, 2> files;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String note { "Starting audio..." };
    int current = 1;
};

class DemoApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "DisTitten Demo"; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }

    void initialise (const juce::String& args) override
    {
        const auto parts = juce::StringArray::fromTokens (args, true);
        const auto file = [&] (int i) { return i < parts.size() ? juce::File::getCurrentWorkingDirectory().getChildFile (parts[i].unquoted()) : juce::File(); };
        window = std::make_unique<Window> (std::make_unique<DemoComponent> (std::array<juce::File, 2> { file (0), file (1) }));
    }

    void shutdown() override { window.reset(); }

private:
    struct Window : juce::DocumentWindow
    {
        explicit Window (std::unique_ptr<DemoComponent> content)
            : DocumentWindow ("DisTitten Demo", col::sheet, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (content.release(), true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::quit(); }
    };

    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION (DemoApp)
