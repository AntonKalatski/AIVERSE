// c++ -std=c++20 -O2 -pthread -I. tests/ecs_wave_executor_test.cpp -o /tmp/aiverse-wave-test
// /tmp/aiverse-wave-test
#include "engine/experiments/EcsWaveExecutor.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace aiverse;
using namespace aiverse::ecs_experiment;
struct Position {int32_t x=0;};
struct Velocity {int32_t x=0;};
struct Output {int32_t value=0;};

static uint64_t execute(unsigned workersCount) {
    World world;
    constexpr size_t count=8192;
    for(size_t i=0;i<count;++i) {
        Entity e=world.create();
        world.add<Position>(e,Position{int32_t(i)});
        world.add<Velocity>(e,Velocity{int32_t(i*2)});
        world.add<Output>(e);
    }
    auto& positions=world.pool<Position>();
    auto& velocities=world.pool<Velocity>();
    auto& outputs=world.pool<Output>();
    DependencyPlan plan;
    plan.add({"positions",{}, {1}, {}, {}, false,false});
    plan.add({"velocities",{}, {2}, {}, {}, false,false});
    plan.add({"combine",{1,2}, {3}, {}, {}, false,false});
    plan.add({"global side effect",{}, {}, {}, {}, false,true});
    auto waves=plan.waves();
    assert(waves.size()==3);
    assert(waves[0].size()==2);
    std::vector<int> after;
    std::vector<WaveExecutor::SystemFn> jobs{
        [&]{for(size_t i=0;i<count;++i)positions.dataAt(i).x+=1;},
        [&]{for(size_t i=0;i<count;++i)velocities.dataAt(i).x*=2;},
        [&]{for(size_t i=0;i<count;++i)
            outputs.dataAt(i).value=positions.dataAt(i).x+velocities.dataAt(i).x;},
        [&]{assert(outputs.dataAt(0).value==1);}
    };
    PersistentWorkers pool{workersCount};
    WaveExecutor executor{pool};
    executor.run(plan,jobs,[&](size_t wave){after.push_back(int(wave));});
    assert((after==std::vector<int>{0,1,2}));
    uint64_t hash=1469598103934665603ull;
    for(size_t i=0;i<count;++i) {
        const int32_t expected=int32_t(i*5+1);
        assert(outputs.dataAt(i).value==expected);
        hash=(hash^uint64_t(outputs.dataAt(i).value))*1099511628211ull;
    }
    return hash;
}
static void testFailure() {
    PersistentWorkers workers{3};
    WaveExecutor executor{workers};
    DependencyPlan plan;
    plan.add({"throws",{}, {1}, {}, {}, false,false});
    plan.add({"completes",{}, {2}, {}, {}, false,false});
    int completed=0;
    std::vector<WaveExecutor::SystemFn> jobs{
        []{throw std::runtime_error("expected failure");},
        [&]{++completed;}
    };
    bool failed=false;
    try{executor.run(plan,jobs);}
    catch(const std::runtime_error&){failed=true;}
    assert(failed && completed==1);
    jobs[0]=[]{};
    executor.run(plan,jobs);
    assert(completed==2);
}
int main(){
    assert(execute(0)==execute(3));
    testFailure();
    std::cout<<"PASS dependency wave execution, sequential/parallel checksum, worker recovery\n";
}
