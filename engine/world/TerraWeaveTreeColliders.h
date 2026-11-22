// TerraWeave tree trunk colliders for any host (character controllers,
// projectiles, vehicles, unit movement, Box3D) without one ECS entity per tree.
//
// Source of truth: the resident TerraWeaveForestScene presentation instances (the same
// stable ForestTreeIdentity used by harvest/regrowth) and the per-species
// Arbor skeleton trunk (branch 0 of generateTree at High LOD, vertical, from
// the ground up to the tree top, base radius = TreeProfile::trunkRadiusCm).
// The mesh adapter scales that skeleton uniformly by scalePermille, so the
// collider is scaled the same way.
//
// Two shapes are produced from one TreeTrunkCollider:
//   * world::SpatialWorld AABB (Fixed metres): FpsMotor / MotionModule /
//     sweep, hitscan and projectile systems already query SpatialWorld;
//   * physics::BodyDesc static capsule (see TerraWeaveTreeCollidersBox3D.h).
// Footprints (RectCm) can be fed to navigation / placement blockers.
//
// Per streamed terrain tile the host reserves a disjoint collider-ID range
// (like TerrainCollisionPublisher). Inside a tile IDs follow the stable
// (speciesId, key) order, so the same resident forest always yields the same
// IDs independent of streaming order. Presentation/host side only: this does
// not make trees authoritative; a deterministic simulation that depends on
// trunks must pin the tiles it queries and republish after harvest/regrowth.
#pragma once
#include "engine/world/SpatialWorld.h"
#include "engine/world/TerraWeave.h"
#include "engine/world/TerraWeaveArbor.h"
#include "engine/world/TerraWeaveForestScene.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <utility>
#include <span>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {

// Unscaled trunk of one species prototype (centimetres).
struct TreeTrunkShape {
    int32_t radiusCm=0;
    int32_t heightCm=0;
    constexpr bool operator==(const TreeTrunkShape&) const = default;
};

// Same profile + prototype seed as TerraWeaveForestScene::addSpecies -> same skeleton.
inline TreeTrunkShape treeTrunkShape(const TreeProfile& profile,uint64_t prototypeSeed){
    const TreeSkeleton skeleton=generateTree(profile,prototypeSeed,TreeLOD::High);
    if(skeleton.branches.empty() || skeleton.branches.front().parent!=-1)
        throw std::logic_error("TerraWeave tree skeleton without trunk");
    const TreeBranch& trunk=skeleton.branches.front();
    TreeTrunkShape shape;
    shape.radiusCm=std::max<int32_t>(1,trunk.startRadiusCm);
    shape.heightCm=std::max<int32_t>(1,std::max(trunk.y1,skeleton.heightCm));
    return shape;
}

struct TreeTrunkColliderSettings {
    uint32_t radiusPermille=1000;   // scale of the trunk base radius
    int32_t minRadiusCm=4;          // never thinner than this
    uint32_t heightPermille=1000;   // 1000 = to the tree top; e.g. 350 = below the crown
    int32_t minHeightCm=150;        // saplings shorter than this get no collider
    int32_t sinkCm=40;              // extend below the sampled ground (slopes)
    size_t maxPerQuery=8192;
};

inline void validateTreeTrunkColliderSettings(const TreeTrunkColliderSettings& s){
    if(s.radiusPermille<100 || s.radiusPermille>4000 ||
       s.minRadiusCm<1 || s.minRadiusCm>500 ||
       s.heightPermille<50 || s.heightPermille>1500 ||
       s.minHeightCm<0 || s.minHeightCm>10000 ||
       s.sinkCm<0 || s.sinkCm>1000 ||
       s.maxPerQuery<1 || s.maxPerQuery>100000)
        throw std::invalid_argument("TerraWeave trunk collider settings");
}

struct TreeTrunkCollider {
    ForestTreeIdentity tree{};
    int64_t xCm=0, zCm=0;
    int32_t baseYCm=0;     // ground - sinkCm
    int32_t radiusCm=0;
    int32_t heightCm=0;    // from baseYCm upwards
    constexpr bool operator==(const TreeTrunkCollider&) const = default;
};

