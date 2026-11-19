// LumaWeave native render-frame coordinator. All sources are presentation-only;
// no ECS authoritativeness or hidden app/main-loop hooks are introduced.
// Call collectScene() AFTER authored/ECS/SpectraFX presentation read barrier.
// Call prepareFrame() AFTER Renderer3D::beginScene() and BEFORE draw/render.
// Choose exactly one PostProcess returned by activePost(), never both direct
// lighting paths. The legacy LightSystem map is the explicit mobile fallback.
#pragma once
#include "LumaWeaveParticleBridge.h"
#include "LumaWeaveMobileFallback.h"
#include "LumaWeavePostProcess.h"
#include "LumaWeaveGiSync.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace aiverse::render {

struct LumaFxSystemSnapshot {
    const fx::ParticleSystem* system=nullptr;
    uint32_t systemNamespace=0;
    int maxLights=128;
};

enum class LumaDirectRoute : uint8_t {
    Disabled=0,
    MetalGpu=1,
    MobileRasterFallback=2
};

struct LumaNativeFrameStats {
    LumaDirectRoute route=LumaDirectRoute::Disabled;
    size_t authored=0,effects=0,areaChildren=0,active=0;
    uint64_t sourceRevision=0,geometryRevision=0;
    bool changed=false,sourceUploadDeferred=false,geometryUploadDeferred=false;
    bool gpuExclusive=false,backendUnavailable=false,sourceLoss=false;
    LumaClusterBuildStats clusters{};
    LumaMobilePlan mobile{};
    LumaWeaveDecision quality{};
};

class LumaWeaveNativeCoordinator {
public:
    LumaWeaveNativeCoordinator(LumaWeavePostProcess& gpu,
                               PostProcess& existingPost,LightSystem& legacy)
        :gpu_(gpu),existing_(existingPost),legacy_(legacy) {}

    // Stable snapshot of the whole frame. MULTIPLE FX particle systems can
    // coexist: each system namespace is required nonzero and unique and the
    // particleUid is stable over a live particle lifetime.
    void collectScene(std::span<const LumaLight> authored,
                      std::span<const LumaFxSystemSnapshot> fxSystems={},
                      std::span<const LumaEffectLight> effects={},
                      std::span<const LumaAreaEmitter> areas={}) {
        if(fxSystems.size()>64)throw std::length_error("LumaWeave FX system count >64");
        std::vector<uint32_t> ids;
        ids.reserve(fxSystems.size());
        std::vector<LumaLight> sources;
        sources.reserve(authored.size()+effects.size());
        for(const auto& light:authored) {
            if(light.key>>62)throw std::invalid_argument("authored LumaWeave source in reserved ID domain");
            sources.push_back(light);
        }
        size_t particlesCount=0;
        for(const auto& e:fxSystems) {
            if(!e.system || !e.systemNamespace ||
               e.systemNamespace>LumaWeaveParticleBridge::kMaxSystemNamespace ||
               e.maxLights<0 || e.maxLights>1024)
                throw std::invalid_argument("invalid LumaWeave particle system snapshot");
            if(std::find(ids.begin(),ids.end(),e.systemNamespace)!=ids.end())
                throw std::invalid_argument("duplicate LumaWeave particle system namespace");
            ids.push_back(e.systemNamespace);
            std::vector<fx::FxIdentifiedPointLight> collected;
            if(e.maxLights) e.system->collectLightsStable(collected,e.maxLights);
            particlesCount+=collected.size();
            for(const auto& particle:collected)
                sources.push_back(LumaWeaveParticleBridge::convert(
                    e.systemNamespace,particle));
        }
        for(const auto& e:effects)sources.push_back(LumaWeaveEffectBridge::convert(e));
        auto all=LumaWeaveAreaLights::combine(std::move(sources),areas);
        // Make all source replacement/duplicate validation transactional:
        // a failed publish leaves the previously accepted snapshot intact.
        const auto update=registry_.publish(std::move(all));
        stats_.changed=update.changed;
        stats_.sourceRevision=update.revision;
        stats_.authored=authored.size();
        stats_.effects=particlesCount+effects.size();
        stats_.areaChildren=registry_.lights().size()-
                            (stats_.authored+stats_.effects);
        stats_.active=registry_.lights().size();
    }

