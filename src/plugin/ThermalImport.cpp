#include "ThermalImport.h"

#include "BinaryData.h"   // factory.zip: presets/thermal, packed by CMakeLists.txt

#include <cstring>
#include <string>
#include <unordered_map>

namespace thermal
{
namespace
{
    // Unpacks the carried presets once per process. The marker holds a hash (64-bit FNV-1a) of the zip it came
    // from, so an update that changes the presets replaces them, and an unchanged folder is left alone.
    juce::File factoryFolder()
    {
        static const juce::File folder = [] {
            auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
           #if JUCE_MAC
            base = base.getChildFile ("Application Support");
           #endif
            const auto dir = base.getChildFile ("DisTitten").getChildFile ("Factory");
            const auto marker = dir.getChildFile (".unpacked");
            juce::uint64 hash = 14695981039346656037ull;
            for (int i = 0; i < BinaryData::factory_zipSize; ++i)
                hash = (hash ^ (juce::uint8) BinaryData::factory_zip[i]) * 1099511628211ull;
            const auto stamp = juce::String::toHexString ((juce::int64) hash);
            if (! marker.existsAsFile() || marker.loadFileAsString() != stamp)
            {
                dir.deleteRecursively();
                dir.createDirectory();
                juce::MemoryInputStream in (BinaryData::factory_zip, (size_t) BinaryData::factory_zipSize, false);
                juce::ZipFile zip (in);
                if (zip.uncompressTo (dir).wasOk()) marker.replaceWithText (stamp);
            }
            return dir;
        }();
        return folder;
    }

    const juce::XmlElement* child (const juce::XmlElement* e, const char* name)
    {
        return e != nullptr ? e->getChildByName (name) : nullptr;
    }

    // "<node>.<attribute>" from Node_Properties or Processor_Group_States; "!" before the attribute inverts.
    // Returns the plain value, or the parameter's default when Thermal didn't store it (Thermal omits
    // attributes that are at their default, e.g. Bypassed on the envelopes).
    double switchValue (const juce::XmlElement& app, const dt::ParamInfo& p)
    {
        const juce::String tag (p.thermalTag);
        const auto node = tag.upToFirstOccurrenceOf (".", false, false);
        auto attr = tag.fromFirstOccurrenceOf (".", false, false);
        const bool invert = attr.startsWithChar ('!');
        if (invert) attr = attr.substring (1);

        const juce::XmlElement* e = nullptr;
        if (auto* props = app.getChildByName ("Node_Properties")) e = props->getChildByName (node);
        if (e == nullptr)
            if (auto* groups = app.getChildByName ("Processor_Group_States")) e = groups->getChildByName (node);

        const double def = dt::toPlain (p, p.defaultNorm);
        if (e == nullptr || ! e->hasAttribute (attr)) return def;
        const double v = e->getDoubleAttribute (attr);
        return invert ? (v >= 0.5 ? 0.0 : 1.0) : v;
    }

    bool bypassed (const juce::XmlElement& app, const juce::String& node)
    {
        if (auto* props = app.getChildByName ("Node_Properties"))
            if (auto* e = props->getChildByName (node))
                return e->getIntAttribute ("Bypassed", 0) != 0;
        return false;
    }

    int paramForTag (const juce::String& tag)
    {
        static const auto index = [] {
            std::unordered_map<std::string, int> m;
            for (int i = 0; i < dt::kNumThermalParams; ++i) m[dt::kParams[i].thermalTag] = i;
            return m;
        }();
        const auto it = index.find (tag.toStdString());
        return it == index.end() ? -1 : it->second;
    }

    int sourceIndex (const juce::String& node)
    {
        if (node == "MKUClickableEnvelope_Envelope1") return 0;
        if (node == "MKUClickableEnvelope_Envelope2") return 1;
        if (node == "MKUMacroNode_Macro1") return 2;
        if (node == "MKUMacroNode_Macro2") return 3;
        return -1;
    }

    // "<Param><suffix><digit>" -> Param and the digit, or false.
    bool splitIndexed (const juce::String& name, const char* suffix, juce::String& param, int& index)
    {
        if (name.length() < 2 || ! juce::CharacterFunctions::isDigit (name.getLastCharacter())) return false;
        const auto stem = name.dropLastCharacters (1);
        if (! stem.endsWith (suffix)) return false;
        param = stem.dropLastCharacters ((int) std::strlen (suffix));
        index = name.getLastCharacter() - '0';
        return param.isNotEmpty() && index >= 0 && index < dt::mod::kNumSources;
    }

