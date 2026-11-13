"""Generate src/core/model/Params.{h,cpp} for DisTherm from the measured Thermal parameter table.

    python3 tools/gen_params.py            # writes src/core/model/Params.{h,cpp}

tools/thermal_params.tsv holds, for each of Thermal's 410 host parameters: host index, host name, XML tag,
default (normalised, exactly as Thermal stores it), lo, hi and the taper power p (plain = lo + (hi - lo) * u^p,
JUCE skew = 1/p). It was measured from the
plugin itself: each parameter set to 41 values through the host, the mapped value read back from its state.

Every Thermal parameter gets a DisTherm id, a UI/host name, Thermal's exact range and skew, its default and
a display format. Thermal's XML tag is kept so Thermal presets/states can be imported.
"""
import re, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'src', 'core', 'model')

rows = []
for l in open(os.path.join(HERE, 'thermal_params.tsv')).read().splitlines()[1:]:
    f = l.split('\t')
    rows.append(dict(host=int(f[0]), hname=f[1], tag=f[2], d=float(f[3]), lo=float(f[4]), hi=float(f[5]), skew=round(1.0 / float(f[6]), 6)))
bytag = {r['tag']: r for r in rows}

WHERE = {'Stage_1': ('s1', 'Stage 1'), 'Stage_2': ('s2', 'Stage 2'), 'Stage_3': ('s3', 'Stage 3'),
         'Slot1': ('fx1', 'FX 1'), 'Slot2': ('fx2', 'FX 2')}
