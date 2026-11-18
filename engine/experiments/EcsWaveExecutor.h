// Experimental opt-in executor for dependency-compatible system waves.
// Not integrated with Simulation::step(). Callers must declare EVERY component,
// resource, RNG/event access, ordered effect, and structural mutation correctly.
#pragma once
#include "EcsDependencyPlan.h"
#include "EcsExperiments.h"
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::ecs_experiment {

class WaveExecutor {
public:
    using SystemFn = std::function<void()>;
    explicit WaveExecutor(PersistentWorkers& workers,
                          size_t minSystemsPerWave=2)
        : workers_(workers), minSystemsPerWave_(minSystemsPerWave) {}

    // Commands and structural writes must be completed only after all jobs
    // in a wave have joined. The caller owns the barrier/flush between waves.
    // Failure rethrows after all tasks in the wave finish. The world may be
    // partially modified; caller must discard/rebuild from a valid snapshot.
    void run(const DependencyPlan& plan,
             const std::vector<SystemFn>& functions,
             const std::function<void(size_t)>& afterWave={}) {
        if (functions.size()!=plan.systems().size())
            throw std::invalid_argument("system/function count mismatch");
        const auto groups=plan.waves();
        for(size_t wave=0;wave<groups.size();++wave) {
            const auto& group=groups[wave];
            if(workers_.workers()==0 || group.size()<minSystemsPerWave_) {
                for(size_t index:group)functions[index]();
            } else {
                workers_.parallelFor(group.size(),[&](size_t begin,size_t end) {
                    for(size_t i=begin;i<end;++i)functions[group[i]]();
                });
            }
            if(afterWave)afterWave(wave);
        }
    }
private:
    PersistentWorkers& workers_;
    size_t minSystemsPerWave_;
};

} // namespace aiverse::ecs_experiment
