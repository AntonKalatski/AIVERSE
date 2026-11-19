// SpectraFX project GPU/CPU emitter playback, render-thread only.
// Project assets and routes are presentation resources. ECS SimEvent remains
// authoritative for the occurrence of events, never for effect particles.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "SpectraFxGpuEmitterLibrary.h"
#include "SpectraFxCueBridge.h"

namespace aiverse::fx {

class SpectraFxHybridPlayback {
public:
    static constexpr size_t kMaxEmitters=64;
    struct Route {
        std::string asset;
        bool enabled=true;
    };
    struct Stats {
        uint64_t played=0,moved=0,stopped=0,killed=0;
        uint64_t missing=0,refused=0,failed=0;
    };

    SpectraFxHybridPlayback(gfx::Device& device,ParticleSystem& cpu,
                            SpectraFxGpuEmitterLibrary& library)
        : device_(device),cpu_(cpu),library_(library) {}

    // Explicit teardown before the RHI device/ParticleSystem is destroyed.
    ~SpectraFxHybridPlayback()=default;

    void route(uint32_t cueId,Route value) {routes_[cueId]=std::move(value);}
    void unroute(uint32_t cueId) {routes_.erase(cueId);}
    void clearRoutes() {routes_.clear();}

    bool dispatch(const SpectraFxCue& cue) {
        if(cue.key==0) {
            // One-shots need their own autonomous retirement policy. Refuse
            // them rather than leaking GPU resources or arbitrary CPU handles.
            ++stats_.refused;
            return false;
        }
        if(cue.action!=SpectraFxCue::Action::Play) {
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
        auto route=routes_.find(cue.id);
        if(route==routes_.end() || !route->second.enabled ||
           !SpectraFxGpuEmitterLibrary::validName(route->second.asset)) {
            ++stats_.missing;return false;
        }
        const auto load=library_.load(route->second.asset);
        const auto* entry=library_.find(route->second.asset);
        if(!load.valid || !entry) {++stats_.failed;return false;}
        // Construct replacement off-map; if it fails, preserve currently
        // playing instance and its existing per-instance resources.
        auto next=std::make_unique<SpectraFxHybridEmitter>();
        if(!next->begin(device_,cpu_,entry->config,cue.position,cue.direction)) {
            ++stats_.failed;return false;
        }
        auto old=active_.find(cue.key);
        if(old==active_.end() && active_.size()>=kMaxEmitters) {
            next->stop();
            ++stats_.refused;
            return false;
        }
        if(old!=active_.end()) {
            old->second->stop();
            old->second=std::move(next);
        } else active_.emplace(cue.key,std::move(next));
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
        for(auto& pair:active_)
            if(pair.second->gpuActive() && !pair.second->suspended() &&
               !pair.second->updateGpu(dt)) ++stats_.failed;
    }

    // Call during a compatible open render pass after the compute stage.
    // This submits only GPU-owned emitters, never duplicates CPU draws.
    void renderGpu(const render::mat4& viewProj,render::float3 cameraRight,
                   render::float3 cameraUp) {
        for(auto& pair:active_)
            if(pair.second->gpuActive() && !pair.second->suspended() &&
               !pair.second->renderGpu(viewProj,cameraRight,cameraUp))
                ++stats_.failed;
    }

    // Rollback/scene unload: prevents stale generation-safe keys from
    // retaining resources across authoritative state discontinuities.
    void reset() {
        for(auto& pair:active_) pair.second->stop();
        active_.clear();
    }

    size_t attached() const {return active_.size();}
    const Stats& stats() const {return stats_;}
    void resetStats() {stats_={};}

private:
    gfx::Device& device_;
    ParticleSystem& cpu_;
    SpectraFxGpuEmitterLibrary& library_;
    std::unordered_map<uint32_t,Route> routes_;
    std::unordered_map<uint64_t,std::unique_ptr<SpectraFxHybridEmitter>> active_;
    Stats stats_{};
};

} // namespace aiverse::fx