FX = [  # (Thermal node, short id, UI name, [(Thermal param, short id, UI name, fmt)])
    ('BitReducer', 'bit', 'Bit Reducer', [('InputGain', 'in', 'Input', 'db'), ('OutputGain', 'out', 'Output', 'db'),
        ('Bits', 'bits', 'Bits', 'pctrange'), ('SampleReduction', 'redux', 'Redux', 'int'), ('WetDry', 'mix', 'Dry/Wet', 'pct')]),
    ('Chorus', 'cho', 'Chorus', [('InputGain', 'in', 'Input', 'db'), ('OutputGain', 'out', 'Output', 'db'),
        ('Rate', 'rate', 'Rate', 'hz2'), ('Depth', 'depth', 'Depth', 'pct'), ('Feedback', 'fb', 'Feedback', 'pctmapped'),
        ('Width', 'width', 'Width', 'pct'), ('WetDry', 'mix', 'Dry/Wet', 'pct')]),
    ('Compressor', 'cmp', 'Compressor', [('InputGain', 'in', 'Input', 'db'), ('OutputGain', 'out', 'Output', 'db'),
        ('MakeupGain', 'gain', 'Gain', 'db'), ('Threshold', 'thresh', 'Threshold', 'db'), ('Ratio', 'ratio', 'Ratio', 'ratio'),
        ('Attack', 'attack', 'Attack', 'time'), ('Release', 'release', 'Release', 'time'), ('WetDry', 'mix', 'Dry/Wet', 'pct')]),
    ('StereoDelay', 'dly', 'Delay', [('InputGain', 'in', 'Input', 'db'), ('OutputGain', 'out', 'Output', 'db'),
        ('Type', 'type', 'Type', 'delaytype'), ('LowPass', 'lp', 'LP', 'hz'), ('HighPass', 'hp', 'HP', 'hz'),
        ('Feedback', 'fb', 'Feedback', 'pct'), ('Time', 'time', 'Time', 'syncnote'), ('DryWet', 'mix', 'Dry/Wet', 'pct')]),
    ('Filter', 'flt', 'Filter', [('InputGain', 'in', 'Input', 'db'), ('OutputGain', 'out', 'Output', 'db'),
        ('Cutoff', 'cutoff', 'Cutoff', 'hz'), ('Resonance', 'res', 'Resonance', 'pct'), ('Drive', 'drive', 'Drive', 'pct'),
        ('Mode', 'mode', 'Mode', 'filtermode'), ('FollowAmount', 'follow', 'Env Follow', 'pctmapped'), ('WetDry', 'mix', 'Dry/Wet', 'pct')]),
    ('Flanger', 'fla', 'Flanger', [('InputGain', 'in', 'Input', 'db'), ('OutputGain', 'out', 'Output', 'db'),
        ('DryWet', 'mix', 'Dry/Wet', 'pct'), ('Time', 'time', 'Time', 'pct'), ('Feedback', 'fb', 'Feedback', 'pct'),
        ('Type', 'type', 'Type', 'flangertype'), ('ModFreq', 'rate', 'Freq Mod', 'pct'), ('ModRange', 'range', 'Mod Range', 'pct')]),
    ('FrequencyShifter', 'fsh', 'Freq Shifter', [('InputGain', 'in', 'Input', 'db'), ('OutputGain', 'out', 'Output', 'db'),
        ('DryWet', 'mix', 'Dry/Wet', 'pct'), ('Frequency', 'freq', 'Frequency', 'pct'), ('FrequencyOffset', 'offset', 'Offset', 'pctmapped')]),
    ('Phaser', 'pha', 'Phaser', [('InputGain', 'in', 'Input', 'db'), ('OutputGain', 'out', 'Output', 'db'),
        ('PhaserType', 'type', 'Type', 'phasertype'), ('CenterFrequency', 'freq', 'Freq', 'hz'), ('Feedback', 'fb', 'Feedback', 'pct'),
        ('Stages', 'stages', 'Stages', 'int'), ('Rate', 'rate', 'Mod Rate', 'hz2'), ('ModulationRange', 'range', 'Freq Mod', 'pct'),
        ('WetDry', 'mix', 'Dry/Wet', 'pct')]),
    ('Reverb', 'rev', 'Reverb', [('InputGain', 'in', 'Input', 'db'), ('OutputGain', 'out', 'Output', 'db'),
        ('Size', 'size', 'Size', 'pct'), ('Damping', 'damp', 'Damping', 'pct'), ('DryWet', 'mix', 'Dry/Wet', 'pct'),
        ('Width', 'width', 'Width', 'pct'), ('Freeze', 'freeze', 'Freeze', 'toggle')]),
]
STAGE = [  # (Thermal node, Thermal param, short id, UI name, fmt)
    ('ThreeBandSplitterNode', 'Band1Hz', 'lo', 'Low', 'hz'),
    ('ThreeBandSplitterNode', 'Band2Hz', 'hi', 'High', 'hz'),
    ('ThreeBandSplitterNode', 'MultiBand', 'split', 'Band Split', 'toggle'),
    ('ThreeBandSummerNode', 'SoloBand', 'solo', 'Solo Band', 'toggle'),
    ('ThreeBandSummerNode', 'Refilter', 'refilter', 'Refilter', 'toggle'),
    ('DustDistoNode', 'DistortionType', 'type', 'Distortion Shape', 'distotype'),
    ('DustDistoNode', 'InputGain', 'drive', 'Drive', 'db'),
    ('DustDistoNode', 'OutputGain', 'out', 'Output', 'db'),
    ('DustDistoNode', 'InputGainComp', 'auto', 'Auto', 'toggle'),
    ('DustDistoNode', 'Control1', 'shape', 'Shape', 'pct'),
    ('DustDistoNode', 'Control2', 'depth', 'Depth', 'pct'),
    ('DustDistoNode', 'Control3', 'freq', 'Frequency', 'pct'),
    ('DustDistoNode', 'Control4', 'clip', 'Clip', 'pct'),
    ('DustDistoNode', 'Feedback', 'fb', 'Feedback', 'pct'),
    ('DustDistoNode', 'FeedbackEnvFollow', 'follow', 'Follow', 'toggle'),
    ('DustDistoNode', 'FeedbackEnvFollowRelease', 'release', 'Feedback Release', 'pctmapped'),
    ('DustDistoNode', 'Time', 'time', 'Time', 'time'),
    ('MidSideGainNode', 'SideGain', 'sides', 'Sides', 'db'),
    ('MidSideGainNode', 'TimeSpreadAmount', 'width', 'Width', 'pct'),
    ('GainBus', 'Gain', 'gain', 'Gain', 'db'),
    ('ToneControl', 'LowShelf', 'tone_lo', 'Tone Low', 'db'),
    ('ToneControl', 'HighShelf', 'tone_hi', 'Tone High', 'db'),
]
GLOBAL = [
    ('MKUInputBus_MainInput_InputGain', 'in_gain', 'Input', 'db'),
    ('MKUOutputBus_MainOutput_OutputGain', 'out_gain', 'Output', 'db'),
    ('MKUOutputBus_MainOutput_DryWet', 'mix', 'Dry/Wet', 'pct'),
    ('MKUOutputBus_MainOutput_HighpassCutoff', 'hp', 'HP Cutoff', 'hz'),
    ('MKUOutputBus_MainOutput_LowpassCutoff', 'lp', 'LP Cutoff', 'hz'),
    ('MKUMacroNode_Macro1_Macro', 'macro1', 'Macro 1', 'pct'),
    ('MKUMacroNode_Macro2_Macro', 'macro2', 'Macro 2', 'pct'),
    ('MKUClickableEnvelope_Envelope1_Rate', 'mod1_rate', 'Mod 1 Rate', 'syncmod'),
    ('MKUClickableEnvelope_Envelope1_Humanize', 'mod1_humanize', 'Mod 1 Humanize', 'pct'),
    ('MKUClickableEnvelope_Envelope2_Rate', 'mod2_rate', 'Mod 2 Rate', 'syncmod'),
    ('MKUClickableEnvelope_Envelope2_Humanize', 'mod2_humanize', 'Mod 2 Humanize', 'pct'),
]
MASTER = [('InputGain', 'in', 'Input', 'db'), ('OutputGain', 'out', 'Output', 'db'), ('MakeupGain', 'gain', 'Gain', 'db'),
          ('Threshold', 'thresh', 'Threshold', 'db'), ('Ratio', 'ratio', 'Ratio', 'ratio'), ('Attack', 'attack', 'Attack', 'time'),
          ('Release', 'release', 'Release', 'time'), ('WetDry', 'mix', 'Dry/Wet', 'pct')]

