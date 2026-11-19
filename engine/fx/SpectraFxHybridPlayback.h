// SpectraFX project GPU/CPU emitter playback, render-thread only.
// Project assets and routes are presentation resources. ECS SimEvent remains
// authoritative for the occurrence of events, never for effect particles.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <memory>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "SpectraFxGpuEmitterLibrary.h"
#include "SpectraFxGpuSharedArena.h"
#include "SpectraFxEmitterEvents.h"
#include "SpectraFxCueBridge.h"

namespace aiverse::fx {

class SpectraFxHybridPlayback {
public:
    static constexpr size_t kMaxEmitters=64;
    static constexpr size_t kMaxGpuPools=8;
    static constexpr uint32_t kMaxTotalPooledParticles=65536;
    struct Route {
        std::string asset;
        bool enabled=true;
        // One-shot presentation ownership, seconds (hard retire and release).
        // The playing emitter may spawn particles until this deadline.
        float oneShotSeconds=3.0f;
        // Opt-in homogeneous GPU arena across routes; empty => existing
        // dedicated hybrid emitter behavior. No change to CPU fallback.
        std::string sharedPool;
        uint32_t sharedPoolCapacity=8192;
    };
    struct Stats {
        uint64_t played=0,moved=0,stopped=0,killed=0;
        uint64_t missing=0,refused=0,failed=0,oneShots=0,retired=0;
        uint64_t gpuImpacts=0,impactDropped=0,impactEffects=0;
        uint64_t pooledPlays=0,pooledFallbacks=0,pooledRetired=0,pooledFailures=0;
    };

    SpectraFxHybridPlayback(gfx::Device& device,ParticleSystem& cpu,
                            SpectraFxGpuEmitterLibrary& library)
        : device_(device),cpu_(cpu),library_(library) {}

    // Destruction should happen before the RHI Device and CPU ParticleSystem.
    ~SpectraFxHybridPlayback() {
        reset();
        sharedRenderer_.release();
    }

    void route(uint32_t cueId,Route value) {routes_[cueId]=std::move(value);}
    void unroute(uint32_t cueId) {routes_.erase(cueId);}
    void clearRoutes() {routes_.clear();}

