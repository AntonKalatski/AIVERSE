// PHYSIS Stage 5: generic Simulation integration, persistent ECS and render-neutral snapshots.
// No dependency on any genre module or rendering backend.
#pragma once
#include "PhysisDisasters.h"
#include "PhysisWind.h"
#include "PhysisGravity.h"
#include "../sim/Simulation.h"

namespace aiverse::physis {

// Stable field-wise serialization for Simulation::PersistentEcsRegistry.
inline void io(Archive& a, Cell& v) { a(v.x, v.z, v.elevation); }
inline void io(Archive& a, Atmosphere& v) {
    a(v.temperature, v.humidity, v.pressure, v.wind.x, v.wind.y, v.rain, v.snow);
}
inline void io(Archive& a, Surface& v) { a(v.wetness, v.snowpack, v.ice); }
inline void io(Archive& a, Settings& v) {
    a(v.originX, v.originZ, v.width, v.height, v.cellSizeMeters, v.seed,
      v.baseTemperature, v.baseHumidity);
}
inline void io(Archive& a, State& v) { a(v.settings, v.tick, v.cells); }
inline void io(Archive& a, ClimateZone& v) {
    a(v.temperatureOffset, v.humidityOffset, v.pressureOffset,
      v.prevailingWind.x, v.prevailingWind.y, v.seasonTemperatureAmplitude,
      v.seasonHumidityAmplitude, v.yearTicks, v.phaseTicks);
}
inline void io(Archive& a, WeatherFront& v) {
    a(v.center.x, v.center.y, v.velocity.x, v.velocity.y,
      v.radiusCells, v.temperatureDelta, v.humidityDelta,
      v.pressureDelta, v.windDelta.x, v.windDelta.y, v.precipitationBoost);
}
inline void io(Archive& a, SurfaceParameters& v) {
    a(v.wettingRate, v.dryingRate, v.snowAccumulationRate, v.meltRate,
      v.freezingRate, v.thawingRate, v.freezingPoint);
}
inline void io(Archive& a, SurfaceMaterial& v) {
    a(v.simulation, v.permeability, v.tractionDry);
}
inline void io(Archive& a, WindZone& v) {
    a(v.center, v.radiusMeters, v.velocity, v.gustAmplitude, v.seed,
      v.startTick, v.durationTicks, v.gustPeriodTicks);
}
inline void io(Archive& a, WindShelter& v) {
    a(v.center, v.radiusMeters, v.attenuation);
}
inline void io(Archive& a, GravitySettings& v) {
    a(v.baseline, v.maxAcceleration);
}
inline void io(Archive& a, GravityField& v) {
    a(v.mode, v.center, v.radiusMeters, v.acceleration, v.radialMagnitude,
      v.priority, v.overrideBaseline, v.enabled);
}
inline void io(Archive& a, HazardCell& v) {
    a(v.fuel, v.fire, v.waterDepth, v.quake);
}
inline void io(Archive& a, DisasterConfig& v) {
    a(v.fireSpreadRate, v.fireGrowthRate, v.fuelConsumptionRate,
      v.rainSuppressionRate, v.wetnessSuppressionRate, v.rainInflowRate,
      v.waterFlowFraction, v.evaporationRate);
}
inline void io(Archive& a, DisasterState& v) {
    a(v.config, v.lastProcessedTick);
}
inline void io(Archive& a, QuakeEvent& v) {
    a(v.centerX, v.centerZ, v.radiusCells, v.magnitude,
      v.startTick, v.durationTicks);
}

inline void registerPersistence(Simulation& simulation, bool disasters = false) {
    // Must be called during GameModule::setup before the registry is sealed.
    simulation.registerPersistentComponent<Cell>("physis.cell", 1);
    simulation.registerPersistentComponent<Atmosphere>("physis.atmosphere", 1);
    simulation.registerPersistentComponent<Surface>("physis.surface", 1);
    simulation.registerPersistentComponent<ClimateZone>("physis.climate_zone", 1);
    simulation.registerPersistentComponent<WeatherFront>("physis.weather_front", 1);
    simulation.registerPersistentComponent<SurfaceMaterial>("physis.material", 1);
    simulation.registerPersistentComponent<WindZone>("physis.wind_zone", 1);
    simulation.registerPersistentComponent<WindShelter>("physis.wind_shelter", 1);
    simulation.registerPersistentComponent<GravityField>("physis.gravity_field", 1);
    simulation.registerPersistentResource<GravitySettings>("physis.gravity_settings", 1);
    simulation.registerPersistentResource<State>("physis.state", 1);
    if (disasters) {
        simulation.registerPersistentComponent<HazardCell>("physis.hazard", 1);
        simulation.registerPersistentComponent<QuakeEvent>("physis.quake_event", 1);
        simulation.registerPersistentResource<DisasterState>("physis.disaster_state", 1);
    }
}

struct InstallOptions {
    Settings climate{};
    DisasterConfig disasters{};
    bool enableDisasters = false;
    // Use ExternalAuthority when NEREID or another water simulation owns
    // water volumes; fire and quakes keep running without duplicate flooding.
    FloodMode floodMode = FloodMode::LocalShallow;
};

inline void install(Simulation& simulation, const InstallOptions& options = {}) {
    // Invalid policy must not partially initialize persistent ECS state.
    if (options.floodMode != FloodMode::LocalShallow &&
        options.floodMode != FloodMode::ExternalAuthority)
        throw std::invalid_argument("invalid PHYSIS flood authority");
    // Universal one-line native project integration. Never call in render code.
    // Install during GameModule::setup; SimSnapshot and file saves use the same
    // authoritative World. The host controls the order of systems it registers.
    initialize(simulation.world(), options.climate);
    if (options.enableDisasters)
        initializeDisasters(simulation.world(), options.disasters);
    registerPersistence(simulation, options.enableDisasters);
    simulation.addSystem("physis.environment",
                         [hazards=options.enableDisasters, flood=options.floodMode](Simulation& sim) {
        if (hazards) advanceEnvironmentWithDisasters(sim.world(), flood);
        else advanceEnvironment(sim.world());
    });
}

struct PresentationCell {
    Cell cell{};
    EnvironmentalResponse environment{};
    HazardCell hazards{};
    bool hasHazards = false;
};

// View / editor / audio / physics adapters supply their own sink. No handles
// enter the ECS; presentation reads immutable copies after a committed tick.
template<class Sink>
inline void forEachPresentationCell(const World& world, Sink&& sink) {
    const auto* state = world.resourceConst<State>();
    if (!state) return;
    for (const Entity entity : state->cells) {
        const auto* coord = world.tryGetConst<Cell>(entity);
        if (!coord) continue;
        PresentationCell result;
        result.cell = *coord;
        if (!environmentAt(world, coord->x, coord->z, result.environment)) continue;
        if (const auto* hazard = world.tryGetConst<HazardCell>(entity)) {
            result.hazards = *hazard;
            result.hasHazards = true;
        }
        sink(result);
    }
}

} // namespace aiverse::physis
