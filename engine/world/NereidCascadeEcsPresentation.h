// Read-only ECS -> waterfall/cascade presentation adapter. This is not
// authoritative simulation state and performs no GPU allocations.
#pragma once
#include "engine/world/NereidCascadeScene.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace aiverse::world {
struct NereidCascadeEcsOptions {
    lib::nereid::WaterSurfaceStyle style{};
    lib::nereid::WaterCascadeMeshOptions mesh{};
};

inline std::vector<NereidCascadeSource> snapshotNereidEcsCascades(
        const World& ecs,NereidCascadeEcsOptions options={}) {
    std::vector<NereidCascadeSource> out;
    ecs.eachExistingConst<lib::nereid::WaterCascadeComponent>(
        [&](Entity entity,const lib::nereid::WaterCascadeComponent& cascade) {
            if(!ecs.alive(entity) || !cascade.enabled)return;
            if(out.size()>=NereidCascadeScene::kMaxCascades)
                throw std::invalid_argument("nereid.cascade: ECS scene limit exceeded");
            const uint64_t id=uint64_t(entity.packed());
            if(id==UINT64_MAX)
                throw std::invalid_argument("nereid.cascade: invalid Entity ID");
            NereidCascadeSource item;
            item.id=id+1;
            item.cascade=cascade;
            item.style=options.style;
            item.options=options.mesh;
            out.push_back(std::move(item));
        });
    std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){
        return a.id<b.id;
    });
    return out;
}
} // namespace aiverse::world
