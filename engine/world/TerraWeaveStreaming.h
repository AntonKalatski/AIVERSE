// TerraWeave automatic observer-driven residency for large worlds.
//
// TerrainMutationCoordinator::publishVisible() and publishGrass() (and the
// one-call publishNativeTerraWeaveFrame barrier) need the HOST to say which
// terrain tiles stay pinned (render + SpatialWorld/Box3D collision) and which
// rectangle receives cosmetic grass. Until now every host had to compute that
// set itself. This planner turns any number of observers (local camera,
// players, AI agents, server interest points) into:
//   * a deterministic, bounded, priority-ordered pinned tile list with
//     enter/leave hysteresis so tiles do not thrash at a ring boundary;
//   * a snapped grass rectangle around the primary observer that only moves
//     when the observer leaves a dead zone (no per-frame GPU churn).
// Genre-independent, presentation/host side only: integer centimetres, no
// floats, no ECS writes, no GPU calls. Result order never depends on the
// order observers are supplied in or on std::map iteration of foreign data.
#pragma once
#include "engine/world/SpatialWorld.h"
#include "engine/world/TerraWeave.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <cstdlib>
#include <span>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {

struct TerraStreamObserver {
    int64_t xCm=0, zCm=0;
    // 0 = use TerraStreamingSettings::collisionRadiusCm. Gameplay agents that
    // only need ground under their feet may pass a smaller radius.
    int32_t radiusCm=0;
    // Lower value = more important when the tile budget overflows. The
    // primary observer (grass centre) is the one with the lowest priority,
    // ties broken by position.
    uint32_t priority=0;
};

struct TerraStreamingSettings {
    int32_t tileSizeCm=12800;        // TerrainMutationCoordinator cell size * 100
    int32_t collisionRadiusCm=19200; // tiles intersecting this disc are pinned
    int32_t hysteresisCm=3200;       // pinned tiles stay until radius+hysteresis
    size_t maxPinnedTiles=64;        // <= 512 (publishVisible hard limit)
    int32_t grassRadiusCm=6400;      // half size of the grass rectangle
    int32_t grassDeadZoneCm=1600;    // grass rectangle re-centres past this
    int32_t grassSnapCm=800;         // grass centre is snapped to this grid
};

inline void validateTerraStreamingSettings(const TerraStreamingSettings& s) {
    if(s.tileSizeCm<1600 || s.tileSizeCm>409600 ||
       s.collisionRadiusCm<0 || s.collisionRadiusCm>100000000 ||
       s.hysteresisCm<0 || s.hysteresisCm>100000000 ||
       s.maxPinnedTiles<1 || s.maxPinnedTiles>512 ||
       s.grassRadiusCm<100 || s.grassRadiusCm>10000000 ||
       s.grassDeadZoneCm<0 || s.grassDeadZoneCm>s.grassRadiusCm ||
       s.grassSnapCm<1 || s.grassSnapCm>s.grassRadiusCm)
        throw std::invalid_argument("TerraWeave streaming settings");
}

struct TerraStreamingPlan {
    // Sorted by (best observer priority, squared distance to the nearest
    // observer, cell) - pass straight to publishVisible() so the nearest
    // tiles are uploaded first under its per-frame upload budget.
    std::vector<world::Cell> pinnedCells;
    RectCm grassArea{};
    bool grassValid=false;
    bool pinnedChanged=false;   // set differs from the previous plan
    bool grassMoved=false;      // grass rectangle differs from the previous plan
    size_t entered=0, left=0;   // tiles added / dropped versus previous plan
    size_t overflowTiles=0;     // wanted tiles dropped by maxPinnedTiles
    // Forest/Horizon hosts that stream from one camera use this observer.
    int64_t primaryXCm=0, primaryZCm=0;
};

class TerraWeaveStreamingPlanner {
public:
    explicit TerraWeaveStreamingPlanner(TerraStreamingSettings settings={})
        :settings_(settings) {validateTerraStreamingSettings(settings_);}

    const TerraStreamingSettings& settings()const{return settings_;}
    const std::vector<world::Cell>& pinned()const{return pinned_;}

    // Tile containing a world position (floor division, negative safe).
    world::Cell tileAt(int64_t xCm,int64_t zCm)const{
        return {int32_t(floorDiv(xCm,settings_.tileSizeCm)),0,
                int32_t(floorDiv(zCm,settings_.tileSizeCm))};
    }
    RectCm tileRect(world::Cell c)const{
        const int64_t s=settings_.tileSizeCm;
        return {int64_t(c.x)*s,int64_t(c.z)*s,int64_t(c.x+1)*s,int64_t(c.z+1)*s};
    }

    // Forget residency history (after a teleport/load the host wants a
    // fresh plan without hysteresis carry-over).
    void reset(){pinned_.clear();grassCentre_.reset();}

