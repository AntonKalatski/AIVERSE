// AIVERSE immutable prefab definitions -> real SceneLoadCoordinator spawn API.
// Intentionally no mutation of SceneAsset, global World state, or immutable T.
// A single catalog is retained by every callback and may be shared across
// additive scenes, editor preview and independent Simulation Worlds.
#pragma once

#include "engine/scene/SceneLoadCoordinator.h"
#include "engine/sim/PrefabStaticCatalog.h"
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::scene {

// A scene may include lights/markers that lack a particular config domain.
// Reject by default; Skip permits optional domains in a composed spawner.
enum class MissingPrefabConfig : uint8_t { Reject = 0, Skip = 1 };

template<class T>
class StaticPrefabSceneBinding {
public:
    using Catalog = storage::PrefabStaticCatalog<T>;
    using Spawn = SceneLoadCoordinator::SpawnPrefab;

    explicit StaticPrefabSceneBinding(
        std::shared_ptr<const Catalog> catalog,
        Spawn afterAttach = {},
        MissingPrefabConfig missing = MissingPrefabConfig::Reject)
      : catalog_(std::move(catalog)), afterAttach_(std::move(afterAttach)),
        missing_(missing) {
        if (!catalog_) throw std::invalid_argument("null prefab static catalog");
    }

    // Call ONCE during GameModule::setup, after registering native
    // persistence. This installs/validates the table and prefab binding
    // fingerprints in the authoritative World before scene instantiation.
    bool bindWorld(World& world) const { return catalog_->bindWorld(world); }

    const Catalog& catalog() const noexcept { return *catalog_; }

    // Compatible with SceneLoadCoordinator's actual SpawnPrefab API.
    // The callback captures immutable catalog lifetime by shared_ptr,
    // so destroying this binding object does not invalidate the spawner.
    // Called only on the Simulation owner thread at a structural barrier.
    Spawn callback() const {
        const auto catalog = catalog_;
        const auto after = afterAttach_;
        const auto missing = missing_;
        return [catalog,after,missing](World& world,Entity entity,
                                     const SceneObjectAsset& asset,
                                     std::string& error) -> bool {
            if (!world.alive(entity)) {
                error = "prefab scene spawn rejected stale Entity";
                return false;
            }
            // Fail rather than silently attaching to a stale content
            // revision after network rollback or cold save/load.
            if (!catalog->bound(world)) {
                error = "prefab static catalog is not bound to this World";
                return false;
            }
            const auto ref = catalog->find(asset.prefab);
            if (!ref.valid()) {
                if (missing == MissingPrefabConfig::Reject) {
                    error = "unknown static prefab: " + asset.prefab;
                    return false;
                }
            } else if (!catalog->attach(world,entity,asset.prefab)) {
                error = "failed to attach immutable prefab config: " + asset.prefab;
                return false;
            }

            // Optional mutable state (position, health, instance overrides)
            // belongs in ordinary ECS components on this new entity.
            // On failure SceneLoadCoordinator destroys every entity
            // belonging to the incomplete new scene.
            if (after && !after(world,entity,asset,error)) {
                if (error.empty()) error = "prefab instance initializer rejected";
                return false;
            }
            return true;
        };
    }

private:
    std::shared_ptr<const Catalog> catalog_;
    Spawn afterAttach_;
    MissingPrefabConfig missing_;
};

// Several independent immutable config domains may coexist on one Entity.
// Sequence is deterministic by registration order, not std::hash order.
// Register the static catalogs in GameModule::setup before creating scenes.
// Every step only mutates the newly created Entity; SceneLoadCoordinator
// owns cleanup if a later domain or an instance initializer fails.
class PrefabSceneSpawnChain {
public:
    using Spawn = SceneLoadCoordinator::SpawnPrefab;

    void add(Spawn stage) {
        if (!stage) throw std::invalid_argument("empty prefab spawn stage");
        stages_.push_back(std::move(stage));
    }
    Spawn callback() const {
        const auto stages=stages_;
        return [stages](World& world,Entity entity,const SceneObjectAsset& asset,
                        std::string& error) -> bool {
            for (const auto& stage : stages) {
                if (!stage(world,entity,asset,error)) {
                    if (error.empty()) error = "prefab stage rejected: " + asset.prefab;
                    return false;
                }
            }
            return true;
        };
    }
    size_t size() const noexcept { return stages_.size(); }

private:
    std::vector<Spawn> stages_;
};

} // namespace aiverse::scene
