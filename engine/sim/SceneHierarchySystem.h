// Opt-in integration of the deterministic Parent hierarchy into Simulation.
// Install after GameModule::setup creates its initial ECS entities.
#pragma once
#include "SceneHierarchy.h"
#include "Simulation.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
namespace aiverse {
inline void installSceneHierarchy(Simulation& simulation,
                                  const std::shared_ptr<SceneHierarchy>& hierarchy,
                                  std::string systemName="scene-hierarchy") {
    if(!hierarchy)throw std::invalid_argument("SceneHierarchy is null");
    hierarchy->rebuild(simulation.world());
    hierarchy->update(simulation.world());
    simulation.addStateRestoredHook([hierarchy](Simulation& s){
        hierarchy->rebuild(s.world());
        hierarchy->update(s.world());
    });
    // Register at the desired point in the tick. For example: after movement,
    // before view/export. Calls update() only on changed hierarchy branches.
    simulation.addSystem(std::move(systemName),[hierarchy](Simulation& s){
        hierarchy->update(s.world());
    });
}
} // namespace aiverse
