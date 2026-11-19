// Empirical read-cost comparison, NOT a CTest performance gate.
// Build Release and run on the target hardware. Includes the identical
// Sparse Set entity traversal for ordinary / shared / static components.
#include "engine/sim/SharedStaticComponents.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

namespace bench {
struct LargeConfig {
    std::array<int32_t,32> fields{};
};
inline void io(aiverse::Archive& archive,LargeConfig& v) {
    for(auto& f:v.fields)archive(f);
}
}
using namespace aiverse;
namespace st=aiverse::storage;

static bench::LargeConfig config(int n) {
    bench::LargeConfig value;
    for(int i=0;i<32;++i) value.fields[size_t(i)]=n+i;
    return value;
}

template<class Fn>
static double medianMs(Fn&& fn) {
    std::vector<double> samples;
    samples.reserve(9);
    for(int run=0;run<10;++run) {
        const auto start=std::chrono::steady_clock::now();
        const auto total=fn();
        const auto end=std::chrono::steady_clock::now();
        // Write to an externally observable sink to prevent elision.
        static std::atomic<uint64_t> sink{0};
        sink.fetch_xor(total,std::memory_order_relaxed);
        if(run) samples.push_back(
            std::chrono::duration<double,std::milli>(end-start).count());
    }
    std::sort(samples.begin(),samples.end());
    return samples[samples.size()/2];
}

int main(int argc,char** argv) {
    const int n=argc>1 ? std::clamp(std::stoi(argv[1]),1,2000000) : 200000;
    constexpr int unique=64;
    World world;
    st::StaticTableBuilder<bench::LargeConfig> builder;
    std::array<st::StaticRef<bench::LargeConfig>,unique> statics{};
    for(int i=0;i<unique;++i)
        statics[size_t(i)]=builder.define(config(i));
    const auto table=builder.seal();
    if(!st::bindStaticTable(world,*table))return 2;
    for(int i=0;i<n;++i) {
        Entity e=world.create();
        auto value=config(i%unique);
        world.add<bench::LargeConfig>(e,value);
        st::setShared(world,e,value);
        if(!st::setStatic(world,e,statics[size_t(i%unique)],*table))
            return 3;
    }
    st::SharedReadView<bench::LargeConfig> shared(world);
    st::StaticReadView<bench::LargeConfig> immutable(world,*table);
    const auto direct=[&] {
        uint64_t sum=0;
        world.eachExistingConst<bench::LargeConfig>(
            [&](Entity,const auto& value) {
                sum+=uint64_t(value.fields[0]+value.fields[31]);
            });
        return sum;
    };
    const auto sharedRead=[&] {
        uint64_t sum=0;
        world.eachExistingConst<st::SharedRef<bench::LargeConfig>>(
            [&](Entity,const auto& handle) {
                const auto* value=shared.get(handle);
                sum+=uint64_t(value->fields[0]+value->fields[31]);
            });
        return sum;
    };
    const auto staticRead=[&] {
        uint64_t sum=0;
        world.eachExistingConst<st::StaticRef<bench::LargeConfig>>(
            [&](Entity,const auto& handle) {
                const auto* value=immutable.get(handle);
                sum+=uint64_t(value->fields[0]+value->fields[31]);
            });
        return sum;
    };
    if(direct()!=sharedRead() || direct()!=staticRead()) {
        std::cerr<<"checksum mismatch\n";
        return 4;
    }
    const auto d=medianMs(direct);
    const auto s=medianMs(sharedRead);
    const auto t=medianMs(staticRead);
    const size_t raw=size_t(n)*sizeof(bench::LargeConfig);
    const size_t compact=size_t(n)*sizeof(st::SharedRef<bench::LargeConfig>)
                       +size_t(unique)*sizeof(bench::LargeConfig);
    std::cout<<std::fixed<<std::setprecision(3)
             <<"entities="<<n<<" uniques="<<unique
             <<" config_bytes="<<sizeof(bench::LargeConfig)<<"\n"
             <<"component_payload_bytes ordinary="<<raw
             <<" shared_static_approx="<<compact<<"\n"
             <<"median_ms ordinary="<<d<<" shared="<<s
             <<" static="<<t<<"\n"
             <<"read_ratio_shared="<<s/d
             <<" read_ratio_static="<<t/d<<"\n"
             <<"NOTE: excludes Sparse Set overhead, hash-index and allocations; "
               "run Release, warm caches, target platform.\n";
}
