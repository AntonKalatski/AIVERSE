// TerraWeave Stage68: native fixed-step bridge for conserved nutrient cycle.
// NEREID is queried for actual body/current; PHYSIS supplies weather through
// immutable completed-tick inputs. No new water solver or RHI runtime.
#pragma once
#include "engine/world/TerraWeaveEcsAuthority.h"
#include "engine/world/TerraWeaveForestEcology.h"
#include "engine/world/TerraWeaveForestDeadwoodEcs.h"
#include "engine/world/TerraWeaveForestNutrientCycle.h"
#include <aiverselib/water/NereidQueryIndex.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace aiverse::terraweave {

struct ForestNutrientWeather {
    uint32_t rainfallPermille=0;
    uint32_t soilMoisturePermille=700;
};
// The host/PHYSIS reads authoritative weather ONCE at the completed tick;
// this function must be deterministic for the same tick and world snapshot.
using ForestNutrientWeatherSampler=std::function<
    ForestNutrientWeather(int64_t worldXcm,int64_t worldZcm)>;

// Prepare one aggregate demand per nutrient cell. The cost is O(N) once
// per nutrient interval for opted-in INTERACTABLE rooted plants and grass
// patches, not every cosmetic tree or individual grass blade.
inline std::map<std::pair<int64_t,int64_t>,uint32_t>
prepareForestNutrientDemand(
        const TerraWeaveAuthorityResource& state,
        const ForestEcologyResource* ecology=nullptr) {
    using Key=std::pair<int64_t,int64_t>;
    std::set<AnchoredPlantId> dead;
    if(ecology){
        validateForestEcologyResource(*ecology);
        for(const auto& item:ecology->plants)
            if(item.dead)dead.insert(item.id);
    }
    std::map<Key,uint32_t> demand;
    const auto add=[&demand](Key key,uint32_t units) {
        uint32_t& current=demand[key];
        current=std::min<uint32_t>(1000000u,
            current+std::min<uint32_t>(units,1000000u-current));
    };
    for(const auto& plant:state.rooted){
        if(plant.fallen || !plant.anchor.plantKey ||
           !plant.anchor.speciesId ||
           dead.contains({plant.anchor.speciesId,plant.anchor.plantKey}))
            continue;
        add(deadwoodSoilKey(plant.anchor.xCm,plant.anchor.zCm),
            plant.anchor.roots.habit==PlantHabit::Shrub?1u:2u);
    }
    for(const auto& patch:state.grass){
        if(!patch.coveragePermille)continue;
        const int64_t x=patch.bounds.minX+
            (patch.bounds.maxX-patch.bounds.minX)/2;
        const int64_t z=patch.bounds.minZ+
            (patch.bounds.maxZ-patch.bounds.minZ)/2;
        add(deadwoodSoilKey(x,z),
            std::max<uint32_t>(1u,patch.coveragePermille*3u/1000u));
    }
    return demand;
}

