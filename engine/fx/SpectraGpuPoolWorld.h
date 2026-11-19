// AIVERSE SpectraFX Stage 28 — shared visual FX emitter resource groups.
// One persistent ComputeParticles per UNIQUE compatible graph+GPU policy,
// not per game object. All fallback effects share one existing CPU BatchWorld.
// Genre-neutral, presentation-only; no gameplay ECS handles or authority.
// This is NOT one monolithic GPU particle allocation across all materials.
#pragma once
#include "SpectraGraphRuntime.h"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace aiverse::spectra {

struct PoolWorldBudgets {
    uint32_t maxGpuGroups = 16;        // unique compiled graph + backend policy
    uint32_t maxGroupCapacity = 1024;  // each ComputeParticles resource (1..4096)
    uint32_t maxBurstsPerFrame = 256;  // global presentation submission limit
    fx::FxQuality quality = fx::FxQuality::High;
};

struct PoolWorldStats {
    uint32_t gpuGroups = 0, cpuDefinitions = 0;
    uint32_t submittedBursts = 0, rejectedBursts = 0, frameBursts = 0;
    uint32_t cpuDrawCalls = 0, gpuDrawCalls = 0;
    uint32_t cpuParticles = 0, gpuParticlesEstimated = 0;
    size_t gpuBytes = 0;
};

class GpuPoolWorld final {
public:
    GpuPoolWorld() = default;
    ~GpuPoolWorld() { shutdown(); }
    GpuPoolWorld(const GpuPoolWorld&) = delete;
    GpuPoolWorld& operator=(const GpuPoolWorld&) = delete;

    // Init/destroy outside active graphics encoders; Device outlives this world.
    bool init(gfx::Device* device, PoolWorldBudgets budgets = {});
    void shutdown();

    // Definitions are cached by FULL serialized effect + GPU options. Two
    // different source names may share one GPU group when truly identical.
    // Unsupported effects are preserved in a shared CPU BatchWorld; RequireGpu
    // rejects rather than approximating. Definitions survive visual reset.
    bool defineGraph(const std::string& name,const Json& graph,
                     const Json& overrides = Json::object(),
                     const GraphRuntimeOptions& options = {});
    bool defineEffect(const std::string& name,const fx::EffectDef& effect,
                      const GraphRuntimeOptions& options = {});
    bool setNamedParameter(const std::string& effectName,
                           const std::string& parameterName,float value);

    // One-shot births only. Different logical sources may provide independent
    // eight-scalar VM parameter snapshots without changing living particles
    // owned by other sources in the same GPU group.
    bool burst(const std::string& effectName, fx::float3 position,
               fx::float3 direction = {0,1,0},
               fx::float4 tint = {1,1,1,1},
               const std::array<float,8>* sourceParams = nullptr);

    // Host calls beginPresentationFrame AFTER Device::beginFrame and BEFORE
    // its scene render pass. step() once before the scene pass, draw() once
    // within that pass, endPresentationFrame() afterward.
    bool beginPresentationFrame(const render::RTSCamera& camera,
                                const render::mat4& viewProj,int width,int height);
    bool step(float dt);
    void draw(const fx::FxFog& fog = {});
    void endPresentationFrame();

    // Scene-level visual rollback reset; clears ALL effects registered to
    // this pool, not a single GPU birth. Do not invoke mid-encoder.
    bool resetForRollback();

    bool ready() const {return initialized_;}
    const std::string& reason() const {return reason_;}
    const PoolWorldStats& stats() const {return stats_;}
    const BatchWorld& cpuBatchWorld() const {return cpu_;}
    bool isGpuEffect(const std::string& effectName) const;
private:
    struct Binding {
        int cpuId = -1;
        size_t gpuIndex = 0;
        bool gpu = false, usesRuntimeParams = false;
        std::array<float,8> defaults{};
        InstanceParameterTable named;
    };
    struct Group {
        std::string key;
        std::unique_ptr<ComputeParticles> particles;
    };
    bool defineCompiled(const std::string& name,const fx::EffectDef& effect,
                        const GraphRuntimeOptions& options,
                        const InstanceParameterTable& named);
    static std::string groupKey(const fx::EffectDef& effect,
                                const GraphRuntimeOptions& options,
                                uint32_t capacity);

    gfx::Device* device_ = nullptr;
    BatchWorld cpu_;
    PoolWorldBudgets budgets_{};
    PoolWorldStats stats_{};
    std::vector<Group> gpuGroups_;
    std::map<std::string,Binding> bindings_;
    bool initialized_ = false, frameOpen_ = false, stepped_ = false, drawn_ = false;
    std::string reason_;
};

} // namespace aiverse::spectra