    // Thermal's modulation (measured, src/core/mod/Modulation.h): a slot i of a parameter is live when the graph
    // connects source i to <Param>ModIn<i>; its amount, polarity and depth range are node properties; depth
    // sources connect to <Param>ModDepthIn<i>.
    void readModulation (const juce::XmlElement& app, dt::ModState& ms)
    {
        ms = dt::ModState();
        juce::String param;
        int slot = 0;

        if (auto* connections = app.getChildByName ("Connections"))
            for (auto* c : connections->getChildIterator())
            {
                const int src = sourceIndex (c->getStringAttribute ("SourceNode"));
                if (src < 0) continue;
                const auto node = c->getStringAttribute ("DestinationNode");
                const auto channel = c->getStringAttribute ("DestinationChannel");
                if (splitIndexed (channel, "ModDepthIn", param, slot))
                {
                    if (const int p = paramForTag (node + "_" + param); p >= 0)
                        ms.targets[(size_t) p].slot[(size_t) slot].depthSources |= (uint8_t) (1u << src);
                }
                else if (splitIndexed (channel, "ModIn", param, slot))
                {
                    if (const int p = paramForTag (node + "_" + param); p >= 0)
                        ms.targets[(size_t) p].slot[(size_t) slot].connected = true;
                }
            }

        if (auto* props = app.getChildByName ("Node_Properties"))
            for (auto* node : props->getChildIterator())
            {
                for (int a = 0; a < node->getNumAttributes(); ++a)
                {
                    const auto name = node->getAttributeName (a);
                    const double v = node->getAttributeValue (a).getDoubleValue();
                    const auto target = [&] (const char* suffix) -> dt::ModSlotParams* {
                        if (! splitIndexed (name, suffix, param, slot)) return nullptr;
                        const int p = paramForTag (node->getTagName() + "_" + param);
                        return p >= 0 ? &ms.targets[(size_t) p].slot[(size_t) slot] : nullptr;
                    };
                    if (auto* s = target ("ModAmount")) s->amount = (float) v;
                    else if (auto* s2 = target ("ModBiPolar")) s2->bipolar = v >= 0.5;
                    else if (auto* s3 = target ("DepthModRange")) s3->depthRange = (float) v;
                }

                const int env = node->getTagName() == "MKUClickableEnvelope_Envelope1" ? 0
                              : node->getTagName() == "MKUClickableEnvelope_Envelope2" ? 1 : -1;
                if (env < 0) continue;
                auto& shape = ms.envelopes[(size_t) env];
                shape.numPoints = juce::jlimit (0, dt::mod::kMaxPoints, node->getIntAttribute ("NumPoints", 2));
                for (int i = 0; i < shape.numPoints; ++i)
                {
                    const auto n = juce::String (i);
                    auto& pt = shape.points[(size_t) i];
                    // Thermal clamps the points on load, not the control points (measured).
                    pt.x = (float) juce::jlimit (0.0, 1.0, node->getDoubleAttribute ("PointX" + n));
                    pt.y = (float) juce::jlimit (0.0, 1.0, node->getDoubleAttribute ("PointY" + n));
                    pt.cx = (float) node->getDoubleAttribute ("ControlPointX" + n);
                    pt.cy = (float) node->getDoubleAttribute ("ControlPointY" + n);
                }
            }
    }
} // namespace

bool read (const juce::XmlElement& root, Patch& out)
{
    if (! root.hasTagName ("Thermal")) return false;
    const auto* app = root.getChildByName ("App_State");
    const auto* parameters = child (app, "Parameters");
    if (parameters == nullptr) return false;

    for (int i = 0; i < dt::kNumParams; ++i)
        out.norm[(size_t) i] = dt::kParams[i].defaultNorm;

    // Thermal's host parameters: the normalised value it stores is exactly DisTitten's normalised value.
    for (int i = 0; i < dt::kNumThermalParams; ++i)
        if (auto* e = parameters->getChildByName (dt::kParams[i].thermalTag))
            out.norm[(size_t) i] = (float) juce::jlimit (0.0, 1.0, e->getDoubleAttribute ("unmapped_value", dt::kParams[i].defaultNorm));

    // The switches.
    for (int i = dt::kNumThermalParams; i < dt::kNumParams; ++i)
        out.norm[(size_t) i] = (float) dt::toNorm (dt::kParams[i], switchValue (*app, dt::kParams[i]));

    // A stage is skipped when its band splitter is bypassed (that is what removes it from Thermal's graph; the
    // output bus's StageN_Active only mirrors it for the display).
    for (int s = 0; s < 3; ++s)
    {
        const auto stage = "Stage_" + juce::String (s + 1);
        if (bypassed (*app, "MKUThreeBandSplitterNode_" + stage))
            out.norm[(size_t) (dt::pid::s1_on + s * (dt::pid::s2_on - dt::pid::s1_on))] = 0.f;
        // Thermal can bypass a stage's gain node, which DisTitten has no switch for: unity gain is the same thing.
        if (bypassed (*app, "MKUGainBus_" + stage))
            out.norm[(size_t) (dt::pid::s1_gain + s * (dt::pid::s2_gain - dt::pid::s1_gain))]
                = (float) dt::toNorm (dt::kParams[dt::pid::s1_gain], 0.0);
    }

    for (int m = 0; m < 2; ++m)
    {
        out.macroNames[m] = "MACRO " + juce::String (m + 1);
        if (auto* props = child (app, "Node_Properties"))
            if (auto* e = props->getChildByName ("MKUMacroNode_Macro" + juce::String (m + 1)))
                out.macroNames[m] = e->getStringAttribute ("MacroName", out.macroNames[m]);
    }

    readModulation (*app, *out.mod);
    for (int e = 0; e < 2; ++e)
    {
        out.envelopeNames[e] = "MOD " + juce::String (e + 1);
        if (auto* props = child (app, "Node_Properties"))
            if (auto* n = props->getChildByName ("MKUClickableEnvelope_Envelope" + juce::String (e + 1)))
                out.envelopeNames[e] = n->getStringAttribute ("Name", out.envelopeNames[e]);
    }
    return true;
}

bool read (const juce::File& file, Patch& out)
{
    if (auto xml = juce::parseXML (file))
        return read (*xml, out);
    return false;
}

juce::File presetFolder() { return factoryFolder().getChildFile ("Presets"); }
juce::File modulationPresetFolder() { return factoryFolder().getChildFile ("SubPresets").getChildFile ("Modulation Presets"); }

} // namespace thermal
