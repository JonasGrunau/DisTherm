#include "PluginProcessor.h"

#include "ParamText.h"
#include "PluginEditor.h"
#include "ThermalImport.h"

DisTittenProcessor::DisTittenProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DisTitten", createLayout())
{
    for (int i = 0; i < dt::kNumParams; ++i)
    {
        params[(size_t) i] = apvts.getParameter (dt::kParams[i].id);
        rawParams[(size_t) i] = apvts.getRawParameterValue (dt::kParams[i].id);
        jassert (params[(size_t) i] != nullptr && rawParams[(size_t) i] != nullptr);
    }
    modChannel->publish (*modEdit);
    engine.setModChannel (modChannel.get());
    setLatencySamples (dt::Engine::kLatency);   // constant at every rate: hosts that ask before prepareToPlay get it too
    startTimerHz (20);
}

DisTittenProcessor::~DisTittenProcessor()
{
    stopTimer();
}

// ---- Modulation, Thermal presets, LOW/HIGH coupling -----------------------------

void DisTittenProcessor::setModState (const dt::ModState& m)
{
    *modEdit = m;
    modChannel->publish (*modEdit);
}

bool DisTittenProcessor::loadThermalState (const juce::XmlElement& root)
{
    thermal::Patch patch;
    if (! thermal::read (root, patch)) return false;
    for (int i = 0; i < dt::kNumParams; ++i)
        params[(size_t) i]->setValueNotifyingHost (patch.norm[(size_t) i]);
    for (int i = 0; i < 2; ++i)
    {
        macroNames[i] = patch.macroNames[i];
        envelopeNames[i] = patch.envelopeNames[i];
    }
    setModState (*patch.mod);
    return true;
}

bool DisTittenProcessor::loadThermalPreset (const juce::File& xmlFile)
{
    if (auto xml = juce::parseXML (xmlFile))
        return loadThermalState (*xml);
    return false;
}

juce::File DisTittenProcessor::userPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DisTitten").getChildFile ("Presets");
}

std::vector<DisTittenProcessor::PresetEntry> DisTittenProcessor::listPresets() const
{
    std::vector<PresetEntry> out;
    const auto add = [&] (const juce::File& root, const juce::String& pattern, bool thermal) {
        if (! root.isDirectory()) return;
        auto files = root.findChildFiles (juce::File::findFiles, true, pattern);
        files.sort();
        for (const auto& f : files)
            out.push_back ({ f, f.getFileNameWithoutExtension(), f.getParentDirectory().getFileName(), thermal });
    };
    add (userPresetFolder(), "*.dtpreset", false);
    add (thermal::presetFolder(), "*.xml", true);
    return out;
}

bool DisTittenProcessor::loadPreset (const juce::File& file)
{
    auto xml = juce::parseXML (file);
    if (xml == nullptr) return false;
    const float mix = rawParams[(size_t) dt::pid::mix]->load();
    bool ok = false;
    if (xml->hasTagName ("Thermal"))
        ok = loadThermalState (*xml);
    else if (xml->hasTagName (apvts.state.getType()))
    {
        juce::MemoryBlock mb;
        copyXmlToBinary (*xml, mb);
        setStateInformation (mb.getData(), (int) mb.getSize());
        ok = true;
    }
    if (! ok) return false;
    if (dryWetLocked)
        params[(size_t) dt::pid::mix]->setValueNotifyingHost (params[(size_t) dt::pid::mix]->convertTo0to1 (mix));
    presetFile = file;
    presetName = file.getFileNameWithoutExtension();
    // Thermal's factory names start with their number ("007 Magma Bubbles"): show the name.
    if (xml->hasTagName ("Thermal") && presetName.substring (0, 3).containsOnly ("0123456789") && presetName[3] == ' ')
        presetName = presetName.substring (4);
    return true;
}

bool DisTittenProcessor::savePreset (const juce::File& file)
{
    juce::MemoryBlock mb;
    getStateInformation (mb);
    auto xml = getXmlFromBinary (mb.getData(), (int) mb.getSize());
    if (xml == nullptr) return false;
    file.getParentDirectory().createDirectory();
    if (! xml->writeTo (file)) return false;
    presetFile = file;
    presetName = file.getFileNameWithoutExtension();
    return true;
}

