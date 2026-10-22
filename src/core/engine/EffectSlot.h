#pragma once

#include "dsp/Smoother.h"
#include "fx/BitReducer.h"
#include "fx/Chorus.h"
#include "fx/Compressor.h"
#include "fx/Filter.h"
#include "fx/Flanger.h"
#include "fx/FrequencyShifter.h"
#include "fx/Phaser.h"
#include "fx/Reverb.h"
#include "fx/StereoDelay.h"
#include "model/Params.h"

#include <vector>

namespace dt
{

// One of Thermal's effect slots (a stage's effect, FX 1 or FX 2): nine effects, one of them active.
// The slot's 65 parameters sit in kParams in the same order for every slot, starting at <slot>_bit_in.
//
// Thermal rewires its graph when the effect or its power button changes, with no crossfade. DisTitten
// crossfades the old and the new path over kSwitchSeconds so a switch never clicks; nothing else differs.
// Inactive effects keep their state (Thermal doesn't process a bypassed node either).
class EffectSlot
{
public:
    static constexpr double kSwitchSeconds = 0.01;

    // base = pid of the slot's first effect parameter (<slot>_bit_in); sync = pid of the slot's delay sync switch
    void configure (int basePid, int delaySyncPid) noexcept { base = basePid; syncPid = delaySyncPid; }

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // plain: every parameter's (modulated) plain value; type/on: the slot's effect switch
    void setParams (const float* plain, int type, bool on, double bpm) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;

    // Gain reduction of the slot's compressor (for the meter), dB <= 0.
    double compressorGainReductionDb() const noexcept { return comp.getGainReductionDb(); }
    int activeEffect() const noexcept { return current; }   // -1: off

private:
    void run (int which, float* l, float* r, int n) noexcept;
    float v (const float* plain, int s1Pid) const noexcept { return plain[base + (s1Pid - pid::s1_bit_in)]; }

    int base = pid::s1_bit_in, syncPid = pid::s1_dly_sync;
    BitReducer bit;
    Chorus chorus;
    Compressor comp;
    StereoDelay delay;
    Filter filter;
    Flanger flanger;
    FrequencyShifter shifter;
    Phaser phaser;
    Reverb reverb;

    int current = -1, previous = -1, target = -1;   // effect index or -1 (off)
    dsp::Fade fade;
    std::vector<float> aL, aR, bL, bR;
};

} // namespace dt
