// TerraWeave Stages 65–70 — compile/link smoke for the complete native
// composition, NEREID micro water and optional PHYSIS bridge include path.
// This deliberately instantiates no real simulation: the independent
// runtime integration fixtures must prove ticks/save/rollback/Box3D.
// The primary benefit is detecting broken C++/header/API integration.
#include "examples/world/terraweave_native/Integration.h"
#include "examples/world/terraweave_native/PhysicsIntegration.h"
#include "engine/world/TerraWeaveNereidMicroNutrients.h"
#include "engine/world/TerraWeavePhysisNutrientBridge.h"
#include <cassert>
#include <cstdint>
#include <type_traits>

int main() {
    using namespace aiverse;
    using namespace aiverse::terraweave;
    static_assert(ForestDeadwoodResource::kMicroWaterCellCm==400);
    static_assert(ForestDeadwoodResource::kNutrientCellCm==3200);
    static_assert(std::is_copy_constructible_v<ForestDeadwoodResource>);
    static_assert(std::is_default_constructible_v<ForestMicroOutcome>);
    ForestDeadwoodResource saved;
    assert(saved.version==3 && saved.microWater.empty());
    assert(deadwoodLedgerBalanced(saved));
    const ForestMicroKey one{1,0,0};
    const std::pair<int64_t,int64_t> expected{0,0};
    assert(microToCoarse(one)==expected);
    // TerraWeave-only Linux build must compile without the PHYSIS
    // header from another agent's feature branch. Native opt-in checks
    // weather availability explicitly and never invents climate state.
    (void)terraweavePhysisWeatherAvailable();
    ForestMicroSettings cfg;
    validateForestMicroSettings(cfg);
    return 0;
}
