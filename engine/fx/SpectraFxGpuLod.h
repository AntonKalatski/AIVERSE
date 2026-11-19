// SpectraFX view-distance scalability for presentation-only GPU effects.
// Pure math: no graphics device access, no ECS/authoritative gameplay state.
#pragma once
#include <algorithm>
#include <cmath>
#include "../render/Math3D.h"

namespace aiverse::fx {

struct SpectraFxGpuLodSettings {
    float nearDistance=0;
    float farDistance=0; // 0 disables distance LOD
    float farSpawnScale=0.15f;
    bool cullBeyondFar=true;

    bool validate() const {
        return std::isfinite(nearDistance) &&
               std::isfinite(farDistance) &&
               std::isfinite(farSpawnScale) &&
               nearDistance>=0 && farDistance>=0 &&
               (farDistance==0 || farDistance>nearDistance) &&
               farSpawnScale>=0 && farSpawnScale<=1;
    }

    float spawnScale(float distance) const {
        if(!validate() || farDistance==0) return 1.0f;
        if(!std::isfinite(distance)) return 0;
        if(distance<=nearDistance) return 1.0f;
        if(distance>=farDistance)
            return cullBeyondFar ? 0.0f : farSpawnScale;
        const float t=(distance-nearDistance)/(farDistance-nearDistance);
        return 1.0f+(farSpawnScale-1.0f)*std::clamp(t,0.0f,1.0f);
    }
    bool visible(float distance) const {
        return validate() && (farDistance==0 || !cullBeyondFar ||
                              (std::isfinite(distance)&&distance<=farDistance));
    }
};

inline float spectraFxDistance(render::float3 a,render::float3 b) {
    const float dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;
    const double squared=double(dx)*dx+double(dy)*dy+double(dz)*dz;
    return std::isfinite(squared) ? float(std::sqrt(squared))
                                  : 1e30f;
}
} // namespace aiverse::fx
