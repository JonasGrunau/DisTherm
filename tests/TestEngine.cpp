#include "engine/Engine.h"
#include "model/ModState.h"
#include "model/Names.h"
#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

using namespace dt;
using namespace dt::testutil;

namespace
{
    constexpr double kFs = 48000.0;
    constexpr int kBlock = 512;
    constexpr int kStride = pid::s2_lo - pid::s1_lo;
    constexpr int kSwitchStride = pid::s2_on - pid::s1_on;

    void setNorm (EngineParams& p, int id, double u) { p.plain[(size_t) id] = (float) toPlain (kParams[id], u); }

    // Everything that colours the signal switched off, as Thermal's isolated measurement state.
    EngineParams isolated (bool stagesOn)
    {
        EngineParams p;
        for (int s = 0; s < 3; ++s)
        {
            p.plain[(size_t) (pid::s1_on + s * kSwitchStride)] = stagesOn ? 1.f : 0.f;
            p.plain[(size_t) (pid::s1_disto_on + s * kSwitchStride)] = 0.f;
            p.plain[(size_t) (pid::s1_split + s * kStride)] = 0.f;   // MultiBand off: the full band through the stage
        }
        p.plain[pid::mc_on] = 0.f;
        p.plain[pid::filters_on] = 0.f;
        return p;
    }

    Stereo render (Engine& e, const Stereo& in, const ModStateChannel* mod = nullptr)
    {
        if (mod != nullptr) e.setModChannel (mod);
        Stereo out = in;
        Transport t;
        t.playing = true;
        t.hasTime = true;
        for (size_t i0 = 0; i0 < in.size(); i0 += kBlock)
        {
            const int n = (int) std::min ((size_t) kBlock, in.size() - i0);
            e.process (out.left.data() + i0, out.right.data() + i0, n, t);
            t.timeInSamples += n;
        }
        return out;
    }

    Stereo noiseStereo (size_t n, double amp = 0.3)
    {
        return { noise (n, 1, amp), noise (n, 2, amp) };
    }

    // Whole-sample shift (testutil::delayed is a fractional FFT delay, not bit exact).
    Signal shifted (const Signal& x, size_t d)
    {
        Signal y (x.size(), 0.f);
        for (size_t i = d; i < x.size(); ++i) y[i] = x[i - d];
        return y;
    }

} // namespace

TEST_CASE ("Engine: the latency is Thermal's 93 samples (31 per stage)", "[engine]")
{
    Engine e;
    CHECK (e.getLatencySamples() == 93);
}

TEST_CASE ("Engine: with every colouring node off the chain is a pure 93-sample delay, bit for bit", "[engine][rest]")
{
    const bool stagesOn = GENERATE (false, true);
    INFO ("stages " << (stagesOn ? "on (MultiBand off, distortion bypassed)" : "skipped"));
    Engine e;
    e.setParams (isolated (stagesOn));
    e.prepare (kFs, kBlock);
    const auto in = noiseStereo (24000);
    const auto out = render (e, in);
    // Thermal fades its input and output gains in from 0 over 5 ms after loading; after that it is exact.
    CHECK (bitIdentical (out.left, shifted (in.left, 93), 2000));
    CHECK (bitIdentical (out.right, shifted (in.right, 93), 2000));
}

TEST_CASE ("Engine: the master dry/wet mixes with Thermal's v^(5/7) law", "[engine]")
{
    auto p = isolated (false);
    p.plain[pid::mix] = 0.5f;
    Engine e;
    e.setParams (p);
    e.prepare (kFs, kBlock);
    const auto in = noiseStereo (24000);
    const auto out = render (e, in);
    // dry and wet are the same signal here: out = (0.5^(5/7) + 0.5^(5/7)) * x
    const double expect = 2.0 * std::pow (0.5, 5.0 / 7.0);
    const double ratio = rms (out.left, 4000) / rms (shifted (in.left, 93), 4000);
    INFO ("gain " << ratio << ", expected " << expect);
    CHECK (std::abs (ratio - expect) < 1e-4);
}

