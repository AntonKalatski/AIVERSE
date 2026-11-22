// PHYSIS automatic install from data (2026-11-22).
//
// Simulation::setup calls this after GameModule::setup and before the
// persistence registry is sealed, for EVERY game module (no genre switch).
// A map with an enabled "physis" block now runs the nature simulation in
// every host that builds a Simulation from it — client, headless, server,
// editor play-test — so lockstep peers and replays agree.
//
// Skipped when:
//   * the map has no "physis" block or "physis.enabled" is false;
//   * "physis.autoInstall" is false (the module wires PHYSIS itself, later);
//   * the module already installed PHYSIS in its own setup().
// "physis.trackedFields": true selects installTrackedFromMap (stable authored
// field identities across saves) instead of installFromMap.
// Build switch: CMake AIVERSE_SIM_PHYSIS=OFF compiles this file out.
#include "engine/physis/PhysisAutoInstall.h"
#include "engine/physis/PhysisProjectConfig.h"
#include <stdexcept>

namespace aiverse::physis {

bool autoInstallFromMap(Simulation& sim, const Json& map) {
    const Json& block = map["physis"];
    if (block.isNull()) return false;
    if (!block.isObject())
        throw std::invalid_argument("PHYSIS map 'physis' must be an object");
    const Json& autoInstall = block["autoInstall"];
    if (!autoInstall.isNull() && autoInstall.type() != Json::Type::Bool)
        throw std::invalid_argument("PHYSIS autoInstall must be boolean");
    if (!autoInstall.boolean(true)) return false;
    if (sim.world().resourceConst<State>()) return false;   // the module did it
    const Json& tracked = block["trackedFields"];
    if (!tracked.isNull() && tracked.type() != Json::Type::Bool)
        throw std::invalid_argument("PHYSIS trackedFields must be boolean");
    return tracked.boolean(false) ? installTrackedFromMap(sim, map) : installFromMap(sim, map);
}

} // namespace aiverse::physis
