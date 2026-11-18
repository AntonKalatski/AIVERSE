// Universal ECS projectile impact -> damage pipeline.
// Compose it with EcsProjectileSystems.h for bullets, missiles, rockets,
// traps or spells without importing modules/rts or modules/fps.
//
// Projectile flight, collider-to-entity lookup and damage application are
// independent systems. All ordered effects occur after projectile traversal,
// in stable dense-order impact sequence, before structural destroy flush.
#pragma once
#include "EcsProjectileSystems.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::genre {

struct HitPointsComponent {
    int32_t current=100;
    int32_t maximum=100;
};

// Shared immediate integer HP commit for ECS damage resolution and legacy
// genre adapters. Do NOT call before the host has resolved armor, factions,
// shields and knockdown. No integer subtraction overflow on large damage.
inline int32_t applyIntegerHitPointDamage(int32_t& current,int32_t incoming) {
    if(current<=0 || incoming<=0)return 0;
    const int32_t applied=std::min(current,incoming);
    current-=applied;
    return applied;
}

struct ProjectileDamageComponent {
    int32_t directDamage=0;
};

struct ColliderEntityIndex {
    // Maps collision-world identifiers to the ECS Entity that owns the hitbox.
    // Generation-safe: stale handles are ignored during damage resolution.
    std::map<uint32_t,Entity> byCollider;
};

struct DamageRequest {
    Entity target=kNullEntity;
    Entity source=kNullEntity;
    uint32_t shotId=0;
    int32_t amount=0;
};

struct DamageQueue {
    std::vector<DamageRequest> pending;
};

struct DamageApplied {
    Entity target=kNullEntity;
    Entity source=kNullEntity;
    uint32_t shotId=0;
    int32_t applied=0;
    int32_t remaining=0;
};

// Bind this callback as addProjectileFlightSystem's impact sink.
// It stages events only; authoritative HitPoints updates are made by a
// separate damage system at a deterministic, explicitly ordered barrier.
inline void enqueueProjectileDamage(Simulation& sim,
                                     const ProjectileImpactEvent& impact) {
    World& world=sim.world();
    const World& read=world;
    const auto* owners=read.resourceConst<ColliderEntityIndex>();
    if(!owners)return;
    const auto match=owners->byCollider.find(impact.contact.collider);
    if(match==owners->byCollider.end() || !world.alive(match->second))return;
    const auto* projectile=read.tryGetConst<ProjectileDamageComponent>(impact.projectile);
    if(!projectile || projectile->directDamage<=0)return;
    auto* queue=world.resource<DamageQueue>();
    if(!queue)return;
    queue->pending.push_back({match->second,impact.owner,impact.shotId,
                              projectile->directDamage});
}

// Default policy applies raw direct damage; custom policies can implement
// armor, resistance, friendly fire or factions without changing this system.
// Policy signature: int32_t(Simulation&, const DamageRequest&, const HitPointsComponent&).
struct DirectHitDamagePolicy {
    int32_t operator()(Simulation&,const DamageRequest& event,
                       const HitPointsComponent&)const {
        return event.amount;
    }
};

// ApplySink signature: void(Simulation&, const DamageApplied&).
// Queue is swapped before iteration: damage staged from callbacks belongs to
// the NEXT resolution barrier. No implicit event sort or RNG consumption.
template<class ApplySink,class DamagePolicy=DirectHitDamagePolicy>
void addDamageResolutionSystem(Simulation& sim,std::string systemName,
                               ApplySink onApplied,DamagePolicy policy={}) {
    sim.addSystem(std::move(systemName),
        [onApplied=std::move(onApplied),policy=std::move(policy)](Simulation& current) mutable {
            World& world=current.world();
            auto* queue=world.resource<DamageQueue>();
            if(!queue || queue->pending.empty())return;
            std::vector<DamageRequest> batch;
            batch.swap(queue->pending);
            for(const auto& request:batch) {
                if(request.amount<=0 || !world.alive(request.target))continue;
                auto* health=world.tryGet<HitPointsComponent>(request.target);
                if(!health || health->maximum<=0 || health->current<=0)continue;
                const int32_t candidate=policy(current,request,*health);
                if(candidate<=0)continue;
                const int32_t applied=applyIntegerHitPointDamage(
                    health->current,candidate);
                onApplied(current,DamageApplied{request.target,request.source,
                                                request.shotId,applied,health->current});
            }
        });
}

} // namespace aiverse::lib::genre
