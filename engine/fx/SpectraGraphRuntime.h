// AIVERSE SpectraFX — capability-based graph execution.
// Conservative GPU eligibility gates the experimental Metal billboard subset.
// Unsupported graphs preserve the full fx::EffectDef on the established CPU
// ParticleSystem. No FPS/RTS dependencies or authoritative gameplay writes.
#pragma once

#include "VfxGraph.h"
#include "SpectraGpuParticles.h"
#include <cstdint>
#include <string>
#include <vector>

namespace aiverse::spectra {

struct GraphRuntimeOptions {
    fx::FxQuality quality = fx::FxQuality::High;
    ComputePreference preference = ComputePreference::Automatic;
    uint32_t capacity = 1024; // GPU-only slots; not a CPU EffectDef particle budget
};

struct GraphGpuEligibility {
    bool eligible = false;
    ComputeEmitterDesc gpu{};
    std::string reason; // why a CPU fallback is mandatory
};

// Stateless, strict capability check for the *compiled* graph. Validated
// EffectDef is the authority, never the graph node names/genre or a heuristic
// like "contains SpriteOutput". No RHI resources are touched.
GraphGpuEligibility inspectGraphGpuEligibility(
    const fx::EffectDef& effect, const GraphRuntimeOptions& options = {});

// One graph definition per instance. Can be used by ANY native project or
// presentation adapter. Instances do not own the graphics Device.
// initGraph compiles v1 graph IR and delegates to initEffect.
// On Auto/PreferGpu: compatible graphs attempt Metal; unsupported or failed
// compute pipelines get the exact original EffectDef on CPU.
// RequireGpu fails loudly if the graph/device cannot use the GPU backend.
// Call step before render pass; draw within the pass. No ECS gameplay state.
class GraphRuntime final {
public:
    GraphRuntime() = default;
    ~GraphRuntime() { shutdown(); }
    GraphRuntime(const GraphRuntime&) = delete;
    GraphRuntime& operator=(const GraphRuntime&) = delete;

    bool initGraph(gfx::Device* device, const Json& graph,
                   const Json& overrides = Json::object(),
                   const GraphRuntimeOptions& options = {});
    bool initEffect(gfx::Device* device, const fx::EffectDef& compiled,
                    const GraphRuntimeOptions& options = {});
    void shutdown();

    bool burstAt(fx::float3 worldPosition,
                 fx::float3 direction = {0,1,0},
                 fx::float4 tint = {1,1,1,1});
    void setView(const render::RTSCamera&, const render::mat4&,
                 int width, int height);
    bool step(float dt); // between beginFrame/endFrame, before scene render pass
    void draw(const fx::FxFog& fog = {}); // during still-open render pass
    void clear();

    ComputeBackend backend() const { return backend_; }
    bool gpuActive() const { return initialized_ && backend_ == ComputeBackend::Metal; }
    const std::string& reason() const { return reason_; }
    const std::vector<std::string>& errors() const { return errors_; }
    const fx::ParticleSystem& cpuFallback() const {
        return useComputeWrapper_ ? gpu_.cpuFallback() : cpu_;
    }
    const ComputeStats& gpuStats() const { return gpu_.stats(); }
private:
    bool initCpu(gfx::Device*, const fx::EffectDef&, fx::FxQuality);
    bool initialized_ = false;
    bool useComputeWrapper_ = false;
    ComputeBackend backend_ = ComputeBackend::Cpu;
    fx::ParticleSystem cpu_;
    ComputeParticles gpu_;
    int cpuEffect_ = -1;
    std::string reason_;
    std::vector<std::string> errors_;
};

} // namespace aiverse::spectra
