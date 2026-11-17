// A shared ECS mechanic: projectile flight and impact staging for every genre.
// The same components and system can be attached to RTS units, FPS weapons,
// turrets on vehicles, drones, spells or user-defined native game objects.
//
// Flight movement/collision policy is independent from who launched the shot.
// Damage, explosions, audio, particle effects, score and presentation are
// deliberately handled by separate systems consuming canonical impact events.
#pragma once
#include "EcsMechanicSystems.h"
#include "Weapons.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::genre {

struct ProjectileFlightComponent {
    ProjectileConfig config{};
    ProjectileState state{};
    Entity owner=kNullEntity;
    uint32_t shotId=0; // stable semantic ID for replay and cross-system ordering
};

struct ProjectileImpactEvent {
    Entity projectile=kNullEntity;
    Entity owner=kNullEntity;
    uint32_t shotId=0;
    ProjectileImpact contact{};
};

// Shared ballistic policy: bullets, shells, launched grenades and any object
// with the same gravity/radius/bounce parameters use the SAME flight solver.
// Other policies may implement steering/homing and still use this ECS system.
struct BallisticFlightPolicy {
    ProjectileStep operator()(ProjectileFlightComponent& projectile,
                              const world::SpatialWorld& collision,Fixed dt) const {
        return stepProjectile(projectile.config,projectile.state,collision,dt);
    }
};

// Provider: const world::SpatialWorld&(Simulation&) to support arbitrary
// world/map/collision owners without RtsModule/fps::Module coupling.
// ImpactSink: void(Simulation&, const ProjectileImpactEvent&), called only
// AFTER all projectile updates in canonical dense order.
// FlightPolicy: ProjectileStep(ProjectileFlightComponent&,SpatialWorld&,Fixed)
// (default BallisticFlightPolicy).
//
// This system owns projectile motion and despawning, but not damage logic.
// In particular, do not allocate a new projectile from the impact callback
// while iterating or use thread-dependent RNG. Damage/explosions must be
// handled in a separate ordered impact/commit stage.
template<class CollisionProvider,class ImpactSink,
         class FlightPolicy=BallisticFlightPolicy>
void addProjectileFlightSystem(Simulation& sim,std::string systemName,
                               CollisionProvider collisionSource,
                               ImpactSink onImpact,
                               FlightPolicy flightPolicy={}) {
    sim.addSystem(std::move(systemName),
        [collisionSource=std::move(collisionSource),
         onImpact=std::move(onImpact),
         flightPolicy=std::move(flightPolicy)](Simulation& current) mutable {
            const world::SpatialWorld& collision=collisionSource(current);
            std::vector<ProjectileImpactEvent> hits;
            std::vector<Entity> expired;
            eachEcsMechanic<ProjectileFlightComponent>(current.world(),
                [&](Entity entity,ProjectileFlightComponent& projectile) {
                    if(!projectile.state.alive) {
                        expired.push_back(entity);
                        return;
                    }
                    const auto result=flightPolicy(projectile,collision,current.dt());
                    if(!result.accepted)return; // invalid input: no gameplay side effects
                    if(result.impact)
                        hits.push_back({entity,projectile.owner,projectile.shotId,
                                        *result.impact});
                    if(!projectile.state.alive)expired.push_back(entity);
                });
            // Canonical order = ECS dense order of the ProjectileFlight pool.
            for(const auto& hit:hits)onImpact(current,hit);
            for(Entity entity:expired)current.world().destroyLater(entity);
        });
}

} // namespace aiverse::lib::genre