    TerraStreamingPlan update(std::span<const TerraStreamObserver> observers){
        TerraStreamingPlan plan;
        if(observers.size()>256)
            throw std::invalid_argument("TerraWeave streaming observer budget");
        for(const auto& o:observers)validateObserver(o);
        // Candidate tiles: every tile intersecting radius+hysteresis of any
        // observer. Tiles inside the plain radius are "wanted"; tiles only in
        // the hysteresis band are kept iff they were pinned before.
        struct Score {uint32_t priority=UINT32_MAX;uint64_t dist2=UINT64_MAX;bool wanted=false;};
        std::map<world::Cell,Score> candidates;
        const std::vector<world::Cell> previous=pinned_;
        const auto wasPinned=[&](world::Cell c){
            return std::binary_search(previous.begin(),previous.end(),c);
        };
        for(const auto& o:observers){
            const int64_t radius=o.radiusCm?o.radiusCm:settings_.collisionRadiusCm;
            const int64_t outer=radius+settings_.hysteresisCm;
            const world::Cell lo=tileAt(o.xCm-outer,o.zCm-outer);
            const world::Cell hi=tileAt(o.xCm+outer,o.zCm+outer);
            if(uint64_t(int64_t(hi.x)-lo.x+1)*uint64_t(int64_t(hi.z)-lo.z+1)>65536u)
                throw std::invalid_argument("TerraWeave streaming radius covers too many tiles");
            for(int32_t z=lo.z;z<=hi.z;++z)
                for(int32_t x=lo.x;x<=hi.x;++x){
                    const world::Cell c{x,0,z};
                    const uint64_t d2=distance2ToTile(o.xCm,o.zCm,c);
                    const bool inside=d2<=uint64_t(radius*radius);
                    const bool band=d2<=uint64_t(outer*outer);
                    if(!inside && !(band && wasPinned(c)))continue;
                    auto& s=candidates[c];
                    s.wanted=s.wanted||inside;
                    if(o.priority<s.priority ||
                       (o.priority==s.priority && d2<s.dist2)){
                        s.priority=o.priority;s.dist2=d2;
                    }
                }
        }
        std::vector<std::pair<Score,world::Cell>> ranked;
        ranked.reserve(candidates.size());
        for(const auto& [cell,score]:candidates)ranked.push_back({score,cell});
        // Wanted tiles first, then kept hysteresis tiles; each by priority,
        // distance and finally the cell itself (total, deterministic order).
        std::sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){
            if(a.first.wanted!=b.first.wanted)return a.first.wanted;
            if(a.first.priority!=b.first.priority)return a.first.priority<b.first.priority;
            if(a.first.dist2!=b.first.dist2)return a.first.dist2<b.first.dist2;
            return a.second<b.second;
        });
        if(ranked.size()>settings_.maxPinnedTiles){
            for(size_t i=settings_.maxPinnedTiles;i<ranked.size();++i)
                if(ranked[i].first.wanted)++plan.overflowTiles;
            ranked.resize(settings_.maxPinnedTiles);
        }
        plan.pinnedCells.reserve(ranked.size());
        for(const auto& [score,cell]:ranked)plan.pinnedCells.push_back(cell);
        std::vector<world::Cell> sorted=plan.pinnedCells;
        std::sort(sorted.begin(),sorted.end());
        for(const auto& c:sorted)if(!std::binary_search(previous.begin(),previous.end(),c))++plan.entered;
        for(const auto& c:previous)if(!std::binary_search(sorted.begin(),sorted.end(),c))++plan.left;
        plan.pinnedChanged=plan.entered||plan.left;
        pinned_=std::move(sorted);

        if(!observers.empty()){
            const TerraStreamObserver* primary=&observers.front();
            for(const auto& o:observers)
                if(o.priority<primary->priority ||
                   (o.priority==primary->priority &&
                    (o.xCm<primary->xCm || (o.xCm==primary->xCm && o.zCm<primary->zCm))))
                    primary=&o;
            plan.primaryXCm=primary->xCm;plan.primaryZCm=primary->zCm;
            const int64_t snap=settings_.grassSnapCm;
            const int64_t sx=floorDiv(primary->xCm+snap/2,snap)*snap;
            const int64_t sz=floorDiv(primary->zCm+snap/2,snap)*snap;
            const auto old=grassCentre_;
            if(!grassCentre_ ||
               std::max(std::abs(primary->xCm-grassCentre_->first),
                        std::abs(primary->zCm-grassCentre_->second))>settings_.grassDeadZoneCm)
                grassCentre_=std::pair<int64_t,int64_t>{sx,sz};
            plan.grassMoved=!old || *old!=*grassCentre_;
            const int64_t r=settings_.grassRadiusCm;
            plan.grassArea={
                std::max(-kWorldLimitCm,grassCentre_->first-r),
                std::max(-kWorldLimitCm,grassCentre_->second-r),
                std::min(kWorldLimitCm,grassCentre_->first+r),
                std::min(kWorldLimitCm,grassCentre_->second+r)};
            plan.grassValid=plan.grassArea.minX<plan.grassArea.maxX &&
                            plan.grassArea.minZ<plan.grassArea.maxZ;
        }
        return plan;
    }

private:
    static int64_t floorDiv(int64_t a,int64_t b){
        int64_t q=a/b; if((a%b!=0)&&((a<0)!=(b<0)))--q; return q;
    }
    uint64_t distance2ToTile(int64_t x,int64_t z,world::Cell c)const{
        const RectCm r=tileRect(c);
        const int64_t dx=x<r.minX?r.minX-x:(x>=r.maxX?x-(r.maxX-1):0);
        const int64_t dz=z<r.minZ?r.minZ-z:(z>=r.maxZ?z-(r.maxZ-1):0);
        return uint64_t(dx)*uint64_t(dx)+uint64_t(dz)*uint64_t(dz);
    }
    void validateObserver(const TerraStreamObserver& o)const{
        const int64_t edge=kWorldLimitCm-int64_t(settings_.tileSizeCm)*2;
        if(o.xCm<-edge || o.xCm>edge || o.zCm<-edge || o.zCm>edge ||
           o.radiusCm<0 || o.radiusCm>100000000)
            throw std::invalid_argument("TerraWeave streaming observer");
    }
    TerraStreamingSettings settings_;
    std::vector<world::Cell> pinned_;          // sorted by Cell
    std::optional<std::pair<int64_t,int64_t>> grassCentre_;
};

} // namespace aiverse::terraweave
