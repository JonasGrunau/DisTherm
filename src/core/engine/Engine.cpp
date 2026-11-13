#include "Engine.h"

#include "EffectSlot.h"
#include "Juce32.h"
#include "dsp/Smoother.h"
#include "fx/Compressor.h"
#include "fx/DistortionNode.h"
#include "fx/Structure.h"
#include "mod/Modulation.h"
#include "model/ModState.h"
#include "model/Names.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dt
{
namespace
{
    constexpr int kStages = 3;
    constexpr int kStageLatency = DistortionNode::kLatency;   // 31: the 2x oversampler of each stage's distortion
    constexpr int kChunk = 32;                                // Thermal re-reads modulation of the bus and master-comp gains every 32 samples
    constexpr double kToggleSeconds = 0.01;                   // DisTherm's click-free switching (Thermal switches hard)

    // The stage blocks of kParams repeat with a fixed stride.
    constexpr int stageParam (int stage, int s1Pid) noexcept { return s1Pid + stage * (pid::s2_lo - pid::s1_lo); }
    constexpr int stageSwitch (int stage, int s1Pid) noexcept { return s1Pid + stage * (pid::s2_on - pid::s1_on); }
    constexpr int stageEffectBase (int stage) noexcept { return pid::s1_bit_in + stage * (pid::s2_bit_in - pid::s1_bit_in); }

    bool on (float plain) noexcept { return plain >= 0.5f; }

    // A node that Thermal switches in and out instantly (width, tone, master compressor, a whole stage): DisTherm
    // crossfades between the node's output and its input instead.
    struct Toggle
    {
        dsp::Fade fade;
        void prepare (double fs, bool initial) { fade.prepare (fs, kToggleSeconds); fade.snap (initial); }
        void set (bool b) { fade.setOn (b); }
        bool off() const { return fade.isOff(); }
        bool full() const { return fade.isOn(); }

        // processed = the node's output (computed on a copy), io = its input, becomes the mix
        void mix (float* ioL, float* ioR, const float* pL, const float* pR, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                const double w = fade.next();
                ioL[i] = (float) (ioL[i] + w * (pL[i] - ioL[i]));
                ioR[i] = (float) (ioR[i] + w * (pR[i] - ioR[i]));
            }
        }
    };
} // namespace

struct Engine::Impl
{
    struct Stage
    {
        Crossover3 xo;
        DistortionNode disto;
        EffectSlot fx;
        MidSideWidth ms;
        GainStage gain;
        ToneControl tone;
        detail::Delay skipL, skipR;   // a skipped stage still carries its 31 samples
        Toggle active, width, toneOn;
    };

    double sr = 48000.0;
    int maxBlock = 512;
    EngineParams prm;
    std::array<float, kNumParams> cur {};   // this block's parameter values, modulation applied

    InputBus in;
    std::array<Stage, kStages> st;
    EffectSlot fx1, fx2;
    Compressor master;
    Toggle masterOn;
    OutputBus out;

    ModEnvelope env[2];
    ModBus bus;
    ModState mod;
    const ModStateChannel* channel = nullptr;
    unsigned seen = 0;
    std::array<int, kNumParams> modList {};
    int numMod = 0;
    std::array<int, kNumParams> delayOf {};   // graph latency in front of each parameter's node
    std::array<BandSplitCoupler, kStages> couplers;   // Thermal's LOW/HIGH coupling, on the parameter values
    float envFirst[2] { 0.f, 0.f };           // each envelope's value at the previous block's first sample

    std::vector<float> dryL, dryR, tmpL, tmpR, envBuf[2], macroBuf[2];
    std::vector<float> inDb, outDb, mcInDb, mcOutDb;   // per 32-sample chunk
    bool chunkIn = false, chunkOut = false, chunkMaster = false;

    EngineMeters meters;

