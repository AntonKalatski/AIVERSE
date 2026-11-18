// AIVERSE NEREID -- opt-in ECS sampling; no FPS/RTS or rendering coupling.
#pragma once
#include <aiverselib/water/NereidWater.h>
#include "engine/sim/CommonComponents.h"
#include "engine/sim/Simulation.h"
#include <string>
#include <utility>

namespace aiverse::lib::nereid {

// Selection is independent of ECS pool insertion order. Higher priority wins,
// then higher surface, then smallest generation-safe Entity ID. No RNG / floats.
inline WaterSample sampleWater(const World& world,Vec3 position) {
    WaterSample sample;
    world.eachExistingConst<WaterBodyComponent>(
        [&](Entity entity,const WaterBodyComponent& body) {
            if(!world.alive(entity) || !coversXZ(body,position))return;
            const bool better=!sample.found ||
                body.priority>sample.priority ||
                (body.priority==sample.priority &&
                 (body.surfaceY>sample.surfaceY ||
                  (body.surfaceY==sample.surfaceY && entity.packed()<sample.body.packed())));
            if(!better)return;
            sample.body=entity;
            sample.kind=body.kind;
            sample.surfaceY=body.surfaceY;
            sample.bottomY=body.bottomY;
            sample.flow=body.flow;
            sample.priority=body.priority;
            sample.depth=fxClamp(body.surfaceY-position.y,Fixed{},body.surfaceY-body.bottomY);
            sample.found=true;
            sample.submerged=position.y<body.surfaceY && position.y>=body.bottomY;
        });
    return sample;
}

// Optional adapter to the existing shared world Transform, after locomotion/
// integration and before water sampling. Projects may supply probes otherwise.
inline void addWaterProbeFromTransformSystem(Simulation& sim,std::string name) {
    sim.addSystem(std::move(name),[](Simulation& current) {
        World& world=current.world();
        auto* probes=world.prepareExistingWrite<WaterProbeComponent>();
        if(!probes)return;
        const World& read=world;
        for(size_t i=0;i<probes->size();++i) {
            const Entity e=probes->entityAt(i);
            if(!world.alive(e))continue;
            if(const auto* t=read.tryGetConst<Transform>(e))
                probes->dataAt(i).position=t->pos;
        }
    });
}

// Read: WaterBody + WaterProbe. Write: WaterContact only. No structural changes.
// Complexity O(bodies * probes); broadphase is a future NEREID stage.
inline void addWaterSamplingSystem(Simulation& sim,std::string name) {
    sim.addSystem(std::move(name),[](Simulation& current) {
        World& world=current.world();
        auto* contacts=world.prepareExistingWrite<WaterContactComponent>();
        if(!contacts)return;
        const World& read=world;
        for(size_t i=0;i<contacts->size();++i) {
            const Entity e=contacts->entityAt(i);
            if(!world.alive(e))continue;
            const auto* probe=read.tryGetConst<WaterProbeComponent>(e);
            auto& contact=contacts->dataAt(i);
            contact.sample=probe?sampleWater(read,probe->position):WaterSample{};
            contact.sampledTick=current.tick();
        }
    });
}

// Native-project integration; registered persistence includes configuration,
// query position AND sampled gameplay data for save/replay/checksum.
inline void registerNereidWaterPersistence(Simulation& sim) {
    sim.registerPersistentComponent<WaterBodyComponent>("nereid.water_body",1);
    sim.registerPersistentComponent<WaterProbeComponent>("nereid.water_probe",1);
    sim.registerPersistentComponent<WaterContactComponent>("nereid.water_contact",1);
}

} // namespace aiverse::lib::nereid
