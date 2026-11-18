// NEREID stage 6 -- opt-in native-game water pipeline.
// Called from GameModule::setup, never from a simulation tick.
#pragma once
#include <aiverselib/water/NereidAdaptiveWater.h>
#include <aiverselib/water/NereidBuoyancySystems.h>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::nereid {

struct NativeWaterPipelineConfig {
    bool enableGeometry=false;
    bool enableBuoyancy=false;
    bool probeFromTransform=true;
    AdaptiveWaterSamplingConfig adaptive{};
};

// Register the ECS components and ordered systems once during native setup.
// The host still owns gravity, integrator and water-body entity lifecycle.
inline void registerNativeWaterPipeline(Simulation& sim,
        std::string prefix="nereid",NativeWaterPipelineConfig config={}) {
    registerNereidWaterPersistence(sim);
    if(config.enableGeometry)registerNereidGeometryPersistence(sim);
    if(config.enableBuoyancy)registerNereidBuoyancyPersistence(sim);
    if(config.probeFromTransform)
        addWaterProbeFromTransformSystem(sim,prefix+".probe");
    addAdaptiveWaterSamplingSystem(sim,prefix+".sample",config.adaptive);
    if(config.enableBuoyancy)
        addWaterBuoyancySystem(sim,prefix+".buoyancy");
}

// Deterministic body entity creation order, same across independent hosts.
// Call at setup or a canonical structural-write barrier; no implicit legacy
// map mutation, resource mirroring, networking or editor dependency.
inline std::vector<Entity> spawnNativeWaterBodies(
        World& world,const std::vector<WaterBodyComponent>& bodies) {
    std::vector<Entity> result;
    result.reserve(bodies.size());
    for(const auto& body:bodies) {
        const Entity e=world.create();
        world.add<WaterBodyComponent>(e,body);
        result.push_back(e);
    }
    return result;
}
} // namespace aiverse::lib::nereid
