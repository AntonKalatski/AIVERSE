#pragma once
#include "GiMainCpuBridge.h"
#include "GiGpuSmallScene.h"

namespace aiverse::render {
// Render-owner-thread bridge. Publish/update only inside a Device frame and
// outside passes. Never reuse compute inputs or mutate a sampled atlas until
// the real queue fence covering its last use completes.
class GiMainGpuBridge {
public:
    bool initialize(gfx::Device& device,GiMainCpuConfig config) {
        if(device_ || config.samples<8 || config.samples>GiGpuSmallScene::kMaxSamples ||
           !std::isfinite(config.traceDistance) || config.traceDistance<=0 || config.traceDistance>1000)
            return false;
        scheduler_.configure(config.origin,config.spacing,config.nx,config.ny,config.nz);
        if(!gpu_.initialize(device,scheduler_.size()))return false;
        device_=&device;config_=config;return true;
    }
    const GiMainCpuConfig& config() const {return config_;}
    const gfx::Device* device() const {return device_;}
    bool ready() const {return device_ && gpu_.ready();}
    size_t pendingProbes() const {return scheduler_.pending();}
    bool busy() const {return pendingFence_!=0;}
    const char* diagnostic() const {return diagnostic_;}

    bool publishGeometry(std::shared_ptr<const GiChunkedGeometrySnapshot> scene,uint64_t revision) {
        blocked_=true;geometryCurrent_=false;
        if(!ready() || !scene || !revision || revision<=revision_) {
            diagnostic_="GI geometry revision rejected";return false;
        }
        if(!canMutate())return false;
        if(!gpu_.setGeometry(*scene,revision)) {
            diagnostic_="GI GPU geometry capacity or upload rejected";return false;
        }
        scene_=std::move(scene);revision_=revision;geometryCurrent_=true;
        scheduler_.invalidateAll(GiDirtyReason::Geometry);
        blocked_=false;diagnostic_="";return true;
    }
    bool publishLights(std::vector<GiMovingLight> lights) {
        blocked_=true;lightsCurrent_=false;
        if(!canMutate())return false;
        if(!gpu_.setLights(std::move(lights))) {
            diagnostic_="GI GPU light capacity or source rejected";return false;
        }
        lightsCurrent_=true;
        scheduler_.invalidateAll(GiDirtyReason::Lighting);
        blocked_=false;diagnostic_="";return true;
    }
    bool publishEnvironment(GiEnvironmentLight environment) {
        blocked_=true;environmentCurrent_=false;
        if(!canMutate() || !gpu_.setEnvironment(environment))return false;
        environmentCurrent_=true;
        scheduler_.invalidateAll(GiDirtyReason::Environment);
        blocked_=false;diagnostic_="";return true;
    }
    // Poll even when a newer source was deferred. Completion of old tickets
    // never by itself reopens a rejected source publication.
    void poll() {
        if(!device_)return;
        gpu_.collectRetirements();
        if(!pendingFence_)return;
        if(!device_->gpuHealthy()) {
            for(const auto& job:inFlight_)scheduler_.abort(job);
            inFlight_.clear();pendingFence_=0;blocked_=true;
            diagnostic_="GI GPU device lost";return;
        }
        if(device_->gpuCompletedFence()<pendingFence_)return;
        for(const auto& job:inFlight_)scheduler_.complete(job);
        inFlight_.clear();pendingFence_=0;
    }
    bool updateFrame(float3 focus,size_t maxProbes=32) {
        poll();
        if(!ready() || !scene_ || !geometryCurrent_ || !lightsCurrent_ || !environmentCurrent_ || blocked_ || !canMutate())return false;
        if(!scheduler_.pending())return true;
        auto jobs=scheduler_.claim(std::min(maxProbes,GiGpuSmallScene::kMaxJobs),focus);
        if(jobs.empty())return false;
        const uint64_t fence=device_->gpuSubmissionFence();
        bool submitted=false;
        try {submitted=gpu_.dispatch(jobs,uint32_t(config_.samples),config_.traceDistance).submitted;}
        catch(...) {
            for(const auto& job:jobs)scheduler_.abort(job);
            blocked_=true;throw;
        }
        if(!submitted) {
            for(const auto& job:jobs)scheduler_.abort(job);
            blocked_=true;diagnostic_="GI GPU dispatch rejected";return false;
        }
        pendingFence_=fence;inFlight_=std::move(jobs);
        return true;
    }
    bool canPresent() const {
        return ready() && scene_ && geometryCurrent_ && lightsCurrent_ && environmentCurrent_ && !blocked_ && !pendingFence_ &&
            !scheduler_.pending() && device_->gpuHealthy();
    }
    // Called by renderer admission; its token covers every later draw in this
    // GPU frame, including other cameras. The host keeps this owner alive.
    gfx::TextureHandle borrowForFrame() const {
        if(!canPresent())return {};
        const auto fence=device_->gpuSubmissionFence();
        if(!fence)return {};
        sampledFence_=std::max(sampledFence_,fence);
        return gpu_.atlas().texture();
    }
    // Host must detach all renderers and prove full GPU idle first.
    bool shutdownAfterGpuIdle() {
        blocked_=true;
        if(!gpu_.shutdownAfterGpuIdle())return false;
        scene_.reset();inFlight_.clear();scheduler_={};
        pendingFence_=sampledFence_=revision_=0;device_=nullptr;return true;
    }
private:
    bool canMutate() {
        poll();
        if(!ready() || !device_->gpuHealthy() || !device_->gpuSubmissionFence())return false;
        if(pendingFence_ || (sampledFence_ && device_->gpuCompletedFence()<sampledFence_)) {
            diagnostic_="GI waiting for prior GPU users";return false;
        }
        return true;
    }
    GiGpuSmallScene gpu_;
    GiUpdateScheduler scheduler_;
    GiMainCpuConfig config_{};
    gfx::Device* device_=nullptr;
    std::shared_ptr<const GiChunkedGeometrySnapshot> scene_;
    std::vector<GiProbeTicket> inFlight_;
    uint64_t revision_=0,pendingFence_=0;
    mutable uint64_t sampledFence_=0;
    bool blocked_=true,geometryCurrent_=false;
    bool lightsCurrent_=true,environmentCurrent_=true;
    const char* diagnostic_="";
};
}
