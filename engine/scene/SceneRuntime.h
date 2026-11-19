// Scene ownership and additive/replacement lifecycle for a single ECS World.
// A scene is a set of ECS entities, NOT another World/Simulation.
// All structural operations are caller-thread tick-barrier operations.
#pragma once

#include "engine/core/Archive.h"
#include "engine/sim/SceneHierarchy.h"
#include "engine/sim/Simulation.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::scene {

struct SceneId {
    uint64_t value = 0;
    constexpr bool valid() const { return value != 0; }
    constexpr bool operator==(const SceneId&) const = default;
};

// Authoritative component: entities without SceneOwner belong to the host,
// not to any unloadable scene. Persistent across scenes by design.
struct SceneOwner {
    SceneId scene{};
};

enum class LoadMode : uint8_t { Additive = 0, Replace = 1 };
enum class ScenePhase : uint8_t { Loading = 0, Active = 1 };

struct SceneRecord {
    std::string asset; // Stable logical scene asset key, not a native filesystem pointer.
    ScenePhase phase = ScenePhase::Loading;
    bool persistent = false; // Survives Replace; can still be explicitly unloaded.
};

// Authoritative World resource. Snapshots/COW copy this with the ECS World.
// Register both SceneOwner and SceneState for Simulation binary persistence.
struct SceneState {
    uint64_t nextId = 1;             // Monotonic: a stale SceneId never becomes valid again.
    uint64_t pendingReplace = 0;     // Exactly one pending replacement per ECS World.
    std::map<uint64_t, SceneRecord> scenes; // Canonical iteration order.
};

inline void io(Archive& a, SceneId& id) { a(id.value); }
inline void io(Archive& a, SceneOwner& owner) { a(owner.scene); }
inline void io(Archive& a, SceneRecord& record) {
    a(record.asset, record.phase, record.persistent);
    if(record.asset.empty() || record.asset.size() > 512 ||
       (record.phase != ScenePhase::Loading && record.phase != ScenePhase::Active))
        a.fail();
}
inline void io(Archive& a, SceneState& state) {
    a(state.nextId, state.pendingReplace, state.scenes);
    if(!a.ok() || state.nextId == 0 || state.scenes.size() > 4096) {
        a.fail();
        return;
    }
    for(const auto& [id, record] : state.scenes) {
        if(id == 0 || id >= state.nextId || record.asset.empty() ||
           record.asset.size() > 512 ||
           (record.phase != ScenePhase::Loading && record.phase != ScenePhase::Active)) {
            a.fail();
            return;
        }
    }
    if(state.pendingReplace != 0) {
        const auto it = state.scenes.find(state.pendingReplace);
        if(it == state.scenes.end() || it->second.phase != ScenePhase::Loading ||
           it->second.persistent)
            a.fail();
    }
}

// Call inside GameModule::setup, before Simulation seals its registry.
// No schema changes are made to existing game modules or legacy saves.
inline void registerScenePersistence(Simulation& simulation) {
    simulation.registerPersistentComponent<SceneOwner>("aiverse.scene.owner", 1);
    simulation.registerPersistentResource<SceneState>("aiverse.scene.state", 1);
}

class SceneRuntime {
public:
    explicit SceneRuntime(World& world) : world_(world) {}

    // Registers a logical scene, but does not synchronously load asset bytes.
    // The host/asset worker populates entities and calls activate() at a tick barrier.
    // A second concurrent Replace request is rejected; cancel the first explicitly.
    SceneId beginLoad(std::string asset, LoadMode mode = LoadMode::Additive,
                      bool persistent = false) {
        if(asset.empty() || asset.size() > 512 ||
           (mode == LoadMode::Replace && persistent))
            return {};
        auto& state = mutableState();
        if(state.scenes.size() >= 4096 ||
           state.nextId == std::numeric_limits<uint64_t>::max() ||
           (mode == LoadMode::Replace && state.pendingReplace != 0))
            return {};
        const uint64_t id = state.nextId;
        state.scenes.emplace(id, SceneRecord{std::move(asset), ScenePhase::Loading, persistent});
        ++state.nextId;
        if(mode == LoadMode::Replace) state.pendingReplace = id;
        return {id};
    }

