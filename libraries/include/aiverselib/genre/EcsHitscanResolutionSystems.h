// Genre-independent ECS hitscan request -> ordered resolved-hit events.
// Instantaneous raycasts do NOT create flying projectiles. The host supplies
// candidate snapshots/rewind, target hitboxes and any obstruction cutoff.
#pragma once
#include "EcsHitscanSystems.h"
#include "engine/sim/Simulation.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::genre {

struct HitscanIntentComponent {
    Vec3 origin{}, delta{};
    Fixed maximumFraction=1_fx; // host can shorten this using world/vehicle occlusion
    uint32_t shotId=0;
    bool requested=false;
};

struct HitscanResolvedEvent {
    Entity shooter=kNullEntity, target=kNullEntity;
    uint32_t shotId=0;
    Vec3 point{};
    Fixed fraction{};
};

// Candidates: const range&(Simulation&,Entity,const HitscanIntentComponent&)
// Eligible: bool(Simulation&,Entity,const Candidate&)
// Bounds: world::Bounds(Simulation&,const Candidate&)
// TargetEntity: Entity(Simulation&,const Candidate&)
// HitSink: void(Simulation&,const HitscanResolvedEvent&).
//
// Each source entity owns one request. Consume requests exactly once, even
// when a ray is invalid, no target is available, or an obstruction wins.
// Hit callbacks run only AFTER stable ECS request traversal, not while the
// source component pool is being updated. Preserve source dense order.
// Host-specific rewind and instant-damage callback ordering must be validated
// before migrating any existing genre's authoritative hit logic.
template<class Candidates,class Eligible,class Bounds,
         class TargetEntity,class HitSink>
void addHitscanResolutionSystem(Simulation& sim,std::string name,
        Candidates candidates,Eligible eligible,Bounds bounds,
        TargetEntity entityOf,HitSink onHit) {
    sim.addSystem(std::move(name),
        [candidates=std::move(candidates),eligible=std::move(eligible),
         bounds=std::move(bounds),entityOf=std::move(entityOf),
         onHit=std::move(onHit)](Simulation& current) mutable {
            World& world=current.world();
            auto* requests=world.prepareExistingWrite<HitscanIntentComponent>();
            if(!requests)return;
            std::vector<HitscanResolvedEvent> events;
            const size_t count=requests->size();
            for(size_t i=0;i<count;++i) {
                const Entity shooter=requests->entityAt(i);
                if(!world.alive(shooter))continue;
                auto& request=requests->dataAt(i);
                if(!request.requested)continue;
                request.requested=false;
                if(request.maximumFraction<=Fixed{} ||
                   request.maximumFraction>1_fx ||
                   request.delta.lengthSq()<=Fixed{})continue;
                const auto& targets=candidates(current,shooter,request);
                const auto selection=selectClosestHitscanCandidate(
                    targets,request.origin,request.delta,request.maximumFraction,
                    [&](const auto& candidate) {
                        return eligible(current,shooter,candidate);
                    },
                    [&](const auto& candidate) {return bounds(current,candidate);});
                if(!selection)continue;
                const Entity victim=entityOf(current,*selection.target);
                if(!world.alive(victim))continue;
                events.push_back({shooter,victim,request.shotId,
                    request.origin+request.delta*selection.fraction,
                    selection.fraction});
            }
            for(const auto& hit:events)onHit(current,hit);
        });
}

} // namespace aiverse::lib::genre
