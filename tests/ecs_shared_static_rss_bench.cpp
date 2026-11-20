// Separate-process memory and read latency benchmark for AIVERSE config stores.
// Run ONCE per mode through tools/ecs-static-scene-validate.sh after feature work.
// Each process reports real OS resident bytes, not just logical payload.
// Usage: aiverse_ecs_shared_static_rss_bench ordinary|shared|static [entities] [unique]
#include "engine/sim/SharedStaticComponents.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <cstdio>
#include <unistd.h>
#endif

namespace benchmark {
struct Config {
    std::array<int32_t,32> values{}; // 128 bytes, like typical immutable config
};
inline void io(aiverse::Archive& archive,Config& config) {
    for(auto& value:config.values) archive(value);
}
static Config value(uint32_t n) {
    Config out;
    for(size_t j=0;j<out.values.size();++j)
        out.values[j]=int32_t(n*64+uint32_t(j));
    return out;
}
static uint64_t residentBytes() {
#if defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t size=MACH_TASK_BASIC_INFO_COUNT;
    if(task_info(mach_task_self(),MACH_TASK_BASIC_INFO,
                 reinterpret_cast<task_info_t>(&info),&size)==KERN_SUCCESS)
        return uint64_t(info.resident_size);
#elif defined(__linux__)
    long pages=0, resident=0;
    if(auto* file=std::fopen("/proc/self/statm","r")) {
        const int read=std::fscanf(file,"%ld %ld",&pages,&resident);
        std::fclose(file);
        if(read==2 && resident>=0) {
            const long pageSize=sysconf(_SC_PAGESIZE);
            if(pageSize>0) return uint64_t(resident)*uint64_t(pageSize);
        }
    }
#endif
    return 0; // Unsupported platform; do not pretend a measured RSS.
}
static std::atomic<uint64_t> checksumSink{0};
template<class Fn>
static double medianMs(Fn&& fn,int samples=11) {
    std::vector<double> times;
    times.reserve(size_t(samples));
    for(int i=0;i<=samples;++i) {
        const auto start=std::chrono::steady_clock::now();
        const uint64_t result=fn();
        const auto stop=std::chrono::steady_clock::now();
        checksumSink.fetch_xor(result,std::memory_order_relaxed);
        if(i) times.push_back(
            std::chrono::duration<double,std::milli>(stop-start).count());
    }
    std::sort(times.begin(),times.end());
    return times[size_t(samples)/2];
}
} // namespace benchmark

int main(int argc,char** argv) {
    using namespace aiverse;
    namespace st=aiverse::storage;
    if(argc<2 || argc>4) {
        std::cerr<<"usage: "<<argv[0]
                 <<" ordinary|shared|static [entities] [unique]\n";
        return 2;
    }
    const std::string mode=argv[1];
    if(mode!="ordinary" && mode!="shared" && mode!="static") {
        std::cerr<<"invalid mode: "<<mode<<"\n";
        return 2;
    }
    const uint32_t n=argc>2?uint32_t(std::stoul(argv[2])):1000000;
    const uint32_t unique=argc>3?uint32_t(std::stoul(argv[3])):64;
    if(n<1 || n>2000000 || unique<1 || unique>100000 ||
       unique>n) {
        std::cerr<<"entities must be 1..2000000, unique 1..min(100000,entities)\n";
        return 2;
    }

    const uint64_t before=benchmark::residentBytes();
    World world;
    std::shared_ptr<const st::StaticTable<benchmark::Config>> staticTable;

    // Catalog creation is outside the per-entity loop; real prefab importers
    // build one shared immutable value for each unique configuration.
    std::vector<st::SharedRef<benchmark::Config>> sharedRefs;
    std::vector<st::StaticRef<benchmark::Config>> staticRefs;
    if(mode=="shared") {
        auto& store=world.setResource<st::SharedStore<benchmark::Config>>();
        sharedRefs.reserve(unique);
        for(uint32_t i=0;i<unique;++i)
            sharedRefs.push_back(store.intern(benchmark::value(i)));
    } else if(mode=="static") {
        st::StaticTableBuilder<benchmark::Config> builder;
        staticRefs.reserve(unique);
        for(uint32_t i=0;i<unique;++i)
            staticRefs.push_back(builder.define(benchmark::value(i)));
        staticTable=builder.seal();
        if(!st::bindStaticTable(world,*staticTable))
            throw std::runtime_error("static config fingerprint binding failed");
    }

    for(uint32_t i=0;i<n;++i) {
        const Entity entity=world.create();
        const uint32_t key=i%unique;
        if(mode=="ordinary")
            world.add<benchmark::Config>(entity,benchmark::value(key));
        else if(mode=="shared")
            world.add<st::SharedRef<benchmark::Config>>(entity,sharedRefs[key]);
        else if(!st::setStatic(world,entity,staticRefs[key],*staticTable))
            throw std::runtime_error("static binding failed");
    }

    const uint64_t after=benchmark::residentBytes();
    const st::SharedReadView<benchmark::Config> sharedView(world);
    // Create the static view only for static mode. No dummy table is
    // allocated in the ordinary/shared measurement processes.
    std::unique_ptr<st::StaticReadView<benchmark::Config>> staticView;
    if(mode=="static")
        staticView=std::make_unique<st::StaticReadView<benchmark::Config>>(
            world,*staticTable);

    auto readAll=[&]{
        uint64_t sum=0;
        if(mode=="ordinary") {
            world.eachExistingConst<benchmark::Config>(
                [&](Entity,const benchmark::Config& config) {
                    sum+=uint64_t(config.values[0])+uint64_t(config.values[31]);
                });
        } else if(mode=="shared") {
            world.eachExistingConst<st::SharedRef<benchmark::Config>>(
                [&](Entity,const st::SharedRef<benchmark::Config>& ref) {
                    const auto* config=sharedView.get(ref);
                    if(!config) throw std::runtime_error("bad shared ref");
                    sum+=uint64_t(config->values[0])+uint64_t(config->values[31]);
                });
        } else {
            world.eachExistingConst<st::StaticRef<benchmark::Config>>(
                [&](Entity,const st::StaticRef<benchmark::Config>& ref) {
                    const auto* config=staticView->get(ref);
                    if(!config) throw std::runtime_error("bad static ref");
                    sum+=uint64_t(config->values[0])+uint64_t(config->values[31]);
                });
        }
        return sum;
    };
    const uint64_t checksum=readAll();
    if(checksum==0) throw std::runtime_error("empty benchmark checksum");
    const double median=benchmark::medianMs(readAll);
    const uint64_t warmed=benchmark::residentBytes();
    const uint64_t delta=after>=before?after-before:0;
    const uint64_t peakDelta=warmed>=before?warmed-before:0;
    std::cout<<std::fixed<<std::setprecision(6)
             <<"{\"mode\":\""<<mode<<"\",\"entities\":"<<n
             <<",\"unique\":"<<unique
             <<",\"config_bytes\":"<<sizeof(benchmark::Config)
             <<",\"rss_baseline_bytes\":"<<before
             <<",\"rss_after_build_bytes\":"<<after
             <<",\"rss_after_reads_bytes\":"<<warmed
             <<",\"rss_build_delta_bytes\":"<<delta
             <<",\"rss_read_delta_bytes\":"<<peakDelta
             <<",\"median_read_ms\":"<<median
             <<",\"checksum\":"<<checksum<<"}\n";
}
