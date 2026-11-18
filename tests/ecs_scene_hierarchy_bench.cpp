// Standalone incremental transform hierarchy microbenchmark (not a frame FPS claim).
// c++ -std=c++20 -O3 -DNDEBUG -I. tests/ecs_scene_hierarchy_bench.cpp -o /tmp/hierarchy-bench
#include "engine/sim/SceneHierarchy.h"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>
using namespace aiverse;
using Clock=std::chrono::steady_clock;
static LocalTransform loc(int n) {
    LocalTransform t;t.position.x=Fixed::fromInt(n);
    return t;
}
static double ms(Clock::time_point a,Clock::time_point b){
    return std::chrono::duration<double,std::milli>(b-a).count();
}
static void benchmark(uint32_t count) {
    World world;SceneHierarchy graph;
    std::vector<Entity> entities;
    entities.reserve(count);
    auto begin=Clock::now();
    for(uint32_t i=0;i<count;++i){
        Entity entity=world.create();
        graph.attach(world,entity,loc(1));
        entities.push_back(entity);
        // Each root has nine direct children: 10-object groups.
        if(i%10!=0)graph.setParent(world,entity,entities[i-i%10]);
    }
    auto initialized=Clock::now();
    const auto full=graph.update(world);
    auto fullEnd=Clock::now();
    const auto clean=graph.update(world);
    auto cleanEnd=Clock::now();
    // Move only every 100th child: 1% of objects and branches.
    for(uint32_t i=9;i<count;i+=100)
        graph.setLocal(world,entities[i],loc(2));
    auto dirtied=Clock::now();
    const auto changed=graph.update(world);
    auto changedEnd=Clock::now();
    const auto visited=graph.lastVisited();
    std::cout<<count<<","<<graph.size()<<","<<full<<","<<clean<<","
             <<changed<<","<<visited<<","
             <<ms(begin,initialized)<<","<<ms(initialized,fullEnd)<<","
             <<ms(fullEnd,cleanEnd)<<","<<ms(dirtied,changedEnd)<<"\n";
}
int main(){
    std::cout<<"entities,indexed,initial_updates,clean_updates,dirty_updates,dirty_visited,"
                "build_ms,initial_ms,clean_ms,dirty_ms\n";
    benchmark(10000);
    benchmark(100000);
}
