// Modulation.h - DisTherm re-creation of Output Thermal's modulation system (black-box measured).
// Header-only, JUCE-free, C++20.  namespace dt.
//
// Contents
//   dt::mod::*            constants, rate tables, the curve flattener and the 2048-point envelope LUT
//   dt::ModEnvelope       MOD 1 / MOD 2 drawable envelope (tempo-synced or free Hz), humanize, per-sample output
//   dt::Macro             MACRO 1 / MACRO 2 (value 0..1, bypass -> 0)
//   dt::ModSlotParams /
//   dt::ModTarget         per-parameter routing: amount, bipolar, depth modulation -> modulated normalised value
//   dt::ModBus            per-sample history of the 4 source signals, read back with a destination-specific
//                         latency-compensation delay (Thermal's graph delays mod signals like audio)
//
// See FINDINGS.md for the measurements behind every constant.  Summary of the model:
//   * A source produces one value per SAMPLE (EnvelopeOut / MacroOut are audio-rate signals).
//   * Envelope value = LUT[2048] built from the drawn points: each segment (point i-1 -> point i) is a quadratic
//     Bezier with control point i, flattened exactly like juce::PathFlatteningIterator (tolerance 0.6) in a
//     1200 x 400 space, then sampled at x = j/2048 (first segment in order with xa <= x <= xb; none -> 0),
//     output = clamp(1 - y, 0, 1)  (stored y is a screen coordinate: 0 = top = 1.0).
//     Read with linear interpolation and wrap-around (cell 2047 interpolates towards entry 0).
//   * Synced: phase = float(pos mod P) / float(P), P = floor(sr*60/bpm*beats) samples (integer!),
//     pos = host timeInSamples + i while playing, an internal counter (continuing) while stopped; pos<0 -> phase 0.
//     Free:   float32 accumulator, inc = 1/floor(sr/f), f = 0.005 + 29.995*u^(10/3) Hz, output-then-increment,
//             reset to 0 when the transport starts playing.
//   * Humanize h: at every cycle wrap (and initially) a new r_i ~ U(-0.5, 0.5) is drawn per point; the LUT uses
//     y_i + h*r_i (clamped to 0..1); x and control points unchanged. Changing h rescales the current draw
//     immediately (LUT rebuilt). Random per instance (not reproducible across instances).
//   * Routing: u = clamp(base + sum_i (amount_i + depthRange_i * sum(depthSrc)) * s_i, 0, 1) in the NORMALISED
//     domain (before the parameter's skew), s_i = src_i (unipolar) or 2*src_i - 1 (bipolar). A slot only counts
//     if its source is connected; the depth term only if a depth source is connected. No clamp of the amounts.
//   * Delivery: destination node at graph latency D (0 / 31 / 62 / 93 samples, see FINDINGS.md) sees the source
//     signals delayed by D; most parameters sample them once per host block at the block's first sample,
//     a few (InputBus InputGain, MasterCompressor In/Out gain, OutputBus OutputGain) every 32-sample chunk.
//   * Envelope Rate / Humanize can themselves be modulated (by macros or by the other envelope): process the sources
//     in dependency order (macros, then the modulating envelope, then the modulated one) and sample the
//     modulation once per block at block sample 0 of the CURRENT block (D = 0). Macros cannot be modulated.
//
// Typical per-block use (see render.cpp):
//   macros -> m[k].process(buf[2+k]);  envelopes in dependency order: p.rate = rateTarget.compute(rateBase, srcAt0);
//   env.setParams(p); env.process(buf[k], n, transport);  bus.writeBlock(k, buf[k], n) for all 4; bus.endBlock(n);
//   for a destination at latency D: float v[4]; bus.values(0, D, v); u = target.compute(base, v);  (or every 32 samples)
#pragma once
// Exact comparisons of floats are deliberate in this file (Thermal's float behaviour); keeps -Wfloat-equal quiet.
#if defined (__clang__) || defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace dt
{
namespace mod
{
    inline constexpr int kLutSize = 2048;
    inline constexpr int kMaxPoints = 64;            // factory presets use up to 39
    inline constexpr int kMaxLinesPerSegment = 1024; // flattening depth cap (JUCE would go deeper only for absurd control points)
    inline constexpr int kNumSources = 4;            // 0 = MOD 1, 1 = MOD 2, 2 = MACRO 1, 3 = MACRO 2
    inline constexpr int kSyncDivisions = 26;

    // Rate (sync) table, index = round(u * 25): 8 Bar ... 1/64t. Cycle length in quarter-note beats = num/den.
    inline constexpr int kSyncNum[kSyncDivisions] = { 32, 28, 24, 20, 16, 12, 8, 4, 3, 2, 4, 3, 1, 2, 3, 1, 1, 3, 1, 1, 3, 1, 1, 3, 1, 1 };
    inline constexpr int kSyncDen[kSyncDivisions] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 2, 1, 3, 4, 2, 3, 8, 4, 6, 16, 8, 12, 32, 16, 24 };
    inline constexpr const char* kSyncNames[kSyncDivisions] = { "8 Bar", "7 Bar", "6 Bar", "5 Bar", "4 Bar", "3 Bar", "2 Bar", "1 Bar",
                                                                "1/2d", "1/2", "1/2t", "1/4d", "1/4", "1/4t", "1/8d", "1/8", "1/8t",
                                                                "1/16d", "1/16", "1/16t", "1/32d", "1/32", "1/32t", "1/64d", "1/64", "1/64t" };

    // Flattening space: juce::PathFlatteningIterator with tolerance 0.6 on the curve scaled to 1200 x 400.
    inline constexpr float kFlatSX = 1200.0f, kFlatSY = 400.0f, kFlatTol = 0.6f;

    // Rate parameter (normalised u, already modulated) -> sync index (DSP rounds; the UI text floors)
    inline int syncIndex (float u) noexcept
    {
        u = std::clamp (u, 0.0f, 1.0f);
        return std::clamp ((int) std::floor (u * 25.0f + 0.5f), 0, kSyncDivisions - 1);
    }

    // Free mode: u -> Hz (NormalisableRange 0.005..30, skew 0.3), float like JUCE
    inline float freeRateHz (float u) noexcept
    {
        u = std::clamp (u, 0.0f, 1.0f);
        return 0.005f + 29.995f * std::exp (std::log (std::max (u, 1.0e-30f)) / 0.3f);
    }

    // integer cycle length in samples (synced)
    inline int64_t syncPeriodSamples (double sampleRate, double bpm, int index) noexcept
    {
        index = std::clamp (index, 0, kSyncDivisions - 1);
        if (! (bpm > 0.0)) bpm = 120.0;
        const double p = (60.0 * sampleRate * (double) kSyncNum[index]) / (bpm * (double) kSyncDen[index]);
        return std::max<int64_t> (1, (int64_t) std::floor (p + 1.0e-9 * p));
    }

    // integer cycle length in samples (free)
    inline int64_t freePeriodSamples (double sampleRate, float u) noexcept
    {
        const double f = (double) freeRateHz (u);
        return std::max<int64_t> (1, (int64_t) std::floor (sampleRate / f));
    }

    struct EnvPoint
    {
        float x = 0, y = 0;   // PointX<i>, PointY<i>  (y: 0 = top = env 1.0)
        float cx = 0, cy = 0; // ControlPointX<i>, ControlPointY<i>: control point of segment (i-1 -> i); unused for i = 0
    };

    // ---- curve flattening (JUCE PathFlatteningIterator, quadratic case) ----
    // Appends the polyline vertices of segment p0 -> p1 (excluding p0), in normalised coordinates.
    // Returns the new vertex count (or -1 if out of space).
    inline int flattenQuad (float x0, float y0, float cx, float cy, float x1, float y1,
                            float* vx, float* vy, int count, int capacity) noexcept
    {
        struct Q { float x1, y1, x2, y2, x3, y3; int depth; };
        Q stack[64];
        int sp = 0;
        stack[sp++] = { x0 * kFlatSX, y0 * kFlatSY, cx * kFlatSX, cy * kFlatSY, x1 * kFlatSX, y1 * kFlatSY, 0 };
        const float tol2 = kFlatTol * kFlatTol;
        int maxDepth = 0;
        for (int l = kMaxLinesPerSegment; l > 2; l >>= 1) ++maxDepth;   // 2^(d+1) lines per leaf level

        auto emit = [&] (float x, float y) -> bool
        {
            if (count >= capacity) return false;
            vx[count] = x / kFlatSX;
            vy[count] = y / kFlatSY;
            ++count;
            return true;
        };

        while (sp > 0)
        {
            const Q q = stack[--sp];
            const float m1x = (q.x1 + q.x2) * 0.5f, m1y = (q.y1 + q.y2) * 0.5f;
            const float m2x = (q.x2 + q.x3) * 0.5f, m2y = (q.y2 + q.y3) * 0.5f;
            const float m3x = (m1x + m2x) * 0.5f, m3y = (m1y + m2y) * 0.5f;
            const float ex = m3x - q.x2, ey = m3y - q.y2;
            const bool outside = ex * ex + ey * ey > tol2;
            const bool canSub = (m3x != m1x && m3x != m2x) || (m3y != m1y && m3y != m2y);
            if (outside && canSub && q.depth < maxDepth && sp < 62)
            {
                // push right half first so the left half is processed first (JUCE order)
                stack[sp++] = { m3x, m3y, m2x, m2y, q.x3, q.y3, q.depth + 1 };
                stack[sp++] = { q.x1, q.y1, m1x, m1y, m3x, m3y, q.depth + 1 };
            }
            else
            {
                if (! emit (m3x, m3y) || ! emit (q.x3, q.y3)) return -1;
            }
        }
        return count;
    }

    // Build the 2048-entry envelope LUT (output units, 0..1) from points with the given y (already humanized).
    // Scratch buffers vx/vy must hold kMaxPoints * kMaxLinesPerSegment + 1 floats; assigned needs kLutSize.
    inline void buildLut (const EnvPoint* pts, const float* yOverride, int numPoints,
                          float* lut, float* vx, float* vy, int capacity, uint8_t* assigned) noexcept
    {
        std::fill (lut, lut + kLutSize, 0.0f);
        if (numPoints < 2) return;   // 0 or 1 point -> constant 0 (measured)

        auto px = [&] (int i) { return std::clamp (pts[i].x, 0.0f, 1.0f); };
        auto py = [&] (int i) { return std::clamp (yOverride ? yOverride[i] : pts[i].y, 0.0f, 1.0f); };

        int n = 0;
        vx[n] = px (0); vy[n] = py (0); ++n;
        for (int i = 1; i < numPoints; ++i)
        {
            const int r = flattenQuad (px (i - 1), py (i - 1), pts[i].cx, pts[i].cy, px (i), py (i), vx, vy, n, capacity);
            if (r < 0) break;
            n = r;
        }

        // first segment in polyline order with xa <= x <= xb wins; backwards segments never match; no match -> 0
        std::fill (assigned, assigned + kLutSize, (uint8_t) 0);
        int remaining = kLutSize;
        for (int s = 0; s + 1 < n && remaining > 0; ++s)
        {
            const float xa = vx[s], xb = vx[s + 1];
            if (! (xa <= xb)) continue;
            const float ya = vy[s], yb = vy[s + 1];
            int j0 = std::max (0, (int) std::floor (xa * (float) kLutSize) - 1);
            int j1 = std::min (kLutSize - 1, (int) std::ceil (xb * (float) kLutSize) + 1);
            for (int j = j0; j <= j1; ++j)
            {
                if (assigned[j]) continue;
                const float x = (float) j / (float) kLutSize;
                if (x < xa || x > xb) continue;
                const float y = (xb == xa) ? ya : ya + (yb - ya) * ((x - xa) / (xb - xa));
                lut[j] = std::clamp (1.0f - y, 0.0f, 1.0f);
                assigned[j] = 1;
                --remaining;
            }
        }
    }

    // LUT read: float index, linear interpolation, wrap-around (as measured)
    inline float readLut (const float* lut, float phase) noexcept
    {
        const float idx = phase * (float) kLutSize;
        const int i0 = (int) std::floor (idx);
        const float fr = idx - (float) i0;
        return lut[i0 & (kLutSize - 1)] * (1.0f - fr) + lut[(i0 + 1) & (kLutSize - 1)] * fr;
    }
} // namespace mod