using TreeTrunkShapes=std::map<uint32_t,TreeTrunkShape>;

inline std::optional<TreeTrunkCollider> makeTreeTrunkCollider(
        const FoliageInstance& tree,const TreeTrunkShape& shape,
        const TreeTrunkColliderSettings& s){
    if(tree.scalePermille==0 || tree.scalePermille>10000 ||
       shape.radiusCm<=0 || shape.heightCm<=0)
        return std::nullopt;
    const int64_t height=int64_t(shape.heightCm)*tree.scalePermille/1000*s.heightPermille/1000;
    if(height<s.minHeightCm || height<=0)return std::nullopt;
    const int64_t radius=std::max<int64_t>(s.minRadiusCm,
        int64_t(shape.radiusCm)*tree.scalePermille/1000*s.radiusPermille/1000);
    TreeTrunkCollider c;
    c.tree={tree.key,tree.speciesId};
    c.xCm=tree.xCm; c.zCm=tree.zCm;
    c.baseYCm=int32_t(std::clamp<int64_t>(int64_t(tree.heightCm)-s.sinkCm,
                                          INT32_MIN/2,INT32_MAX/2));
    c.radiusCm=int32_t(std::min<int64_t>(radius,100000));
    c.heightCm=int32_t(std::min<int64_t>(height+s.sinkCm,1000000));
    return c;
}

// Trunks of all presented (not removed / not hidden) resident trees whose
// root lies in `region`, sorted by stable identity. Species without a
// registered shape are skipped (e.g. shrubs registered without trunks).
inline std::vector<TreeTrunkCollider> collectTreeTrunkColliders(
        const TerraWeaveForestScene& forest,RectCm region,const TreeTrunkShapes& shapes,
        const TreeTrunkColliderSettings& settings={}){
    validateTreeTrunkColliderSettings(settings);
    const auto trees=forest.treesInRect(region,settings.maxPerQuery);
    std::vector<TreeTrunkCollider> out;
    out.reserve(trees.size());
    for(const auto& tree:trees){
        if(tree.habit!=PlantHabit::Tree)continue;
        const auto shape=shapes.find(tree.speciesId);
        if(shape==shapes.end())continue;
        if(auto c=makeTreeTrunkCollider(tree,shape->second,settings))out.push_back(*c);
    }
    std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.tree<b.tree;});
    return out;
}

// Fixed-point AABB in metres (SpatialWorld units). The square circumscribes
// the trunk circle: conservative for movement, yaw independent.
inline world::Bounds treeTrunkBounds(const TreeTrunkCollider& c){
    const auto cm=[](int64_t v){return Fixed::fromRatio(v,100);};
    return {{cm(c.xCm-c.radiusCm),cm(c.baseYCm),cm(c.zCm-c.radiusCm)},
            {cm(c.xCm+c.radiusCm),cm(int64_t(c.baseYCm)+c.heightCm),cm(c.zCm+c.radiusCm)}};
}

inline RectCm treeTrunkFootprint(const TreeTrunkCollider& c){
    return {c.xCm-c.radiusCm,c.zCm-c.radiusCm,c.xCm+c.radiusCm+1,c.zCm+c.radiusCm+1};
}

struct TreeTrunkTileRange {
    world::Cell cell{};
    uint32_t firstId=0;
    uint32_t capacity=0;
    uint32_t count=0;
};