    Impl()
    {
        // D per destination (measured): the input bus and stage 1's splitter and distortion see the sources
        // undelayed; everything after each stage's distortion is 31 samples later; the master section sees 93.
        delayOf.fill (3 * kStageLatency);
        delayOf[pid::in_gain] = 0;
        for (int s = 0; s < kStages; ++s)
        {
            for (int p = pid::s1_lo; p <= pid::s1_tone_hi; ++p)
            {
                const bool front = p == pid::s1_lo || p == pid::s1_hi || p == pid::s1_split
                                   || (p >= pid::s1_type && p <= pid::s1_time);
                delayOf[(size_t) stageParam (s, p)] = (s + (front ? 0 : 1)) * kStageLatency;
            }
            for (int p = 0; p < pid::s2_bit_in - pid::s1_bit_in; ++p)
                delayOf[(size_t) (stageEffectBase (s) + p)] = (s + 1) * kStageLatency;
        }
        for (int s = 0; s < kStages; ++s)
            st[(size_t) s].fx.configure (stageEffectBase (s), pid::s1_dly_sync + s * (pid::s2_dly_sync - pid::s1_dly_sync));
        fx1.configure (pid::fx1_bit_in, pid::fx1_dly_sync);
        fx2.configure (pid::fx2_bit_in, pid::fx2_dly_sync);
    }

    void rebuildModList()
    {
        numMod = 0;
        for (int p = 0; p < kNumThermalParams; ++p)
            if (mod.targets[(size_t) p].active())
                modList[(size_t) numMod++] = p;
        for (int k = 0; k < 2; ++k)
            env[k].setPoints (mod.envelopes[(size_t) k].points.data(), mod.envelopes[(size_t) k].numPoints);
    }

    std::array<float, kNumParams> base {};   // the parameter values modulation starts from (coupled bands)

    float modulated (int p, int sample) const noexcept
    {
        float src[mod::kNumSources];
        bus.values (sample, delayOf[(size_t) p], src);
        const auto& info = kParams[p];
        const float u = mod.targets[(size_t) p].compute (juce32::norm (info, base[(size_t) p]), src);
        return juce32::plain (info, u);
    }

    // Thermal keeps LOW at least 30 px of its band display below HIGH; it couples the parameter values (and writes
    // them back), so modulation then moves the coupled values.
    void coupleBands()
    {
        base = prm.plain;
        for (int s = 0; s < kStages; ++s)
        {
            const int lo = stageParam (s, pid::s1_lo), hi = stageParam (s, pid::s1_hi);
            if (on (prm.plain[(size_t) stageSwitch (s, pid::s1_on)]) && on (prm.plain[(size_t) stageParam (s, pid::s1_split)]))
            {
                couplers[(size_t) s].apply (std::clamp ((double) prm.plain[(size_t) lo], 30.0, 22000.0),
                                            std::clamp ((double) prm.plain[(size_t) hi], 30.0, 22000.0));
                base[(size_t) lo] = (float) couplers[(size_t) s].band1();
                base[(size_t) hi] = (float) couplers[(size_t) s].band2();
            }
        }
    }

    void runSources (int n, const Transport& t)
    {
        const float m1 = prm.plain[pid::macro1], m2 = prm.plain[pid::macro2];
        std::fill (macroBuf[0].begin(), macroBuf[0].begin() + n, m1);
        std::fill (macroBuf[1].begin(), macroBuf[1].begin() + n, m2);

        ModTransport mt;
        mt.isPlaying = t.playing;
        mt.hasTimeInSamples = t.hasTime;
        mt.timeInSamples = t.timeInSamples;
        mt.bpm = t.bpm > 0.0 ? t.bpm : 120.0;

        // An envelope's own Rate/Humanize can be modulated by the macros and by the other envelope; Thermal runs the
        // sources in dependency order and reads that modulation once per block at the current block's first sample.
        const auto dependsOn = [&] (int k, int other) {
            const int rate = k == 0 ? pid::mod1_rate : pid::mod2_rate, hum = k == 0 ? pid::mod1_humanize : pid::mod2_humanize;
            return mod.targets[(size_t) rate].slot[(size_t) other].connected || mod.targets[(size_t) hum].slot[(size_t) other].connected;
        };
        const int order[2] = { dependsOn (0, 1) && ! dependsOn (1, 0) ? 1 : 0, dependsOn (0, 1) && ! dependsOn (1, 0) ? 0 : 1 };
        bool done[2] { false, false };
        for (int k : order)
        {
            const float src[mod::kNumSources] = { done[0] ? envBuf[0][0] : envFirst[0], done[1] ? envBuf[1][0] : envFirst[1], m1, m2 };
            const int rate = k == 0 ? pid::mod1_rate : pid::mod2_rate, hum = k == 0 ? pid::mod1_humanize : pid::mod2_humanize;
            ModEnvelopeParams ep;
            ep.rate = mod.targets[(size_t) rate].compute (juce32::norm (kParams[rate], prm.plain[(size_t) rate]), src);
            ep.humanize = mod.targets[(size_t) hum].compute (juce32::norm (kParams[hum], prm.plain[(size_t) hum]), src);
            ep.synced = on (prm.plain[k == 0 ? pid::mod1_sync : pid::mod2_sync]);
            ep.bypassed = ! on (prm.plain[k == 0 ? pid::mod1_on : pid::mod2_on]);
            env[k].setParams (ep);
            env[k].process (envBuf[k].data(), n, mt);
            done[k] = true;
        }
        envFirst[0] = envBuf[0][0];
        envFirst[1] = envBuf[1][0];

        bus.writeBlock (0, envBuf[0].data(), n);
        bus.writeBlock (1, envBuf[1].data(), n);
        bus.writeBlock (2, macroBuf[0].data(), n);
        bus.writeBlock (3, macroBuf[1].data(), n);
        bus.endBlock (n);
    }

