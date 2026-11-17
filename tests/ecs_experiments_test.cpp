// c++ -std=c++20 -O3 -pthread -I. tests/ecs_experiments_test.cpp -o /tmp/aiverse-ecs-experiments-test
// /tmp/aiverse-ecs-experiments-test
#include "engine/experiments/EcsExperiments.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
using namespace aiverse;
using namespace aiverse::ecs_experiment;
struct Position { int64_t x=0; };
struct Velocity { int64_t x=1; };
static uint64_t result(World& world) {
    uint64_t hash=1469598103934665603ull;
    auto& p=world.pool<Position>();
    for(size_t i=0;i<p.size();++i) {
        hash=(hash^p.entityAt(i).packed())*1099511628211ull;
        hash=(hash^uint64_t(p.dataAt(i).x))*1099511628211ull;
    }
    return hash;
}
int main() {
    constexpr uint32_t count=100000;
    World initial;
    for(uint32_t i=0;i<count;++i) {
        auto e=initial.create();
        initial.add<Position>(e,Position{int64_t(i)});
        if(i%4==0) initial.add<Velocity>(e,Velocity{});
    }
    World sequential=initial,parallel=initial;
    auto q=PreparedAll<Position,Velocity>{};
    q.each(sequential,[](Entity,Position& p,Velocity& v){p.x+=v.x;});
    {
        PersistentWorkers workers(3);
        // Independent per-entity writes only; filters inspect immutable pool.
        eachParallel<Position>(parallel,workers,[&](Entity e,Position& p){
            if(auto* velocity=parallel.tryGet<Velocity>(e)) p.x+=velocity->x;
        });
        assert(result(sequential)==result(parallel));
        for(int iter=0;iter<5;++iter) {
            q.each(sequential,[](Entity,Position& p,Velocity& v){p.x+=v.x;});
            eachParallel<Position>(parallel,workers,[&](Entity e,Position& p){
                if(auto* velocity=parallel.tryGet<Velocity>(e)) p.x+=velocity->x;
            });
            assert(result(sequential)==result(parallel));
        }
    }
    // Deferred structural operations have a well-defined lane order.
    {
        DeterministicCommands buffer(2);
        Entity e=parallel.create();
        buffer.enqueue(1,[e](World& w){w.add<Velocity>(e,Velocity{7});});
        buffer.enqueue(0,[e](World& w){w.add<Position>(e,Position{9});});
        buffer.flush(parallel);
        assert(parallel.has<Position>(e)&&parallel.has<Velocity>(e));
        assert(parallel.get<Position>(e).x==9);
        assert(parallel.get<Velocity>(e).x==7);
        buffer.enqueue(0,[e](World& w){w.destroyLater(e);});
        buffer.flush(parallel);
        assert(!parallel.alive(e));
        auto reused=parallel.create();
        assert(reused.index==e.index && reused.gen!=e.gen);
    }
    // Dirty snapshot applies only marked pages; unmarked data is preserved.
    {
        std::vector<Position> before(1024),updated(1024);
        for(size_t i=0;i<before.size();++i) before[i].x=int64_t(i);
        updated=before;
        updated[3].x=100;
        updated[800].x=200;
        DirtyPageSnapshot<Position,128> delta;
        delta.begin(updated.size());
        delta.mark(3);delta.mark(800);
        delta.capture(updated);
        assert(delta.pageCount()==2);
        assert(delta.bytes()==256*sizeof(Position));
        delta.apply(before);
        for(size_t i=0;i<before.size();++i) assert(before[i].x==updated[i].x);
    }
    std::cout<<"PASS prepared, persistent parallel, deterministic commands, dirty pages\n";
}
