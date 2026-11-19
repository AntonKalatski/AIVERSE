// Minimal opt-in native game composition of existing NEREID ECS water,
// TerraWeave authoritative terrain/vegetation and the shared WorldScene.
// This is a reusable example header (not an engine-level genre module).
#pragma once
#include <aiverselib/water/NereidNativeWater.h>
#include "engine/world/TerraWeaveNativeDriver.h"
#include <functional>
#include <string>
#include <utility>

namespace aiverse::examples::terraweave_native {

// Invoke once from GameModule::setup, before the Simulation registry seals.
// Native water owns ALL WaterContact/current/bank flow calculations.
// TerraWeave owns Earthworks soil, rooted plants and sparse grass coverage,
// persisted through the one rollback-safe ECS authority resource.
inline void registerSystems(
        Simulation& sim,
        terraweave::TerraWeaveEcsAuthorityConfig cfg,
        std::function<void(
            const terraweave::EcsBankTerrainCommit&)> onEroded={}) {
    lib::nereid::NativeWaterPipelineConfig water{};
    water.enableBankErosion=true;
    // This example gives bank cells explicit WaterProbe positions. A native
    // game that uses Transform-driven probes may choose true instead.
    water.probeFromTransform=false;
    lib::nereid::registerNativeWaterPipeline(sim,"nereid",water);
    terraweave::registerTerraWeaveBankVertexPersistence(sim);
    terraweave::registerTerraWeaveAuthorityResource(sim,cfg);
    terraweave::addTerraWeaveAuthoritativeBankSystem(
        sim,"terraweave.bank_commit",std::move(cfg),
        [after=std::move(onEroded)](
            const terraweave::EcsBankTerrainCommit& change) {
            // A real server may append fall events to a host-managed
            // presentation journal. NEVER step Box3D/GPU inside this ECS
            // callback or mutate ECS again. A missing sink is permitted:
            // the canonical ECS resource still contains the new fallen flags.
            if(after)after(change);
        });
}

inline Entity addWaterBody(
        World& ecs,lib::nereid::WaterBodyComponent source) {
    if(!lib::nereid::validWaterBody(source))
        throw std::invalid_argument(
            "TerraWeave native sample requires a valid NEREID water body");
    const Entity id=ecs.create();
    ecs.add<lib::nereid::WaterBodyComponent>(id,source);
    return id;
}

// Creates one authoritative shoreline cell (NOT one entity per grass blade).
// The X/Z bank probe may be on the water side while its EarthVertex is the
// adjacent ground surface whose soil is being eroded.
inline Entity addShoreCell(
        World& ecs,terraweave::EarthVertex groundVertex,
        int32_t groundHeightCm,Vec3 waterProbePosition,
        lib::nereid::WaterBankSoilComponent source) {
    source.soil.groundCm=groundHeightCm;
    if(source.wetContactPermille>1000u ||
       source.waveEnergyPermille>1000u ||
       source.bankSlopePermille>8000u)
        throw std::invalid_argument("TerraWeave invalid shoreline contact");
    lib::nereid::validateShoreErosion(
        source.soil,{},source.settings);
    const Entity id=ecs.create();
    ecs.add<lib::nereid::WaterProbeComponent>(
        id,lib::nereid::WaterProbeComponent{waterProbePosition});
    ecs.add<lib::nereid::WaterContactComponent>(
        id,lib::nereid::WaterContactComponent{});
    ecs.add<lib::nereid::WaterBankSoilComponent>(id,source);
    ecs.add<lib::nereid::WaterBankErosionOutputComponent>(
        id,lib::nereid::WaterBankErosionOutputComponent{});
    ecs.add<terraweave::BankEarthVertexComponent>(
        id,terraweave::BankEarthVertexComponent{groundVertex});
    return id;
}

inline terraweave::NativeTerrainPublishResult publishFrame(
        const Simulation& sim,
        terraweave::NativeTerraWeaveWorldBinding& binding,
        terraweave::TerrainMutationCoordinator& terrain,
        world::WorldScene& streamedScene,gfx::Device& device,
        world::SpatialWorld& collision,nav::Mesh& navigation,
        const terraweave::EarthworksNavSource& navSource,
        std::span<const world::Cell> pinnedCollisionTiles,
        terraweave::RectCm grassArea,
        const terraweave::TerrainMutationCoordinator::ColliderIdForCell&
            collisionIds,std::string* navError=nullptr,
        bool afterRollback=false) {
    return terraweave::publishNativeTerraWeaveFrame(
        sim.world(),binding,terrain,streamedScene,device,collision,
        navigation,navSource,pinnedCollisionTiles,grassArea,collisionIds,
        navError,2u,afterRollback);
}

} // namespace aiverse::examples::terraweave_native
