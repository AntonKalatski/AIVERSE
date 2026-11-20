// SpectraFX — genre-independent, read-only ECS/presentation cue translation.
//
// A native game module publishes cue records at an explicit frame boundary.
// SpectraFxCueBridge resolves the game-defined cue ID to an effect and sends
// commands to SpectraFxCommandQueue. No ECS mutation, genre or renderer coupling.
//
// A game may map SimEvent -> SpectraFxCue via translate() in its presentation
// adapter; the bridge intentionally does not assume a module's SimEvent type IDs.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "SpectraFxCommandQueue.h"

namespace aiverse::fx {

// A cue is a transient presentation record, never authoritative gameplay state.
// Stable instance keys must incorporate entity generation (and cue slot), not
// merely a recyclable entity index. key == 0 is a one-shot.
struct SpectraFxCue {
    enum class Action : uint8_t { Play, Move, Stop, Kill };
    uint32_t id = 0;                  // host-defined cue ID
    Action action = Action::Play;
    uint64_t key = 0;                 // host-defined generation-safe instance key
    float3 position{};
    float3 direction{0, 1, 0};
    float scale = 1;
    float4 tint{1, 1, 1, 1};
};

struct SpectraFxBinding {
    std::string effect;
    float scale = 1;
    float4 tint{1, 1, 1, 1};
    bool enabled = true;
};

// Per-GameModule bindings can be loaded from authored assets, but are not
// hard-coded to FPS, RTS or any cue enumeration.
class SpectraFxCueBridge {
public:
    struct Stats {
        uint64_t received = 0;
        uint64_t resolved = 0;
        uint64_t unbound = 0;
        uint64_t invalid = 0;
        uint64_t rejected = 0;       // downstream queue capacity reached
    };

    void bind(uint32_t id, SpectraFxBinding binding) {
        bindings_[id] = std::move(binding);
    }
    void unbind(uint32_t id) { bindings_.erase(id); }
    void clearBindings() { bindings_.clear(); }
    const SpectraFxBinding* binding(uint32_t id) const {
        const auto it = bindings_.find(id);
        return it == bindings_.end() ? nullptr : &it->second;
    }

    // Non-Play cues operate on existing keyed instances even if their original
    // binding was removed: an effect still needs to stop after hot-reload.
    bool dispatch(const SpectraFxCue& cue, SpectraFxCommandQueue& queue) {
        ++stats_.received;
        bool accepted = false;
        if (cue.action == SpectraFxCue::Action::Play) {
            const SpectraFxBinding* b = binding(cue.id);
            if (!b || !b->enabled || b->effect.empty()) {
                ++stats_.unbound;
                return false;
            }
            accepted = queue.play(b->effect, cue.position, cue.key,
                                  cue.direction, b->scale * cue.scale,
                                  {b->tint.x * cue.tint.x, b->tint.y * cue.tint.y,
                                   b->tint.z * cue.tint.z, b->tint.w * cue.tint.w});
        } else {
            if (cue.key == 0) {
                ++stats_.invalid;
                return false;
            }
            switch (cue.action) {
            case SpectraFxCue::Action::Move:
                accepted = queue.move(cue.key, cue.position, cue.direction);
                break;
            case SpectraFxCue::Action::Stop:
                accepted = queue.stop(cue.key, false);
                break;
            case SpectraFxCue::Action::Kill:
                accepted = queue.stop(cue.key, true);
                break;
            case SpectraFxCue::Action::Play:
                break;
            }
        }
        if (accepted) ++stats_.resolved;
        else ++stats_.rejected;
        return accepted;
    }

    // A caller snapshots/extracts immutable cues after the authoritative tick,
    // sorts producer streams if needed, and passes them here in stable order.
    template<class Range>
    void dispatchAll(const Range& cues, SpectraFxCommandQueue& queue) {
        for (const auto& cue : cues) dispatch(cue, queue);
    }

    // Adapt game-defined events to reusable cues without changing the module's
    // SimEvent format. Return false to ignore an event; no pointer to ECS
    // storage is retained beyond this call.
    template<class EventRange, class Translator>
    void translate(const EventRange& events, Translator&& translator,
                   SpectraFxCommandQueue& queue) {
        for (const auto& event : events) {
            SpectraFxCue cue;
            if (translator(event, cue)) dispatch(cue, queue);
        }
    }

    const Stats& stats() const { return stats_; }
    void resetStats() { stats_ = {}; }

private:
    std::unordered_map<uint32_t, SpectraFxBinding> bindings_;
    Stats stats_{};
};

} // namespace aiverse::fx
