// TerraWeave: owner-thread orchestration for excavation and NEREID-driven
// erosion. Live terrain geometry/collision is rebuilt with one shared surface
// topology. No duplicate water solver, implicit ECS scheduler or backend RHI.
#pragma once
#include "engine/render/TerraWeaveTerrainPatchRender.h"
#include "engine/world/TerraWeaveNereidErosionBridge.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct TerrainMutationResult {
    EarthworkChange earth{};
    PlantSupportUpdate vegetation{};
    std::vector<GrassCoverChange> grass{};
    uint64_t tick=0;
};

// This coordinator does not own the authoritative fixed-step ECS world.
// Call excavate()/erode() only after an authoritative game tick; call
// publishVisible() only on the graphics/SpatialWorld owner thread. Geometry
// publication is bounded separately from the gameplay tick and is revisioned.
class TerrainMutationCoordinator {
public:
    using ColliderIdForCell=std::function<uint32_t(world::Cell)>;
    using MaterialForCell=std::function<const render::Model*(world::Cell)>;

    TerrainMutationCoordinator(EarthworksLayer& ground,
                               int32_t cellSizeMetres,uint32_t resolution,
                               RootSupportRegistry* roots=nullptr,
                               GrassCoverRegistry* grass=nullptr)
        : earth_(ground),roots_(roots),grass_(grass),
          water_(earth_),cellSizeMetres_(cellSizeMetres),
          resolution_(resolution) {
        if(cellSizeMetres_<16 || cellSizeMetres_>4096 ||
           resolution_<2 || resolution_>129)
            throw std::invalid_argument("TerraWeave runtime terrain tile geometry");
    }
    NereidEarthworksBridge& waterBridge(){return water_;}
    const NereidEarthworksBridge& waterBridge()const{return water_;}
    size_t pendingTiles()const{return pending_.size();}
    size_t residentTiles()const{return residents_.size();}
    uint64_t soilRevision()const{return earth_.revision();}

    // Optional saved root/grass registries remain caller-owned, so native
    // games decide what to persist and how to replicate each plant/area.
    TerrainMutationResult excavate(EarthworkBrush brush,uint64_t tick){
        TerrainMutationResult out;
        out.tick=tick;
        out.earth=earth_.sculpt(brush);
        if(out.earth.vertices.empty())return out;
        if(grass_)out.grass=grass_->applyEarthwork(
            out.earth,earth_.spacingCm());
        if(roots_)out.vegetation=roots_->applyGroundChange(
            soilSnapshot(),out.earth,tick);
        schedule(out.earth);
        return out;
    }

    BankTick erode(const std::vector<NereidBankProbe>& probes,
                   uint64_t completedTick,uint32_t dtMs,
                   lib::nereid::ShoreErosionSettings settings={}){
        auto out=water_.step(probes,completedTick,dtMs,
                             roots_,settings,grass_);
        schedule(out.earth);
        return out;
    }

    // Supports non-water authoritative edits (replay, roadworks, removal of
    // buildings) and keeps the same scene publication policy.
    void notifyExternalEarthwork(const EarthworkChange& change){
        if(!change.vertices.empty() && change.revision>earth_.revision())
            throw std::invalid_argument("TerraWeave external soil revision in future");
        schedule(change);
    }

    // A live, immutable-height callback must only be consumed on the owner
    // thread. Never capture this into async foliage/Horizon workers; first
    // copy committed tile heights or an immutable snapshot of Earthworks.
    SoilHeightSampler soilSnapshot()const {
        return [this](int64_t x,int64_t z)->std::optional<int32_t>{
            const int64_t spacing=earth_.spacingCm();
            if(x<=-kWorldLimitCm+spacing || x>=kWorldLimitCm-spacing ||
               z<=-kWorldLimitCm+spacing || z>=kWorldLimitCm-spacing)
                return std::nullopt;
            return earth_.sampleHeightCm(x,z);
        };
    }