// Thermal keeps LOW at least 30 pixels of its band display below HIGH and writes the result back into its own
// parameters (measured); the engine computes the coupling, this mirrors it into the parameters.
void DisTittenProcessor::timerCallback()
{
    const auto& m = engine.meters();
    for (int s = 0; s < 3; ++s)
    {
        const int stride = dt::pid::s2_lo - dt::pid::s1_lo;
        if (! (rawParams[(size_t) (dt::pid::s1_on + s * (dt::pid::s2_on - dt::pid::s1_on))]->load() >= 0.5f
               && rawParams[(size_t) (dt::pid::s1_split + s * stride)]->load() >= 0.5f))
            continue;
        const int ids[2] = { dt::pid::s1_lo + s * stride, dt::pid::s1_hi + s * stride };
        for (int b = 0; b < 2; ++b)
        {
            const float coupled = m.coupledBand[(size_t) s][(size_t) b].load (std::memory_order_relaxed);
            const float host = rawParams[(size_t) ids[b]]->load();
            if (coupled > 0.f && std::abs (coupled - host) > 1e-3f * host)
                params[(size_t) ids[b]]->setValueNotifyingHost (params[(size_t) ids[b]]->convertTo0to1 (coupled));
        }
    }
}

std::unique_ptr<juce::XmlElement> DisTittenProcessor::modulationXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("Modulation");
    for (int m = 0; m < 2; ++m)
    {
        xml->setAttribute ("macro" + juce::String (m + 1), macroNames[m]);
        xml->setAttribute ("envelope" + juce::String (m + 1), envelopeNames[m]);
    }
    for (int p = 0; p < dt::kNumParams; ++p)
        for (int s = 0; s < dt::mod::kNumSources; ++s)
        {
            const auto& slot = modEdit->targets[(size_t) p].slot[(size_t) s];
            if (! slot.connected && slot.amount == 0.f && slot.depthRange == 0.f && slot.depthSources == 0) continue;
            auto* r = xml->createNewChildElement ("Route");
            r->setAttribute ("param", dt::kParams[p].id);
            r->setAttribute ("source", s);
            r->setAttribute ("connected", slot.connected ? 1 : 0);
            r->setAttribute ("amount", slot.amount);
            r->setAttribute ("bipolar", slot.bipolar ? 1 : 0);
            r->setAttribute ("depthRange", slot.depthRange);
            r->setAttribute ("depthSources", (int) slot.depthSources);
        }
    for (int e = 0; e < 2; ++e)
    {
        auto* env = xml->createNewChildElement ("Envelope");
        env->setAttribute ("index", e);
        const auto& shape = modEdit->envelopes[(size_t) e];
        for (int i = 0; i < shape.numPoints; ++i)
        {
            auto* pt = env->createNewChildElement ("Point");
            pt->setAttribute ("x", shape.points[(size_t) i].x);
            pt->setAttribute ("y", shape.points[(size_t) i].y);
            pt->setAttribute ("cx", shape.points[(size_t) i].cx);
            pt->setAttribute ("cy", shape.points[(size_t) i].cy);
        }
    }
    return xml;
}

void DisTittenProcessor::readModulationXml (const juce::XmlElement& xml)
{
    auto m = std::make_unique<dt::ModState>();
    for (int i = 0; i < 2; ++i)
    {
        macroNames[i] = xml.getStringAttribute ("macro" + juce::String (i + 1), "MACRO " + juce::String (i + 1));
        envelopeNames[i] = xml.getStringAttribute ("envelope" + juce::String (i + 1), "MOD " + juce::String (i + 1));
    }
    for (auto* r : xml.getChildWithTagNameIterator ("Route"))
    {
        int p = -1;
        for (int i = 0; i < dt::kNumParams; ++i)
            if (r->getStringAttribute ("param") == dt::kParams[i].id) { p = i; break; }
        const int s = r->getIntAttribute ("source", -1);
        if (p < 0 || s < 0 || s >= dt::mod::kNumSources) continue;
        auto& slot = m->targets[(size_t) p].slot[(size_t) s];
        slot.connected = r->getIntAttribute ("connected") != 0;
        slot.amount = (float) r->getDoubleAttribute ("amount");
        slot.bipolar = r->getIntAttribute ("bipolar") != 0;
        slot.depthRange = (float) r->getDoubleAttribute ("depthRange");
        slot.depthSources = (uint8_t) r->getIntAttribute ("depthSources");
    }
    for (auto* env : xml.getChildWithTagNameIterator ("Envelope"))
    {
        const int e = env->getIntAttribute ("index", -1);
        if (e < 0 || e > 1) continue;
        auto& shape = m->envelopes[(size_t) e];
        shape.numPoints = 0;
        for (auto* pt : env->getChildWithTagNameIterator ("Point"))
        {
            if (shape.numPoints >= dt::mod::kMaxPoints) break;
            auto& p = shape.points[(size_t) shape.numPoints++];
            p.x = (float) pt->getDoubleAttribute ("x");
            p.y = (float) pt->getDoubleAttribute ("y");
            p.cx = (float) pt->getDoubleAttribute ("cx");
            p.cy = (float) pt->getDoubleAttribute ("cy");
        }
    }
    setModState (*m);
}

