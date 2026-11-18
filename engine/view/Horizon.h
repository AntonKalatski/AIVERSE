// AIVERSE View: spherical horizon occlusion for any curved visual world.
// The calling host owns its own geometry, camera transform and LOD policy.
// Presentation-only: do not use in authoritative physics or hit testing.
#pragma once
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>

namespace aiverse::view {
// Camera is in the spherical body's local frame; cap direction and cosine
// bound the angular footprint of a surface chunk. maxSurfaceRadius bounds
// the furthest surface point in the cap, not its rendered sprite radius.
// Returns true only when the whole cap is below the occluder's horizon.
inline bool sphericalCapHiddenByHorizon(
        render::float3 cameraLocal,float occluderRadius,
        render::float3 capDirection,float cosCapHalfAngle,
        float maxSurfaceRadius) {
    const float eyeRadius=render::length(cameraLocal);
    if(!(eyeRadius>occluderRadius) || !(occluderRadius>0.f))
        return false; // inside sphere (or invalid geometry): no false cull
    const render::float3 eyeDir=cameraLocal*(1.f/eyeRadius);
    const float eyeAngle=std::acos(std::clamp(
        render::dot(eyeDir,capDirection),-1.f,1.f));
    const float capAngle=std::acos(std::clamp(cosCapHalfAngle,-1.f,1.f));
    const float horizonAngle=
        std::acos(occluderRadius/eyeRadius)+
        std::acos(std::clamp(occluderRadius/
            std::max(maxSurfaceRadius,occluderRadius),-1.f,1.f));
    return eyeAngle-capAngle>horizonAngle;
}
} // namespace aiverse::view
