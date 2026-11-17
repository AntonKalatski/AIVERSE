// c++ -std=c++20 -O3 -pthread -I. tests/ecs_experiments_test.cpp -o /tmp/aiverse-ecs-experiments-test
// /tmp/aiverse-ecs-experiments-test
#include "engine/experiments/EcsExperiments.h"
#include "engine/experiments/EcsDependencyPlan.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <atomic>
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
    // Enqueue during a command callback must not invalidate the draining vector;
    // the newly enqueued command is intentionally deferred to the next flush.
    {
        World w;
        DeterministicCommands commands(2);
        int count=0;
        commands.enqueue(0,[&](World&) {
            ++count;
            commands.enqueue(0,[&](World&){count+=10;});
        });
        commands.enqueue(1,[&](World&){count+=2;});
        commands.flush(w);
        assert(count==3);
        commands.flush(w);
        assert(count==13);
        commands.enqueue(0,[&](World&){count+=100;});
        commands.discard();
        commands.flush(w);
        assert(count==13);
    }
    // Each deterministic lane has one producer until flush/discard.
    // A different thread must be rejected rather than racing on vector storage.
    {
        World w;
        DeterministicCommands commands(2);
        int sequence=0;
        commands.enqueue(0,[&](World&){sequence=sequence*10+1;});
        std::atomic<bool> rejected{false};
        std::thread competing([&] {
            try {
                commands.enqueue(0,[&](World&){sequence=999;});
            } catch(const std::logic_error&) {rejected.store(true);}
        });
        competing.join();
        assert(rejected.load());
        std::thread independent([&] {
            commands.enqueue(1,[&](World&){sequence=sequence*10+2;});
        });
        independent.join();
        commands.flush(w);
        assert(sequence==12);
        // Ownership is reset after draining, another thread can own that lane.
        std::thread next([&] {
            commands.enqueue(0,[&](World&){sequence=sequence*10+3;});
        });
        next.join();
        commands.flush(w);
        assert(sequence==123);
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
    // Adaptive threshold uses caller for small jobs; parallel range assignment stays deterministic.
    {
        PersistentWorkers workers(3);
        std::vector<Position> first(500),second(500);
        World a=initial,b=initial;
        eachAdaptive<Position>(a,workers,ExecutionMode::Auto,{200000,2048},
            [](Entity,Position& p){p.x+=2;});
        eachAdaptive<Position>(b,workers,ExecutionMode::Sequential,{},
            [](Entity,Position& p){p.x+=2;});
        assert(result(a)==result(b));
        eachAdaptive<Position>(a,workers,ExecutionMode::Auto,{1024,64},
            [](Entity,Position& p){p.x+=3;});
        eachAdaptive<Position>(b,workers,ExecutionMode::Sequential,{},
            [](Entity,Position& p){p.x+=3;});
        assert(result(a)==result(b));

        // Exceptions from workers AND caller must propagate; the worker pool remains reusable.
        bool caught=false;
        try {
            workers.parallelFor(10000,[](size_t begin,size_t end){
                if(begin<end)throw std::runtime_error("worker or caller failed");
            });
        } catch(const std::runtime_error&) {caught=true;}
        assert(caught);
        std::atomic<size_t> processed{0};
        workers.parallelFor(10000,[&](size_t begin,size_t end){processed.fetch_add(end-begin);});
        assert(processed==10000);
        // Nested use of the same pool must fail fast, not deadlock.
        std::atomic<size_t> nestedRejected{0};
        workers.parallelFor(512,[&](size_t begin,size_t end) {
            if(begin==end)return;
            try {
                workers.parallelFor(8,[](size_t,size_t){});
            } catch(const std::logic_error&) {
                nestedRejected.fetch_add(1);
            }
        });
        assert(nestedRejected==4);

    }
    {
        DependencyPlan plan;
        plan.add({"move",{}, {1}, {}, {}, false,false});
        plan.add({"independent",{}, {2}, {}, {}, false,false});
        plan.add({"read_positions",{1}, {}, {}, {}, false,false});
        plan.add({"write_positions",{}, {1}, {}, {}, false,false});
        plan.add({"rng",{}, {}, {}, {7}, false,false});
        plan.add({"rng_read",{}, {}, {7}, {}, false,false});
        plan.add({"spawn",{}, {}, {}, {}, true,false});
        auto waves=plan.waves();
        assert(waves.size()>=3);
        assert(waves[0].size()>=2);
        auto indexOf=[&](size_t i) {
            for(size_t w=0;w<waves.size();++w)
                for(size_t j:waves[w])if(j==i)return w;
            return waves.size();
        };
        assert(indexOf(2)>indexOf(0));
        assert(indexOf(3)>indexOf(2));
        assert(indexOf(5)>indexOf(4));
        assert(indexOf(6)>indexOf(5));
    }
    std::cout<<"PASS prepared, adaptive jobs, exceptions, dependency waves, deterministic commands, dirty pages\n";
}