// Host transport for one block (what Thermal reads from the play head)
struct ModTransport
{
    bool isPlaying = true;
    bool hasTimeInSamples = true;
    int64_t timeInSamples = 0;   // host timeline position of the block's first sample
    double bpm = 120.0;
};

struct ModEnvelopeParams
{
    float rate = 0.28f;      // normalised Rate parameter INCLUDING its own modulation (Thermal: 0.28 = 1 Bar)
    bool synced = true;      // node property RateCurrentMapping (1 = synced, 0 = free Hz)
    float humanize = 0.0f;   // normalised Humanize 0..1 incl. modulation
    bool bypassed = false;   // node property Bypassed -> output 0
};

// ===================================================================================================
// ModEnvelope
// ===================================================================================================
class ModEnvelope
{
public:
    ModEnvelope()
    {
        const int cap = mod::kMaxPoints * mod::kMaxLinesPerSegment + 1;
        vx.assign ((size_t) cap, 0.0f);
        vy.assign ((size_t) cap, 0.0f);
        std::random_device rd;
        rng.seed (((uint64_t) rd() << 32) ^ (uint64_t) rd());
        setPoints (defaultPoints().data(), 2);
    }

    static std::array<mod::EnvPoint, 2> defaultPoints() noexcept
    {
        // Thermal default: flat at 0.5 (PointY 0.5), control 1 at the midpoint
        return { mod::EnvPoint { 0.0f, 0.5f, 0.0f, 0.0f }, mod::EnvPoint { 1.0f, 0.5f, 0.5f, 0.5f } };
    }

    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sr = sampleRate > 0 ? sampleRate : 48000.0;
        reset();
    }

    void reset() noexcept
    {
        counter = 0;
        freePhase = 0.0f;
        wasPlaying = false;
        lastPm = -1;
        needsRegen = true;
    }

    void setSeed (uint64_t seed) noexcept { rng.seed (seed); }

    // Not real-time safe only in the sense that it rebuilds the LUT (O(points + 2048)); no allocation.
    void setPoints (const mod::EnvPoint* p, int n) noexcept
    {
        numPoints = std::clamp (n, 0, mod::kMaxPoints);
        for (int i = 0; i < numPoints; ++i) points[(size_t) i] = p[i];
        needsRegen = true;
    }

    void setParams (const ModEnvelopeParams& p) noexcept { params = p; }

    const float* lut() const noexcept { return table.data(); }

    // Per-sample output (0..1) for one block.
    void process (float* out, int numSamples, const ModTransport& t) noexcept
    {
        if (needsRegen) regenerate();
        else if (std::clamp (params.humanize, 0.0f, 1.0f) != lutHumanize) rebuild();   // humanize change rescales the current draw immediately

        if (params.synced)
        {
            const int idx = mod::syncIndex (params.rate);
            const int64_t P = mod::syncPeriodSamples (sr, t.bpm, idx);
            const int64_t pos0 = (t.isPlaying && t.hasTimeInSamples) ? t.timeInSamples : counter;
            for (int i = 0; i < numSamples; ++i)
            {
                const int64_t pos = pos0 + i;
                const int64_t pm = pos > 0 ? pos % P : 0;    // negative timeline -> phase 0 (measured)
                if (lastPm >= 0 && pm < lastPm) regenerate(); // cycle wrap -> new humanize draw
                lastPm = pm;
                const float ph = (float) pm / (float) P;
                out[i] = params.bypassed ? 0.0f : mod::readLut (table.data(), ph);
            }
            counter = pos0 + numSamples;
            // keep the free accumulator coherent if the mode is switched later
        }
        else
        {
            if (t.isPlaying && ! wasPlaying) freePhase = 0.0f;   // transport start resets the free LFO
            const int64_t P = mod::freePeriodSamples (sr, params.rate);
            const float inc = 1.0f / (float) P;
            for (int i = 0; i < numSamples; ++i)
            {
                out[i] = params.bypassed ? 0.0f : mod::readLut (table.data(), freePhase);
                freePhase += inc;
                if (freePhase >= 1.0f)
                {
                    freePhase -= 1.0f;
                    regenerate();
                }
            }
            counter += numSamples;
            lastPm = -1;
        }
        wasPlaying = t.isPlaying;
    }