params = []  # (id, name, tag, fmt)
for tag, pid, name, fmt in GLOBAL:
    params.append((pid, name, tag, fmt))
for t, pid, name, fmt in MASTER:
    params.append((f'mc_{pid}', f'Master Comp {name}', f'MKUCompressor_MasterCompressor_{t}', fmt))
for s in (1, 2, 3):
    w, wn = WHERE[f'Stage_{s}']
    for node, t, pid, name, fmt in STAGE:
        params.append((f'{w}_{pid}', f'{wn} {name}', f'MKU{node}_Stage_{s}_{t}', fmt))
for where in ('Stage_1', 'Stage_2', 'Stage_3', 'Slot1', 'Slot2'):
    w, wn = WHERE[where]
    for node, fid, fname, plist in FX:
        for t, pid, name, fmt in plist:
            params.append((f'{w}_{fid}_{pid}', f'{wn} {fname} {name}', f'MKU{node}_{where}_{t}', fmt))

assert len(params) == len(rows) == 410, (len(params), len(rows))
assert set(p[2] for p in params) == set(bytag), set(bytag) ^ set(p[2] for p in params)
assert len(set(p[0] for p in params)) == 410

FMTS = ['db', 'hz', 'hz2', 'pct', 'pctmapped', 'pctrange', 'int', 'ratio', 'time', 'toggle', 'distotype', 'filtermode',
        'delaytype', 'flangertype', 'phasertype', 'syncnote', 'syncmod']
enum_name = {'db': 'decibels', 'hz': 'hertz', 'hz2': 'rateHz', 'pct': 'percentNorm', 'pctmapped': 'percentValue',
             'pctrange': 'percentOfRange', 'int': 'integer', 'ratio': 'ratio', 'time': 'time', 'toggle': 'toggle',
             'distotype': 'distortionType', 'filtermode': 'filterMode', 'delaytype': 'delayType', 'flangertype': 'flangerType',
             'phasertype': 'phaserType', 'syncnote': 'syncNote', 'syncmod': 'syncMod', 'onoff': 'onOff',
             'effecttype': 'effectType'}
FMTS += ['onoff', 'effecttype']

