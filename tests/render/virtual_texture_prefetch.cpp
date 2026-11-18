#include "engine/gfx/VirtualTexturePrefetch.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace aiverse::gfx;
namespace {
void require(bool yes,const char* why) { if(!yes) throw std::runtime_error(why); }

void coarseAndBudgets() {
    VirtualTexturePrefetchConfig cfg;
    cfg.tilesX=8; cfg.tilesY=4; cfg.mipLevels=4;
    cfg.virtualLayers=2; cfg.maxCandidates=128;
    cfg.maxRequestsPerFrame=5;
    VirtualTexturePrefetchPlanner p;
    require(p.initialize(cfg),"valid atlas config");
    auto requests=p.plan({VirtualTextureVisibleRegion{0,0,1,1,0,1,2}});
    require(requests.size()==5 && p.stats().uniqueCandidates==11 &&
            p.stats().deferredByBudget==6 && !p.stats().candidateLimitReached,
            "complete coarse-to-mip1 candidates with per-frame cap");
    require(requests[0].key==VirtualPageKey{3,0,0,0} && requests[0].pin,
            "coarsest pinned fallback always requested first");
    require(requests[1].key==VirtualPageKey{2,0,0,0} &&
            requests[2].key==VirtualPageKey{2,1,0,0},
            "all mid-level pages before any fine page");
    require(requests[3].key.mip==1 && requests[4].key.mip==1,
            "budget spillover into fine pages");
    requests=p.plan({VirtualTextureVisibleRegion{0,0,1,1,0,1,2}},
                    [](VirtualPageKey k){return k.mip>=2;});
    require(requests.size()==5 && requests[0].key.mip==1 &&
            p.stats().alreadyResident==3,
            "exact-resident coarse pages skipped, more fine pages planned");
}
void clippingAndDedup() {
    VirtualTexturePrefetchConfig cfg;
    cfg.tilesX=8; cfg.tilesY=8; cfg.mipLevels=4;
    cfg.maxCandidates=128; cfg.maxRequestsPerFrame=32;
    VirtualTexturePrefetchPlanner p;
    require(p.initialize(cfg),"init clipping fixture");
    std::vector<VirtualTextureVisibleRegion> views{
        {-10.,-10.,0.25,0.25,0,0,1},
        {0.,0.,0.25,0.25,0,0,9},
        {0.5,0.5,1.,1.,0,2,4}
    };
    const auto list=p.plan(views);
    require(p.stats().invalidRegions==0 &&
            p.stats().uniqueCandidates==12,
            "overlapping rectangles deduped and clipped");
    require(list.size()==12 && list[0].key.mip==3,
            "bounded merge retains global coarse-to-fine");
    size_t fine=0;
    for (const auto& req:list) {
        require(req.key.layer==0 && req.key.x>=0 && req.key.y>=0,
                "clipped tiles stay inside the input atlas");
        if(req.key.mip==0) {
            ++fine;
            require(req.priority==9,"overlapping high-priority region dominates");
        }
    }
    require(fine==4,"two-by-two fine tile visibility request");
}
void priorityLayersAndInvalid() {
    VirtualTexturePrefetchConfig cfg;
    cfg.tilesX=16;cfg.tilesY=8;cfg.mipLevels=5;cfg.virtualLayers=2;
    cfg.maxCandidates=3;cfg.maxRequestsPerFrame=2;
    VirtualTexturePrefetchPlanner p;
    require(p.initialize(cfg),"initialize candidate capped planner");
    std::vector<VirtualTextureVisibleRegion> views{
        {0,0,0.5,0.5,0,0,1},
        {0.5,0,1,0.5,1,0,8},
        {std::numeric_limits<double>::quiet_NaN(),0,1,1,0,0,9},
        {0,0,0,1,0,0,9},
        {0,0,1,1,5,0,9}
    };
    auto list=p.plan(views);
    require(p.stats().invalidRegions==3 &&
            p.stats().candidateLimitReached &&
            p.stats().uniqueCandidates==3 &&
            p.stats().produced==2 && p.stats().deferredByBudget==1,
            "malformed views and bounded candidate generation");
    require(list[0].key.layer==1 && list[0].priority==8,
            "higher-priority layer wins among same mip");
    require(list[0].pin && list[1].pin,
            "coarsest mip/face requests are pinned");
    cfg.maxCandidates=0;
    require(!p.initialize(cfg) && !p.ready(),
            "invalid config resets a prior usable planner");
    require(p.plan(views).empty(),"uninitialized planner has no requests");
}
void regionCapAndStableOrdering() {
    VirtualTexturePrefetchConfig cfg;
    cfg.tilesX=4;cfg.tilesY=4;cfg.mipLevels=3;
    cfg.maxRegions=2;cfg.maxCandidates=32;cfg.maxRequestsPerFrame=32;
    VirtualTexturePrefetchPlanner planner;
    require(planner.initialize(cfg),"region cap config valid");
    const std::vector<VirtualTextureVisibleRegion> views{
        {0,0,0.5,0.5,0,0,6},
        {0.5,0.5,1,1,0,0,6},
        {0,0,1,1,0,0,999}
    };
    const auto first=planner.plan(views);
    require(planner.stats().invalidRegions==1 &&
            !first.empty(),"too many regions are ignored deterministically");
    const auto second=planner.plan(views);
    require(first.size()==second.size(),"stable request cardinality");
    for(size_t i=0;i<first.size();++i)
        require(first[i].key==second[i].key && first[i].pin==second[i].pin,
                "equal inputs yield identical stable request order");
}
}
int main(){
    try { coarseAndBudgets();clippingAndDedup();
          priorityLayersAndInvalid();regionCapAndStableOrdering(); }
    catch(const std::exception& e){
        std::fprintf(stderr,"Virtual texture prefetch FAILED: %s\n",e.what());
        return 1;
    }
    std::puts("Virtual texture visibility planning contracts: OK");
    return 0;
}
