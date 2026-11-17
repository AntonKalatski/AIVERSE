// Measure World deep copy vs opt-in pool-COW snapshots.
// c++ -std=c++20 -O3 -DNDEBUG -pthread -I. tests/ecs_cow_bench.cpp -o /tmp/aiverse-ecs-cow-bench
#include "engine/core/ECS.h"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace aiverse;
struct Position{int64_t x=0,y=0,z=0;};
struct Velocity{int64_t x=1,y=2,z=3;};
struct StaticData{int64_t data[4]{};};
using Clock=std::chrono::steady_clock;
static World seed(uint32_t count) {
    World w;
    for(uint32_t i=0;i<count;++i) {
        Entity e=w.create();
        w.add<Position>(e,Position{int64_t(i),0,0});
        w.add<Velocity>(e);
        w.add<StaticData>(e,StaticData{});
    }
    return w;
}
static void run(uint32_t count,bool cow,bool change) {
    World w=seed(count);
    w.enableCowSnapshots(cow);
    std::vector<World> snapshots;
    snapshots.reserve(12);
    const auto start=Clock::now();
    for(int tick=0;tick<12;++tick) {
        snapshots.emplace_back(w);
        if(change) {
            // Only the outer Pool descriptor and touched 256-element page detach.
            w.get<Position>(Entity{uint32_t(tick),0}).x+=1;
        }
    }
    const auto elapsed=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    const size_t shared=w.sharedPoolCount();
    const size_t sharedResources=w.sharedResourceCount();
    const size_t sharedPages=w.sharedCowPageCount();
    if(change) {
        // Original snapshots must remain immutable despite further mutations.
        if(snapshots[0].get<Position>(Entity{0,0}).x!=0)std::abort();
        if(w.get<Position>(Entity{0,0}).x!=1)std::abort();
    }
    std::cout<<(cow?"cow":"deep")<<","<<count<<","
             <<(change?"one-write-per-tick":"read-only")<<","<<elapsed
             <<","<<shared<<","<<sharedResources<<","<<sharedPages<<"\n";
}
int main(){
    std::cout<<"mode,entities,changes,12snapshots_ms,shared_pools,shared_resources,shared_payload_pages\n";
    for(uint32_t n:{10000u,100000u,1000000u})
        for(bool change:{false,true}) {
            run(n,false,change);run(n,true,change);
        }
}
