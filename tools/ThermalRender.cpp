// distitten_render: renders audio through DisTitten with an Output Thermal preset or state loaded, for A/B
// comparison against Thermal itself (black-box verification of the recreation).
//
//   distitten_render <thermal.xml | -> in.f32 out.f32 [sr=48000] [block=512] [bpm=120] [pre=0.1]
//
// in/out are raw interleaved stereo float32. "-" keeps DisTitten's defaults (= Thermal's default state).
// Like the Thermal measurement host, the processor is prepared first and the state loaded afterwards, so
// parameter glides start from the defaults the way they do in Thermal; `pre` seconds of silence are rendered
// first and dropped. The playhead plays from sample 0 of the pre-roll at a fixed tempo.

#include "plugin/PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <iostream>

namespace
{
    struct Head : juce::AudioPlayHead
    {
        double bpm = 120.0, sr = 48000.0;
        juce::int64 pos = 0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setBpm (bpm);
            p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            p.setTimeInSamples (pos);
            p.setTimeInSeconds ((double) pos / sr);
            const double ppq = (double) pos / sr * bpm / 60.0;
            p.setPpqPosition (ppq);
            p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
            p.setIsPlaying (true);
            return p;
        }
    };
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 4)
    {
        std::cerr << "usage: distitten_render <thermal.xml|-> in.f32 out.f32 [sr=] [block=] [bpm=] [pre=]\n";
        return 1;
    }
    juce::StringPairArray opt;
    opt.set ("sr", "48000"); opt.set ("block", "512"); opt.set ("bpm", "120"); opt.set ("pre", "0.1");
    for (int i = 4; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        opt.set (a.upToFirstOccurrenceOf ("=", false, false), a.fromFirstOccurrenceOf ("=", false, false));
    }
    const double sr = opt["sr"].getDoubleValue();
    const int block = opt["block"].getIntValue();

    juce::MemoryBlock raw;
    if (! juce::File (argv[2]).loadFileAsData (raw)) { std::cerr << "cannot read " << argv[2] << "\n"; return 1; }
    const int n = (int) (raw.getSize() / (2 * sizeof (float)));
    const auto* in = static_cast<const float*> (raw.getData());

    DisTittenProcessor proc;
    Head head;
    head.bpm = opt["bpm"].getDoubleValue();
    head.sr = sr;
    proc.setPlayHead (&head);
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);
    if (juce::String (argv[1]) != "-" && ! proc.loadThermalPreset (juce::File (argv[1])))
    {
        std::cerr << "not a Thermal state: " << argv[1] << "\n";
        return 1;
    }

    const int pre = (int) (opt["pre"].getDoubleValue() * sr);
    const int total = pre + n;
    std::vector<float> out ((size_t) n * 2, 0.f);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += block)
    {
        const int len = std::min (block, total - pos);
        buf.setSize (2, len, false, false, true);
        for (int i = 0; i < len; ++i)
        {
            const int s = pos + i - pre;
            const bool inside = s >= 0 && s < n;
            buf.setSample (0, i, inside ? in[2 * s] : 0.f);
            buf.setSample (1, i, inside ? in[2 * s + 1] : 0.f);
        }
        head.pos = pos;
        proc.processBlock (buf, midi);
        for (int i = 0; i < len; ++i)
        {
            const int s = pos + i - pre;
            if (s >= 0 && s < n)
            {
                out[(size_t) (2 * s)] = buf.getSample (0, i);
                out[(size_t) (2 * s + 1)] = buf.getSample (1, i);
            }
        }
    }
    juce::File (argv[3]).replaceWithData (out.data(), out.size() * sizeof (float));
    return 0;
}
