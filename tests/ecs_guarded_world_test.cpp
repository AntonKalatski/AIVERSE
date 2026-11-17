// c++ -std=c++20 -O2 -pthread -I. tests/ecs_guarded_world_test.cpp -o /tmp/aiverse-ecs-guarded
// /tmp/aiverse-ecs-guarded
#include "engine/experiments/EcsGuardedWorld.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace aiverse;
using namespace aiverse::ecs_experiment;
struct Position {int64_t value=0;};
struct Velocity {int64_t value=0;};
struct State {int64_t value=7;};
static uint64_t run(unsigned count) {
    World w;
    for(int i=0;i<2000;++i) {
        Entity e=w.create();
        w.add<Position>(e,Position{int64_t(i)});
        w.add<Velocity>(e,Velocity{2});
    }
    w.setResource<State>(State{7});
    GuardedWorld view(w);
    const AccessId p=componentId<Position>(),v=componentId<Velocity>(),s=componentId<State>();
    DependencyPlan plan;
    plan.add({"move",{v},{p},{},{},false,false});
    plan.add({"resource",{},{},{},{s},false,false});
    plan.add({"read",{p},{},{s},{},false,false});
    // Resource and movement can run concurrently, read system waits for both.
    auto waves=plan.waves();
    assert(waves.size()==2 && waves[0].size()==2 && waves[1].size()==1);
    uint64_t sum=0;
    std::vector<WaveExecutor::SystemFn> jobs{
        [&]{view.eachWrite<Position>([&](Entity e,Position& pos){
            const auto* vel=view.read<Velocity>(e);
            assert(vel);pos.value+=vel->value;
        });},
        [&]{auto* resource=view.writeResource<State>();assert(resource);resource->value+=3;},
        [&]{const auto* state=view.readResource<State>();assert(state && state->value==10);
            view.eachRead<Position>([&](Entity,const Position& p){sum+=uint64_t(p.value);});}
    };
    PersistentWorkers workers{count};
    WaveExecutor executor{workers};
    runChecked(executor,plan,jobs);
    assert(sum==uint64_t(2000)*uint64_t(1999)/2+4000);
    return sum;
}
static void testAccessViolations() {
    World world;
    Entity e=world.create();
    world.add<Position>(e);
    world.add<Velocity>(e);
    world.setResource<State>(State{});
    GuardedWorld view(world);
    PersistentWorkers workers{3};
    WaveExecutor executor{workers};
    DependencyPlan plan;
    const AccessId p=componentId<Position>(),v=componentId<Velocity>();
    plan.add({"allowed",{p},{},{},{},false,false});
    plan.add({"denied",{p},{},{},{},false,false});
    std::vector<WaveExecutor::SystemFn> jobs{
        [&]{assert(view.read<Position>(e));},
        [&]{view.write<Velocity>(e);}
    };
    bool caught=false;
    try {runChecked(executor,plan,jobs);}
    catch(const AccessViolation&){caught=true;}
    assert(caught);
    // The worker pool remains reusable after a violation.
    jobs[1]=[&]{assert(view.read<Position>(e));};
    runChecked(executor,plan,jobs);
    // Component write needs explicit Writes.
    {
        SystemAccess a{"check",{p},{},{},{},false,false};
        AccessScope scope(a);
        caught=false;
        try{view.write<Position>(e);}catch(const AccessViolation&){caught=true;}
        assert(caught);
        caught=false;
        try{view.create();}catch(const AccessViolation&){caught=true;}
        assert(caught);
        caught=false;
        try{view.readResource<State>();}catch(const AccessViolation&){caught=true;}
        assert(caught);
    }
    // A structural modification requires both structural and write access.
    {
        SystemAccess a{"mutation",{}, {v},{},{},true,false};
        AccessScope scope(a);
        view.remove<Velocity>(e);
        assert(!world.has<Velocity>(e));
    }
    caught=false;
    try{view.read<Position>(e);}catch(const AccessViolation&){caught=true;}
    assert(caught);
}
int main(){
    assert(run(0)==run(3));
    testAccessViolations();
    std::cout<<"PASS guarded access, dependency barriers, parallel parity, exception recovery\n";
}
