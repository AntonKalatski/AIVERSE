// SpectraFX mapping for the engine's actual SimEvent presentation stream.
// This is an optional game-owned adapter, not a new gameplay authority.
// Bind arbitrary event types to cue IDs; default one-shot is safe for an
// event without a persistent generation-safe instance key.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <utility>

#include "SpectraFxSimulationBridge.h"

namespace aiverse::fx {

struct SpectraFxEventRule {
    uint32_t cueId = 0;
    SpectraFxCue::Action action = SpectraFxCue::Action::Play;
    float scale = 1;
    float4 tint{1, 1, 1, 1};
};

// This mapping is module-agnostic: ev.type is defined by the originating
// GameModule. KeyFn decides which effects are persistent. For ordinary
// one-shots it may return 0; for attached effects it MUST return a nonzero,
// generation-safe key unique to the simulation/session + cue slot.
// IMPORTANT: never derive a persistent key from a recyclable index alone.
class SpectraFxEventMap {
public:
    void bind(uint16_t eventType, SpectraFxEventRule rule) {
        rules_[eventType] = std::move(rule);
    }
    void unbind(uint16_t eventType) { rules_.erase(eventType); }
    void clear() { rules_.clear(); }
    const SpectraFxEventRule* rule(uint16_t eventType) const {
        auto it = rules_.find(eventType);
        return it == rules_.end() ? nullptr : &it->second;
    }

    // KeyFn: uint64_t(const SimEvent&, uint32_t cueId).
    // The mapping itself is read-only, and all conversion is float-only for
    // presentation. This method does not change Simulation or its event data.
    template<class KeyFn>
    bool translate(const aiverse::SimEvent& ev, SpectraFxCue& out,
                   KeyFn&& keyFor) const {
        const SpectraFxEventRule* found = rule(ev.type);
        if (!found) return false;
        const uint64_t key = keyFor(ev, found->cueId);
        if (found->action != SpectraFxCue::Action::Play && key == 0)
            return false;
        out = {};
        out.id = found->cueId;
        out.action = found->action;
        out.key = key;
        out.position = spectraFxPosition(ev.pos);
        out.scale = found->scale;
        out.tint = found->tint;
        return true;
    }

    // Convenience: only map one-shot Play events. Explicitly reject Move /
    // Stop / Kill without a generation-safe key provider.
    bool translateOneShot(const aiverse::SimEvent& ev, SpectraFxCue& out) const {
        return translate(ev, out,
            [](const aiverse::SimEvent&, uint32_t) -> uint64_t { return 0; });
    }

private:
    std::unordered_map<uint16_t, SpectraFxEventRule> rules_;
};

// Bind effects to a read-only collector of real Simulation events, avoiding
// duplicate per-game data plumbing. The caller must retain map, bridge and
// queue until the registered Simulation is destroyed.
inline void installSpectraFxEventMap(
        aiverse::Simulation& sim, const SpectraFxEventMap& map,
        SpectraFxCueBridge& bridge, SpectraFxCommandQueue& queue) {
    installSpectraFxCollector(sim, bridge, queue,
        [&map](const aiverse::SimEvent& ev, SpectraFxCue& cue) {
            return map.translateOneShot(ev, cue);
        });
}

} // namespace aiverse::fx
