#pragma once
#include "WorldAssets.h"
#include "engine/render/Renderer3D.h"
#include "engine/world/NereidWaterScene.h"
#include "engine/world/NereidUnderwaterView.h"
#include "engine/world/NereidRipples.h"

namespace aiverse::world {
// CPU model decode/terrain tessellation in the worker; GPU publication on update's thread.
class WorldScene {
public:
    WorldScene(const Json& manifest,AssetReader reader,std::shared_ptr<render::ModelTextureCache> textures = {});
    ~WorldScene();
    void update(gfx::Device&,std::span<const Vec3> observers,int radius=2);
    void draw(render::Renderer3D&,Cell renderOrigin,int originCellSize);
    void shutdown(gfx::Device&);
    // Opt-in presentation-only water; call before update. Never mutates ECS.
    void setNereidSurfaces(std::vector<NereidSceneSurface> surfaces);
    size_t nereidVisibleSurfaceCount()const;
    NereidUnderwaterView nereidUnderwaterAt(Vec3 camera)const;
    // Explicit presentation read barrier: consume one ECS tick of events.
    void ingestNereidInteractions(
        const std::vector<lib::nereid::WaterPresentationEvent>& events,
        uint32_t completedTick) {ripples_.ingest(events,completedTick);}
    template<class Callback> void ingestNereidInteractions(
        const std::vector<lib::nereid::WaterPresentationEvent>& events,
        uint32_t completedTick,Callback&& onSpectraFxOrAudio) {
        ripples_.ingest(events,completedTick,
                        std::forward<Callback>(onSpectraFxOrAudio));
    }
    void advanceNereidRipples(float deltaSeconds) {
        ripples_.advance(deltaSeconds);
    }
    const NereidRippleField& nereidRipples()const{return ripples_;}

    const AsyncChunkStream::Status& status()const{return stream_->status();}
private:
    struct Instance { size_t model=0;render::mat4 transform; };
    struct Prepared final:AsyncChunkStream::Resource {
        std::vector<std::shared_ptr<render::Model>> models;
        std::vector<std::string> sharedKeys;                  // cache key per model, empty: owned by this chunk
        std::vector<Instance> instances;
    };
    struct ModelCache;
    WorldAssets assets_;
    std::shared_ptr<render::ModelTextureCache> textures_;
    std::shared_ptr<ModelCache> cache_;                       // "shareModels": one decoded / uploaded model per file
    bool shareModels_=false;
    float overhang_=0;                                        // "meshOverhang": units a mesh may reach past its chunk
    void releaseChunk(gfx::Device&,Prepared&);
    size_t uploadBytes_=16*1024*1024;
    std::map<Cell,std::shared_ptr<Prepared>> resident_;
    std::unique_ptr<AsyncChunkStream> stream_;
    NereidWaterScene nereidWater_;
    NereidRippleField ripples_;
    std::shared_ptr<Prepared> prepare(Cell,const AssetReader&);
};
}
