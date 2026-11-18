// Deterministic non-physical projectile motion for any game genre.
// Kinematic projectiles do not query collision or integrate rigid-body forces.
// The host resolves target lifetime and applies damage in a later system.
#pragma once
#include "EcsProjectileSystems.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::genre {

struct KinematicProjectileComponent {
    Vec3 origin{}, position{}, destination{};
    Entity owner=kNullEntity;
    Entity target=kNullEntity;
    uint32_t shotId=0;
    uint32_t elapsed=0, duration=1;
    bool alive=true;
};

struct KinematicArrivalEvent {
    Entity projectile=kNullEntity, owner=kNullEntity, target=kNullEntity;
    uint32_t shotId=0;
    Vec3 position{};
};

// Fixed endpoint interpolation: no collision query, no gravity and no RNG.
// Duration is measured in simulation ticks, so the result is independent of
// render frame rate and worker scheduling. A moving target is NOT followed:
// use a separate target-tracking policy if that behavior is desired.
inline bool stepKinematicProjectile(KinematicProjectileComponent& shot) {
    if(!shot.alive || shot.duration==0 || shot.duration>100000 ||
       shot.elapsed>shot.duration)return false;
    if(shot.elapsed==shot.duration){shot.alive=false;return true;}
    ++shot.elapsed;
    const Fixed fraction=Fixed::fromRatio(shot.elapsed,shot.duration);
    shot.position=shot.origin+(shot.destination-shot.origin)*fraction;
    if(shot.elapsed==shot.duration)shot.alive=false;
    return true;
}

// ArrivalSink: void(Simulation&,const KinematicArrivalEvent&).
// Events are emitted after stable ECS dense-order traversal; despawn is
// deferred. The sink decides whether a stale/dead target should take damage.
// Do not register this system and the physical flight system for the same
// projectile entity.
template<class ArrivalSink>
void addKinematicProjectileSystem(Simulation& sim,std::string name,
                                  ArrivalSink onArrival) {
    sim.addSystem(std::move(name),
        [onArrival=std::move(onArrival)](Simulation& current) mutable {
            std::vector<KinematicArrivalEvent> arrivals;
            std::vector<Entity> expired;
            World& world=current.world();
            eachEcsMechanic<KinematicProjectileComponent>(world,
                [&](Entity entity,KinematicProjectileComponent& shot) {
                    if(!world.alive(entity))return;
                    if(!shot.alive){expired.push_back(entity);return;}
                    if(!stepKinematicProjectile(shot))return;
                    if(!shot.alive) {
                        arrivals.push_back({entity,shot.owner,shot.target,
                                            shot.shotId,shot.position});
                        expired.push_back(entity);
                    }
                });
            for(const auto& event:arrivals)onArrival(current,event);
            for(Entity entity:expired)world.destroyLater(entity);
        });
}
} // namespace aiverse::lib::genre