    void applyModulation (int n)
    {
        coupleBands();
        cur = base;
        chunkIn = chunkOut = chunkMaster = false;
        for (int j = 0; j < numMod; ++j)
        {
            const int p = modList[(size_t) j];
            if (p == pid::mod1_rate || p == pid::mod1_humanize || p == pid::mod2_rate || p == pid::mod2_humanize)
                continue;   // handled with the sources
            cur[(size_t) p] = modulated (p, 0);
            chunkIn |= p == pid::in_gain;
            chunkOut |= p == pid::out_gain;
            chunkMaster |= p == pid::mc_in || p == pid::mc_out;
        }
        const int chunks = (n + kChunk - 1) / kChunk;
        for (int c = 0; c < chunks; ++c)
        {
            const int i = c * kChunk;
            inDb[(size_t) c] = chunkIn ? modulated (pid::in_gain, i) : cur[pid::in_gain];
            outDb[(size_t) c] = chunkOut ? modulated (pid::out_gain, i) : cur[pid::out_gain];
            mcInDb[(size_t) c] = chunkMaster ? modulated (pid::mc_in, i) : cur[pid::mc_in];
            mcOutDb[(size_t) c] = chunkMaster ? modulated (pid::mc_out, i) : cur[pid::mc_out];
        }
    }

