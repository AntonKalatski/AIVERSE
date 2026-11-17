// AIVERSE ECS baseline benchmark. Build:
// c++ -std=c++20 -O3 -DNDEBUG -I. tests/ecs_architecture_bench.cpp -o /tmp/aiverse-ecs-bench
// /tmp/aiverse-ecs-bench
#include "engine/core/ECS.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace aiverse;
using Clock = std::chrono::steady_clock;
struct Position { int64_t x=0, y=0, z=0; };
struct Velocity { int64_t x=1, y=2, z=3; };
struct Health { int32_t hp=100; };
struct Tag { uint32_t value=0; };
static volatile uint64_t sink = 0;

template<class F> double measure(F&& fn) {
    const auto start=Clock::now();
    fn();
    return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
}
static uint64_t hash(World& w) {
    uint64_t h=1469598103934665603ull;
    w.each<Position,Velocity>([&](Entity e,Position& p,Velocity& v) {
        for(uint64_t x : {e.packed(),uint64_t(p.x),uint64_t(p.y),uint64_t(p.z),uint64_t(v.x)})
            { h^=x; h*=1099511628211ull; }
    });
    return h;
}
static void run(size_t n) {
    World w;
    std::vector<Entity> entities;
    entities.reserve(n);
    double spawn=measure([&] {
        for(size_t i=0;i<n;++i) {
            Entity e=w.create(); entities.push_back(e);
            w.add<Position>(e,Position{int64_t(i),0,0});
            if(i%4==0) w.add<Velocity>(e,Velocity{});
            if(i%8==0) w.add<Health>(e,Health{});
            if(i%16==0) w.add<Tag>(e,Tag{});
        }
    });
    // Two logically equivalent query orders; second should visit fewer candidates.
    double broad=measure([&] {
        for(int step=0;step<10;++step)
            w.each<Position,Velocity>([](Entity,Position& p,Velocity& v) {
                p.x+=v.x; p.y+=v.y; p.z+=v.z;
            });
    });
    const auto h1=hash(w);
    double narrow=measure([&] {
        for(int step=0;step<10;++step)
            w.each<Velocity,Position>([](Entity,Velocity& v,Position& p) {
                p.x+=v.x; p.y+=v.y; p.z+=v.z;
            });
    });
    const auto h2=hash(w);
    World snapshot;
    double copy=measure([&] { snapshot.copyFrom(w); });
    const auto hs=hash(snapshot);
    double destroy=measure([&] {
        for(size_t i=0;i<n;i+=8) w.destroyLater(entities[i]);
        w.flush();
    });
    sink = sink ^ h1 ^ h2 ^ hs;
    std::cout << "baseline," << n << "," << spawn << "," << broad
              << "," << narrow << "," << copy << "," << destroy
              << "," << h1 << "," << h2 << "," << hs << "\n";
}
int main(int argc,char** argv) {
    std::cout << "variant,entities,spawn_ms,query_broad_10x_ms,query_narrow_10x_ms,snapshot_ms,destroy_1of8_ms,hash_broad,hash_narrow,hash_snapshot\n";
    if(argc>1) run(std::strtoull(argv[1],nullptr,10));
    else for(size_t n : {size_t(10000),size_t(100000),size_t(1000000)}) run(n);
}
