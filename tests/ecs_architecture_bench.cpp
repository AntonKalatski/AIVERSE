// Standalone ECS query benchmark (no production ECS changes).
// c++ -std=c++20 -O3 -DNDEBUG -I. tests/ecs_architecture_bench.cpp -o /tmp/aiverse-ecs-bench
// /tmp/aiverse-ecs-bench [entity_count]
#include "engine/core/ECS.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace aiverse;
using Clock=std::chrono::steady_clock;
struct Position { int64_t x=0,y=0,z=0; };
struct Velocity { int64_t x=1,y=2,z=3; };
struct Health { int32_t hp=100; };
struct Tag { uint32_t value=0; };
static volatile uint64_t observed=0;

template<class F> double elapsed(F&& fn) {
    auto t=Clock::now(); fn();
    return std::chrono::duration<double,std::milli>(Clock::now()-t).count();
}
static uint64_t checksum(World& w) {
    uint64_t h=1469598103934665603ull;
    w.each<Position>([&](Entity e,Position& p) {
        for(uint64_t v:{e.packed(),uint64_t(p.x),uint64_t(p.y),uint64_t(p.z)}) {
            h^=v; h*=1099511628211ull;
        }
    });
    return h;
}
static World makeWorld(size_t n) {
    World w;
    for(size_t i=0;i<n;++i) {
        Entity e=w.create();
        w.add<Position>(e,Position{int64_t(i),0,0});
        if(i%4==0) w.add<Velocity>(e,Velocity{});
        if(i%8==0) w.add<Health>(e,Health{});
        if(i%16==0) w.add<Tag>(e,Tag{});
    }
    return w;
}
enum class Query { Broad, Narrow, AutoSmallest };
static void update(World& w,Query query) {
    auto broad=[&] {
        w.each<Position,Velocity>([](Entity,Position& p,Velocity& v) {
            p.x+=v.x; p.y+=v.y; p.z+=v.z;
        });
    };
    auto narrow=[&] {
        w.each<Velocity,Position>([](Entity,Velocity& v,Position& p) {
            p.x+=v.x; p.y+=v.y; p.z+=v.z;
        });
    };
    if(query==Query::Broad) broad();
    else if(query==Query::Narrow) narrow();
    else if(w.count<Velocity>()<w.count<Position>()) narrow();
    else broad();
}
static double median(std::vector<double> a) {
    std::sort(a.begin(),a.end()); return a[a.size()/2];
}
static void run(size_t n) {
    constexpr int kSteps=10,kWarmup=2,kSamples=7;
    World seed=makeWorld(n);
    uint64_t expected=0;
    double queryTimes[3]{};
    for(int mode=0;mode<3;++mode) {
        std::vector<double> samples;
        for(int rep=-kWarmup;rep<kSamples;++rep) {
            World w=seed; // Reset OUTSIDE measured region: identical initial state.
            double ms=elapsed([&] {
                for(int i=0;i<kSteps;++i) update(w,Query(mode));
            });
            uint64_t h=checksum(w);
            if(mode==0 && rep==-kWarmup) expected=h;
            if(h!=expected) throw std::runtime_error("query checksum mismatch");
            observed=observed^h;
            if(rep>=0) samples.push_back(ms);
        }
        queryTimes[mode]=median(samples);
    }
    std::vector<double> copies;
    for(int rep=-kWarmup;rep<kSamples;++rep) {
        World target;
        double ms=elapsed([&] {target.copyFrom(seed);});
        if(checksum(target)!=checksum(seed)) throw std::runtime_error("snapshot checksum mismatch");
        if(rep>=0) copies.push_back(ms);
    }
    // Structural churn is measured separately from query timings.
    std::vector<double> churn;
    for(int rep=-kWarmup;rep<kSamples;++rep) {
        World w=seed;
        double ms=elapsed([&] {
            for(size_t i=0;i<n;i+=8) w.destroyLater(Entity{uint32_t(i),0});
            w.flush();
        });
        if(rep>=0) churn.push_back(ms);
    }
    std::cout<<"sparse-set,"<<n<<","<<queryTimes[0]<<","<<queryTimes[1]<<","
             <<queryTimes[2]<<","<<median(copies)<<","<<median(churn)<<","
             <<expected<<"\n";
}
int main(int argc,char** argv) {
    std::cout<<"variant,entities,broad_10x_median_ms,narrow_10x_median_ms,auto_smallest_10x_median_ms,snapshot_median_ms,destroy_1of8_median_ms,checksum\n";
    try {
        if(argc>1) run(std::strtoull(argv[1],nullptr,10));
        else for(size_t n:{size_t(10000),size_t(100000),size_t(1000000)}) run(n);
    } catch(const std::exception& e) {
        std::cerr<<"FAIL: "<<e.what()<<"\n"; return 1;
    }
}
