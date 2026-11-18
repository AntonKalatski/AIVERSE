#pragma once
#include "WorldAssets.h"
#include "engine/render/Renderer3D.h"

namespace aiverse::terraweave { class TerraWeaveForestScene; class TerraWeaveHorizonScene; }
namespace aiverse::world {
// CPU model decode/terrain tessellation in the worker; GPU publication on update's thread.
class WorldScene {
public:
    WorldScene(const Json& manifest,AssetReader reader,std::shared_ptr<render::ModelTextureCache> textures = {});
    ~WorldScene();
    void update(gfx::Device&,std::span<const Vec3> observers,int radius=2);
    void draw(render::Renderer3D&,Cell renderOrigin,int originCellSize);
    void shutdown(gfx::Device&);
    const AsyncChunkStream::Status& status()const{return stream_->status();}
private:
    struct Instance { size_t model=0;render::mat4 transform; };
    struct Prepared final:AsyncChunkStream::Resource {
        std::vector<std::shared_ptr<render::Model>> models;
        std::vector<std::string> sharedKeys;                  // cache key per model, empty: owned by this chunk
        std::vector<Instance> instances;
        // Optional render-only authored landscape snapshot, populated only
        // when this WorldScene opts into TerraWeaveForest. Fixed metre data
        // is converted to signed cm before workers publish it.
        uint32_t terrainResolution = 0;
        std::vector<int32_t> terrainHeightsCm;
        std::vector<Bounds> placementBoxes; // local-space collision footprints
    };
    struct ModelCache;
    WorldAssets assets_;
    std::shared_ptr<render::ModelTextureCache> textures_;
    std::shared_ptr<ModelCache> cache_;                       // "shareModels": one decoded / uploaded model per file
    bool shareModels_=false;
    // Null unless the manifest explicitly opts in to procedural forests.
    std::unique_ptr<terraweave::TerraWeaveForestScene> terraForest_;
    std::unique_ptr<terraweave::TerraWeaveHorizonScene> terraHorizon_;
    bool terraForestReady_=false;
    float overhang_=0;                                        // "meshOverhang": units a mesh may reach past its chunk
    void releaseChunk(gfx::Device&,Prepared&);
    size_t uploadBytes_=16*1024*1024;
    std::map<Cell,std::shared_ptr<Prepared>> resident_;
    std::unique_ptr<AsyncChunkStream> stream_;
    std::shared_ptr<Prepared> prepare(Cell,const AssetReader&);
};
}