private:
    // new random draw r_i ~ U(-0.5, 0.5) per point (at every cycle wrap and initially), then rebuild
    void regenerate() noexcept
    {
        needsRegen = false;
        std::uniform_real_distribution<float> uni (-0.5f, 0.5f);
        for (int i = 0; i < numPoints; ++i) rnd[(size_t) i] = uni (rng);
        rebuild();
    }

    // humanized y_i = clamp(y_i + h * r_i, 0, 1) with the current h (x and control points unchanged)
    void rebuild() noexcept
    {
        const float h = std::clamp (params.humanize, 0.0f, 1.0f);
        lutHumanize = h;
        for (int i = 0; i < numPoints; ++i)
            yh[(size_t) i] = std::clamp (std::clamp (points[(size_t) i].y, 0.0f, 1.0f) + h * rnd[(size_t) i], 0.0f, 1.0f);
        mod::buildLut (points.data(), yh.data(), numPoints, table.data(), vx.data(), vy.data(), (int) vx.size(), assigned.data());
    }

    double sr = 48000.0;
    ModEnvelopeParams params;
    std::array<mod::EnvPoint, mod::kMaxPoints> points {};
    std::array<float, mod::kMaxPoints> yh {};
    std::array<float, mod::kMaxPoints> rnd {};
    float lutHumanize = -1.0f;
    int numPoints = 0;
    std::array<float, mod::kLutSize> table {};
    std::array<uint8_t, mod::kLutSize> assigned {};
    std::vector<float> vx, vy;
    int64_t counter = 0;
    int64_t lastPm = -1;
    float freePhase = 0.0f;
    bool wasPlaying = false;
    bool needsRegen = true;
    std::mt19937_64 rng;
};

