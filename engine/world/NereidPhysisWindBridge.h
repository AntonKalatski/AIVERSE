// NEREID -> PHYSIS (or any weather module) presentation-only adapter.
// PHYSIS owns the weather simulation; the host passes its read-only wind
// sample after the ECS read barrier. No dependency on PHYSIS internals or
// a particular game genre is introduced here.
#pragma once
#include "engine/world/WorldScene.h"
#include "engine/world/NereidWindPresentation.h"

namespace aiverse::world {

inline void applyNereidPhysisWindSample(
        WorldScene& scene,Vec3 worldWind,Fixed gust,
        Fixed waterCoupling=1_fx) {
    scene.setNereidVisualWind(
        nereidVisualWindFromSample(worldWind,gust,waterCoupling));
}

// Clear visual weather when unloading, changing levels or disconnecting the
// upstream PHYSIS source; the next supplied snapshot may set it again.
inline void applyNereidPhysisWindZones(
        WorldScene& scene,std::vector<NereidWindZone> zones) {
    scene.setNereidWindZones(std::move(zones));
}
inline void clearNereidPhysisWind(WorldScene& scene) {
    scene.setNereidVisualWind({});
    scene.setNereidWindZones({});
}
} // namespace aiverse::world
