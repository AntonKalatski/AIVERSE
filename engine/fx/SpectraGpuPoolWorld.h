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
#include <limits>
#include <string>
#include <vector>

namespace aiverse::spectra {

// A handle is valid only for the creating GpuPoolWorld and its current
// initialization epoch. Recycled slots always get a new generation.
struct PoolSourceId {
    uint64_t worldEpoch=0;
    uint32_t slot=std::numeric_limits<uint32_t>::max();
    uint32_t generation=0;
    bool valid() const {
        return worldEpoch!=0 && generation!=0 &&
               slot!=std::numeric_limits<uint32_t>::max();
    }
};

// Continuous emission is strictly presentation-only, bounded per-source and
// by the shared PoolWorldBudgets::maxBurstsPerFrame. A lifetime of 0 is infinite.
struct PoolContinuousEmission {
    float burstsPerSecond=0;       // [0,120], rate-based births
    float metresPerBurst=0;        // 0 disables distance-based births
    float lifetimeSeconds=0;      // 0 = until explicitly stopped
    uint32_t maxBurstsPerFrame=8; // [1,64]
};

struct PoolWorldBudgets {
    uint32_t maxGpuGroups = 16;        // unique compiled graph + backend policy
    uint32_t maxGroupCapacity = 1024;  // each ComputeParticles resource (1..4096)
    uint32_t maxBurstsPerFrame = 256;  // global presentation submission limit
    uint32_t maxSources = 4096;        // live per-source emitter handles
    fx::FxQuality quality = fx::FxQuality::High;
    // Budget is shared across all GPU groups owned by this world. Optionally
    // supply the SAME external budget to other worlds/GraphRuntime instances
    // using this Device. The defaults reserve RHI room for other readbacks.
    CheckpointReadbackLimits checkpointReadback{};
    std::shared_ptr<CheckpointReadbackBudget> sharedCheckpointBudget;
};

struct PoolCheckpointStats {
    uint32_t activeGroups=0, pendingGroups=0, freshGroups=0;
    uint32_t staleGroups=0, missingGroups=0, restoreEligibleGroups=0;
    uint64_t requests=0, completed=0, failed=0, budgetDenied=0;
    uint64_t adaptiveBackoffs=0, adaptiveRecoveries=0;
    uint32_t maximumCurrentIntervalFrames=0;
    uint32_t killedSlotsAwaitingFence=0;
    uint64_t killedSlotsReclaimed=0;
    // The supported Metal compute Device reports health; other RHIs are
    // "unknown", NOT healthy by assertion.
    bool gpuDeviceHealthKnown=false;
    bool gpuDeviceHealthy=false;
    uint64_t gpuDeviceUnhealthy=0;
    uint64_t gpuKillUploadFailures=0;
    uint64_t gpuComputeBeginFailures=0;
    uint64_t gpuResetFailures=0;
    uint64_t gpuPassOrderRejections=0;
    uint64_t killScatterDispatches=0;
    size_t killIndexBytes=0;
    size_t bytesReturned=0; // successful callback payload, not GPU DMA metrics
    // If explicitly shared with other worlds, these budget counters reflect
    // the full shared scheduler, not only this GpuPoolWorld.
    CheckpointReadbackStats budget{};
};

// Reset is an ordered, restartable GPU-group transaction, not a single
// cross-device atomic command buffer. Records exactly which groups accepted
// reset uploads. Scene emission must stay blocked while retryRequired is true.
struct PoolResetReport {
    bool retryRequired=false;
    bool deviceLost=false;       // requires explicit host graphics-device recovery
    uint32_t groupCount=0;
    uint32_t groupsAccepted=0;
    uint32_t failedGroup=std::numeric_limits<uint32_t>::max();
    uint64_t attempts=0;
};

// CPU-side reconstruction recipe. No old graphics handles, entity IDs,
 // GPU particles or simulation time. Suitable for an explicit NEW pool after
 // a graphics Device loss, not a transparent restoration of live FX.
struct PoolRecoveryEffect {
    std::string name;
    fx::EffectDef effect;
    GraphRuntimeOptions options{};
    InstanceParameterTable named;
    std::array<float,8> defaults{};
};
struct PoolRecoveryManifest {
    PoolWorldBudgets budgets{};
    std::vector<PoolRecoveryEffect> effects;
};

struct PoolWorldStats {
    uint32_t gpuGroups = 0, cpuDefinitions = 0;
    uint32_t activeSources=0;
    uint64_t continuousBursts=0, continuousSkipped=0;
    uint64_t selectiveKillRequests=0;
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

