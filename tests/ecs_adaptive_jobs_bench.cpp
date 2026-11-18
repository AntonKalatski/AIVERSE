// Same-storage, same-callback adaptive scheduling benchmark.
// c++ -std=c++20 -O3 -DNDEBUG -pthread -I. tests/ecs_adaptive_jobs_bench.cpp -o /tmp/aiverse-ecs-adaptive
// /tmp/aiverse-ecs-adaptive [entity_count]
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
using Clock=std::chrono::steady_clock;
struct Position{int64_t x=0,y=0,z=0;};
struct Velocity{int64_t x=1,y=2,z=3;};
static volatile uint64_t observed=0;
static World build(size_t n,int density){
    World w;
    for(uint32_t i=0;i<n;++i){
        Entity e=w.create();
        w.add<Position>(e,Position{int64_t(i),0,0});
        if(i%100<uint32_t(density))w.add<Velocity>(e);
    }
    return w;
}
static uint64_t checksum(World& w){
    uint64_t h=1469598103934665603ull;
    w.each<Position>([&](Entity e,Position& p){
        for(uint64_t x:{e.packed(),uint64_t(p.x),uint64_t(p.y),uint64_t(p.z)})
            h=(h^x)*1099511628211ull;
    });
    return h;
}
template<class F>static double timeMs(F&& fn){
    const auto start=Clock::now();fn();
    return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
}
static double median(std::vector<double> x){
    std::sort(x.begin(),x.end());return x[x.size()/2];
}
static void test(size_t n,int density){
    World base=build(n,density);
    const size_t matched=base.count<Velocity>();
    uint64_t expected=0;
    for(int mode=0;mode<3;++mode){
        PersistentWorkers workers{7};
        std::vector<double> samples;
        for(int rep=-2;rep<7;++rep){
            World w=base;
            const double ms=timeMs([&] {
                for(int step=0;step<10;++step){
                    eachAdaptive<Velocity>(w,workers,
                        mode==0?ExecutionMode::Sequential:
                        mode==1?ExecutionMode::Parallel:ExecutionMode::Auto,
                        ParallelPolicy{8192,2048},
                        [&](Entity e,Velocity& v){
                            auto& p=w.get<Position>(e);
                            p.x+=v.x;p.y+=v.y;p.z+=v.z;
                        });
                }
            });
            const uint64_t h=checksum(w);
            if(!expected)expected=h;
            if(h!=expected)throw std::runtime_error("adaptive checksum mismatch");
            observed=observed^h;
            if(rep>=0)samples.push_back(ms);
        }
        const char* names[]={"sequential","parallel-8-lanes","auto-8-lanes"};
        std::cout<<names[mode]<<","<<n<<","<<density<<","<<matched<<","
                 <<median(samples)<<","<<expected<<"\n";
    }
}
int main(int argc,char** argv){
    try{
        const size_t count=argc>1?std::strtoull(argv[1],nullptr,10):0;
        std::cout<<"mode,entities,density_percent,matching_entities,10x_median_ms,checksum\n";
        if(count){for(int d:{1,25,100})test(count,d);}
        else for(size_t n:{size_t(10000),size_t(100000),size_t(1000000)})
            for(int d:{1,25,100})test(n,d);
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
