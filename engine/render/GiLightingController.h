// Genre-independent host-facing dynamic indirect lighting owner.
// Chooses bounded Vulkan/Metal compute only when the concrete RHI supports it;
// all other devices (including GLES3/WebGL2) use the CPU reference and sparse
// sampled RGBA32F atlas. A caller must still supply updated immutable geometry,
// stable light identities, and explicitly opt in before displaying this overlay.
// Never persists ECS state; never modifies direct point/sun light ownership.
#pragma once
#include "GiDynamicRuntime.h"
#include "GiGpuDynamicRuntime.h"
#include "GiLightBridge.h"
#include "GiEnvironmentBridge.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {

enum class GiTransportMode : uint8_t { Off, CpuProbes, GpuCompute };

struct GiTransportConfig {
    float3 origin{};
    float spacing=1;
    int nx=8,ny=4,nz=8;
    uint32_t raysPerProbe=16;
    float traceDistance=12;
    bool preferGpu=true;
    double cpuFrameBudgetMs=1.25;
    size_t cpuMaxProbes=4;
};

class GiLightingController {
public:
    struct StepStats {
        GiTransportMode mode=GiTransportMode::Off;
        GiRuntimeStats cpu{};
        GiGpuDynamicRuntime::FrameStats gpu{};
        size_t pending=0;
        bool canPresent=false;
    };

    GiLightingController()=default;
    GiLightingController(const GiLightingController&)=delete;
    GiLightingController& operator=(const GiLightingController&)=delete;

    // Resources may not be destroyed while a Vulkan/Metal GPU batch is pending.
    // If shutdown returns false, keep calling updateFrame() until the fence is
    // complete, unbind any Renderer3D, then shutdown again.
    bool shutdown() {
        if (mode_==GiTransportMode::GpuCompute && !gpu_.shutdown()) return false;
        if (mode_==GiTransportMode::CpuProbes) cpu_.shutdown();
        scene_.reset();
        mode_=GiTransportMode::Off;
        device_=nullptr;
        revision_=0;
        return true;
    }

    bool initialize(gfx::Device& device, GiTransportConfig cfg) {
        if (!shutdown()) return false;
        if (cfg.cpuMaxProbes==0 || cfg.cpuMaxProbes>1024 ||
            cfg.raysPerProbe<8 || cfg.raysPerProbe>GiGpuSmallScene::kMaxSamples)
            return false;
        // The same config/dimensions must be used when failing over to CPU,
        // preserving the geometric location of each probe.
        if (cfg.preferGpu &&
            (device.info().backend==gfx::Backend::Metal ||
             device.info().backend==gfx::Backend::Vulkan) &&
            gpu_.initialize(device,cfg.origin,cfg.spacing,cfg.nx,cfg.ny,cfg.nz,
                            cfg.raysPerProbe,cfg.traceDistance)) {
            device_=&device;
            config_=cfg;
            mode_=GiTransportMode::GpuCompute;
            return true;
        }
        // No compute support is a NORMAL condition on GLES3 / WebGL2.
        // CPU GI only requires sampled RGBA32F, not floating-point MRT/HDR.
        try {
            cpu_.cpuTraceBudget().configure(cfg.cpuFrameBudgetMs,cfg.cpuMaxProbes);
            if (!cpu_.initialize(device,cfg.origin,cfg.spacing,cfg.nx,cfg.ny,
                                 cfg.nz,int(cfg.raysPerProbe),cfg.traceDistance))
                return false;
        } catch (const std::invalid_argument&) { return false; }
        device_=&device;
        config_=cfg;
        mode_=GiTransportMode::CpuProbes;
        return true;
    }

    GiTransportMode mode() const { return mode_; }
    bool ready() const { return mode_!=GiTransportMode::Off; }
    bool gpuBusy() const {
        return mode_==GiTransportMode::GpuCompute && gpu_.busy();
    }
    size_t maxMovingLights() const {
        return mode_==GiTransportMode::GpuCompute ? GiGpuSmallScene::kMaxLights :
               mode_==GiTransportMode::CpuProbes ? 64 : 0;
    }
    const GiTransportConfig& config() const { return config_; }