// ===================================================================================================
// Macro (MACRO 1 / MACRO 2): output = parameter value (0..1), constant over the block; Bypassed -> 0.
// Host automation of the macro is not smoothed by the macro itself.
// ===================================================================================================
class Macro
{
public:
    void prepare (double, int) {}
    void reset() noexcept {}
    void setValue (float v) noexcept { value = std::clamp (v, 0.0f, 1.0f); }
    void setBypassed (bool b) noexcept { bypassed = b; }
    float current() const noexcept { return bypassed ? 0.0f : value; }
    void process (float* out, int numSamples) const noexcept { std::fill (out, out + numSamples, current()); }
private:
    float value = 0.5f;
    bool bypassed = false;
};

// ===================================================================================================
// Routing of one destination parameter
//   Connection <src>:<out> -> <node>:<Param>ModIn<i>         => slot i connected (i = source index)
//   props <Param>ModAmount<i> (-1..1), <Param>ModBiPolar<i>, <Param>DepthModRange<i>
//   Connection <depthSrc>:<out> -> <node>:<Param>ModDepthIn<i> => depth source of slot i (any source, usually a macro)
// ===================================================================================================
struct ModSlotParams
{
    bool connected = false;
    float amount = 0.0f;
    bool bipolar = false;
    float depthRange = 0.0f;
    uint8_t depthSources = 0;   // bit k set: source k is connected to <Param>ModDepthIn<i> (several sources sum, measured)
};