TEST_CASE ("Engine: the default patch (Thermal's default state) makes finite, audible output", "[engine]")
{
    Engine e;
    e.setParams (EngineParams {});
    e.prepare (kFs, kBlock);
    const auto loop = psyLoop (kFs, 2.0);
    const auto out = render (e, loop);
    CHECK (allFinite (out.left));
    CHECK (allFinite (out.right));
    CHECK (rms (out.left, 24000) > 0.01);
}

TEST_CASE ("Engine: every effect in every slot runs finite", "[engine][fx]")
{
    const int slot = GENERATE (0, 1, 2, 3, 4);
    const int fx = GENERATE (range (0, names::kNumEffects));
    INFO ("slot " << slot << ", effect " << names::kEffects[fx]);
    auto p = isolated (true);
    if (slot < 3)
    {
        p.plain[(size_t) (pid::s1_fx_type + slot * kSwitchStride)] = (float) fx;
        p.plain[(size_t) (pid::s1_fx_on + slot * kSwitchStride)] = 1.f;
    }
    else
    {
        p.plain[slot == 3 ? pid::fx1_type : pid::fx2_type] = (float) fx;
        p.plain[slot == 3 ? pid::fx1_on : pid::fx2_on] = 1.f;
    }
    Engine e;
    e.setParams (p);
    e.prepare (kFs, kBlock);
    const auto out = render (e, noiseStereo (48000));
    CHECK (allFinite (out.left));
    CHECK (allFinite (out.right));
    CHECK (peakAbs (out.left) < 50.0);
}

TEST_CASE ("Engine: every distortion type stays finite at full drive with full feedback", "[engine][disto]")
{
    const int type = GENERATE (range (0, names::kNumDistortionTypes));
    INFO (names::kDistortionTypes[type]);
    EngineParams p;
    p.plain[pid::s1_type] = (float) type;
    p.plain[pid::s1_drive] = 36.f;
    p.plain[pid::s1_fb] = 1.f;
    p.plain[pid::s1_follow] = 0.f;
    for (int c : { pid::s1_shape, pid::s1_depth, pid::s1_freq, pid::s1_clip }) p.plain[(size_t) c] = 0.7f;
    Engine e;
    e.setParams (p);
    e.prepare (kFs, kBlock);
    const auto out = render (e, noiseStereo (48000, 0.5));
    CHECK (allFinite (out.left));
    CHECK (allFinite (out.right));
}

TEST_CASE ("Engine: modulation follows Thermal's routing formula (depth scaled by a macro)", "[engine][mod]")
{
    // u = clamp (base + (amount + range * macro2) * macro1, 0, 1) on stage 1's gain (-60..+6 dB over u),
    // measured in Thermal to 1.8e-7. A DC input reads the gain straight from the output.
    auto p = isolated (true);
    setNorm (p, pid::s1_gain, 0.5);          // -27 dB
    p.plain[pid::macro1] = 0.8f;
    p.plain[pid::macro2] = 0.6f;
    ModState m;
    auto& slot = m.targets[pid::s1_gain].slot[2];   // Macro 1
    slot.connected = true;
    slot.amount = 0.1f;
    slot.depthRange = 0.25f;
    slot.depthSources = 1u << 3;   // Macro 2 scales the depth
    ModStateChannel ch;
    ch.publish (m);

    Engine e;
    e.setParams (p);
    e.prepare (kFs, kBlock);
    Stereo dc { Signal (24000, 0.25f), Signal (24000, 0.25f) };
    const auto out = render (e, dc, &ch);
    const double u = 0.5 + (0.1 + 0.25 * 0.6) * 0.8;
    const double expectDb = -60.0 + 66.0 * u;
    const double gotDb = db (out.left.back() / 0.25);
    INFO ("gain " << gotDb << " dB, expected " << expectDb << " dB");
    CHECK (std::abs (gotDb - expectDb) < 1e-3);
}
