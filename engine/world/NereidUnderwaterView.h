// NEREID Stage 17: read-only camera-water optical state for the Prisma owner.
// This header contains no PostFx, GPU or renderer dependencies. It never
// modifies canonical ECS/physics water state or creates a second water owner.
#pragma once
#include "engine/world/NereidWaterScene.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace aiverse::world {

// A view snapshot, not a persistent ECS component. The compositor may read
// attenuation/tint, then apply its own depth-aware fullscreen postprocess.
struct NereidUnderwaterView {
    bool submerged=false;
    uint64_t waterId=0;
    double depthMetres=0;
    double waterSurfaceY=0;
    float absorption=0;
    float transmission=1;
    float deepR=0,deepG=0,deepB=0;
    float turbidity=0;
    float distortion=0, scattering=0;
};

// Priority semantics follow shared NEREID samples where possible, but only
// fully submerged camera volumes participate in this *visual* view query.
template<class EachFn>
inline NereidUnderwaterView sampleNereidUnderwaterFrom(
        EachFn&& each,Vec3 camera) {
    const NereidSceneSurface* winner=nullptr;
    uint64_t winnerId=0;
    each([&](const NereidSceneSurface& s,size_t i) {
        const auto& body=s.body;
        if(!body.enabled || body.surfaceY<=body.bottomY ||
           camera.y>=body.surfaceY || camera.y<body.bottomY)return;
        const auto* geometry=s.useGeometry?&s.geometry:nullptr;
        if(!lib::nereid::coversWaterBody(body,geometry,camera))return;
        const uint64_t id=s.id?s.id:
            ((uint64_t(1)<<63)|(uint64_t(i)+1));
        if(!winner || body.priority>winner->body.priority ||
           (body.priority==winner->body.priority &&
            (body.surfaceY>winner->body.surfaceY ||
             (body.surfaceY==winner->body.surfaceY && id<winnerId)))) {
            winner=&s;winnerId=id;
        }
    });
    NereidUnderwaterView out;
    if(!winner)return out;
    const auto& body=winner->body;
    const auto& style=winner->style;
    out.submerged=true;
    out.waterId=winnerId;
    out.waterSurfaceY=body.surfaceY.toDouble();
    out.depthMetres=(body.surfaceY-camera.y).toDouble();
    out.absorption=std::clamp(style.absorption,0.f,4.f);
    out.transmission=float(std::exp(-std::clamp(out.depthMetres,0.0,10000.0)*
                                     double(out.absorption)));
    out.deepR=std::clamp(style.deepR,0.f,1.f);
    out.deepG=std::clamp(style.deepG,0.f,1.f);
    out.deepB=std::clamp(style.deepB,0.f,1.f);
    out.turbidity=1.f-out.transmission;
    out.distortion=std::clamp(style.underwaterDistortion,0.f,1.f);
    out.scattering=std::clamp(style.underwaterScattering,0.f,1.f);
    return out;
}
inline NereidUnderwaterView sampleNereidUnderwater(
        const std::vector<NereidSceneSurface>& surfaces,Vec3 camera) {
    return sampleNereidUnderwaterFrom([&](auto&& visit) {
        for(size_t i=0;i<surfaces.size();++i)visit(surfaces[i],i);
    },camera);
}
inline NereidUnderwaterView sampleNereidUnderwater(
        const NereidWaterScene& scene,Vec3 camera) {
    return sampleNereidUnderwaterFrom([&](auto&& visit) {
        size_t i=0;
        scene.forEachSource([&](const NereidSceneSurface& s) {
            visit(s,i++);
        });
    },camera);
}
} // namespace aiverse::world