struct ModTarget
{
    std::array<ModSlotParams, mod::kNumSources> slot {};

    bool active() const noexcept
    {
        for (auto& s : slot) if (s.connected) return true;
        return false;
    }

    // base: normalised parameter value (host/automation), src: the 4 source values as seen by this destination.
    float compute (float base, const float* src) const noexcept
    {
        float u = base;
        for (int i = 0; i < mod::kNumSources; ++i)
        {
            const auto& s = slot[(size_t) i];
            if (! s.connected) continue;
            float depth = s.amount;
            if (s.depthSources != 0)
            {
                float d = 0.0f;   // depth sources are always used unipolar (0..1); a bypassed source contributes 0
                for (int k = 0; k < mod::kNumSources; ++k)
                    if (s.depthSources & (1u << k)) d += src[k];
                depth += s.depthRange * d;
            }
            const float v = s.bipolar ? 2.0f * src[i] - 1.0f : src[i];
            u += depth * v;
        }
        return std::clamp (u, 0.0f, 1.0f);
    }
};

// ===================================================================================================
// ModBus: history of the 4 per-sample source signals so that a destination at graph latency D reads
// src(t - D) (Thermal's latency compensation applies to modulation connections too).
// Usage per host block:  bus.beginBlock(n); fill bus.write(k) for k = 0..3 with n samples; then
//   bus.valuesAt(i, D, out4) gives the 4 source values at block sample i for latency D.
// ===================================================================================================
class ModBus
{
public:
    static constexpr int kMaxDelay = 128;

