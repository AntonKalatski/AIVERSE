// c++ -std=c++20 -O3 -DNDEBUG -pthread -I. tests/ecs_persistent_jobs_bench.cpp -o /tmp/aiverse-ecs-jobs
// /tmp/aiverse-ecs-jobs [entity_count]
#include "engine/experiments/EcsExperiments.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace aiverse;
using namespace aiverse::ecs_experiment;
struct Position { int64_t x=0,y=0,z=0; };
struct Velocity { int64_t x=1,y=2,z=3; };
static volatile uint64_t observed=0;
using Clock=std::chrono::steady_clock;
static double median(std::vector<double> x){std::sort(x.begin(),x.end());return x[x.size()/2];}
template<class Fn> static double timed(Fn&& fn){
    const auto a=Clock::now();fn();
    return std::chrono::duration<double,std::milli>(Clock::now()-a).count();
}
static World makeWorld(size_t n,int density) {
    World w;
    for(uint32_t i=0;i<n;++i) {
        Entity e=w.create();
        w.add<Position>(e,Position{int64_t(i),0,0});
        if(int(i%100)<density)w.add<Velocity>(e);
    }
    return w;
}
static uint64_t hash(World& w) {
    uint64_t h=1469598103934665603ull;
    w.each<Position>([&](Entity e,Position& p){
        for(uint64_t v:{e.packed(),uint64_t(p.x),uint64_t(p.y),uint64_t(p.z)})h=(h^v)*1099511628211ull;
    });
    return h;
}
static void run(size_t n,int density){
    World base=makeWorld(n,density);
    uint64_t expected=0;
    for(int workersCount: {0,1,2,3,7}) {
        // Thread creation is intentionally outside the timed region, unlike
        // previous std::thread-per-query microbenchmarks.
        PersistentWorkers workers{unsigned(workersCount)};
        std::vector<double> samples;
        for(int rep=-2;rep<7;++rep){
            World w=base;
            const double ms=timed([&]{
                for(int step=0;step<10;++step){
                    if(workersCount==0) {
                        w.each<Velocity,Position>([](Entity,Velocity& v,Position& p){
                            p.x+=v.x;p.y+=v.y;p.z+=v.z;
                        });
                    } else {
                        eachParallel<Velocity>(w,workers,[&](Entity e,Velocity& v){
                            auto& p=w.get<Position>(e);
                            p.x+=v.x;p.y+=v.y;p.z+=v.z;
                        });
                    }
                }
            });
            const uint64_t h=hash(w);
            if(!expected)expected=h;
            if(h!=expected)throw std::runtime_error("parallel checksum mismatch");
            observed=observed^h;
            if(rep>=0)samples.push_back(ms);
        }
        std::cout<<n<<","<<density<<","<<workersCount<<","<<median(samples)<<","<<expected<<"\n";
    }
}
int main(int argc,char** argv) {
    try {
        size_t n=argc>1?std::strtoull(argv[1],nullptr,10):0;
        std::cout<<"entities,density_percent,workers,10x_median_ms,checksum\n";
        if(n){for(int density:{1,25,100})run(n,density);}
        else for(size_t count:{size_t(10000),size_t(100000),size_t(1000000)})
            for(int density:{1,25,100})run(count,density);
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