# Thermal keeps these in node properties / processor-group state, not in host parameters. DisTherm makes them
# parameters too (undoable, automatable). (id, name, Thermal source "<node>.<attribute>" with "!" = inverted,
# kind, default plain value)
EXTRAS = []
for s_ in (1, 2, 3):
    EXTRAS += [
        (f's{s_}_on', f'Stage {s_} On', f'MKUOutputBus_MainOutput.Stage{s_}_Active', 'onoff', 1.0),
        (f's{s_}_disto_on', f'Stage {s_} Distortion On', f'MKUDustDistoNode_Stage_{s_}.!Bypassed', 'onoff', 1.0),
        (f's{s_}_fx_type', f'Stage {s_} Effect', f'Stage_{s_}_Effects.ActiveSlot', 'effecttype', 0.0),
        (f's{s_}_fx_on', f'Stage {s_} Effect On', f'Stage_{s_}_Effects.!Bypassed', 'onoff', 0.0),
        (f's{s_}_width_on', f'Stage {s_} Width On', f'MKUMidSideGainNode_Stage_{s_}.!Bypassed', 'onoff', 0.0),
        (f's{s_}_tone_on', f'Stage {s_} Tone On', f'MKUToneControl_Stage_{s_}.!Bypassed', 'onoff', 0.0),
        (f's{s_}_time_sync', f'Stage {s_} Feedback Sync', f'MKUDustDistoNode_Stage_{s_}.TimeCurrentMapping', 'onoff', 0.0),
    ]
for w, where in (('s1', 'Stage_1'), ('s2', 'Stage_2'), ('s3', 'Stage_3'), ('fx1', 'Slot1'), ('fx2', 'Slot2')):
    EXTRAS.append((f'{w}_dly_sync', f'{WHERE[where][1]} Delay Sync', f'MKUStereoDelay_{where}.TimeCurrentMapping', 'onoff', 1.0))
EXTRAS += [
    ('fx1_type', 'FX 1 Effect', 'Slot1.ActiveSlot', 'effecttype', 0.0),
    ('fx1_on', 'FX 1 On', 'Slot1.!Bypassed', 'onoff', 0.0),
    ('fx2_type', 'FX 2 Effect', 'Slot2.ActiveSlot', 'effecttype', 5.0),
    ('fx2_on', 'FX 2 On', 'Slot2.!Bypassed', 'onoff', 0.0),
    ('mc_on', 'Master Comp On', 'MKUCompressor_MasterCompressor.!Bypassed', 'onoff', 1.0),
    ('filters_on', 'Master Filters On', 'MKUOutputBus_MainOutput.!FiltersBypassed', 'onoff', 1.0),
    ('mod1_on', 'Mod 1 On', 'MKUClickableEnvelope_Envelope1.!Bypassed', 'onoff', 1.0),
    ('mod2_on', 'Mod 2 On', 'MKUClickableEnvelope_Envelope2.!Bypassed', 'onoff', 1.0),
    ('mod1_sync', 'Mod 1 Sync', 'MKUClickableEnvelope_Envelope1.RateCurrentMapping', 'onoff', 1.0),
    ('mod2_sync', 'Mod 2 Sync', 'MKUClickableEnvelope_Envelope2.RateCurrentMapping', 'onoff', 1.0),
]


def cpp_float(v):
    s = repr(float(v))
    if 'e' not in s and '.' not in s: s += '.0'
    return s + 'f'


hdr = []
hdr.append('''#pragma once

// GENERATED by tools/gen_params.py from measurements of Output Thermal 1.2.1 (black-box: every parameter's
// mapped value read back at 41 points, display texts at 201 points). Do not edit by hand.
//
// Every parameter maps its normalised value u (0..1) like juce::NormalisableRange<float> with a skew factor:
//   plain = lo + (hi - lo) * exp (log (u) / skew)      (= u^(1/skew)), computed in float
// which is exactly how Thermal maps it (skews 1, 0.5, 0.35, 0.3, 0.26, 0.25, 0.2), so normalised automation and
// Thermal presets carry over 1:1, down to the float rounding that some of its DSP depends on.
// Ids are DisTherm's save format: append only, never rename or reorder.

#include <cmath>

// Exact comparisons (skew == 1, an empty range) are the point here; keeps JUCE's -Wfloat-equal quiet.
#if defined (__clang__) || defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

namespace dt
{

enum class ParamFormat
{
''')
for f in FMTS:
    hdr.append(f'    {enum_name[f]},\n')
