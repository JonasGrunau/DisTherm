#pragma once

#include "Params.h"
#include "mod/Modulation.h"

#include <array>
#include <atomic>
#include <cstring>

namespace dt
{

// Everything about modulation that isn't a host parameter (Thermal keeps it in node properties and graph
// connections): the four mod slots of every parameter and the two envelope shapes. Edited on the message
// thread (editor, preset import), read by the audio thread through ModStateChannel.
struct ModState
{
    // targets[param].slot[source], source 0 = Mod 1, 1 = Mod 2, 2 = Macro 1, 3 = Macro 2 (Thermal's ModIn index).
    std::array<ModTarget, kNumParams> targets {};

    struct Shape
    {
        std::array<mod::EnvPoint, mod::kMaxPoints> points {};
        int numPoints = 2;
    };
    std::array<Shape, 2> envelopes {};

    ModState()
    {
        const auto def = ModEnvelope::defaultPoints();
        for (auto& e : envelopes)
        {
            e.numPoints = (int) def.size();
            for (size_t i = 0; i < def.size(); ++i) e.points[i] = def[i];
        }
    }
};

// Single writer (message thread), single reader (audio thread), no locks: a sequence counter around the
// writer's copy (odd while writing). The reader copies only when the sequence moved, and keeps its previous
// copy if a write was in progress or overlapped the copy; it tries again next block.
class ModStateChannel
{
public:
    void publish (const ModState& s) noexcept
    {
        seq.fetch_add (1, std::memory_order_acq_rel);   // odd: writing
        std::atomic_thread_fence (std::memory_order_release);
        std::memcpy (static_cast<void*> (&shared), &s, sizeof (ModState));
        std::atomic_thread_fence (std::memory_order_release);
        seq.fetch_add (1, std::memory_order_acq_rel);   // even: done
    }

    // True if `out` was updated.
    bool fetch (ModState& out, unsigned& lastSeen) const noexcept
    {
        const unsigned a = seq.load (std::memory_order_acquire);
        if ((a & 1u) != 0 || a == lastSeen) return false;
        std::atomic_thread_fence (std::memory_order_acquire);
        std::memcpy (static_cast<void*> (&scratch), &shared, sizeof (ModState));
        std::atomic_thread_fence (std::memory_order_acquire);
        if (seq.load (std::memory_order_acquire) != a) return false;
        std::memcpy (static_cast<void*> (&out), &scratch, sizeof (ModState));
        lastSeen = a;
        return true;
    }

private:
    std::atomic<unsigned> seq { 0 };
    ModState shared;
    mutable ModState scratch;
};

} // namespace dt