    void prepare (int maxBlockSize)
    {
        size = 1;
        while (size < maxBlockSize + kMaxDelay + 1) size <<= 1;
        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        writePos = 0; blockStart = 0;
    }

    // write the n samples of source k for the current block (k = 0..3)
    void writeBlock (int k, const float* x, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            buf[(size_t) k][(size_t) ((writePos + i) & (size - 1))] = x[i];
    }

    void endBlock (int n) noexcept
    {
        blockStart = writePos;
        writePos = (writePos + n) & (size - 1);
    }

    // value of source k at sample i of the block that was just written, delayed by D samples
    float value (int k, int i, int D) const noexcept
    {
        return buf[(size_t) k][(size_t) ((blockStart + i - D) & (size - 1))];
    }

    void values (int i, int D, float* out4) const noexcept
    {
        for (int k = 0; k < mod::kNumSources; ++k) out4[k] = value (k, i, D);
    }

private:
    std::array<std::vector<float>, mod::kNumSources> buf;
    int size = 0, writePos = 0, blockStart = 0;
};

// Graph latency (samples) at which each node sees the modulation signals (= audio latency before the node).
// Measured: InputBus, Stage1 splitter, Stage1 DustDisto: 0; nodes after the Stage1 DustDisto (stage-1 effect,
// MidSide, GainBus, Tone) and the Stage2 splitter/DustDisto: 31; after Stage2 DustDisto ... Stage3 DustDisto: 62;
// after Stage3 DustDisto (stage-3 effect..tone), Slot1, Slot2, MasterCompressor, OutputBus: 93.
namespace mod
{
    inline constexpr int kDelayPreStage1Disto = 0;
    inline constexpr int kDelayStage1 = 31;
    inline constexpr int kDelayStage2 = 62;
    inline constexpr int kDelayStage3AndMaster = 93;
    inline constexpr int kChunk = 32;   // chunk size for the per-chunk destinations (chunks restart every host block)
}

} // namespace dt

#if defined (__clang__) || defined (__GNUC__)
 #pragma GCC diagnostic pop
#endif