    // Publisher owns a read-barrier-frozen immutable scene snapshot. The
    // controller retains its shared ownership across all CPU pointer borrows.
    // GPU publication may refuse while the previous dispatch is pending.
    bool setGeometry(std::shared_ptr<const GiChunkedGeometrySnapshot> next,
                     uint64_t revision) {
        if (!ready() || !next || revision==0 || revision<=revision_) return false;
        if (mode_==GiTransportMode::GpuCompute) {
            if (!gpu_.setGeometry(next,revision)) return false;
        } else {
            cpu_.setTraceGeometry(next->traceView(),revision);
        }
        scene_=std::move(next);
        revision_=revision;
        return true;
    }

    bool setGeometryRegion(std::shared_ptr<const GiChunkedGeometrySnapshot> next,
                           uint64_t revision,GiBounds changed,
                           float extraRadius=0) {
        if (!ready() || !next || !scene_ || revision==0 ||
            revision<=revision_) return false;
        if (mode_==GiTransportMode::GpuCompute) {
            if (!gpu_.setGeometryRegion(next,revision,changed,extraRadius))
                return false;
        } else {
            cpu_.setTraceGeometryRegion(next->traceView(),revision,
                                        changed,extraRadius);
        }
        scene_=std::move(next);
        revision_=revision;
        return true;
    }

    // GPU supports 16 moving lights, CPU supports 64; never silently truncate
    // incoming authored sources and change the actual lighting design.
    bool setLights(std::vector<GiMovingLight> lights) {
        if (!ready() || lights.size()>maxMovingLights()) return false;
        if (mode_==GiTransportMode::GpuCompute)
            return gpu_.setLights(std::move(lights));
        cpu_.setLights(std::move(lights));
        return true;
    }

    // Environmental first bounce is separately opted in: the caller MUST
    // exclude these sources from its baked static GI base (or regenerate it).
    // Zero sun/sky intensity keeps the legacy static ambient path unchanged.
    bool setEnvironment(GiEnvironmentLight environment) {
        if (!ready() || !environment.valid()) return false;
        if (mode_==GiTransportMode::GpuCompute)
            return gpu_.setEnvironment(environment);
        (void)cpu_.setEnvironment(environment);
        return true;
    }

    // Capture the ordinary per-scene sun/sky only when the host EXPLICITLY
    // authorizes indirect bounce. Passing both multipliers as zero is a safe
    // no-op on default/off source state and avoids counting baked sky twice.
    bool syncRendererEnvironment(const Renderer3D& renderer,
                                 float sunBounceMultiplier=0,
                                 float skyBounceMultiplier=0) {
        return setEnvironment(giEnvironmentFromLighting(
            renderer.lighting,sunBounceMultiplier,skyBounceMultiplier));
    }

    // Use the same lights authored for the normal direct lighting path.
    // Only sources with indirectMultiplier>0 participate; static/baked lamps
    // stay excluded by default. Source namespace is stable per world/owner.
    bool setAuthoredLights(const std::vector<PointLight>& source,
                          uint32_t namespaceId,float3 focus={},
                          GiLightOverflowPolicy policy=GiLightOverflowPolicy::Reject,
                          GiLightBridge::Stats* stats=nullptr) {
        if(!ready())return false;
        auto converted=GiLightBridge::collect(
            source,namespaceId,maxMovingLights(),focus,policy,stats);
        return setLights(std::move(converted));
    }

    // Called AFTER Renderer3D::lights.add() for the current scene and BEFORE
    // the GI updateFrame/render pass. This copies the stable source descriptors,
    // so no gameplay/ECS pointers escape into an asynchronous GPU submission.
    // Spot cone and opt-in indirectMultiplier remain the source of truth.
    bool syncRendererLights(const Renderer3D& renderer,uint32_t namespaceId,
                            float3 focus={},
                            GiLightOverflowPolicy policy=GiLightOverflowPolicy::Reject,
                            GiLightBridge::Stats* stats=nullptr) {
        return setAuthoredLights(renderer.lights.submittedLights(),
                                namespaceId,focus,policy,stats);
    }