inline ForestNutrientCycleOutcome advanceForestNutrientCycleEcs(
        World& ecs,const TerraWeaveEcsAuthorityConfig& cfg,
        uint64_t completedTick,
        ForestNutrientCycleSettings settings={},
        ForestNutrientWeatherSampler weather={}) {
    validateForestNutrientCycleSettings(settings);
    const auto* old=ecs.resourceConst<ForestDeadwoodResource>();
    const auto* authority=ecs.resourceConst<TerraWeaveAuthorityResource>();
    if(!old || !authority)
        throw std::logic_error(
            "TerraWeave nutrient cycle needs deadwood and soil authority");
    if(old->nutrientInitialized &&
       completedTick<old->lastNutrientTick)
        throw std::invalid_argument("TerraWeave nutrient tick reversed");
    if(old->nutrientInitialized &&
       completedTick-old->lastNutrientTick<settings.intervalTicks)
        return {};
    const auto ground=materializeEcsTerrain(*authority,cfg);
    const lib::nereid::WaterQueryIndex water(ecs);
    const auto* ecology=ecs.resourceConst<ForestEcologyResource>();
    const auto demand=prepareForestNutrientDemand(*authority,ecology);
    ForestDeadwoodResource next=*old;
    using Key=std::pair<int64_t,int64_t>;

    const auto sample=[&](int64_t cellX,int64_t cellZ) {
        ForestNutrientCellClimate result;
        const int64_t x=cellX*ForestDeadwoodResource::kNutrientCellCm+
            ForestDeadwoodResource::kNutrientCellCm/2;
        const int64_t z=cellZ*ForestDeadwoodResource::kNutrientCellCm+
            ForestDeadwoodResource::kNutrientCellCm/2;
        const auto met=weather?weather(x,z):ForestNutrientWeather{};
        result.rainfallPermille=met.rainfallPermille;
        result.soilMoisturePermille=met.soilMoisturePermille;
        if(const auto it=demand.find(Key{cellX,cellZ});
           it!=demand.end())result.demandUnits=it->second;
        const int64_t edge=kWorldLimitCm-ground.spacingCm();
        if(x>-edge && x<edge && z>-edge && z<edge){
            const int32_t height=ground.sampleHeightCm(x,z);
            const auto resolved=water.sample({
                Fixed::fromRatio(x,100),
                Fixed::fromRatio(height,100),
                Fixed::fromRatio(z,100)});
            if(resolved.found && resolved.submerged){
                result.submergedPermille=1000;
                // Flood-only samples without a canonical body must not invent
                // a common transport path across isolated pools.
                // 0 is reserved for 'no body'. ECS Entity(index=0,
                // generation=0) is valid and packs to zero, so offset the
                // packed identifier to keep that first water body usable.
                result.waterBodyKey=resolved.body.valid()?
                    resolved.body.packed()+1ull:0ull;
                const auto cmPerSec=[](Fixed speed)->int32_t{
                    const int64_t cap=100ll*Fixed::kOne;
                    return int32_t(std::clamp<int64_t>(
                        speed.raw,-cap,cap)*100/Fixed::kOne);
                };
                result.currentXcmPerSec=cmPerSec(resolved.flow.x);
                result.currentZcmPerSec=cmPerSec(resolved.flow.z);
            }
        }
        return result;
    };

    auto report=advanceForestNutrientCycle(
        next,completedTick,sample,settings);
    World staged=ecs;
    // Cursor, last tick and nutrient ledger always persist on a due step,
    // even when no units were transported: rollback must replay the same
    // scheduled sample order.
    staged.setResource<ForestDeadwoodResource>(std::move(next));
    ecs=std::move(staged);
    return report;
}

// Explicit authored river/outlet export. The native host verifies a real
// NEREID outlet and assigns a strictly increasing command sequence for
// deterministic replay. No water volume or flow state is mutated here.
inline uint64_t exportForestDissolvedNutrientsEcs(
        World& ecs,int64_t cellX,int64_t cellZ,
        uint64_t maximumUnits,uint64_t commandId) {
    const auto* previous=ecs.resourceConst<ForestDeadwoodResource>();
    if(!previous)
        throw std::logic_error("TerraWeave missing nutrient outflow resource");
    if(!commandId)
        throw std::invalid_argument("TerraWeave invalid nutrient outlet command");
    if(commandId<=previous->lastOutflowCommandId)return 0;
    ForestDeadwoodResource next=*previous;
    const uint64_t exported=exportForestDissolvedNutrients(
        next,cellX,cellZ,maximumUnits,commandId);
    World staged=ecs;
    staged.setResource<ForestDeadwoodResource>(std::move(next));
    ecs=std::move(staged);
    return exported;
}

inline void addTerraWeaveForestNutrientSystem(
        Simulation& sim,std::string name,
        TerraWeaveEcsAuthorityConfig cfg,
        ForestNutrientCycleSettings settings={},
        ForestNutrientWeatherSampler weather={},
        std::function<void(const ForestNutrientCycleOutcome&)> onChange={}) {
    validateEcsAuthorityConfig(cfg);
    validateForestNutrientCycleSettings(settings);
    sim.addSystem(std::move(name),
        [cfg=std::move(cfg),settings,weather=std::move(weather),
         onChange=std::move(onChange)](Simulation& current){
            auto report=advanceForestNutrientCycleEcs(
                current.world(),cfg,current.tick(),settings,weather);
            if(onChange && (report.absorbedUnits ||
                            report.leachedUnits || report.advectedUnits))
                onChange(report); // enqueue only; no Box3D/GPU here
        });
}

} // namespace aiverse::terraweave
