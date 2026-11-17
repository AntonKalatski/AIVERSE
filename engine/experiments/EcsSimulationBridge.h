// Opt-in bridge: register one checked dependency-wave group as a normal Simulation
// system. All existing Simulation systems keep their exact sequential behavior.
// Registered group callbacks see ONLY GuardedWorld, not Simulation/World.
// This is experimental and not a hard sandbox against external captures.
#pragma once
#include "EcsGuardedWorld.h"
#include "../sim/Simulation.h"
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::ecs_experiment {

struct GuardedSystem {
    SystemAccess access;
    std::function<void(GuardedWorld&)> run;
};

// Use only after all non-job structural setup is done. The protected group cannot
// create/destroy entities, change component composition, or emit ordered events.
// Its callbacks may read/write existing components and registered World resources
// exclusively through GuardedWorld, with complete read/write declarations.
struct GuardedGroupOptions {
    // Debug/safety mode: a deep-copy snapshot is taken before the group.
    // On failure, restore World before propagating the exception. Expensive;
    // it is not a replacement for full Simulation-level transactional ticks.
    bool restoreWorldOnFailure=false;
};

inline void addGuardedGroup(Simulation& simulation,
                            std::string groupName,
                            std::vector<GuardedSystem> systems,
                            unsigned workerCount=0,
                            GuardedGroupOptions options={}) {
    if(systems.empty()) throw std::invalid_argument("empty guarded group");
    for(const auto& system:systems) {
        if(!system.run)throw std::invalid_argument("empty guarded system");
        if(system.access.structural || system.access.orderedSideEffects)
            throw std::invalid_argument("guarded group forbids structural and ordered effects");
    }
    auto pool=std::make_shared<PersistentWorkers>(workerCount);
    simulation.addSystem(std::move(groupName),
        [systems=std::move(systems),pool=std::move(pool),options](Simulation& sim) {
            DependencyPlan plan;
            for(const auto& system:systems)plan.add(system.access);
            GuardedWorld world{sim.world()};
            WaveExecutor executor{*pool};
            std::vector<WaveExecutor::SystemFn> jobs;
            jobs.reserve(systems.size());
            for(size_t i=0;i<systems.size();++i)
                jobs.emplace_back([&world,&systems,i] {systems[i].run(world);});
            // No structural changes inside this group, so barrier is no-op.
            // Simulation::step() continues to flush once after the group.
            if(!options.restoreWorldOnFailure) {
                runChecked(executor,plan,jobs);
            } else {
                World snapshot=sim.world();
                try {
                    runChecked(executor,plan,jobs);
                } catch(...) {
                    // All workers have joined before runChecked rethrows.
                    // GameModule state outside World is not rolled back here.
                    sim.world().copyFrom(snapshot);
                    throw;
                }
            }
        });
}

} // namespace aiverse::ecs_experiment