    void configure (double bpm)
    {
        const auto& c = cur;
        const auto& h = prm.plain;   // switches are never modulated
        in.setParams ({ c[pid::in_gain] });

        for (int s = 0; s < kStages; ++s)
        {
            auto& S = st[(size_t) s];
            const auto P = [&] (int s1Pid) { return c[(size_t) stageParam (s, s1Pid)]; };
            const bool stageOn = on (h[(size_t) stageSwitch (s, pid::s1_on)]);
            S.active.set (stageOn);

            Crossover3Params xp;
            xp.band1Hz = P (pid::s1_lo);
            xp.band2Hz = P (pid::s1_hi);
            xp.multiBand = on (P (pid::s1_split));   // Thermal outputs NaN between 0 and 1; a switch here
            xp.soloBand = P (pid::s1_solo) > 0.f;
            xp.refilter = P (pid::s1_refilter) > 0.f;
            xp.applyCoupling = false;   // done on the parameter values, before modulation (coupleBands)

            DistortionNodeParams dp;
            int type = (int) std::floor (P (pid::s1_type) + 1e-6f);
            if (type >= names::kNumDistortionTypes && type < 32) type = names::kNumDistortionTypes - 1;   // Thermal crashes on 19..31
            dp.type = std::clamp (type, 0, 33);
            dp.controls[0] = P (pid::s1_shape);
            dp.controls[1] = P (pid::s1_depth);
            dp.controls[2] = P (pid::s1_freq);
            dp.controls[3] = P (pid::s1_clip);
            dp.driveDb = P (pid::s1_drive);
            dp.outDb = P (pid::s1_out);
            dp.autoComp = P (pid::s1_auto) >= 1.f;
            dp.feedback = P (pid::s1_fb);
            dp.timeSec = P (pid::s1_time);
            dp.sync = on (h[(size_t) stageSwitch (s, pid::s1_time_sync)]);
            dp.syncIndex = (int) std::lround (juce32::norm (kParams[pid::s1_time], P (pid::s1_time)) * 18.f);
            dp.bpm = bpm;
            dp.follow = P (pid::s1_follow) > 0.f;
            dp.release = P (pid::s1_release);
            dp.bypassed = ! on (h[(size_t) stageSwitch (s, pid::s1_disto_on)]);

            if (stageOn || ! S.active.off())   // a skipped stage keeps its state frozen, as in Thermal
            {
                S.xo.setParams (xp);
                S.disto.setParams (dp);
                S.ms.setParams ({ P (pid::s1_sides), P (pid::s1_width), true });
                S.gain.setParams ({ P (pid::s1_gain), true });
            }
            S.fx.setParams (c.data(), (int) h[(size_t) stageSwitch (s, pid::s1_fx_type)], on (h[(size_t) stageSwitch (s, pid::s1_fx_on)]), bpm);
            S.width.set (on (h[(size_t) stageSwitch (s, pid::s1_width_on)]));
            S.toneOn.set (on (h[(size_t) stageSwitch (s, pid::s1_tone_on)]));
            S.tone.setParams ({ P (pid::s1_tone_lo), P (pid::s1_tone_hi), true });

            const bool split = stageOn && xp.multiBand;
            out.setDryCompensation (s, { split, S.xo.coupledBand1(), S.xo.coupledBand2(), split && xp.refilter });
        }

        fx1.setParams (c.data(), (int) h[pid::fx1_type], on (h[pid::fx1_on]), bpm);
        fx2.setParams (c.data(), (int) h[pid::fx2_type], on (h[pid::fx2_on]), bpm);

        CompressorParams mp;
        mp.inputGainDb = c[pid::mc_in];
        mp.outputGainDb = c[pid::mc_out];
        mp.thresholdDb = c[pid::mc_thresh];
        mp.ratio = c[pid::mc_ratio];
        mp.attackSec = c[pid::mc_attack];
        mp.releaseSec = c[pid::mc_release];
        mp.makeupDb = c[pid::mc_gain];
        mp.wetDry = c[pid::mc_mix];
        master.setParams (mp);
        masterOn.set (on (h[pid::mc_on]));

        OutputBusParams op;
        op.dryWet = c[pid::mix];
        op.outputGainDb = c[pid::out_gain];
        op.lowpassHz = c[pid::lp];
        op.highpassHz = c[pid::hp];
        op.filtersBypassed = ! on (h[pid::filters_on]);
        out.setParams (op);
    }

    // One stage's processing when it is on: split, the mid band through distortion, effect, width and gain, sum.
    void runStage (Stage& S, float* l, float* r, int n)
    {
        S.xo.split (l, r, n);
        float* mL = S.xo.midsL();
        float* mR = S.xo.midsR();
        S.disto.process (mL, mR, n);
        S.fx.process (mL, mR, n);
        runToggled (S.width, S.ms, mL, mR, n);
        S.gain.process (mL, mR, n);
        S.xo.sum (mL, mR, l, r, n);
    }

    // A node with an on/off switch: run it fully, not at all (its smoothers still advance), or crossfaded.
    template <typename Node>
    void runToggled (Toggle& t, Node& node, float* l, float* r, int n)
    {
        if (t.full()) { node.process (l, r, n); return; }
        if (t.off()) { setEnabled (node, false); node.process (l, r, n); setEnabled (node, true); return; }
        std::copy (l, l + n, tmpL.begin());
        std::copy (r, r + n, tmpR.begin());
        node.process (tmpL.data(), tmpR.data(), n);
        t.mix (l, r, tmpL.data(), tmpR.data(), n);
    }