    // Generation-safe presentation-only source ownership. Each source holds
    // its own parameter snapshot and named effect binding revision.
    // pause/retire prevent FUTURE births. stopSource(source, true) can
    // additionally invalidate that source's living GPU physical slots,
    // preserving unrelated source particles in the same GPU group.
    PoolSourceId createSource(const std::string& effectName);
    bool setSourceNamedParameter(PoolSourceId source,const std::string& name,float value);
    bool pauseSource(PoolSourceId source);
    bool resumeSource(PoolSourceId source);
    bool retireSource(PoolSourceId source);
    bool stopSource(PoolSourceId source,bool killLiving);
    // Read-only preflight for atomic ECS source staging. RHI health may still
    // change after checking; runtime operations always verify again.
    bool canStopSource(PoolSourceId source,bool killLiving) const;
    bool killSourceParticles(PoolSourceId source);
    bool setSourceTransform(PoolSourceId source,fx::float3 position,
                            fx::float3 direction={0,1,0},
                            fx::float4 tint={1,1,1,1});
    bool detachSourceTransform(PoolSourceId source);
    bool configureContinuous(PoolSourceId source,PoolContinuousEmission emission);
    // Checks binding revision without being affected by pause state.
    bool sourceBindingCurrent(PoolSourceId source) const;
    bool sourceActive(PoolSourceId source) const;
    bool emitSource(PoolSourceId source,fx::float3 position,
                    fx::float3 direction={0,1,0},
                    fx::float4 tint={1,1,1,1});

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
    // On device loss: do not resurrect old GPU particles. Host must keep the
    // old gfx::Device alive while shutting this pool down, then create a fresh
    // pool against a recovered Device and re-declare effects from authoring.
    const PoolResetReport& resetReport() const {return resetReport_;}
    // Export authored definitions while this pool (even an unhealthy one)
    // still exists. Create a different GpuPoolWorld on the new healthy
    // Device and import there. No live particles or source handles survive.
    PoolRecoveryManifest exportRecoveryManifest() const;
    bool importRecoveryManifest(gfx::Device* recoveredDevice,
                                const PoolRecoveryManifest& manifest);
    bool presentationBlocked() const {
        return resetReport_.retryRequired || resetReport_.deviceLost ||
               (device_ && !device_->gpuHealthy());
    }

    bool ready() const {return initialized_;}
    const std::string& reason() const {return reason_;}
    const PoolWorldStats& stats() const {return stats_;}
    // On-demand, no GPU readback. Reflects callbacks delivered since step().
    PoolCheckpointStats checkpointStats() const;
    // Copies an existing CURRENT-revision GPU checkpoint for one pooled
    // compute group. Does not request GPU readback or allocate unbounded
    // diagnostic data. Caller chooses where/how to persist bytes.
    bool exportFreshCheckpoint(size_t groupIndex,std::vector<uint8_t>& bytes,
        size_t maxBytes=kGpuCheckpointExportMaxBytes,
        std::string* error=nullptr) const;
    std::shared_ptr<CheckpointReadbackBudget> checkpointBudget() const {
        return checkpointBudget_;
    }
    const BatchWorld& cpuBatchWorld() const {return cpu_;}
    bool isGpuEffect(const std::string& effectName) const;
private:
    struct Binding {
        int cpuId = -1;
        size_t gpuIndex = 0;
        bool gpu = false, usesRuntimeParams = false;
        std::array<float,8> defaults{};
        InstanceParameterTable named;
        uint64_t revision=0;
    };
    struct Source {
        std::string effectName;
        std::array<float,8> values{};
        uint64_t bindingRevision=0;
        uint32_t generation=0;
        bool active=false, paused=false, exhausted=false;
        uint64_t token=0;
        bool gpu=false;
        size_t gpuIndex=0;
        bool transformAttached=false;
        fx::float3 position{},lastStepPosition{},direction{0,1,0};
        fx::float4 tint{1,1,1,1};
        PoolContinuousEmission continuous{};
        float accumulatedRate=0, accumulatedDistance=0, elapsed=0;
        std::vector<fx::FxHandle> cpuHandles;
    };
    struct Group {
        std::string key;
        std::unique_ptr<ComputeParticles> particles;
        bool stepOk=false; // never draw stale GPU vertices after a failed step
    };
    bool defineCompiled(const std::string& name,const fx::EffectDef& effect,
                        const GraphRuntimeOptions& options,
                        const InstanceParameterTable& named);
    static std::string groupKey(const fx::EffectDef& effect,
                                const GraphRuntimeOptions& options,
                                uint32_t capacity);
    bool burstImpl(const std::string& effectName,fx::float3 position,
                   fx::float3 direction,fx::float4 tint,
                   const std::array<float,8>* sourceParams,uint64_t sourceToken,
                   fx::FxHandle* cpuHandle);
    void tickContinuous(float dt);
    void recountBindings();
    Source* getSource(PoolSourceId source);
    const Source* getSource(PoolSourceId source) const;

    gfx::Device* device_ = nullptr;
    BatchWorld cpu_;
    PoolWorldBudgets budgets_{};
    PoolWorldStats stats_{};
    std::shared_ptr<CheckpointReadbackBudget> checkpointBudget_;
    std::vector<Group> gpuGroups_;
    PoolResetReport resetReport_{};
    std::vector<uint8_t> resetAccepted_; // per GPU group, retained across retries
    std::map<std::string,Binding> bindings_;
    std::map<std::string,PoolRecoveryEffect> definitions_;
    std::vector<Source> sources_;
    std::vector<uint32_t> freeSourceSlots_;
    uint64_t sourceWorldEpoch_=0, nextBindingRevision_=1;
    uint64_t nextSourceToken_=1;
    // Round-robin dispatch start prevents persistent budget starvation when
    // several GPU groups request checkpoint captures in the same cadence.
    size_t nextGroupStart_ = 0;
    bool initialized_ = false, frameOpen_ = false, stepped_ = false, drawn_ = false;
    std::string reason_;
};

} // namespace aiverse::spectra
