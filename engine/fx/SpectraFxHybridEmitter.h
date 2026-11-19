// SpectraFX opt-in hybrid presentation emitter.
// Selects exactly one owner (CPU ParticleSystem or GPU slot pool) for a batch.
// A backend failure is explicit: no silent dual-simulation or mid-frame
// migration of authoritative state. Call only on the device/render thread.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "SpectraFxExecutionPolicy.h"
#include "SpectraFxGpuParticleSlots.h"
#include "SpectraFxGpuImpactFeedback.h"
#include "SpectraFxGpuBillboardRenderer.h"
#include "SpectraFxGpuSpawnScheduler.h"
#include "ParticleSystem.h"

namespace aiverse::fx {

class SpectraFxHybridEmitter {
public:
    struct Config {
        SpectraFxBackendRequest backend;
        std::string cpuEffect;
        // Optional authored CPU sub-effect on visual GPU ground impact.
        // Requires environment.reportImpacts and is rate-capped by playback.
        std::string impactEffect;
        SpectraFxGpuBillboardRenderer::Appearance appearance{};
        SpectraFxGpuEnvironment environment{};
        SpectraFxGpuSpawnSettings spawning{};
        FxQuality quality=FxQuality::High;
        bool autoSpawnGpu=true;
    };

    // Begin an emitter with no GPU particles yet. GPU emits are explicit
    // on safe buffer update points; CPU fallback plays an authored effect.
    bool begin(gfx::Device& device,ParticleSystem& cpu,Config config,
               float3 origin={0,0,0},float3 direction={0,1,0},
               SpectraFxGpuBillboardRenderer* sharedRenderer=nullptr) {
        stop();
        // Configure before allocating GPU resources: failed settings do not
        // replace or partially initialize any backend.
        if(!scheduler_.configure(config.spawning)) {
            status_="Invalid SpectraFX GPU spawn settings";
            return false;
        }
        cpu_=&cpu;
        config_=std::move(config);
        origin_=origin;
        direction_=direction;
        decision_=chooseSpectraFxBackend(device,config_.backend);
        if(!decision_.accepted) {
            status_="requested backend unavailable and CPU fallback disabled";
            cpu_=nullptr;
            return false;
        }
        if(decision_.backend==SpectraFxExecutionBackend::Gpu) {
            if(slots_.init(device,config_.backend.particles) &&
               (sharedRenderer ? sharedRenderer->ready() : renderer_.init(device))) {
                rendererOwner_=sharedRenderer ? sharedRenderer : &renderer_;
                gpuDevice_=&device;
                backend_=SpectraFxExecutionBackend::Gpu;
                status_="GPU emitter initialized; awaiting spawn at buffer-safe point";
                return true;
            }
            slots_.shutdown();
            renderer_.release();
            if(!config_.backend.allowCpuFallback) {
                status_="GPU allocation/shader failed and fallback disabled";
                cpu_=nullptr;
                return false;
            }
        }
        if(config_.cpuEffect.empty()) {
            status_="CPU fallback requires an authored effect name";
            cpu_=nullptr;
            return false;
        }
        cpuHandle_=cpu.play(config_.cpuEffect,origin,direction);
        if(!cpuHandle_.valid()) {
            status_="CPU ParticleSystem rejected effect";
            cpu_=nullptr;
            return false;
        }
        backend_=SpectraFxExecutionBackend::Cpu;
        status_="CPU ParticleSystem selected";
        return true;
    }

    // GPU only: emits into dormant slots, safe update point outside passes.
    // The CPU path has its own authored spawning logic in ParticleSystem.
    SpectraFxGpuParticleHandle emitGpu(const SpectraFxGpuSeed& seed) {
        if(!gpuActive() || suspended_) return {};
        return slots_.emit(seed);
    }
    bool killGpu(SpectraFxGpuParticleHandle handle) {
        return gpuActive() && slots_.kill(handle);
    }

