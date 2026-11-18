// TerraWeave Horizon: async region baking with ONE resident model and atlas.
// Reuses AIVERSE AsyncChunkStream for background CPU work; only the owning
// render thread uploads and releases GPU buffers. The previous GPU proxy
// stays displayed until a new region has been successfully uploaded.
#pragma once
#include "engine/render/TerraWeaveHorizonModel.h"
#include "engine/world/TerraWeaveHorizonArchive.h"
#include "engine/render/Renderer3D.h"
#include "engine/world/AsyncChunkStream.h"
#include "engine/render/TerraWeaveForestAdapter.h"
#include <atomic>
#include <cstdint>
#include <chrono>
#include <limits>
#include <memory>
#include <functional>
#include <vector>
#include <map>
#include <mutex>
#include <stdexcept>

namespace aiverse::terraweave {

// Host-owned asset plumbing for static worlds; all callbacks execute on
// the bounded Horizon CPU worker. Paths/storage belong to the game, not RHI.
struct HorizonArchiveStore {
    std::function<bool(int32_t cellX,int32_t cellZ,
                       std::vector<uint8_t>& archive)> read;
    std::function<void(int32_t cellX,int32_t cellZ,
                       const std::vector<uint8_t>& archive)> write;
    // Stable content hash of authored terrain/biome/masks. Set to zero for
    // procedural worlds; with live authored sources, zero disables disk reuse.
    uint64_t authoredContentHash=0;
};

struct HorizonRuntimeStats {
    size_t clusters=0, atlasBytes=0, vertices=0, triangles=0;
    double cpuPrepareMs=0, gpuUploadMs=0;
    size_t preparing=0, discarded=0; // stream status on owning thread
    size_t cachedRegions=0, cacheHits=0; // bounded CPU bake reuse
    size_t archiveHits=0; // full re-use across game launches, when configured
};

class TerraWeaveHorizonScene {
public:
    TerraWeaveHorizonScene(TerrainSettings terrain, BiomeSettings biomes,
                           HorizonSettings settings = {})
        : terrain_(terrain), biomes_(biomes), settings_(settings) {
        validateHorizonSettings(settings_);
        world::StreamLimits limits;
        limits.residentChunks=1;
        limits.residentBytes=kRegionBudget;
        limits.retryUpdates=2;
        stream_=std::make_unique<world::AsyncChunkStream>(
            limits, 2*kRegionBudget, 1,
            [this](world::Cell cell)->world::AsyncChunkStream::Payload {
                auto result=std::make_shared<Prepared>();
                const auto start=std::chrono::steady_clock::now();
                // Snap to the same stable middle-ring grid when preparing,
                // preventing a full atlas re-bake at every camera movement.
                const int64_t x=int64_t(cell.x)*settings_.middleTileCm+
                                settings_.middleTileCm/2;
                const int64_t z=int64_t(cell.z)*settings_.middleTileCm+
                                settings_.middleTileCm/2;
                // Retain a coherent generation+surface snapshot per job.
                // An old generation is never served from the current cache.
                const auto generation=sources_.load(std::memory_order_acquire);
                const uint64_t revision=generation?generation->revision:0;
                std::shared_ptr<const HorizonBake> baked;
                if(settings_.maxCachedRegions){
                    std::lock_guard lock(cacheMutex_);
                    const auto it=cache_.find(cell);
                    if(it!=cache_.end()&&it->second.revision==revision){
                        baked=it->second.bake;
                        it->second.stamp=++cacheTick_;
                        ++cacheHits_;
                    }
                }
                if(!baked){
                    const auto surface=generation?generation->surface:nullptr;
                    const auto archive=archiveStore_;
                    const bool diskAllowed=archive &&
                        (!surface || archive->authoredContentHash!=0);
                    const uint64_t fingerprint=diskAllowed ?
                        horizonArchiveFingerprint(terrain_,biomes_,settings_,
                                                  archive->authoredContentHash)
                        : 0;
                    if(diskAllowed && archive->read){
                        try {
                            std::vector<uint8_t> bytes;
                            if(archive->read(cell.x,cell.z,bytes)){
                                auto previous=decodeHorizonArchive(bytes,fingerprint);
                                // Reject a wrong/colliding storage key even
                                // if its settings and payload are valid.
                                if(previous.cameraXcm==x && previous.cameraZcm==z){
                                    baked=std::make_shared<const HorizonBake>(
                                        std::move(previous));
                                    ++archiveHits_;
                                }
                            }
                        } catch(const std::exception&) {
                            // Missing/corrupt/stale cache assets are not a
                            // reason to fail streaming. Generate afresh.
                        }
                    }
                    if(!baked){
                        baked=std::make_shared<const HorizonBake>(bakeHorizon(
                            terrain_,biomes_,settings_,x,z,surface.get()));
                        if(diskAllowed && archive->write){
                            try {
                                auto bytes=encodeHorizonArchive(*baked,fingerprint);
                                archive->write(cell.x,cell.z,bytes);
                            } catch(const std::exception&) {
                                // Caching is best effort, never gameplay state.
                            }
                        }
                    }
                    if(settings_.maxCachedRegions){
                        std::lock_guard lock(cacheMutex_);
                        if(sources_.load(std::memory_order_acquire)==generation){
                            cache_[cell]={baked,revision,++cacheTick_};
                            while(cache_.size()>settings_.maxCachedRegions){
                                auto old=cache_.begin();
                                for(auto it=cache_.begin();it!=cache_.end();++it)
                                    if(it->second.stamp<old->second.stamp)
                                        old=it;
                                cache_.erase(old);
                            }
                        }
                    }
                }
                result->bake=std::move(baked);
                result->model=makeHorizonModel(*result->bake);
                if(result->model)render::prepareModelUpload(*result->model);
                const auto end=std::chrono::steady_clock::now();
                result->cpuPrepareMs=
                    std::chrono::duration<double,std::milli>(end-start).count();
                return result;
            });
    }
    TerraWeaveHorizonScene(const TerraWeaveHorizonScene&)=delete;
    TerraWeaveHorizonScene& operator=(const TerraWeaveHorizonScene&)=delete;
    ~TerraWeaveHorizonScene() {
        if(stream_)stream_->shutdown([](world::Cell){});
        // Call shutdown(device) before destroying the RHI for GPU cleanup.
    }

