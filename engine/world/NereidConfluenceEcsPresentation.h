// NEREID Stage 42 — immutable ECS confluence snapshot for WorldScene.
// Call after a completed ECS read barrier, and publish water surfaces from
// the same tick. Generation-safe Entity IDs are encoded as packed()+1.
#pragma once
#include "engine/world/NereidConfluenceScene.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace aiverse::world {
inline std::vector<NereidConfluenceSource> snapshotNereidEcsConfluences(
        const World& ecs) {
    std::vector<NereidConfluenceSource> out;
    ecs.eachExistingConst<lib::nereid::WaterConfluenceComponent>(
        [&](Entity e,const lib::nereid::WaterConfluenceComponent& junction) {
            if(!ecs.alive(e) || !junction.enabled)return;
            if(!lib::nereid::validWaterConfluence(junction))
                throw std::invalid_argument(
                    "nereid.confluence: invalid ECS confluence policy");
            if(out.size()>=NereidConfluenceScene::kMaxSources)
                throw std::invalid_argument(
                    "nereid.confluence: scene source capacity exceeded");
            if(uint64_t(e.packed())==UINT64_MAX)return;
            if(!ecs.alive(junction.outlet) ||
               !ecs.tryGetConst<lib::nereid::WaterBodyComponent>(
                   junction.outlet))return;
            NereidConfluenceSource item;
            item.id=uint64_t(e.packed())+1;
            item.junction=junction;
            item.outletWaterId=uint64_t(junction.outlet.packed())+1;
            for(size_t i=0;i<junction.inletCount;++i) {
                const Entity inlet=junction.inlets[i];
                if(!ecs.alive(inlet) ||
                   !ecs.tryGetConst<lib::nereid::WaterBodyComponent>(inlet))
                    return; // no half-resolved confluences
                const uint64_t packed=uint64_t(inlet.packed());
                if(packed==UINT64_MAX)return;
                item.inletWaterIds[i]=packed+1;
            }
            out.push_back(std::move(item));
        });
    std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){
        return a.id<b.id;
    });
    return out;
}
} // namespace aiverse::world
