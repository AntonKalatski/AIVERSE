// Minimal native-project entry point for LumaWeave frame orchestration.
// Does NOT own Device::beginFrame()/endFrame(): those are platform-owned and
// may include multiple viewports. Does NOT mutate authoritative ECS.
// Surface drawer callback queues the actual models/terrain/sprites in the
// usual Renderer3D path after beginScene and before render.
// Host is responsible for correct screen pass and optional UI after resolve.
#pragma once
#include "LumaWeaveNativeCoordinator.h"
#include "GiGpuCascadeRuntime.h"
#include <cstdint>
#include <span>
#include <utility>

namespace aiverse::render {

struct LumaNativeViewInput {
    RTSCamera camera{};
    uint32_t width=0,height=0;
    float visibleLightRadius=100;
    LumaWeaveQuality quality=LumaWeaveQuality::Balanced;
    // Ray BVH is optional for GLES/Vulkan/legacy raster lighting. When absent,
    // geometryRevision MUST be zero. The Metal direct path requires both.
    uint64_t geometryRevision=0;
    const GiGpuBvh* sceneGeometry=nullptr;  // immutable shared ray-query BVH

    // Optional INDIRECT lighting (separate bounced term). The host owns the
    // cascade runtime and MUST have published its own geometry revision before
    // this frame. This does not make direct light the GI probe source.
    GiGpuCascadeRuntime* indirectGi=nullptr;
    size_t giProbeBudget=64;
    // Explicit opt-in: export only GI-eligible point lights (max 16) from
    // LumaWeave. False preserves externally authored GI lights unchanged.
    bool synchronizeGiPointSources=false;
    std::span<const LumaLight> authored{};
    std::span<const LumaFxSystemSnapshot> fxSystems{};
    std::span<const LumaEffectLight> effectLights{};
    std::span<const LumaAreaEmitter> areaLights{};
    bool allowMobileFallback=true;
};

struct LumaNativeViewResult {
    LumaNativeFrameStats sources{};
    LumaWeaveMetalScreen::Completion priorGpuCompletion{};
    GiCascadeFrame giFrame{};           // actual submitted/dirty probe tickets
    LumaGiSyncStatus giSourceSync{};    // meaningful if sync was requested
    bool giRequested=false;
    bool giBound=false;
    bool giDeferred=false;             // no stale bounced light on failed sync
    bool submittedFrame=false;
};

class LumaWeaveNativeDriver {
public:
    LumaWeaveNativeDriver(gfx::Device& device,Renderer3D& renderer,
                         PostProcess& existing)
        :device_(device),renderer_(renderer),existing_(existing),
         post_(existing,renderer.lights),
         coordinator_(post_,existing,renderer.lights) {}
    LumaWeaveNativeDriver(const LumaWeaveNativeDriver&)=delete;
    LumaWeaveNativeDriver& operator=(const LumaWeaveNativeDriver&)=delete;

    bool initialize(){
        // On weak/mobile backends this fails cleanly and the existing
        // LightSystem/lighting path remains fully available.
        return post_.initialize(device_);
    }
    bool shutdown(){return post_.shutdown();}
    LumaWeavePostProcess& gpuPost(){return post_;}
    LumaWeaveNativeCoordinator& coordinator(){return coordinator_;}

    // Requires the host has ALREADY called device.beginFrame and completed
    // ECS/SpectraFX read barriers. One view at a time. The submitted callback
    // may render opaque/transparent native models and user terrain normally.
    template<class SubmitScene>
    LumaNativeViewResult frame(const LumaNativeViewInput& input,
                               const gfx::PassDesc& output,
                               SubmitScene&& submitScene) {
        if(!input.width || !input.height ||
           (input.sceneGeometry==nullptr)!=(input.geometryRevision==0))
            throw std::invalid_argument(
                "LumaWeave frame requires nonzero view and coherent optional ray BVH revision");
        LumaNativeViewResult result;
        result.priorGpuCompletion=post_.pollGpu(); // callbacks from prior beginFrame
        coordinator_.collectScene(input.authored,input.fxSystems,
                                  input.effectLights,input.areaLights);
        renderer_.beginScene(input.camera,int(input.width),int(input.height));
        // beginScene() intentionally does not clear borrowed GI overlays:
        // reset them for EVERY view, including when this frame has no GI.
        // Also release borrowed pointers on exceptions and on frame exit.
        renderer_.clearDynamicGiOverlay();
        struct ScopedGiOverlay {
            Renderer3D& renderer;
            ~ScopedGiOverlay(){renderer.clearDynamicGiOverlay();}
        } scopedGi{renderer_};
        result.sources=coordinator_.prepareFrame(
            input.sceneGeometry,input.geometryRevision,input.camera.target,
            input.visibleLightRadius,input.width,input.height,input.quality,
            input.allowMobileFallback);
        if(input.indirectGi) {
            result.giRequested=true;
            auto& gi=*input.indirectGi;
            // Poll actual Metal completion callbacks BEFORE attempting to
            // republish moving bounce lights. No old-light GI is displayed
            // while a newer source revision is waiting for a GPU fence.
            if(input.synchronizeGiPointSources) {
                result.giFrame=gi.updateFrame(0,input.camera.target);
                result.giSourceSync=coordinator_.syncGi(gi);
                result.giDeferred=!result.giSourceSync.accepted;
            }
            if(gi.ready() && !result.giDeferred) {
                result.giFrame=gi.updateFrame(input.giProbeBudget,
                                              input.camera.target);
                result.giBound=gi.bindCascades(renderer_);
            }
        }
        std::forward<SubmitScene>(submitScene)(renderer_);
        renderer_.render(output,&coordinator_.activePost());
        renderer_.resolvePostFx();
        result.submittedFrame=true;
        return result;
    }

private:
    gfx::Device& device_;
    Renderer3D& renderer_;
    PostProcess& existing_;
    LumaWeavePostProcess post_;
    LumaWeaveNativeCoordinator coordinator_;
};

} // namespace aiverse::render