    bool dispatch(const SpectraFxCue& cue) {
        if(cue.action!=SpectraFxCue::Action::Play) {
            if(!cue.key) {++stats_.refused;return false;}
            auto pooled=pooledActive_.find(cue.key);
            if(pooled!=pooledActive_.end()) {
                auto* arena=poolArena(pooled->second.name);
                if(!arena) {++stats_.failed;return false;}
                if(cue.action==SpectraFxCue::Action::Move) {
                    if(!arena->moveEmitter(pooled->second.ticket,
                                           cue.position,cue.direction)) {
                        ++stats_.failed;return false;
                    }
                    ++stats_.moved;return true;
                }
                if(!arena->removeEmitter(pooled->second.ticket,
                                         cue.action==SpectraFxCue::Action::Kill)) {
                    ++stats_.failed;return false;
                }
                pooledActive_.erase(pooled);
                if(cue.action==SpectraFxCue::Action::Kill) ++stats_.killed;
                else ++stats_.stopped;
                return true;
            }
            auto it=active_.find(cue.key);
            if(it==active_.end()) {++stats_.missing;return false;}
            if(cue.action==SpectraFxCue::Action::Move) {
                it->second->moveOrigin(cue.position,cue.direction);
                ++stats_.moved;return true;
            }
            it->second->stop(cue.action==SpectraFxCue::Action::Kill);
            active_.erase(it);
            if(cue.action==SpectraFxCue::Action::Kill) ++stats_.killed;
            else ++stats_.stopped;
            return true;
        }
        const auto route=routes_.find(cue.id);
        if(route==routes_.end() || !route->second.enabled ||
           !SpectraFxGpuEmitterLibrary::validName(route->second.asset)) {
            ++stats_.missing;return false;
        }
        const bool oneShot=cue.key==0;
        if(oneShot && (!std::isfinite(route->second.oneShotSeconds) ||
                       route->second.oneShotSeconds<=0 ||
                       route->second.oneShotSeconds>60)) {
            ++stats_.refused;return false;
        }
        if(active_.size()+oneShots_.size()+pooledActive_.size()+
           pooledShots_.size()>=kMaxEmitters &&
           (oneShot || (active_.find(cue.key)==active_.end() &&
                        pooledActive_.find(cue.key)==pooledActive_.end()))) {
            ++stats_.refused;return false;
        }
        const auto load=library_.load(route->second.asset);
        const auto* entry=library_.find(route->second.asset);
        if(!load.valid || !entry) {++stats_.failed;return false;}
        // Shared pools are explicit project opt-ins. Unsupported devices,
        // heterogeneous settings, or an exhausted pool use the existing
        // per-emitter HybridEmitter path with the authored CPU fallback.
        if(!route->second.sharedPool.empty()) {
            PooledRef pooled;
            if(tryStartPooled(route->second,entry->config,cue,pooled)) {
                if(!oneShot && !retireKey(cue.key)) {
                    auto* arena=poolArena(pooled.name);
                    if(arena) arena->removeEmitter(pooled.ticket,true);
                    ++stats_.failed;
                    return false;
                }
                if(oneShot) {
                    pooledShots_.push_back({std::move(pooled),0,
                                            route->second.oneShotSeconds});
                    ++stats_.oneShots;
                } else pooledActive_[cue.key]=std::move(pooled);
                ++stats_.played;
                ++stats_.pooledPlays;
                return true;
            }
            ++stats_.pooledFallbacks;
        }
        // All GPU emitter batches share one static quad, MSL/GLSL shader,
        // and alpha/additive render pipelines; only particle state is private.
        const auto preference=chooseSpectraFxBackend(device_,entry->config.backend);
        const bool gpuRequested=preference.accepted &&
                                preference.backend==SpectraFxExecutionBackend::Gpu;
        if(gpuRequested && !sharedRendererAttempted_) {
            sharedRendererAttempted_=true;
            sharedRenderer_.init(device_);
        }
        auto next=std::make_unique<SpectraFxHybridEmitter>();
        if(!next->begin(device_,cpu_,entry->config,cue.position,cue.direction,
                        gpuRequested ? &sharedRenderer_ : nullptr)) {
            ++stats_.failed;return false;
        }
        if(oneShot) {
            oneShots_.push_back({std::move(next),0,route->second.oneShotSeconds});
            ++stats_.oneShots;
        } else {
            if(!retireKey(cue.key)) {
                next->stop();
                ++stats_.failed;
                return false;
            }
            auto it=active_.find(cue.key);
            if(it!=active_.end()) {
                it->second->stop();
                it->second=std::move(next);
            } else active_.emplace(cue.key,std::move(next));
        }
        ++stats_.played;
        return true;
    }

    template<class Range>
    void dispatchAll(const Range& cues) {
        for(const auto& cue:cues) dispatch(cue);
    }

    // Call inside RHI beginFrame/endFrame, outside all render passes.
    // The host separately updates the shared CPU ParticleSystem ONCE/frame.
    void updateGpu(float dt) {
        if(!std::isfinite(dt)||dt<0||dt>0.1f) {++stats_.failed;return;}
        for(auto& pair:active_)
            advanceGpu(*pair.second,dt);
        for(size_t i=0;i<oneShots_.size();) {
            auto& shot=oneShots_[i];
            // Retire exactly once; GPU and CPU resources are bounded by the
            // authored deadline, independent of whether particles remain.
            if(shot.elapsed+dt>=shot.duration) {
                shot.emitter->stop();
                oneShots_.erase(oneShots_.begin()+std::ptrdiff_t(i));
                ++stats_.retired;
                continue;
            }
            advanceGpu(*shot.emitter,dt);
            shot.elapsed+=dt;
            ++i;
        }
    }

    // Camera-aware update used by native clients. The no-camera overload
    // remains for standalone hosts and preserves LOD settings from last view.
    void updateGpu(float dt,render::float3 cameraEye) {
        for(auto& pair:active_) pair.second->updateLod(cameraEye);
        for(auto& shot:oneShots_) shot.emitter->updateLod(cameraEye);
        updateGpu(dt);
    }

