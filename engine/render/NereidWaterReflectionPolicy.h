// NEREID: one planar reflection pass must never be sampled at another Y.
// This is render/presentation policy, never a deterministic gameplay input.
#pragma once
#include <cmath>
namespace aiverse::render {
inline bool sameWaterReflectionPlane(float surfaceY,float planarY,float tolerance=0.025f) {
    return std::isfinite(surfaceY) && std::isfinite(planarY) &&
           std::isfinite(tolerance) && tolerance>=0.f &&
           std::abs(surfaceY-planarY)<=tolerance;
}
} // namespace aiverse::render
