// Optional authoritative content-identity binding for host-side collision
// namespaces. Range leases and CollisionStreams are non-authoritative, but
// the canonical layout fingerprint participates in native v10 saves/checksum
// and must match after rollback/cold load before physics is resumed.
#pragma once
#include "engine/scene/SceneCollisionNamespaces.h"
#include "engine/sim/Simulation.h"
#include <string>
#include <utility>

namespace aiverse::scene {

struct CollisionNamespaceBinding {
    uint64_t fingerprint=0;
};
inline void io(Archive& archive,CollisionNamespaceBinding& binding) {
    archive(binding.fingerprint);
}

inline bool validateCollisionNamespaces(const World& world,
                                        const SceneCollisionNamespaces& catalog) {
    const auto* bound=world.resourceConst<CollisionNamespaceBinding>();
    return catalog.sealed() && bound &&
           bound->fingerprint==catalog.fingerprint();
}
inline bool bindCollisionNamespaces(World& world,
                                    const SceneCollisionNamespaces& catalog) {
    if(!catalog.sealed())return false;
    const auto* existing=world.resourceConst<CollisionNamespaceBinding>();
    if(existing) return existing->fingerprint==catalog.fingerprint();
    world.setResource<CollisionNamespaceBinding>(
        CollisionNamespaceBinding{catalog.fingerprint()});
    return true;
}

// Call in GameModule::setup BEFORE Simulation seals native schemas.
// A host with more than one physical collision World needs distinct
// stable resource types/IDs per owner; this helper binds one catalog
// for the default SpatialWorld belonging to this Simulation.
inline void registerCollisionNamespacePersistence(
    Simulation& simulation,
    std::string stableId="aiverse.scene.collision-namespaces",
    uint32_t version=1) {
    simulation.registerPersistentResource<CollisionNamespaceBinding>(
        std::move(stableId),version);
}

} // namespace aiverse::scene
