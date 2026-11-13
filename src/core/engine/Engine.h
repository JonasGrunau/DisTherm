#pragma once

#include "model/Params.h"

#include <array>
#include <atomic>
#include <memory>

namespace dt
{

class ModStateChannel;

// What the engine reports back once per block, for the editor (any thread may read).
struct EngineMeters
{
    std::array<std::array<std::atomic<float>, 2>, 3> coupledBand {};   // per stage: LOW / HIGH after Thermal's coupling (Hz)
    std::atomic<float> masterGainReductionDb { 0.f };
    std::array<std::atomic<float>, 5> slotGainReductionDb {};          // stage 1..3 effect, FX 1, FX 2 (compressor)
    std::array<std::atomic<float>, 2> envelope {};                     // Mod 1 / Mod 2 output at the block end
    std::array<std::atomic<float>, 4> source {};                       // Mod 1, Mod 2, Macro 1, Macro 2 at the block start
};

struct Transport
{
    double bpm = 120.0;
    double ppq = 0.0;              // quarter notes since the song start, at the block's first sample
    bool hasPpq = false;
    long long timeInSamples = 0;   // the host's sample position: synced mod envelopes follow this, as in Thermal
    bool hasTime = false;
    bool playing = false;
    bool hasPlayHead = false;
};

// Every parameter's plain value (kParams order), copied from the host once per block.
struct EngineParams
{
    std::array<float, kNumParams> plain {};
    bool render = false;   // offline render / bounce: best quality

    EngineParams()
    {
        for (int i = 0; i < kNumParams; ++i)
            plain[(size_t) i] = (float) toPlain (kParams[i], kParams[i].defaultNorm);
    }
};

// Thermal's whole graph, stereo, in place:
//
//   in ─ input gain ─┬─ stage 1 ─ stage 2 ─ stage 3 ─ FX 1 ─ FX 2 ─ master comp ─┬─ dry/wet ─ gain ─ LP/HP ─ out
//                    └──────────────────────── dry ──────────────────────────────┘
//   stage: band split ─ mids: distortion ─ effect ─ width ─ gain ─┬─ + lows + highs ─ tone
//
// Thermal processes its graph once per host block, and three of its measured behaviours depend on the block
// (the filter follower's cutoff update, the compressor's slow-release flag and its attack/release smoothing),
// so process() takes the host block whole, as Thermal does.
//
// Latency: each stage carries 31 samples (its oversampler's delay, also when the stage is off), 93 in total.
// Thermal doesn't report it; DisTherm does.
class Engine
{
public:
    Engine();
    ~Engine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setParams (const EngineParams&) noexcept;
    // The modulation routing and envelope shapes, published by the message thread (picked up at the next block).
    void setModChannel (const ModStateChannel* channel) noexcept;
    void process (float* left, float* right, int numSamples, const Transport&) noexcept;

    const EngineMeters& meters() const noexcept;

    static constexpr int kLatency = 93;
    int getLatencySamples() const noexcept { return kLatency; }
    double getTailSeconds() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace dt
