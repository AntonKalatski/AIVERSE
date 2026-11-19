// TerraWeave authoritative ECS resource for native games. Compatible with
// Simulation World COW snapshots, native persistence and rollback: all sparse
// mutable gameplay state lives in a persistent World resource, while RHI,
// NavMesh and Box3D are derived/presentation consumers on their owner threads.
// NEREID remains the ONLY authoritative water flow/erosion calculator.
#pragma once
#include "engine/world/TerraWeaveEcsBankBridge.h"
#include "engine/world/TerraWeavePersistence.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct TerraWeaveEcsAuthorityConfig {
    TerrainSettings generator{};
    int32_t soilGridCm=100,bedrockCm=-1000000;
    std::string immutableRecipeId; // exact procedural/asset version identity
};

inline void validateEcsAuthorityConfig(
        const TerraWeaveEcsAuthorityConfig& config){
    if(config.immutableRecipeId.empty() ||
       config.immutableRecipeId.size()>kTerraWeaveRecipeIdMax ||
       config.soilGridCm<10 || config.soilGridCm>10000 ||
       config.generator.basePeriodCm<100 ||
       config.generator.octaves<1 || config.generator.octaves>12 ||
       config.generator.reliefCm<0)
        throw std::invalid_argument("TerraWeave native ECS authority config");
}

struct TerraWeaveAuthorityResource {
    uint32_t version=2;
    uint64_t stateRevision=0; // includes ecology updates even without changing terrain
    std::string immutableRecipeId;
    EarthworksSnapshot earth;
    std::vector<AnchoredPlant> rooted;
    std::vector<GrassPatch> grass;
};

inline void io(Archive& a,TerraWeaveAuthorityResource& state){
    a(state.version);
    if(!a.ok() || state.version!=2u){a.fail();return;}
    a(state.stateRevision);
    save_detail::recipe(a,state.immutableRecipeId);
    a(state.earth.spacingCm,state.earth.bedrockCm,
      state.earth.revision);
    save_detail::array(a,state.earth.heights,1000000u,20);
    save_detail::array(a,state.rooted,RootSupportRegistry::kMaxAnchors,90);
    save_detail::array(a,state.grass,GrassCoverRegistry::kMaxPatches,75);
}

// A new authority world starts with an empty sparse overlay; its background
// procedural height source is immutable config captured by value.
inline TerraWeaveAuthorityResource initialTerraWeaveResource(
        const TerraWeaveEcsAuthorityConfig& cfg){
    validateEcsAuthorityConfig(cfg);
    TerraWeaveAuthorityResource out;
    out.immutableRecipeId=cfg.immutableRecipeId;
    out.earth.spacingCm=cfg.soilGridCm;
    out.earth.bedrockCm=cfg.bedrockCm;
    return out;
}

inline EarthworksLayer materializeEcsTerrain(
        const TerraWeaveAuthorityResource& state,
        const TerraWeaveEcsAuthorityConfig& cfg){
    validateEcsAuthorityConfig(cfg);
    if(state.version!=2u ||
       state.immutableRecipeId!=cfg.immutableRecipeId)
        throw std::invalid_argument("TerraWeave native ECS recipe mismatch");
    const TerrainSettings generated=cfg.generator;
    EarthworksLayer earth(cfg.soilGridCm,
        [generated](int64_t x,int64_t z){
            return heightAtCm(generated,x,z);
        },cfg.bedrockCm);
    earth.setSnapshot(state.earth);
    return earth;
}

inline void registerTerraWeaveAuthorityResource(
        Simulation& sim,const TerraWeaveEcsAuthorityConfig& cfg){
    const auto initial=initialTerraWeaveResource(cfg);
    sim.registerPersistentResource<TerraWeaveAuthorityResource>(
        "terraweave.authority",2);
    sim.world().setResource<TerraWeaveAuthorityResource>(initial);
}

// Native users may spawn authoritative plants/patches by supplying the
// resource's rooted/grass vectors during setup (before first tick).
// Game modules should edit them through a validated staging copy.
struct TerraWeaveAuthoritativeEcsChange {
    EarthworkChange earth;
    PlantSupportUpdate rooted;
    std::vector<GrassCoverChange> grass;
    uint32_t tick=0;
};

