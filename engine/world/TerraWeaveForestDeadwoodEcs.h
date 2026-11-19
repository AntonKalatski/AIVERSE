// TerraWeave Stage67 — opt-in persistent deadwood decay on fixed ECS ticks.
// Decay, mineralisation and physics-removal notifications are authoritative
// data only. The native Box3D/render/navmesh host owns visual side effects.
#pragma once
#include "engine/world/TerraWeaveEcsAuthority.h"
#include "engine/world/TerraWeaveForestDeadwood.h"
#include <functional>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace aiverse::terraweave {

inline void registerTerraWeaveForestDeadwoodPersistence(Simulation& sim) {
    sim.registerPersistentResource<ForestDeadwoodResource>(
        "terraweave.forest_deadwood",1);
    sim.world().setResource<ForestDeadwoodResource>(
        ForestDeadwoodResource{});
}

inline ForestDeadwoodDecayOutcome advanceForestDeadwoodEcs(
        World& ecs,uint64_t completedTick,
        ForestDeadwoodDecaySettings settings={}) {
    const auto* existing=ecs.resourceConst<ForestDeadwoodResource>();
    if(!existing)
        throw std::logic_error("TerraWeave missing deadwood persistence");
    ForestDeadwoodResource next=*existing;
    auto result=advanceForestDeadwood(next,completedTick,settings);
    // Even a no-loss tick advances the persistent cursor and sample times.
    // Never leave a partly processed soil/wood transaction in World.
    if(next.initialized &&
       (!existing->initialized ||
        next.lastStepTick!=existing->lastStepTick)){
        World staged=ecs;
        staged.setResource<ForestDeadwoodResource>(std::move(next));
        ecs=std::move(staged);
    }
    return result;
}

// Events are the authoritative completed-decay transitions. Host must
// forward them through a post-ECS queue, not step Box3D from the system.
inline void addTerraWeaveForestDeadwoodSystem(
        Simulation& sim,std::string name,
        ForestDeadwoodDecaySettings settings={},
        std::function<void(const ForestDeadwoodDecayOutcome&)> onDecay={}) {
    validateForestDeadwoodDecaySettings(settings);
    sim.addSystem(std::move(name),
        [settings,onDecay=std::move(onDecay)](Simulation& current){
            auto result=advanceForestDeadwoodEcs(
                current.world(),current.tick(),settings);
            if(onDecay && result.decayedUnits)
                onDecay(result);
        });
}

// External fall events are authoritative root-support transitions, not
// necessarily ecological natural deaths. Host calls once at its ECS
// structural barrier, after soil/roots were committed and before physics.
// This pure World mutation seeds one organic mass record by stable identity.
inline bool seedFallenForestDeadwoodEcs(
        World& ecs,AnchoredPlantId id,uint64_t completedTick,
        uint64_t organicUnits=0,
        uint32_t rateUnitsPer1000Ticks=40,
        uint32_t nutrientsPermille=300) {
    const auto* authority=ecs.resourceConst<TerraWeaveAuthorityResource>();
    const auto* previous=ecs.resourceConst<ForestDeadwoodResource>();
    if(!authority || !previous)
        throw std::logic_error("TerraWeave fall needs authority/deadwood");
    const auto found=std::find_if(
        authority->rooted.begin(),authority->rooted.end(),
        [id](const AnchoredPlant& plant){
            return plant.anchor.speciesId==id.speciesId &&
                   plant.anchor.plantKey==id.key;
        });
    if(found==authority->rooted.end() || !found->fallen)
        throw std::invalid_argument(
            "TerraWeave deadwood fall requires fallen canonical roots");
    ForestDeadwoodResource next=*previous;
    if(!seedForestDeadwood(next,*found,completedTick,true,
                           organicUnits,rateUnitsPer1000Ticks,
                           nutrientsPermille))
        return false;
    World staged=ecs;
    staged.setResource<ForestDeadwoodResource>(std::move(next));
    ecs=std::move(staged);
    return true;
}

// Bulk intake for all root-support fall events produced by one authoritative
// soil/bank ECS transaction. Sort once and stage ONE saved resource update.
// This avoids a full rooted population scan or World copy per fallen tree.
inline size_t seedFallenForestDeadwoodEventsEcs(
        World& ecs,
        std::span<const PlantFallEvent> events,
        uint64_t completedTick) {
    if(events.size()>65536u)
        throw std::length_error("TerraWeave fallen deadwood event budget");
    if(events.empty())return 0;
    const auto* authority=ecs.resourceConst<TerraWeaveAuthorityResource>();
    const auto* previous=ecs.resourceConst<ForestDeadwoodResource>();
    if(!authority || !previous)
        throw std::logic_error("TerraWeave fall needs authority/deadwood");
    std::map<AnchoredPlantId,const AnchoredPlant*> byId;
    for(const auto& plant:authority->rooted){
        if(!byId.emplace(AnchoredPlantId{plant.anchor.speciesId,
                      plant.anchor.plantKey},&plant).second)
            throw std::invalid_argument("TerraWeave duplicate rooted fall ID");
    }
    std::set<AnchoredPlantId> seen;
    for(const auto& event:events){
        if(event.tick!=completedTick ||
           !event.id.key || !event.id.speciesId ||
           !seen.insert(event.id).second)
            throw std::invalid_argument("TerraWeave stale/duplicate fall event");
        const auto it=byId.find(event.id);
        if(it==byId.end() || !it->second->fallen)
            throw std::invalid_argument(
                "TerraWeave falling deadwood has no fallen rooted plant");
    }
    ForestDeadwoodResource next=*previous;
    size_t newlySeeded=0;
    for(const auto& id:seen){
        const AnchoredPlant& plant=*byId.at(id);
        if(seedForestDeadwood(next,plant,completedTick,true))
            ++newlySeeded;
    }
    if(!newlySeeded)return 0;
    World staged=ecs;
    staged.setResource<ForestDeadwoodResource>(std::move(next));
    ecs=std::move(staged);
    return newlySeeded;
}

} // namespace aiverse::terraweave
