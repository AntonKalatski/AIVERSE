// Host several independent deterministic Simulations in one process.
// This is NOT a second entity namespace inside the same ECS World: each
// Simulation already owns its World, tick, RNG, commands and snapshots.
#pragma once

#include "engine/sim/Simulation.h"
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace aiverse::scene {

struct WorldHandle {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;
    constexpr bool valid() const { return index != UINT32_MAX && generation != 0; }
    constexpr bool operator==(const WorldHandle&) const = default;
};

// Raw Entity IDs may collide across ECS Worlds. All cross-world references
// must carry a WorldHandle and be resolved through the owning host.
struct WorldEntityRef {
    WorldHandle world{};
    Entity entity = kNullEntity;
    constexpr bool operator==(const WorldEntityRef&) const = default;
};

class SimulationHost {
public:
    WorldHandle insert(std::unique_ptr<Simulation> simulation) {
        if(!simulation) return {};
        if(!free_.empty()) {
            const uint32_t index = free_.back();
            free_.pop_back();
            Slot& slot = slots_[index];
            slot.simulation = std::move(simulation);
            slot.paused = false;
            return {index, slot.generation};
        }
        if(slots_.size() >= uint64_t(UINT32_MAX)) return {};
        const uint32_t index = uint32_t(slots_.size());
        slots_.push_back(Slot{1, std::move(simulation), false});
        return {index, 1};
    }

    Simulation* get(WorldHandle handle) {
        if(!handle.valid() || handle.index >= slots_.size()) return nullptr;
        Slot& slot = slots_[handle.index];
        return slot.generation == handle.generation ? slot.simulation.get() : nullptr;
    }
    const Simulation* get(WorldHandle handle) const {
        if(!handle.valid() || handle.index >= slots_.size()) return nullptr;
        const Slot& slot = slots_[handle.index];
        return slot.generation == handle.generation ? slot.simulation.get() : nullptr;
    }
    bool contains(WorldHandle handle) const { return get(handle) != nullptr; }
    bool alive(WorldEntityRef ref) const {
        const Simulation* simulation = get(ref.world);
        return simulation && simulation->world().alive(ref.entity);
    }

    // Ownership is exclusive. Removing a world destroys its Simulation and
    // invalidates every WorldEntityRef pointing into that incarnation.
    bool erase(WorldHandle handle) {
        if(!get(handle)) return false;
        Slot& slot = slots_[handle.index];
        slot.simulation.reset();
        slot.paused = false;
        if(slot.generation != std::numeric_limits<uint32_t>::max()) {
            ++slot.generation;
            free_.push_back(handle.index);
        } // Retire a slot at generation wrap rather than alias stale handles.
        return true;
    }

    bool setPaused(WorldHandle handle, bool paused) {
        if(!get(handle)) return false;
        slots_[handle.index].paused = paused;
        return true;
    }
    bool isPaused(WorldHandle handle) const {
        return contains(handle) && slots_[handle.index].paused;
    }
    size_t size() const {
        size_t count = 0;
        for(const Slot& slot : slots_) if(slot.simulation) ++count;
        return count;
    }

    // Deterministic slot order; independent fixed ticks. Run on the owner
    // thread, not while worlds are being inserted/erased from callbacks.
    // Per-Simulation readyToStep() may independently block one world.
    void stepAll() {
        for(Slot& slot : slots_)
            if(slot.simulation && !slot.paused) slot.simulation->step();
    }
    void advanceAll(double realSeconds, int maxTicks = 8) {
        for(Slot& slot : slots_)
            if(slot.simulation && !slot.paused)
                slot.simulation->advance(realSeconds, maxTicks);
    }

private:
    struct Slot {
        uint32_t generation = 1;
        std::unique_ptr<Simulation> simulation;
        bool paused = false;
    };
    std::vector<Slot> slots_;
    std::vector<uint32_t> free_;
};

} // namespace aiverse::scene