    // Create or adopt dynamic entities while the scene is loading or active.
    // SceneId and Entity are local to this World.
    Entity createEntity(SceneId id) {
        if(!record(id)) return kNullEntity;
        Entity entity = world_.create();
        try { world_.add<SceneOwner>(entity, SceneOwner{id}); }
        catch(...) { world_.destroy(entity); throw; }
        return entity;
    }
    bool adopt(SceneId id, Entity entity) {
        if(!record(id) || !world_.alive(entity) || world_.has<SceneOwner>(entity))
            return false;
        world_.add<SceneOwner>(entity, SceneOwner{id});
        return true;
    }
    bool transfer(Entity entity, SceneId destination) {
        if(!record(destination) || !world_.alive(entity) ||
           !world_.has<SceneOwner>(entity))
            return false;
        world_.add<SceneOwner>(entity, SceneOwner{destination});
        return true;
    }
    SceneId ownerOf(Entity entity) const {
        const auto* owner = world_.tryGetConst<SceneOwner>(entity);
        return owner ? owner->scene : SceneId{};
    }

    // Commit an asynchronously prepared scene at a fixed tick barrier.
    // Replace keeps persistent scenes, destroying all other scene-owned entities
    // only AFTER the replacement has reached the ready-to-activate barrier.
    // If a SceneHierarchy is installed, pass it to detach tracked nodes safely.
    bool activate(SceneId id, SceneHierarchy* hierarchy = nullptr) {
        const auto* current = record(id);
        if(!current || current->phase != ScenePhase::Loading) return false;
        const auto* state = stateView();
        const bool replacing = state && state->pendingReplace == id.value;
        if(replacing) {
            std::vector<SceneId> toUnload;
            for(const auto& [other, scene] : state->scenes) {
                if(other != id.value && !scene.persistent)
                    toUnload.push_back({other});
            }
            for(SceneId old : toUnload) unload(old, hierarchy);
        }
        auto& stateMut = mutableState();
        const auto it = stateMut.scenes.find(id.value);
        if(it == stateMut.scenes.end()) return false;
        it->second.phase = ScenePhase::Active;
        if(stateMut.pendingReplace == id.value) stateMut.pendingReplace = 0;
        return true;
    }

    // Abort an uncommitted load. A late async completion cannot reuse its ID.
    bool cancel(SceneId id, SceneHierarchy* hierarchy = nullptr) {
        const auto* current = record(id);
        return current && current->phase == ScenePhase::Loading && unload(id, hierarchy);
    }

    // Destruction is deferred by the caller until a structural/tick barrier:
    // do not invoke inside an ECS each() or concurrent job. Foreign children
    // of removed parents are reparented to roots by SceneHierarchy::detach().
    bool unload(SceneId id, SceneHierarchy* hierarchy = nullptr) {
        if(!record(id)) return false;
        std::vector<Entity> victims;
        world_.eachExistingConst<SceneOwner>([&](Entity entity, const SceneOwner& owner) {
            if(owner.scene == id && world_.alive(entity))
                victims.push_back(entity);
        });
        std::sort(victims.begin(), victims.end(), [](Entity a, Entity b) {
            return a.index != b.index ? a.index < b.index : a.gen < b.gen;
        });
        if(hierarchy) {
            for(Entity entity : victims)
                if(hierarchy->contains(entity)) hierarchy->detach(world_, entity);
        }
        for(Entity entity : victims) world_.destroy(entity);

        auto& state = mutableState();
        state.scenes.erase(id.value);
        if(state.pendingReplace == id.value) state.pendingReplace = 0;
        return true;
    }

    const SceneRecord* record(SceneId id) const {
        if(!id.valid()) return nullptr;
        const auto* state = stateView();
        if(!state) return nullptr;
        const auto it = state->scenes.find(id.value);
        return it == state->scenes.end() ? nullptr : &it->second;
    }
    size_t count() const {
        const auto* state = stateView();
        return state ? state->scenes.size() : 0;
    }
    const SceneState* stateView() const {
        return static_cast<const World&>(world_).resourceConst<SceneState>();
    }

private:
    SceneState& mutableState() {
        if(auto* value = world_.resource<SceneState>()) return *value;
        return world_.setResource<SceneState>();
    }
    World& world_;
};

} // namespace aiverse::scene