    // geometry: EXACT same immutable GI GPU BVH (do not duplicate tracing).
    // Host calls this every frame AFTER Renderer3D.beginScene (which resets
    // the legacy dynamic point lights). On stale/moving source input the
    // GPU material-light output is disabled until source/geometry packets
    // can be atomically published, preventing display of old lighting.
    LumaNativeFrameStats prepareFrame(const GiGpuBvh& geometry,uint64_t geometryRevision,
                                       float3 cameraFocus,float viewRadius,
                                       uint32_t viewW,uint32_t viewH,
                                       LumaWeaveQuality requestedQuality=
                                           LumaWeaveQuality::Balanced,
                                       bool allowMobileFallback=true) {
        if(!geometryRevision || !viewW || !viewH ||
           !std::isfinite(cameraFocus.x)||!std::isfinite(cameraFocus.y)||
           !std::isfinite(cameraFocus.z) ||
           !std::isfinite(viewRadius) || viewRadius<=0 || viewRadius>100000)
            throw std::invalid_argument("invalid LumaWeave native view or geometry");
        stats_.route=LumaDirectRoute::Disabled;
        stats_.gpuExclusive=false;
        stats_.backendUnavailable=false;
        stats_.sourceUploadDeferred=stats_.geometryUploadDeferred=false;
        stats_.sourceLoss=false;
        stats_.mobile={};
        stats_.quality={};
        stats_.geometryRevision=geometryRevision;

        const bool gridCurrent=clustersReady_ &&
            clusters_.sourceRevision()==registry_.revision();
        if(!gridCurrent) {
            stats_.clusters=clusters_.rebuild(registry_,clusterConfig_);
            clustersReady_=true;
        }

        // Default remain disabled until all source publication + separate
        // material capture is supported by current GPU and PostProcess.
        gpu_.setEnabled(false);
        // GPU capture requires an actually active linear HDR downstream.
        // Otherwise choose the legacy mobile path instead of suppressing
        // legacy lights for a postprocess wrapper that will never run.
        if(requestedQuality!=LumaWeaveQuality::Off && gpu_.ready() &&
           existing_.active() && existing_.linearScene()) {
            stats_.quality=LumaWeaveQualityPolicy::choose(
                requestedQuality,viewW,viewH,registry_.lights().size(),
                gpu_.screen().settings());
            const bool noLegacy=legacy_.count()==0 &&
                                !legacy_.staticPointLightsActive();
            if(stats_.quality.admitted && noLegacy) {
                const bool lights=gpu_.publishSources(registry_,clusters_);
                const bool geom=lights && gpu_.publishGeometry(geometry,geometryRevision);
                stats_.sourceUploadDeferred=!lights;
                stats_.geometryUploadDeferred=lights && !geom;
                if(lights && geom) {
                    gpu_.setQuality(requestedQuality);
                    gpu_.setEnabled(true);
                    stats_.route=LumaDirectRoute::MetalGpu;
                    stats_.gpuExclusive=true;
                    return stats_;
                }
            }
        } else {
            stats_.backendUnavailable=true;
        }

        // The same source may already be present in an externally managed
        // LightSystem. If that map is nonempty, do not add this registry again:
        // there is no generic way to prove identity compatibility here.
        if(allowMobileFallback && requestedQuality!=LumaWeaveQuality::Off &&
           legacy_.quality()!=LightQuality::Off &&
           legacy_.count()==0 && !legacy_.staticPointLightsActive()) {
            stats_.mobile=LumaWeaveMobileFallback::plan(
                registry_,cameraFocus,viewRadius,LightSystem::kMaxLights);
            if(LumaWeaveMobileFallback::enqueue(legacy_,stats_.mobile)) {
                stats_.route=LumaDirectRoute::MobileRasterFallback;
                stats_.sourceLoss=stats_.mobile.lossy;
            }
        }
        return stats_;
    }

    // In GPU mode use the Luma wrapper; in raster fallback mode use exactly
    // the existing PostProcess (no double light from a stale GPU history).
    PostProcess& activePost() const {
        return stats_.route==LumaDirectRoute::MetalGpu ?
            static_cast<PostProcess&>(gpu_) : existing_;
    }
    // Separate indirect GI synchronization; never mixes bounce output into
    // LumaWeave's direct light buffer. Caller must poll GPU fences first.
    template<class GiRuntime>
    LumaGiSyncStatus syncGi(GiRuntime& gi) {
        return giSync_.publish(registry_,gi);
    }
    const LumaWeaveRegistry& registry() const{return registry_;}
    const LumaWeaveClusters& clusters() const{return clusters_;}
    const LumaNativeFrameStats& stats() const{return stats_;}
    void setClusterConfig(LumaClusterConfig cfg) {
        clusterConfig_=cfg;
        clustersReady_=false;
    }
    void reset() {
        // Does not destroy RHI; native host must separately drain GPU fences.
        registry_=LumaWeaveRegistry{};
        clusters_=LumaWeaveClusters{};
        clustersReady_=false;
        giSync_.reset();
        stats_={};
        gpu_.setEnabled(false);
    }

private:
    LumaWeavePostProcess& gpu_;
    PostProcess& existing_;
    LightSystem& legacy_;
    LumaWeaveRegistry registry_;
    LumaWeaveClusters clusters_;
    LumaWeaveGiSync giSync_;
    LumaClusterConfig clusterConfig_{};
    LumaNativeFrameStats stats_{};
    bool clustersReady_=false;
};

} // namespace aiverse::render