// Publishes trunk AABBs into a SpatialWorld per tile. Transactional like the
// terrain publisher: every check happens before the live world is touched.
class TreeTrunkCollisionPublisher {
public:
    const std::map<world::Cell,TreeTrunkTileRange>& tiles()const{return tiles_;}
    size_t colliderCount()const{
        size_t n=0; for(const auto& [c,t]:tiles_){(void)c;n+=t.count;} return n;
    }
    // Replace the trunks of one tile. `trunks` must be sorted by identity
    // (collectTreeTrunkColliders does that). Returns the published count.
    size_t publish(world::SpatialWorld& spatial,world::Cell cell,
                   std::span<const TreeTrunkCollider> trunks,
                   uint32_t firstId,uint32_t capacity){
        if(!firstId || !capacity || capacity>65535u ||
           uint64_t(firstId)+capacity-1>UINT32_MAX)
            throw std::invalid_argument("TerraWeave trunk collider ID range");
        if(trunks.size()>capacity)
            throw std::length_error("TerraWeave trunk collider tile capacity");
        for(size_t i=1;i<trunks.size();++i)
            if(!(trunks[i-1].tree<trunks[i].tree))
                throw std::invalid_argument("TerraWeave trunks not sorted by identity");
        const uint64_t limit=uint64_t(firstId)+capacity;
        for(const auto& [tile,old]:tiles_){
            if(tile==cell)continue;
            const uint64_t a=old.firstId,b=a+old.capacity;
            if(uint64_t(firstId)<b && a<limit)
                throw std::invalid_argument("TerraWeave overlapping trunk collider ranges");
        }
        const auto old=tiles_.find(cell);
        for(uint64_t i=firstId;i<uint64_t(firstId)+trunks.size();++i){
            const bool owned=old!=tiles_.end() && i>=old->second.firstId &&
                i<uint64_t(old->second.firstId)+old->second.count;
            if(!owned && spatial.colliders().contains(uint32_t(i)))
                throw std::invalid_argument("TerraWeave trunk collider ID occupied");
        }
        std::vector<world::Bounds> bounds;
        bounds.reserve(trunks.size());
        for(const auto& t:trunks){
            // SpatialWorld accepts |coordinate| <= 1,000,000 m.
            constexpr int64_t kSpatialLimitCm=100000000;
            if(t.radiusCm<=0 || t.heightCm<=0 ||
               std::abs(t.xCm)+t.radiusCm>kSpatialLimitCm ||
               std::abs(t.zCm)+t.radiusCm>kSpatialLimitCm ||
               std::abs(int64_t(t.baseYCm))+t.heightCm>kSpatialLimitCm)
                throw std::invalid_argument("TerraWeave invalid trunk collider");
            bounds.push_back(treeTrunkBounds(t));
        }
        if(old!=tiles_.end())
            for(uint64_t i=old->second.firstId;i<uint64_t(old->second.firstId)+old->second.count;++i)
                spatial.remove(uint32_t(i));
        for(size_t i=0;i<bounds.size();++i)
            if(!spatial.add({firstId+uint32_t(i),bounds[i]}))
                throw std::logic_error("TerraWeave trunk collider rejected");
        tiles_[cell]={cell,firstId,capacity,uint32_t(trunks.size())};
        return trunks.size();
    }
    void unload(world::SpatialWorld& spatial,world::Cell cell){
        const auto it=tiles_.find(cell);
        if(it==tiles_.end())return;
        for(uint64_t i=it->second.firstId;i<uint64_t(it->second.firstId)+it->second.count;++i)
            spatial.remove(uint32_t(i));
        tiles_.erase(it);
    }
    // Keep exactly `cells` (e.g. TerraStreamingPlan::pinnedCells): unload the
    // others. Publishing the new ones is the caller's per-tile call above.
    void retainOnly(world::SpatialWorld& spatial,std::span<const world::Cell> cells){
        std::vector<world::Cell> keep(cells.begin(),cells.end());
        std::sort(keep.begin(),keep.end());
        for(auto it=tiles_.begin();it!=tiles_.end();){
            if(!std::binary_search(keep.begin(),keep.end(),it->first)){
                const auto cell=it->first; ++it; unload(spatial,cell);
            }else ++it;
        }
    }
    // Which tree owns a SpatialWorld hit id (for harvest / damage / VFX).
    std::optional<std::pair<world::Cell,uint32_t>> slotOf(uint32_t id)const{
        for(const auto& [cell,t]:tiles_)
            if(id>=t.firstId && uint64_t(id)<uint64_t(t.firstId)+t.count)
                return std::pair<world::Cell,uint32_t>{cell,id-t.firstId};
        return std::nullopt;
    }
    void shutdown(world::SpatialWorld& spatial){
        while(!tiles_.empty())unload(spatial,tiles_.begin()->first);
    }
private:
    std::map<world::Cell,TreeTrunkTileRange> tiles_;
};

} // namespace aiverse::terraweave
