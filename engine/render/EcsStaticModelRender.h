#pragma once
// Universal presentation adapter for immutable ECS renderables.
//
// Authoritative ECS components carry stable asset keys and integer revisions,
// not Model pointers, GPU handles, float transforms, or backend state.
// Call this adapter after the SceneHierarchy update barrier, from a render
// thread that owns Renderer3D. It never mutates the ECS World or simulation.
#include "../core/ECS.h"
#include "../sim/CommonComponents.h"
#include "Renderer3D.h"
#include "SceneHierarchyRender.h"
#include <cstddef>
#include <cstdint>
#include <utility>

namespace aiverse::render {

// Optional presentation component. Presence makes an Entity eligible for a
// single static model draw; no FPS/RTS/vehicle-specific concepts are involved.
// A zero asset key or zero appearance revision means "not ready to submit".
// A host that mutates arbitrary native-only appearance data must increment
// appearanceRevision; Renderer3D checks Model geometry and material GPU state.
struct EcsStaticModelRender {
    uint32_t assetKey = 0;
    uint64_t appearanceRevision = 1;
    uint32_t flags = 0;
    bool visible = true;
};

// The host owns stable namespaced draw IDs. A raw Entity::packed() is not
// sufficient when one Renderer3D renders multiple Worlds or several draws
// per Entity. The key callback must return a nonzero unique identifier.
// The asset resolver must return a live Model (or nullptr when streaming).
// Submit is a presentation callback, permitting a mock sink in CPU-only tests
// or custom per-project presentation hooks without linking RTS/FPS modules.
template<class AssetResolver, class StableKey, class Submit>
size_t visitEcsStaticModels(const aiverse::World& world,
                            AssetResolver&& resolve,
                            StableKey&& stableKey,
                            Submit&& submit,
                            aiverse::Vec3 renderOrigin = {}) {
    size_t submitted = 0;
    world.eachExistingConst<EcsStaticModelRender>(
        [&](aiverse::Entity entity, const EcsStaticModelRender& view) {
            if (!view.visible || !view.assetKey || !view.appearanceRevision)
                return;
            const auto* xf = world.tryGetConst<aiverse::WorldTransform>(entity);
            if (!xf) return; // without the hierarchy-derived transform: not ready
            const Model* model = resolve(view.assetKey);
            if (!model) return;
            const uint64_t id = stableKey(entity);
            if (!id) return;
            ModelDraw draw{};
            draw.model = model;
            draw.world = hierarchyMatrix(*xf, renderOrigin);
            draw.persistentId = id;
            draw.staticPacketRevision = view.appearanceRevision;
            draw.flags = kDrawStatic | view.flags;
            submit(entity, draw);
            ++submitted;
        });
    return submitted;
}

// Public native/project-facing integration. Render only: simulation tick,
// authoritative serialization and deterministic hashes are untouched.
template<class AssetResolver, class StableKey>
size_t drawEcsStaticModels(const aiverse::World& world,
                           Renderer3D& renderer,
                           AssetResolver&& resolve,
                           StableKey&& stableKey,
                           aiverse::Vec3 renderOrigin = {}) {
    return visitEcsStaticModels(world,
        std::forward<AssetResolver>(resolve),
        std::forward<StableKey>(stableKey),
        [&renderer](aiverse::Entity, const ModelDraw& draw) {
            renderer.drawModel(draw);
        }, renderOrigin);
}

// Call on the presentation thread before an ECS Entity is removed, its
// asset key changes to a different Model, or its stable identity is reused.
// This is not a simulation system: renderer caches never enter rollback state.
template<class StableKey>
void invalidateEcsStaticModel(Renderer3D& renderer,
                              aiverse::Entity entity,
                              StableKey&& stableKey) {
    renderer.invalidateStaticModelCache(stableKey(entity));
}

} // namespace aiverse::render
