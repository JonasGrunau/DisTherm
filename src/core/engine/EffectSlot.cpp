#include "EffectSlot.h"

#include "engine/Juce32.h"
#include "model/Names.h"

#include <algorithm>

namespace dt
{

void EffectSlot::prepare (double sampleRate, int maxBlockSize)
{
    bit.prepare (sampleRate, maxBlockSize);
    chorus.prepare (sampleRate, maxBlockSize);
    comp.prepare (sampleRate, maxBlockSize);
    delay.prepare (sampleRate, maxBlockSize);
    filter.prepare (sampleRate, maxBlockSize);
    flanger.prepare (sampleRate, maxBlockSize);
    shifter.prepare (sampleRate, maxBlockSize);
    phaser.prepare (sampleRate, maxBlockSize);
    reverb.prepare (sampleRate, maxBlockSize);
    fade.prepare (sampleRate, kSwitchSeconds);
    for (auto* b : { &aL, &aR, &bL, &bR }) b->assign ((size_t) std::max (1, maxBlockSize), 0.f);
    current = previous = target;
    fade.snap (true);
}

void EffectSlot::reset()
{
    bit.reset();
    chorus.reset();
    comp.reset();
    delay.reset();
    filter.reset();
    flanger.reset();
    shifter.reset();
    phaser.reset();
    reverb.reset();
    // Thermal prepares a fresh instance with default parameters and loads the state afterwards; the modulation
    // effects' smoothers and LFOs start from there (measured; the flanger's rate even starts at 5 Hz).
    chorus.startFrom (ChorusParams {});
    flanger.startFrom (FlangerParams {});
    phaser.startFrom (PhaserParams {});
    current = previous = target;
    fade.snap (true);
}

void EffectSlot::setParams (const float* p, int type, bool on, double bpm) noexcept
{
    target = on ? std::clamp (type, 0, names::kNumEffects - 1) : -1;

    BitReducerParams b;
    b.inputGainDb = v (p, pid::s1_bit_in);
    b.outputGainDb = v (p, pid::s1_bit_out);
    b.bits = v (p, pid::s1_bit_bits);
    b.sampleReduction = v (p, pid::s1_bit_redux);
    b.wetDry = v (p, pid::s1_bit_mix);
    bit.setParams (b);

    ChorusParams c;
    c.inputGainDb = v (p, pid::s1_cho_in);
    c.outputGainDb = v (p, pid::s1_cho_out);
    c.rateHz = v (p, pid::s1_cho_rate);
    c.depth = v (p, pid::s1_cho_depth);
    c.feedback = v (p, pid::s1_cho_fb);
    c.width = v (p, pid::s1_cho_width);
    c.wetDry = v (p, pid::s1_cho_mix);
    chorus.setParams (c);

    CompressorParams k;
    k.inputGainDb = v (p, pid::s1_cmp_in);
    k.outputGainDb = v (p, pid::s1_cmp_out);
    k.thresholdDb = v (p, pid::s1_cmp_thresh);
    k.ratio = v (p, pid::s1_cmp_ratio);
    k.attackSec = v (p, pid::s1_cmp_attack);
    k.releaseSec = v (p, pid::s1_cmp_release);
    k.makeupDb = v (p, pid::s1_cmp_gain);
    k.wetDry = v (p, pid::s1_cmp_mix);
    comp.setParams (k);

    StereoDelayParams d;
    d.inputGainDb = v (p, pid::s1_dly_in);
    d.outputGainDb = v (p, pid::s1_dly_out);
    d.type = v (p, pid::s1_dly_type);
    d.tempoSync = p[syncPid] >= 0.5f;
    // Synced: the Time knob's plain value is the division index (0..18). Free: Thermal maps the same knob
    // position to 0.001..2.5 s with skew 0.3 (measured).
    d.time = d.tempoSync ? v (p, pid::s1_dly_time) : juce32::from0to1 (0.001f, 2.5f, 0.3f, v (p, pid::s1_dly_time) / 18.f);
    d.bpm = bpm;
    d.feedback = v (p, pid::s1_dly_fb);
    d.lowPassHz = v (p, pid::s1_dly_lp);
    d.highPassHz = v (p, pid::s1_dly_hp);
    d.dryWet = v (p, pid::s1_dly_mix);
    delay.setParams (d);

    FilterParams f;
    f.inputGainDb = v (p, pid::s1_flt_in);
    f.outputGainDb = v (p, pid::s1_flt_out);
    f.cutoffHz = v (p, pid::s1_flt_cutoff);
    f.resonance = v (p, pid::s1_flt_res);
    f.drive = v (p, pid::s1_flt_drive);
    f.mode = v (p, pid::s1_flt_mode);
    f.followAmount = v (p, pid::s1_flt_follow);
    f.wetDry = v (p, pid::s1_flt_mix);
    filter.setParams (f);

    FlangerParams l;
    l.inputGainDb = v (p, pid::s1_fla_in);
    l.outputGainDb = v (p, pid::s1_fla_out);
    l.time = v (p, pid::s1_fla_time);
    l.feedback = v (p, pid::s1_fla_fb);
    l.type = v (p, pid::s1_fla_type);
    l.modFreq = v (p, pid::s1_fla_rate);
    l.modRange = v (p, pid::s1_fla_range);
    l.dryWet = v (p, pid::s1_fla_mix);
    flanger.setParams (l);

    FrequencyShifterParams s;
    s.inputGainDb = v (p, pid::s1_fsh_in);
    s.outputGainDb = v (p, pid::s1_fsh_out);
    s.frequency = v (p, pid::s1_fsh_freq);
    s.offset = v (p, pid::s1_fsh_offset);
    s.dryWet = v (p, pid::s1_fsh_mix);
    shifter.setParams (s);

    PhaserParams h;
    h.inputGainDb = v (p, pid::s1_pha_in);
    h.outputGainDb = v (p, pid::s1_pha_out);
    h.type = v (p, pid::s1_pha_type);
    h.centreHz = v (p, pid::s1_pha_freq);
    h.feedback = v (p, pid::s1_pha_fb);
    h.stages = v (p, pid::s1_pha_stages);
    h.rateHz = v (p, pid::s1_pha_rate);
    h.modRange = v (p, pid::s1_pha_range);
    h.wetDry = v (p, pid::s1_pha_mix);
    phaser.setParams (h);

    ReverbParams r;
    r.inputGainDb = v (p, pid::s1_rev_in);
    r.outputGainDb = v (p, pid::s1_rev_out);
    r.size = v (p, pid::s1_rev_size);
    r.damping = v (p, pid::s1_rev_damp);
    r.width = v (p, pid::s1_rev_width);
    r.freeze = v (p, pid::s1_rev_freeze);
    r.dryWet = v (p, pid::s1_rev_mix);
    reverb.setParams (r);
}

void EffectSlot::run (int which, float* l, float* r, int n) noexcept
{
    switch (which)
    {
        case names::bitReducer:  bit.process (l, r, n); break;
        case names::chorus:      chorus.process (l, r, n); break;
        case names::compressor:  comp.process (l, r, n); break;
        case names::delay:       delay.process (l, r, n); break;
        case names::filter:      filter.process (l, r, n); break;
        case names::flanger:     flanger.process (l, r, n); break;
        case names::freqShifter: shifter.process (l, r, n); break;
        case names::phaser:      phaser.process (l, r, n); break;
        case names::reverb:      reverb.process (l, r, n); break;
        default: break;   // off: the signal passes
    }
}

void EffectSlot::process (float* left, float* right, int n) noexcept
{
    if (target != current)
    {
        previous = current;
        current = target;
        fade.snap (false);
        fade.setOn (true);
    }

    if (fade.isOn())
    {
        run (current, left, right, n);
        return;
    }

    std::copy (left, left + n, aL.begin());
    std::copy (right, right + n, aR.begin());
    std::copy (left, left + n, bL.begin());
    std::copy (right, right + n, bR.begin());
    run (previous, aL.data(), aR.data(), n);
    run (current, bL.data(), bR.data(), n);
    for (int i = 0; i < n; ++i)
    {
        const double w = fade.next();
        left[i] = (float) (aL[(size_t) i] + w * (bL[(size_t) i] - aL[(size_t) i]));
        right[i] = (float) (aR[(size_t) i] + w * (bR[(size_t) i] - aR[(size_t) i]));
    }
}

} // namespace dt