hdr.append('''};

struct ParamInfo
{
    const char* id;            // DisTherm parameter id (save format)
    const char* name;          // host / UI name
    const char* thermalTag;    // Thermal's XML parameter tag (preset import)
    float lo, hi, skew;        // juce::NormalisableRange<float> (lo, hi, 0, skew)
    float defaultNorm;         // Thermal's default, normalised (exactly as Thermal stores it)
    ParamFormat format;
};

namespace pid
{
    enum Id : int
    {
''')
for i, (pid, name, tag, fmt) in enumerate(params):
    hdr.append(f'        {pid}, // {i}\n')
hdr.append(f'        kThermalCount, // {len(params)}: Thermal\'s own host parameters end here\n')
for i, e in enumerate(EXTRAS):
    hdr.append(f'        {e[0]}{" = kThermalCount" if i == 0 else ""}, // {len(params) + i}\n')
hdr.append(f'''        kCount
    }};
}}

inline constexpr int kNumThermalParams = pid::kThermalCount;
inline constexpr int kNumParams = pid::kCount;

// kParams[0 .. kNumThermalParams) are Thermal's host parameters; the rest are the switches Thermal keeps in
// node properties (thermalTag then reads "<node>.<attribute>", with "!" when DisTherm's value is inverted).
extern const ParamInfo kParams[kNumParams];

// juce::NormalisableRange<float>::convertFrom0to1 / convertTo0to1, in float like Thermal and the host parameters,
// and without fused multiply-adds: Thermal's build doesn't fuse (its default 0 dB gain is exactly 0 only unfused).
inline float toPlain (const ParamInfo& p, double norm) noexcept
{{
   #if defined (__clang__)
    #pragma clang fp contract (off)
   #endif
    float u = (float) (norm < 0.0 ? 0.0 : (norm > 1.0 ? 1.0 : norm));
    if (p.skew != 1.0f && u > 0.0f) u = std::exp (std::log (u) / p.skew);
    return p.lo + (p.hi - p.lo) * u;
}}

inline float toNorm (const ParamInfo& p, double plain) noexcept
{{
   #if defined (__clang__)
    #pragma clang fp contract (off)
   #endif
    if (p.hi == p.lo) return 0.0f;
    float t = ((float) plain - p.lo) / (p.hi - p.lo);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return p.skew == 1.0f ? t : std::pow (t, p.skew);
}}

}} // namespace dt

#if defined (__clang__) || defined (__GNUC__)
 #pragma GCC diagnostic pop
#endif
''')

src = ['''// GENERATED by tools/gen_params.py from measurements of Output Thermal 1.2.1. Do not edit by hand.
#include "Params.h"

namespace dt
{

const ParamInfo kParams[kNumParams] = {
''']
for pid, name, tag, fmt in params:
    r = bytag[tag]
    src.append(f'    {{ "{pid}", "{name}", "{tag}", {cpp_float(r["lo"])}, {cpp_float(r["hi"])}, {cpp_float(r["skew"])}, {cpp_float(r["d"])}, ParamFormat::{enum_name[fmt]} }},\n')
for pid, name, tag, kind, d in EXTRAS:
    hi = 8.0 if kind == 'effecttype' else 1.0
    src.append(f'    {{ "{pid}", "{name}", "{tag}", 0.0f, {cpp_float(hi)}, 1.0f, {cpp_float(d / hi)}, ParamFormat::{enum_name[kind]} }},\n')
src.append('};\n\n} // namespace dt\n')

os.makedirs(OUT, exist_ok=True)
open(os.path.join(OUT, 'Params.h'), 'w').write(''.join(hdr))
open(os.path.join(OUT, 'Params.cpp'), 'w').write(''.join(src))
print('wrote', len(params), '+', len(EXTRAS), 'params to', OUT)