    // Geometry, vegetation, material and weather publishers can use one
    // shared event accumulator. GPU invalidation preserves in-flight revision
    // safety; CPU invalidation additionally clears its old atlas coefficients.
    // For changed geometry/material reflectance, republish BVH/albedo first.
    GiWorldDirtyAccumulator::Stats applyWorldChanges(
            GiWorldDirtyAccumulator& changes) {
        if(mode_==GiTransportMode::GpuCompute)
            return gpu_.applyWorldChanges(changes);
        if(mode_==GiTransportMode::CpuProbes)
            return cpu_.applyWorldChanges(changes);
        return {};
    }

    void invalidate(GiBounds bounds,GiDirtyReason reason,
                    float influenceRadius=0) {
        if(mode_==GiTransportMode::GpuCompute)
            (void)gpu_.invalidate(bounds,reason,influenceRadius);
        else if(mode_==GiTransportMode::CpuProbes)
            cpu_.invalidate(bounds,reason,influenceRadius);
    }

    // Call after the per-world publication barrier, between RHI beginFrame and
    // draw passes for GPU. CPU mode is also safe before a render pass.
    StepStats updateFrame(float3 focus,size_t gpuProbeBudget=32) {
        StepStats s;
        s.mode=mode_;
        if (mode_==GiTransportMode::GpuCompute) {
            s.gpu=gpu_.updateFrame(gpuProbeBudget,focus);
            s.pending=s.gpu.dirty;
            s.canPresent=s.gpu.safeForPresentation && bool(scene_);
        } else if (mode_==GiTransportMode::CpuProbes) {
            s.cpu=cpu_.updateAdaptive(focus);
            s.pending=s.cpu.dirty;
            s.canPresent=bool(scene_);
        }
        return s;
    }

    // Renderer3D borrows the atlas. Unbind BEFORE shutdown/reinitialization
    // and always after a rejected bind. Do not claim a successful GI frame if
    // no scene or the GPU's actual completion fence has not been observed.
    bool bind(Renderer3D& renderer) const {
        if (!scene_ || !ready()) {
            renderer.clearDynamicGiOverlay();
            return false;
        }
        const bool ok=mode_==GiTransportMode::GpuCompute
            ? gpu_.bind(renderer) : cpu_.bind(renderer);
        if (!ok) renderer.clearDynamicGiOverlay();
        return ok;
    }
    void unbind(Renderer3D& renderer) const {
        renderer.clearDynamicGiOverlay();
    }

    // Only for external Metal completion sources where readBufferAsync is
    // unavailable. Vulkan ignores this: its own VkFence serial is mandatory.
    bool confirmExternalGpuCompletion(uint64_t batchSerial,bool successful) {
        return mode_==GiTransportMode::GpuCompute &&
               gpu_.confirmExternalCompletion(batchSerial,successful);
    }

    // CPU reference for diagnostics; null for GPU transport (no CPU readback).
    const GiDynamicRuntime* cpuRuntime() const {
        return mode_==GiTransportMode::CpuProbes ? &cpu_ : nullptr;
    }
    const GiGpuDynamicRuntime* gpuRuntime() const {
        return mode_==GiTransportMode::GpuCompute ? &gpu_ : nullptr;
    }

private:
    gfx::Device* device_=nullptr;
    GiTransportMode mode_=GiTransportMode::Off;
    GiTransportConfig config_{};
    uint64_t revision_=0;
    std::shared_ptr<const GiChunkedGeometrySnapshot> scene_;
    GiDynamicRuntime cpu_;
    GiGpuDynamicRuntime gpu_;
};

} // namespace aiverse::render