// A single native fixed-step action. Existing ECS World resource is
// canonical; no external mutable EarthworksLayer sidecar survives rollback.
// Commit state only when ALL ground/roots/grass validation succeeds.
inline TerraWeaveAuthoritativeEcsChange
excavateAuthoritativeEcs(World& world,
                        const TerraWeaveEcsAuthorityConfig& cfg,
                        EarthworkBrush brush,uint32_t tick){
    const auto* old=world.resourceConst<TerraWeaveAuthorityResource>();
    if(!old)throw std::logic_error("TerraWeave ECS resource not initialized");
    auto ground=materializeEcsTerrain(*old,cfg);
    RootSupportRegistry roots;
    GrassCoverRegistry grass;
    roots.setSnapshot(old->rooted);
    grass.setSnapshot(old->grass);

    TerraWeaveAuthoritativeEcsChange result;
    result.tick=tick;
    result.earth=ground.sculpt(brush);
    if(result.earth.vertices.empty())return result;
    result.grass=grass.applyEarthwork(
        result.earth,ground.spacingCm());
    const SoilHeightSampler sample=[&ground](int64_t x,int64_t z)
            ->std::optional<int32_t>{
        const int64_t edge=kWorldLimitCm-ground.spacingCm();
        if(x<=-edge||x>=edge||z<=-edge||z>=edge)return std::nullopt;
        return ground.sampleHeightCm(x,z);
    };
    result.rooted=roots.applyGroundChange(sample,result.earth,tick);
    TerraWeaveAuthorityResource updated=*old;
    updated.earth=ground.snapshot();
    if(updated.stateRevision==UINT64_MAX)
        throw std::overflow_error("TerraWeave ECS state revision overflow");
    ++updated.stateRevision;
    updated.rooted=roots.snapshot();
    updated.grass=grass.snapshot();
    world.setResource<TerraWeaveAuthorityResource>(std::move(updated));
    return result;
}

// Install this system AFTER registerNativeWaterPipeline(sim,config) with
// enableBankErosion=true and AFTER registerTerraWeaveBankVertexPersistence.
// NEREID's bank ECS components already hold current, sediment, carry, wetness.
// Only the corresponding terrain/root/grass ECS resource is updated here.
// Callback should enqueue output in the native host's post-tick presentation
// queue; no graphics/Box3D work or non-deterministic side effects in callback.
template<class OnCommit>
inline void addTerraWeaveAuthoritativeBankSystem(
        Simulation& sim,std::string name,
        TerraWeaveEcsAuthorityConfig cfg,OnCommit&& callback){
    validateEcsAuthorityConfig(cfg);
    sim.addSystem(std::move(name),
        [cfg=std::move(cfg),
         onCommit=std::forward<OnCommit>(callback)]
        (Simulation& current)mutable{
            World& ecs=current.world();
            const auto* existing=
                ecs.resourceConst<TerraWeaveAuthorityResource>();
            if(!existing)
                throw std::logic_error("TerraWeave ECS resource not initialized");
            auto ground=materializeEcsTerrain(*existing,cfg);
            RootSupportRegistry roots;
            GrassCoverRegistry grass;
            roots.setSnapshot(existing->rooted);
            grass.setSnapshot(existing->grass);
            auto change=commitNereidBankTerrain(
                ecs,current.tick(),ground,&roots,&grass);
            if(change.earth.vertices.empty())return;
            TerraWeaveAuthorityResource updated=*existing;
            updated.earth=ground.snapshot();
    if(updated.stateRevision==UINT64_MAX)
        throw std::overflow_error("TerraWeave ECS state revision overflow");
    ++updated.stateRevision;
            updated.rooted=roots.snapshot();
            updated.grass=grass.snapshot();
            ecs.setResource<TerraWeaveAuthorityResource>(
                std::move(updated));
            // Host callback must not mutate the ECS or access gfx/Box3D
            // directly, and must avoid throwing after the ECS commit.
            onCommit(change);
        });
}

} // namespace aiverse::terraweave
