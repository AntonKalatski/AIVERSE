// PHYSIS automatic install from the map "physis" block; see PhysisAutoInstall.cpp.
#pragma once
#include "engine/data/Json.h"

namespace aiverse {
class Simulation;
namespace physis {
// True when PHYSIS was installed by this call. Throws on a malformed block.
bool autoInstallFromMap(Simulation& sim, const Json& map);
} // namespace physis
} // namespace aiverse