    // GPU only, between beginFrame/endFrame and outside rendering passes.
    // For CPU selection, caller must update the shared ParticleSystem once
    // for the entire scene; no CPU update is performed by this adapter.
    bool updateGpu(float dt) {
        if(!gpuActive() || suspended_) return false;
        if(config_.autoSpawnGpu) {
            const bool scheduled=scheduler_.tick(dt,origin_,direction_,
                spectraFxSpawnQualityScale(config_.quality),
                [this](render::float3 pos,render::float3 vel,float life) {
                    return slots_.emit({pos,vel,0,life}).valid();
                });
            if(!scheduled) {status_="Invalid GPU emission frame";return false;}
        }
        if(slots_.step(dt,config_.environment)) {
            if(config_.environment.reportImpacts && gpuDevice_)
                impactFeedback_.request(*gpuDevice_,slots_);
            return true;
        }
        status_="GPU compute step failed; emitter suspended pending reset";
        suspended_=true;
        return false;
    }

    // Update attachment transform before the next GPU spawn tick.
    void moveOrigin(float3 pos,float3 direction={0,1,0}) {
        origin_=pos;
        direction_=direction;
        if(cpuActive()) cpu_->move(cpuHandle_,pos,direction);
    }
    const SpectraFxGpuSpawnStats& spawnStats() const {return scheduler_.stats();}
    // Delivers only presentation records, on the RHI owner thread. Discarded
    // generations never produce a callback after particle-slot reuse.
    template<class Callback>
    void drainGpuImpacts(Callback&& callback,size_t maxEvents=128) {
        if(gpuActive())
            impactFeedback_.drain(slots_,std::forward<Callback>(callback),maxEvents);
    }
    const SpectraFxGpuImpactFeedback::Stats& impactStats() const {
        return impactFeedback_.stats();
    }

    // GPU only, inside the scene render pass after updateGpu completed.
    bool renderGpu(const render::mat4& viewProj,render::float3 cameraRight,
                   render::float3 cameraUp) {
        return gpuActive() && !suspended_ && rendererOwner_ &&
               rendererOwner_->draw(slots_.evolution(),viewProj,cameraRight,
                                    cameraUp,config_.appearance);
    }

    // Stop emission and destroy owned GPU resources before the RHI device.
    // Does NOT stop or clear unrelated CPU effects.
    void stop(bool killCpu=true) {
        if(cpu_&&cpuHandle_.valid()) {
            if(killCpu) cpu_->kill(cpuHandle_);
            else cpu_->stop(cpuHandle_);
        }
        cpuHandle_={};
        impactFeedback_.reset();
        gpuDevice_=nullptr;
        renderer_.release(); // only owned/local pipelines; shared lives in playback
        rendererOwner_=nullptr;
        slots_.shutdown();
        cpu_=nullptr;
        suspended_=false;
        scheduler_.reset();
        backend_=SpectraFxExecutionBackend::Cpu;
    }

    bool gpuActive() const {
        return cpu_ && backend_==SpectraFxExecutionBackend::Gpu &&
               slots_.evolution().ready();
    }
    bool cpuActive() const {
        return cpu_ && backend_==SpectraFxExecutionBackend::Cpu &&
               cpuHandle_.valid();
    }
    bool suspended() const {return suspended_;}
    SpectraFxExecutionBackend backend() const {return backend_;}
    const SpectraFxBackendDecision& decision() const {return decision_;}
    const Config& config() const {return config_;}
    const std::string& status() const {return status_;}
    uint32_t gpuActiveParticles() const {return slots_.activeCount();}

private:
    ParticleSystem* cpu_=nullptr;
    FxHandle cpuHandle_{};
    Config config_{};
    SpectraFxBackendDecision decision_{};
    SpectraFxExecutionBackend backend_=SpectraFxExecutionBackend::Cpu;
    SpectraFxGpuParticleSlots slots_{};
    SpectraFxGpuBillboardRenderer renderer_{};
    SpectraFxGpuBillboardRenderer* rendererOwner_=nullptr;
    SpectraFxGpuSpawnScheduler scheduler_{};
    SpectraFxGpuImpactFeedback impactFeedback_{};
    gfx::Device* gpuDevice_=nullptr;
    float3 origin_{},direction_{0,1,0};
    std::string status_;
    bool suspended_=false;
};
} // namespace aiverse::fx
