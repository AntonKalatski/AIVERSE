// AIVERSE NEREID -- water bodies on spheres (planet seas): deterministic ECS sampling and buoyancy.
// A sphere body is a sea shell around a centre: points closer to the centre than the surface radius (and
// farther than the floor) are in the water. "Up" is the radial direction; the current is a rotation about
// an axis (metres per second at the surface). Fixed-point only; no renderer, genre or planet-module types.
// Plane bodies stay in NereidWater.h; a project may use both (separate contact components).
#pragma once
#include <aiverselib/water/NereidBuoyancySystems.h>
#include <string>
#include <utility>

namespace aiverse::lib::nereid {

struct WaterSphereComponent {
    WaterKind kind = WaterKind::Ocean;
    Vec3 center{};                 // sphere centre, simulation space
    Fixed surfaceRadius = 1_fx;    // sea surface
    Fixed floorRadius{};           // deepest water (0: down to the centre)
    Vec3 axis{Fixed{}, 1_fx, Fixed{}};   // current rotation axis (need not be unit)
    Fixed currentSpeed{};          // m/s at the surface, eastward about `axis`; 0 = still water
    int32_t priority = 0;          // larger overrides smaller where shells overlap
    bool enabled = true;
};

// The sphere counterpart of WaterSample: depth / altitude are radial.
struct SphereWaterSample {
    Entity body = kNullEntity;
    WaterKind kind = WaterKind::Ocean;
    Vec3 up{};                     // unit radial direction at the probe
    Fixed surfaceRadius{}, floorRadius{};
    Fixed depth{};                 // below the surface, 0 above it
    Vec3 flow{};                   // tangential current
    int32_t priority = 0;
    bool found = false, submerged = false;
};
struct WaterSphereContactComponent {
    SphereWaterSample sample{};
    uint32_t sampledTick = 0;
};

inline bool validWaterSphere(const WaterSphereComponent& s) {
    return s.enabled && s.surfaceRadius > Fixed{} && s.floorRadius >= Fixed{} && s.floorRadius < s.surfaceRadius;
}

inline Vec3 sphereCross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// Pure sampling of one sphere body (nothing found when the probe sits on the centre).
inline SphereWaterSample sampleWaterSphere(Entity entity, const WaterSphereComponent& s, Vec3 position) {
    SphereWaterSample out;
    if (!validWaterSphere(s)) return out;
    const Vec3 r = position - s.center;
    const Fixed dist = r.length();
    if (dist.raw <= 0) return out;
    out.body = entity;
    out.kind = s.kind;
    out.up = r / dist;
    out.surfaceRadius = s.surfaceRadius;
    out.floorRadius = s.floorRadius;
    out.depth = fxClamp(s.surfaceRadius - dist, Fixed{}, s.surfaceRadius - s.floorRadius);
    out.priority = s.priority;
    out.found = true;
    out.submerged = dist < s.surfaceRadius && dist >= s.floorRadius;
    if (s.currentSpeed.raw != 0) {   // east = axis x up, scaled so the surface moves at currentSpeed
        const Vec3 east = sphereCross(s.axis, out.up);
        const Fixed len = east.length();
        if (len.raw > 0) out.flow = east * s.currentSpeed / len;
    }
    return out;
}

// Highest priority wins, then the larger surface, then the smallest Entity id. Only bodies whose shell
// (up to the surface) contains the probe or whose surface it is above are considered: the nearest sea.
inline SphereWaterSample sampleSphereWater(const World& world, Vec3 position) {
    SphereWaterSample best;
    world.eachExistingConst<WaterSphereComponent>([&](Entity entity, const WaterSphereComponent& s) {
        if (!world.alive(entity)) return;
        const SphereWaterSample c = sampleWaterSphere(entity, s, position);
        if (!c.found) return;
        const bool better = !best.found || c.priority > best.priority ||
            (c.priority == best.priority && (c.submerged && !best.submerged)) ||
            (c.priority == best.priority && c.submerged == best.submerged &&
             (c.surfaceRadius > best.surfaceRadius ||
              (c.surfaceRadius == best.surfaceRadius && entity.packed() < best.body.packed())));
        if (better) best = c;
    });
    return best;
}

// Buoyancy along the radial up (the plane policy of computeWaterForces with "up" taken from the sample).
inline WaterForceComponent computeSphereWaterForces(const SphereWaterSample& sample, const WaterBuoyancyComponent& params,
                                                    Vec3 velocity) {
    WaterForceComponent out;
    if (!sample.found || !sample.submerged || !validBuoyancy(params)) return out;
    out.immersedFraction = fxClamp(sample.depth / params.immersionHeight, Fixed{}, 1_fx);
    const Vec3 relative = velocity - sample.flow;
    out.acceleration = sample.up * (params.buoyancyAcceleration * out.immersedFraction) +
                       relative * (-params.linearDrag * out.immersedFraction);
    return out;
}

// Read: WaterSphere + WaterProbe. Write: WaterSphereContact only. Place after the probe system.
inline void addSphereWaterSamplingSystem(Simulation& sim, std::string name) {
    sim.addSystem(std::move(name), [](Simulation& current) {
        World& world = current.world();
        if (!world.countExisting<WaterSphereContactComponent>()) return;
        auto* contacts = &world.pool<WaterSphereContactComponent>();   // detach the COW pool before writing
        const World& read = world;
        for (size_t i = 0; i < contacts->size(); ++i) {
            const Entity e = contacts->entityAt(i);
            if (!world.alive(e)) continue;
            const auto* probe = read.tryGetConst<WaterProbeComponent>(e);
            auto& contact = contacts->dataAt(i);
            contact.sample = probe ? sampleSphereWater(read, probe->position) : SphereWaterSample{};
            contact.sampledTick = current.tick();
        }
    });
}

// Entities with a sphere contact get their WaterForceComponent from it (radial buoyancy, drag to the current).
inline void addSphereWaterBuoyancySystem(Simulation& sim, std::string name) {
    sim.addSystem(std::move(name), [](Simulation& current) {
        World& world = current.world();
        if (!world.countExisting<WaterSphereContactComponent>()) return;
        auto* forces = world.prepareExistingWrite<WaterForceComponent>();
        if (!forces) return;
        const World& read = world;
        for (size_t i = 0; i < forces->size(); ++i) {
            const Entity e = forces->entityAt(i);
            if (!world.alive(e)) continue;
            const auto* contact = read.tryGetConst<WaterSphereContactComponent>(e);
            if (!contact) continue;   // plane contacts keep the plane buoyancy system
            const auto* params = read.tryGetConst<WaterBuoyancyComponent>(e);
            const auto* velocity = read.tryGetConst<WaterVelocityInputComponent>(e);
            auto& force = forces->dataAt(i);
            force = params && velocity && contact->sampledTick == current.tick()
                ? computeSphereWaterForces(contact->sample, *params, velocity->velocity) : WaterForceComponent{};
            force.sampledTick = current.tick();
        }
    });
}

inline void io(Archive& a, WaterSphereComponent& s) {
    a(s.kind, s.center, s.surfaceRadius, s.floorRadius, s.axis, s.currentSpeed, s.priority, s.enabled);
}
inline void io(Archive& a, SphereWaterSample& s) {
    a(s.body, s.kind, s.up, s.surfaceRadius, s.floorRadius, s.depth, s.flow, s.priority, s.found, s.submerged);
}
inline void io(Archive& a, WaterSphereContactComponent& c) { a(c.sample, c.sampledTick); }

inline void registerNereidSphereWaterPersistence(Simulation& sim) {
    sim.registerPersistentComponent<WaterSphereComponent>("nereid.water_sphere", 1);
    sim.registerPersistentComponent<WaterSphereContactComponent>("nereid.water_sphere_contact", 1);
}

} // namespace aiverse::lib::nereid
