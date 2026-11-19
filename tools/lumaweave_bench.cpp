// LumaWeave reproducible CPU-side 100/500/1000 moving-light benchmark.
// This is a DEVELOPER TOOL, not a CTest and not a GPU-timing substitute.
// Run only in the consolidated acceptance pass after all feature sources
// are committed and native-compiled. Prints exact CPU planning medians/p95
// and a separately labelled conservative GPU work budget.
#include "engine/render/LumaWeaveStress.h"
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

using namespace aiverse::render;

static double median(std::vector<double> values) {
    if(values.empty())return 0;
    std::sort(values.begin(),values.end());
    return values[values.size()/2];
}
static double p95(std::vector<double> values) {
    if(values.empty())return 0;
    std::sort(values.begin(),values.end());
    const size_t rank=(values.size()*95+99)/100;
    return values[std::min(values.size()-1,rank?rank-1:0)];
}
int main(int argc,char** argv) {
    try {
        int frames=20;
        if(argc>1) {
            char* end=nullptr;
            const long parsed=std::strtol(argv[1],&end,10);
            if(!end || *end || parsed<1 || parsed>500)
                throw std::invalid_argument("usage: aiverse_lumaweave_bench [frames 1..500]");
            frames=int(parsed);
        }
        std::printf("LumaWeave CPU-only benchmark | native complete-profile samples=%d\n",frames);
        std::printf("WARNING: no Metal GPU time, no fps, no GPU ray results in this program\n");
        std::printf("lights,frames,registry_median_ms,registry_p95_ms,clusters_median_ms,clusters_p95_ms,packet_median_ms,packet_p95_ms,candidate_visits,csr_entries,csr_overflows,packed_bytes,gpu_policy_admitted,gpu_policy_w,gpu_policy_h,samples,shadow_ray_budget,gpu_ms\n");
        for(uint32_t count:LumaWeaveStress::kCounts) {
            std::vector<double> reg,cluster,pack;
            reg.reserve(size_t(frames));cluster.reserve(size_t(frames));pack.reserve(size_t(frames));
            LumaStressProfile last;
            for(int i=0;i<frames;++i) {
                last=LumaWeaveStress::cpuProfile(count,uint32_t(i),1920,1080,
                                                LumaWeaveQuality::Balanced);
                reg.push_back(last.registryCpuMs);
                cluster.push_back(last.clustersCpuMs);
                pack.push_back(last.packetCpuMs);
            }
            std::printf("%u,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%llu,%zu,%zu,%zu,%d,%u,%u,%u,%llu,%s\n",
                count,frames,median(reg),p95(reg),median(cluster),p95(cluster),
                median(pack),p95(pack),
                static_cast<unsigned long long>(last.candidateVisits),
                last.clusterReferences,last.clusterOverflows,last.gpuPacketBytes,
                last.selected.admitted?1:0,last.selected.width,last.selected.height,
                last.selected.samples,
                static_cast<unsigned long long>(last.selected.maximumShadowRays),
                "NOT_MEASURED");
        }
        return 0;
    }catch(const std::exception& e) {
        std::fprintf(stderr,"LumaWeave benchmark failed: %s\n",e.what());
        return 1;
    }
}
