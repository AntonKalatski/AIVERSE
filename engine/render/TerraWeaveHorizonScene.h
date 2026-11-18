// TerraWeave Horizon: async region baking with ONE resident model and atlas.
// Reuses AIVERSE AsyncChunkStream for background CPU work; only the owning
// render thread uploads and releases GPU buffers. The previous GPU proxy
// stays displayed until a new region has been successfully uploaded.
#pragma once
#include "engine/render/TerraWeaveHorizonModel.h"
#include "engine/render/Renderer3D.h"
#include "engine/world/AsyncChunkStream.h"
#include "engine/render/TerraWeaveForestAdapter.h"
#include <cstdint>
#include <chrono>
#include <limits>
#include <memory>
#include <stdexcept>

namespace aiverse::terraweave {

struct HorizonRuntimeStats {
    size_t clusters=0, atlasBytes=0, vertices=0, triangles=0;
    double cpuPrepareMs=0, gpuUploadMs=0;
    size_t preparing=0, discarded=0; // stream status on owning thread
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
                result->bake=bakeHorizon(terrain_,biomes_,settings_,x,z);
                result->model=makeHorizonModel(result->bake);
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

    void update(gfx::Device& device,ForestCameraCm observer) {
        if(observer.x<-kWorldLimitCm||observer.x>kWorldLimitCm||
           observer.z<-kWorldLimitCm||observer.z>kWorldLimitCm)
            throw std::out_of_range("TerraWeave Horizon camera extent");
        const world::Cell desired{
            static_cast<int32_t>(detail::floorDiv(observer.x,settings_.middleTileCm)),
            0,
            static_cast<int32_t>(detail::floorDiv(observer.z,settings_.middleTileCm))
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
                const size_t nextAtlasBytes=prepared->bake.atlasRgba.size();
                const size_t nextVertices=next->vertices.size();
                const size_t nextTriangles=next->indices.size()/3;
                const auto uploadStart=std::chrono::steady_clock::now();
                if(!render::uploadModel(device,*next)){
                    render::releaseModel(device,*next);
                    return false;
                }
                if(model_)render::releaseModel(device,*model_);
                model_=std::move(next);
                originXcm_=prepared->bake.originXcm;
                originZcm_=prepared->bake.originZcm;
                clusters_=prepared->bake.clusters.size();
                terrainQuads_=prepared->bake.terrainResolution*
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
        return out;
    }
    const world::AsyncChunkStream::Status& status() const {
        if(stream_)return stream_->status();
        static const world::AsyncChunkStream::Status empty;
        return empty;
    }

private:
    struct Prepared final : world::AsyncChunkStream::Resource {
        HorizonBake bake;
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
};

} // namespace aiverse::terraweave