juce::AudioProcessorValueTreeState::ParameterLayout DisTittenProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < dt::kNumParams; ++i)
    {
        const auto& p = dt::kParams[i];
        const ParameterID id { p.id, 1 };
        const float defPlain = dt::toPlain (p, p.defaultNorm);

        if (p.format == dt::ParamFormat::onOff)
        {
            layout.add (std::make_unique<AudioParameterBool> (id, p.name, defPlain >= 0.5));
            continue;
        }
        if (p.format == dt::ParamFormat::effectType)
        {
            StringArray names;
            for (auto* n : dt::names::kEffects) names.add (n);
            layout.add (std::make_unique<AudioParameterChoice> (id, p.name, names, roundToInt (defPlain)));
            continue;
        }

        // Thermal's own parameters stay continuous floats with Thermal's exact skew, so normalised automation and
        // imported Thermal states mean the same thing here. Stepped ones (types, modes, stages) are floored by
        // the DSP, as Thermal does.
        NormalisableRange<float> range (p.lo, p.hi, 0.f, p.skew);
        layout.add (std::make_unique<AudioParameterFloat> (
            id, p.name, range, (float) defPlain,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([&p] (float v, int) { return ptext::text (p, v); })
                .withValueFromStringFunction ([&p] (const String& t) { return (float) ptext::parse (p, t); })));
    }
    return layout;
}

void DisTittenProcessor::readParams() noexcept
{
    for (int i = 0; i < dt::kNumParams; ++i)
        block.plain[(size_t) i] = rawParams[(size_t) i]->load (std::memory_order_relaxed);
    block.render = isNonRealtime();   // may change between blocks: read it every block
}

dt::Transport DisTittenProcessor::transport() const noexcept
{
    dt::Transport t;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            t.hasPlayHead = true;
            if (auto bpm = pos->getBpm()) t.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) { t.ppq = *ppq; t.hasPpq = true; }
            if (auto ts = pos->getTimeInSamples()) { t.timeInSamples = *ts; t.hasTime = true; }
            t.playing = pos->getIsPlaying();
        }
    return t;
}

// ---- Audio --------------------------------------------------------------------

bool DisTittenProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const auto ok = [] (const juce::AudioChannelSet& s) { return s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo(); };
    // Mono in / stereo out is fine; stereo into mono is not.
    return ok (in) && ok (out) && in.size() <= out.size();
}

void DisTittenProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate.store (sampleRate, std::memory_order_relaxed);
    readParams();
    engine.setParams (block);
    engine.prepare (sampleRate, std::max (1, samplesPerBlock));
    scratch.setSize (2, std::max (1, samplesPerBlock));
    setLatencySamples (engine.getLatencySamples());
}

void DisTittenProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int ins = getMainBusNumInputChannels(), outs = getMainBusNumOutputChannels();
    if (n == 0 || ins == 0 || outs == 0) return;
    if (scratch.getNumSamples() < n) scratch.setSize (2, n, false, false, true);   // a host that lied about the block size

    float* left = buffer.getWritePointer (0);
    float* right = outs > 1 ? buffer.getWritePointer (1) : scratch.getWritePointer (0);
    if (ins == 1) juce::FloatVectorOperations::copy (right, left, n);

    const auto tap = [&] (dt::Tap& t, std::atomic<float>& peak) {
        float* m = scratch.getWritePointer (1);
        juce::FloatVectorOperations::copy (m, left, n);
        juce::FloatVectorOperations::add (m, right, n);
        juce::FloatVectorOperations::multiply (m, 0.5f, n);
        t.push (m, n);
        const auto r = juce::FloatVectorOperations::findMinAndMax (m, n);
        const float p = std::max (-r.getStart(), r.getEnd());
        if (p > peak.load (std::memory_order_relaxed)) peak.store (p, std::memory_order_relaxed);
    };

    tap (inTap, inPeak);
    readParams();
    engine.setParams (block);
    engine.process (left, right, n, transport());   // once per host block, like Thermal (see engine/Engine.h)
    tap (outTap, outPeak);

    if (outs == 1) // mono out: the sum
    {
        juce::FloatVectorOperations::add (left, right, n);
        juce::FloatVectorOperations::multiply (left, 0.5f, n);
    }
    for (int c = outs > 1 ? 2 : 1; c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, n);
}

// ---- State --------------------------------------------------------------------

void DisTittenProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
    {
        xml->deleteAllChildElementsWithTagName ("Modulation");
        xml->addChildElement (modulationXml().release());
        copyXmlToBinary (*xml, destData);
    }
}

void DisTittenProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    if (xml->hasTagName ("Thermal"))   // a Thermal state pasted in as-is
    {
        loadThermalState (*xml);
        return;
    }
    if (! xml->hasTagName (apvts.state.getType())) return;
    if (auto* m = xml->getChildByName ("Modulation"))
        readModulationXml (*m);
    else
        setModState (dt::ModState());
    xml->deleteAllChildElementsWithTagName ("Modulation");
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* DisTittenProcessor::createEditor() { return new DisTittenEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DisTittenProcessor(); }