    // "visible" is the host's required/pinned terrain set, not only frustum
    // visible cells: authoritative collision must remain resident wherever
    // gameplay could query it. The host reserves collider ID ranges.
    // At most maxUploads (1..8) RHI+collision replacements per call.
    // Even after many soil ticks, only the latest Earthworks revision is baked
    // once for each affected visible tile.
    size_t publishVisible(gfx::Device& device,world::SpatialWorld& spatial,
                          std::span<const world::Cell> visible,
                          size_t maxUploads,
                          const ColliderIdForCell& ids,
                          const MaterialForCell& materials={},
                          float uvScale=0.05f) {
        if(!ids || maxUploads<1 || maxUploads>8 ||
           visible.size()>8192u)
            throw std::invalid_argument("TerraWeave terrain residency budget");
        std::set<world::Cell> needed;
        for(const auto& tile:visible){
            if(tile.y!=0)
                throw std::invalid_argument("TerraWeave tiles are X/Z columns");
            needed.insert(tile);
        }
        // Do not evict gameplay collision just because it left the camera's
        // frustum. Caller supplies the full simulation-pinned tile set.
        for(auto it=residents_.begin();it!=residents_.end();){
            if(!needed.contains(it->first)){
                it->second.shutdown(device,spatial,colliders_);
                it=residents_.erase(it);
            }else ++it;
        }
        for(const auto& tile:needed)
            if(!residents_.contains(tile) && !pending_.contains(tile))
                pending_.insert_or_assign(tile,earth_.revision());

        size_t published=0;
        for(const auto& tile:needed){
            if(published>=maxUploads)break;
            const auto it=pending_.find(tile);
            if(it==pending_.end())continue;
            // No GPU or collision resources are modified if CPU baking
            // fails; the dirty request remains queued for a safe retry.
            const TerrainPatch patch=bakeEarthworksTerrainPatch(
                earth_,tile,cellSizeMetres_,resolution_);
            auto [slot,created]=residents_.try_emplace(tile);
            try {
                const render::Model* material=
                    materials?materials(tile):nullptr;
                if(!slot->second.publish(device,spatial,colliders_,
                                         patch,ids(tile),material,uvScale)){
                    if(created)residents_.erase(slot);
                    break; // GPU budget exhausted; retry a later frame
                }
            }catch(...){
                if(created)residents_.erase(slot);
                throw;
            }
            pending_.erase(it);
            ++published;
        }
        return published;
    }

    // Both the graphics pass and the collision publisher see the same mesh
    // revision. The original baked terrain draw must be suppressed by host
    // when a live patch is drawn, or z-fighting will occur.
    void draw(render::Renderer3D& renderer,
              TerrainPatchRenderOriginCm origin={}) const {
        for(const auto& [cell,patch]:residents_){
            (void)cell;patch.submit(renderer,origin);
        }
    }

    std::vector<TerrainPatchDirtyRegion> takeTerrainDirty(){
        std::vector<TerrainPatchDirtyRegion> out;
        for(auto& [cell,resident]:residents_){
            (void)cell;
            auto changed=resident.takeDirty();
            out.insert(out.end(),changed.begin(),changed.end());
        }
        std::sort(out.begin(),out.end(),
            [](const auto& a,const auto& b){
                if(a.cell<b.cell)return true;
                if(b.cell<a.cell)return false;
                return a.sourceRevision<b.sourceRevision;
            });
        return out;
    }

    void shutdown(gfx::Device& device,world::SpatialWorld& spatial){
        for(auto& [cell,resident]:residents_){
            (void)cell;resident.shutdown(device,spatial,colliders_);
        }
        residents_.clear();
        // Preserve pending dirties/earthwater simulation data for reload.
    }

private:
    void schedule(const EarthworkChange& change){
        if(change.vertices.empty())return;
        const int64_t pad=earth_.spacingCm();
        const auto clamp=[](int64_t value)->int64_t{
            return std::clamp<int64_t>(value,-kWorldLimitCm,
                                      kWorldLimitCm);
        };
        const int64_t x0=clamp(change.dirty.minX-pad);
        const int64_t x1=clamp(change.dirty.maxX+pad);
        const int64_t z0=clamp(change.dirty.minZ-pad);
        const int64_t z1=clamp(change.dirty.maxZ+pad);
        const int64_t span=int64_t(cellSizeMetres_)*100;
        const int64_t cx0=detail::floorDiv(x0,span);
        const int64_t cx1=detail::floorDiv(x1,span);
        const int64_t cz0=detail::floorDiv(z0,span);
        const int64_t cz1=detail::floorDiv(z1,span);
        const int64_t width=cx1-cx0+1,height=cz1-cz0+1;
        if(width<=0 || height<=0 || width*height>65536u)
            throw std::length_error("TerraWeave soil revision dirty tile budget");
        // Stage map before publication so a partially allocated dirty queue
        // never escapes to another thread. Duplicate cells coalesce to the
        // newest monotonic revision.
        auto next=pending_;
        for(int64_t z=cz0;z<=cz1;++z)
            for(int64_t x=cx0;x<=cx1;++x){
                if(x<INT32_MIN || x>INT32_MAX ||
                   z<INT32_MIN || z>INT32_MAX)
                    throw std::out_of_range("TerraWeave dirty world cell");
                const world::Cell cell{int32_t(x),0,int32_t(z)};
                next.insert_or_assign(cell,change.revision);
            }
        pending_.swap(next);
    }

    EarthworksLayer& earth_;
    RootSupportRegistry* roots_=nullptr;
    GrassCoverRegistry* grass_=nullptr;
    NereidEarthworksBridge water_;
    int32_t cellSizeMetres_=256;
    uint32_t resolution_=33;
    TerrainCollisionPublisher colliders_;
    std::map<world::Cell,TerrainPatchResident> residents_;
    std::map<world::Cell,uint64_t> pending_;
};

} // namespace aiverse::terraweave