    static void setEnabled (MidSideWidth& m, bool e) { m.setEnabled (e); }
    static void setEnabled (ToneControl& t, bool e) { t.setEnabled (e); }

    void processBlock (float* L, float* R, int n, const Transport& t)
    {
        if (channel != nullptr && channel->fetch (mod, seen))
            rebuildModList();

        runSources (n, t);
        applyModulation (n);
        configure (t.bpm > 0.0 ? t.bpm : 120.0);

        // input bus (its gain re-reads modulation every 32 samples)
        for (int i0 = 0, c = 0; i0 < n; i0 += kChunk, ++c)
        {
            const int len = std::min (kChunk, n - i0);
            if (chunkIn) in.setParams ({ inDb[(size_t) c] });
            in.process (L + i0, R + i0, len);
        }
        std::copy (L, L + n, dryL.begin());
        std::copy (R, R + n, dryR.begin());

        for (int s = 0; s < kStages; ++s)
        {
            auto& S = st[(size_t) s];
            // the skipped path (a pure 31-sample delay) always runs, so either path is ready to take over
            if (! S.active.off())
            {
                std::copy (L, L + n, stageL.begin());
                std::copy (R, R + n, stageR.begin());
                runStage (S, stageL.data(), stageR.data(), n);
            }
            for (int i = 0; i < n; ++i) { L[i] = S.skipL.tick (L[i]); R[i] = S.skipR.tick (R[i]); }
            if (S.active.full())
            {
                std::copy (stageL.begin(), stageL.begin() + n, L);
                std::copy (stageR.begin(), stageR.begin() + n, R);
            }
            else if (! S.active.off())
                S.active.mix (L, R, stageL.data(), stageR.data(), n);
            runToggled (S.toneOn, S.tone, L, R, n);   // the tone control also runs on a skipped stage
        }

        fx1.process (L, R, n);
        fx2.process (L, R, n);

        if (masterOn.full())
            master.process (L, R, n, mcInDb.data(), mcOutDb.data(), kChunk);
        else if (! masterOn.off())
        {
            std::copy (L, L + n, tmpL.begin());
            std::copy (R, R + n, tmpR.begin());
            master.process (tmpL.data(), tmpR.data(), n, mcInDb.data(), mcOutDb.data(), kChunk);
            masterOn.mix (L, R, tmpL.data(), tmpR.data(), n);
        }

        // output bus (its gain re-reads modulation every 32 samples)
        for (int i0 = 0, c = 0; i0 < n; i0 += kChunk, ++c)
        {
            const int len = std::min (kChunk, n - i0);
            if (chunkOut)
            {
                OutputBusParams op;
                op.dryWet = cur[pid::mix];
                op.outputGainDb = outDb[(size_t) c];
                op.lowpassHz = cur[pid::lp];
                op.highpassHz = cur[pid::hp];
                op.filtersBypassed = ! on (prm.plain[pid::filters_on]);
                out.setParams (op);
            }
            out.process (L + i0, R + i0, dryL.data() + i0, dryR.data() + i0, len);
        }

        for (int s = 0; s < kStages; ++s)
        {
            meters.coupledBand[(size_t) s][0].store ((float) couplers[(size_t) s].band1(), std::memory_order_relaxed);
            meters.coupledBand[(size_t) s][1].store ((float) couplers[(size_t) s].band2(), std::memory_order_relaxed);
            meters.slotGainReductionDb[(size_t) s].store ((float) st[(size_t) s].fx.compressorGainReductionDb(), std::memory_order_relaxed);
        }
        meters.slotGainReductionDb[3].store ((float) fx1.compressorGainReductionDb(), std::memory_order_relaxed);
        meters.slotGainReductionDb[4].store ((float) fx2.compressorGainReductionDb(), std::memory_order_relaxed);
        meters.masterGainReductionDb.store (masterOn.off() ? 0.f : (float) master.getGainReductionDb(), std::memory_order_relaxed);
        meters.envelope[0].store (envBuf[0][(size_t) n - 1], std::memory_order_relaxed);
        meters.envelope[1].store (envBuf[1][(size_t) n - 1], std::memory_order_relaxed);
        meters.source[0].store (envBuf[0][0], std::memory_order_relaxed);
        meters.source[1].store (envBuf[1][0], std::memory_order_relaxed);
        meters.source[2].store (macroBuf[0][0], std::memory_order_relaxed);
        meters.source[3].store (macroBuf[1][0], std::memory_order_relaxed);
    }