    // The owner thread publishes an immutable snapshot. Canceled workers
    // retain their captured shared_ptr until they finish; old GPU proxy
    // remains in place until a replacement is uploaded.
    void setSurfaceSources(std::shared_ptr<const HorizonSurfaceSources> source) {
        auto generation=std::make_shared<SourceGeneration>();
        generation->revision=++sourceRevision_; // owner-thread only
        generation->surface=std::move(source);
        sources_.store(std::move(generation),std::memory_order_release);
        {
            std::lock_guard lock(cacheMutex_);
            cache_.clear(); // previous generation's ground/masks are invalid
        }
        if(stream_ && cataloged_) {
            stream_->remove(cell_,[](world::Cell){});
            cataloged_=false; // update() schedules a new revision for this cell
        }
    }
    // Configure optional packed/static game-asset lookup before streaming
    // starts. No implicit filesystem access is ever performed by the engine.
    void setArchiveStore(std::shared_ptr<const HorizonArchiveStore> store) {
        if(streamStarted_)throw std::logic_error(
            "TerraWeave Horizon archive store must be set before update");
        archiveStore_=std::move(store);
    }
    const TerrainSettings& terrainSettings() const {return terrain_;}
    const BiomeSettings& biomeSettings() const {return biomes_;}
    const HorizonSettings& settings() const {return settings_;}

    void update(gfx::Device& device,ForestCameraCm observer) {
        streamStarted_=true;
        if(observer.x<-kWorldLimitCm||observer.x>kWorldLimitCm||
           observer.z<-kWorldLimitCm||observer.z>kWorldLimitCm)
            throw std::out_of_range("TerraWeave Horizon camera extent");
        // Keep the current region until the camera moves past the cell
        // boundary plus a hysteresis margin. This prevents rapid rebuilds
        // when the observer oscillates near a 128m streaming edge.
        const int64_t side=settings_.middleTileCm;
        int64_t wantedX=detail::floorDiv(observer.x,side);
        int64_t wantedZ=detail::floorDiv(observer.z,side);
        const int64_t margin=settings_.regionHysteresisCm;
        if(cataloged_ && margin>0) {
            const auto inExpandedCell=[side,margin](int64_t position,
                                                     int32_t cell) {
                const int64_t lo=int64_t(cell)*side-margin;
                const int64_t hi=(int64_t(cell)+1)*side+margin;
                return position>=lo && position<hi;
            };
            if(inExpandedCell(observer.x,cell_.x))wantedX=cell_.x;
            if(inExpandedCell(observer.z,cell_.z))wantedZ=cell_.z;
        }
        const world::Cell desired{
            static_cast<int32_t>(wantedX),0,static_cast<int32_t>(wantedZ)
        };
        if(!cataloged_||!(desired==cell_)) {
            if(cataloged_)stream_->remove(cell_,[](world::Cell){});
            if(!stream_->add(desired,kRegionBudget))
                throw std::runtime_error("TerraWeave Horizon region reservation");
            cell_=desired;
            cataloged_=true;
        }
        stream_->update({},std::span<const world::Cell>(&desired,1),
            1,1,[&](world::Cell cell,const world::AsyncChunkStream::Payload& payload) {
                if(!(cell==cell_))return false;
                auto prepared=std::static_pointer_cast<Prepared>(payload);
                // If the next region is empty (e.g. ocean and terrain
                // proxy disabled), retire the old forest rather than leaving
                // stale distant silhouettes on-screen indefinitely.
                if(!prepared->model){
                    if(model_)render::releaseModel(device,*model_);
                    model_.reset();
                    clusters_=0;
                    terrainQuads_=0;
                    runtime_={};
                    runtime_.cpuPrepareMs=prepared->cpuPrepareMs;
                    return true;
                }
                // CPU model is already prepared on worker. uploadModel only
                // touches the renderer-owned Device on this update thread.
                auto next=std::move(prepared->model);
                const size_t nextAtlasBytes=prepared->bake->atlasRgba.size();
                const size_t nextVertices=next->vertices.size();
                const size_t nextTriangles=next->indices.size()/3;
                const auto uploadStart=std::chrono::steady_clock::now();
                if(!render::uploadModel(device,*next)){
                    render::releaseModel(device,*next);
                    return false;
                }
                if(model_)render::releaseModel(device,*model_);
                model_=std::move(next);
                originXcm_=prepared->bake->originXcm;
                originZcm_=prepared->bake->originZcm;
                clusters_=prepared->bake->clusters.size();
                terrainQuads_=prepared->bake->terrainResolution*
                              prepared->bake.terrainResolution;
                runtime_.clusters=clusters_;
                runtime_.atlasBytes=nextAtlasBytes;
                runtime_.vertices=nextVertices;
                runtime_.triangles=nextTriangles;
                runtime_.cpuPrepareMs=prepared->cpuPrepareMs;
                runtime_.gpuUploadMs=std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-uploadStart).count();
                return true;
            },[](world::Cell){});
    }