    // Call during a compatible open render pass after the compute stage.
    // This submits only GPU-owned emitters, never duplicates CPU draws.
    void renderGpu(const render::mat4& viewProj,render::float3 cameraRight,
                   render::float3 cameraUp) {
        for(auto& pair:active_)
            drawGpu(*pair.second,viewProj,cameraRight,cameraUp);
        for(auto& shot:oneShots_)
            drawGpu(*shot.emitter,viewProj,cameraRight,cameraUp);
    }

    // Called on the GPU owner/presentation thread after async readback
    // callbacks have been delivered (typically a subsequent beginFrame).
    // Emits no gameplay events and never modifies Simulation or ECS.
    void publishGpuImpacts(SpectraFxEmitterEventRouter& router,
                           size_t maxEvents=128) {
        size_t remaining=maxEvents;
        auto forward=[&](uint64_t key,SpectraFxHybridEmitter& emitter) {
            if(!remaining) return;
            emitter.drainGpuImpacts([&](const SpectraFxGpuImpact& impact) {
                SpectraFxEmitterEventRecord record;
                record.emitterKey=key;
                record.type=SpectraFxEmitterEvent::Impact;
                record.marker=impact.particle.index;
                record.position=impact.position;
                if(router.publish(record)) ++stats_.gpuImpacts;
                else ++stats_.impactDropped;
                --remaining;
            },remaining);
        };
        for(auto& pair:active_) forward(pair.first,*pair.second);
        for(auto& oneShot:oneShots_) forward(0,*oneShot.emitter);
    }

    // Optional authored on-impact CPU sub-effects. Call once after
    // ParticleSystem::setView and before ParticleSystem::update, on the
    // presentation thread. Effects are bounded and NEVER gameplay hits.
    void playGpuImpactEffects(size_t maxEffects=32) {
        size_t remaining=maxEffects;
        auto emit=[&](SpectraFxHybridEmitter& emitter) {
            const auto& name=emitter.config().impactEffect;
            if(name.empty() || !remaining) return;
            emitter.drainGpuImpacts([&](const SpectraFxGpuImpact& impact) {
                const auto h=cpu_.play(name,impact.position);
                if(h.valid()) ++stats_.impactEffects;
                else ++stats_.impactDropped;
                --remaining;
            },remaining);
        };
        for(auto& pair:active_) emit(*pair.second);
        for(auto& shot:oneShots_) emit(*shot.emitter);
    }

    // Rollback/scene unload: prevents stale generation-safe keys from
    // retaining resources across authoritative state discontinuities.
    void reset() {
        for(auto& pair:active_) pair.second->stop();
        active_.clear();
        for(auto& shot:oneShots_) shot.emitter->stop();
        oneShots_.clear();
    }

    size_t attached() const {return active_.size();}
    size_t oneShotCount() const {return oneShots_.size();}
    const Stats& stats() const {return stats_;}
    void resetStats() {stats_={};}

private:
    struct OneShot {
        std::unique_ptr<SpectraFxHybridEmitter> emitter;
        float elapsed=0,duration=3;
    };
    void advanceGpu(SpectraFxHybridEmitter& emitter,float dt) {
        if(emitter.gpuActive()&&!emitter.suspended()&&!emitter.updateGpu(dt))
            ++stats_.failed;
    }
    void drawGpu(SpectraFxHybridEmitter& emitter,
                 const render::mat4& viewProj,render::float3 right,
                 render::float3 up) {
        if(emitter.gpuActive()&&!emitter.suspended()&&
           !emitter.renderGpu(viewProj,right,up)) ++stats_.failed;
    }
    gfx::Device& device_;
    ParticleSystem& cpu_;
    SpectraFxGpuEmitterLibrary& library_;
    SpectraFxGpuBillboardRenderer sharedRenderer_{};
    bool sharedRendererAttempted_=false;
    std::unordered_map<uint32_t,Route> routes_;
    std::unordered_map<uint64_t,std::unique_ptr<SpectraFxHybridEmitter>> active_;
    std::vector<OneShot> oneShots_;
    Stats stats_{};
};

} // namespace aiverse::fx