    std::vector<float> stageL, stageR;
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    auto& I = *impl;
    I.sr = sampleRate;
    I.maxBlock = std::max (1, maxBlockSize);
    I.applyModulation (0);
    I.configure (120.0);   // modules take the current parameters as their start state in prepare (as Thermal loads)

    I.in.prepare (sampleRate, I.maxBlock);
    for (auto& S : I.st)
    {
        S.xo.prepare (sampleRate, I.maxBlock, kStageLatency);
        S.disto.prepare (sampleRate, I.maxBlock);
        S.fx.prepare (sampleRate, I.maxBlock);
        S.ms.prepare (sampleRate, I.maxBlock);
        S.gain.prepare (sampleRate, I.maxBlock);
        S.tone.prepare (sampleRate, I.maxBlock);
        S.skipL.prepare (kStageLatency);
        S.skipR.prepare (kStageLatency);
    }
    I.fx1.prepare (sampleRate, I.maxBlock);
    I.fx2.prepare (sampleRate, I.maxBlock);
    I.master.prepare (sampleRate, I.maxBlock);
    I.out.prepare (sampleRate, I.maxBlock, kStages * kStageLatency);
    for (int k = 0; k < 2; ++k) I.env[k].prepare (sampleRate, I.maxBlock);
    I.bus.prepare (I.maxBlock);

    const auto block = (size_t) I.maxBlock;
    for (auto* v : { &I.dryL, &I.dryR, &I.tmpL, &I.tmpR, &I.stageL, &I.stageR, &I.envBuf[0], &I.envBuf[1], &I.macroBuf[0], &I.macroBuf[1] })
        v->assign (block, 0.f);
    const auto chunks = (size_t) ((I.maxBlock + kChunk - 1) / kChunk);
    for (auto* v : { &I.inDb, &I.outDb, &I.mcInDb, &I.mcOutDb }) v->assign (chunks, 0.f);

    reset();
}

void Engine::reset()
{
    auto& I = *impl;
    I.in.reset();
    for (auto& c : I.couplers) c.reset();
    for (int s = 0; s < kStages; ++s)
    {
        auto& S = I.st[(size_t) s];
        S.xo.reset();
        S.disto.reset();
        S.fx.reset();
        S.ms.reset();
        S.gain.reset();
        S.tone.reset();
        S.skipL.clear();
        S.skipR.clear();
        S.active.prepare (I.sr, on (I.prm.plain[(size_t) stageSwitch (s, pid::s1_on)]));
        S.width.prepare (I.sr, on (I.prm.plain[(size_t) stageSwitch (s, pid::s1_width_on)]));
        S.toneOn.prepare (I.sr, on (I.prm.plain[(size_t) stageSwitch (s, pid::s1_tone_on)]));
    }
    I.fx1.reset();
    I.fx2.reset();
    I.master.reset();
    I.masterOn.prepare (I.sr, on (I.prm.plain[pid::mc_on]));
    I.out.reset();
    for (auto& e : I.env) e.reset();
    I.bus.reset();
    I.envFirst[0] = I.envFirst[1] = 0.f;
}

void Engine::setParams (const EngineParams& p) noexcept { impl->prm = p; }

void Engine::setModChannel (const ModStateChannel* c) noexcept { impl->channel = c; }

void Engine::process (float* left, float* right, int numSamples, const Transport& t) noexcept
{
    // Thermal processes the whole host block at once; a host block larger than promised is split.
    Transport tt = t;
    for (int i0 = 0; i0 < numSamples; i0 += impl->maxBlock)
    {
        const int n = std::min (impl->maxBlock, numSamples - i0);
        impl->processBlock (left + i0, right + i0, n, tt);
        tt.timeInSamples += n;
    }
}

double Engine::getTailSeconds() const noexcept { return StereoDelay::kMaxDelay; }

const EngineMeters& Engine::meters() const noexcept { return impl->meters; }

} // namespace dt
