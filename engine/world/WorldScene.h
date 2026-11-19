#pragma once
#include "WorldAssets.h"
#include "engine/render/Renderer3D.h"
#include "engine/world/NereidWaterScene.h"
#include "engine/world/NereidCascadeScene.h"
#include "engine/world/NereidUnderwaterView.h"
#include "engine/world/NereidRipples.h"
#include "engine/world/NereidWindPresentation.h"
#include <cmath>
#include <algorithm>

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
    // Native projects may override static waterfall manifest from ECS.
    void setNereidCascades(std::vector<NereidCascadeSource> cascades) {
        cascades_.setCascades(std::move(cascades));
    }
    size_t nereidCascadeCount()const{return cascades_.residentCount();}
    template<class Fn> void forEachNereidCascade(Fn&& fn)const {
        cascades_.forEachSource(std::forward<Fn>(fn));
    }
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
        ripples_.advance(deltaSeconds,[this](uint64_t waterId,
                                     double x,double y,double z) {
            // Presentation-only coordinate quantization before an immutable
            // fixed-point current lookup; never enters authoritative ECS.
            auto fixed=[](double value) {
                constexpr double limit=1e12;
                const double safe=std::clamp(value,-limit,limit);
                return Fixed::fromRaw(int64_t(std::llround(safe*Fixed::kOne)));
            };
            return nereidWater_.currentForInteractionWater(
                waterId,{fixed(x),fixed(y),fixed(z)});
        });
        ripples_.applyVisualWind(deltaSeconds,visualWind_.east,
                                visualWind_.north,visualWind_.gust,
                                visualWind_.coupling);
    }
    void setNereidVisualWind(NereidVisualWind wind) {
        visualWind_=normalizeNereidWind(wind);
    }
    NereidVisualWind nereidVisualWind()const{return visualWind_;}
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
    NereidCascadeScene cascades_;
    NereidRippleField ripples_;
    NereidVisualWind visualWind_{};
    std::shared_ptr<Prepared> prepare(Cell,const AssetReader&);
};
}