    void submit(render::Renderer3D& renderer,ForestRenderOriginCm origin = {}) const {
        if(!model_)return;
        render::ModelDraw draw;
        draw.model=model_.get();
        draw.world=render::mat4::translate({
            float(originXcm_-origin.x)*0.01f,
            float(-origin.y)*0.01f,
            float(originZcm_-origin.z)*0.01f
        });
        draw.flags=render::kDrawStatic;
        draw.persistentId=detail::hash2(originXcm_,originZcm_,
                                          0x5445525241484c4fULL)|1u;
        renderer.drawModel(draw); // renderer groups two materials into few draws
    }
    void shutdown(gfx::Device& device) {
        if(stream_){
            stream_->shutdown([](world::Cell){});
            stream_.reset();
        }
        if(model_){render::releaseModel(device,*model_);model_.reset();}
        { std::lock_guard lock(cacheMutex_); cache_.clear(); }
    }
    bool ready() const {return model_!=nullptr;}
    size_t clusterCount() const {return clusters_;}
    size_t approximateTerrainQuads() const {return terrainQuads_;}
    HorizonRuntimeStats runtimeStats() const {
        auto out=runtime_;
        if(stream_) {
            const auto& s=stream_->status();
            out.preparing=s.preparing;
            out.discarded=s.discarded;
        }
        {
            std::lock_guard lock(cacheMutex_);
            out.cachedRegions=cache_.size();
        }
        out.cacheHits=cacheHits_.load(std::memory_order_relaxed);
        out.archiveHits=archiveHits_.load(std::memory_order_relaxed);
        return out;
    }
    const world::AsyncChunkStream::Status& status() const {
        if(stream_)return stream_->status();
        static const world::AsyncChunkStream::Status empty;
        return empty;
    }

private:
    struct SourceGeneration {
        uint64_t revision=0;
        std::shared_ptr<const HorizonSurfaceSources> surface;
    };
    struct CacheEntry {
        std::shared_ptr<const HorizonBake> bake;
        uint64_t revision=0, stamp=0;
    };
    struct Prepared final : world::AsyncChunkStream::Resource {
        std::shared_ptr<const HorizonBake> bake;
        std::unique_ptr<render::Model> model;
        double cpuPrepareMs=0;
    };
    static constexpr size_t kRegionBudget=24u*1024u*1024u;
    TerrainSettings terrain_;
    BiomeSettings biomes_;
    HorizonSettings settings_;
    std::unique_ptr<world::AsyncChunkStream> stream_;
    std::unique_ptr<render::Model> model_;
    world::Cell cell_{};
    bool cataloged_=false;
    int64_t originXcm_=0,originZcm_=0;
    size_t clusters_=0,terrainQuads_=0;
    HorizonRuntimeStats runtime_{};
    std::atomic<std::shared_ptr<const SourceGeneration>> sources_{};
    uint64_t sourceRevision_=0; // update thread only
    mutable std::mutex cacheMutex_;
    std::map<world::Cell,CacheEntry> cache_;
    uint64_t cacheTick_=0; // protected by cacheMutex_
    std::atomic<size_t> cacheHits_{0};
    std::atomic<size_t> archiveHits_{0};
    std::shared_ptr<const HorizonArchiveStore> archiveStore_;
    bool streamStarted_=false;
};

} // namespace aiverse::terraweave
