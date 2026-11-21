#pragma once
#include "GiMainCpuBridge.h"
#include "GiGpuSmallScene.h"
#include "GiGpuChunkBvhCache.h"
#include "GiGpuPagedBvhArena.h"
#include "GiGpuResidencyPlanner.h"
#include <algorithm>
#include <exception>

namespace aiverse::render {
// Render-owner-thread bridge. Publish/update only inside a Device frame and
// outside passes. Never reuse compute inputs or mutate a sampled atlas until
// the real queue fence covering its last use completes.
//
// Large worlds: only chunks whose bounds a probe ray, its sun/sky shadow ray or
// a point/spot shadow ray (radius <= lightReach) can reach are made resident
// (GiGpuResidencyPlanner). If those still exceed the GPU triangle budget the
// publication is refused; a relevant occluder is never dropped. Resident
// chunks keep stable addresses in the GPU streams (GiGpuPagedBvhArena), so an
// edit uploads only the changed BLAS pages and the small TLAS prefix.
class GiMainGpuBridge {
public:
    static constexpr float kDefaultLightReach=64;
    // lightReach: largest point/spot radius the resident set must cover.
    bool initialize(gfx::Device& device,GiMainCpuConfig config,float lightReach=kDefaultLightReach) {
        if(device_ || config.samples<8 || config.samples>GiGpuSmallScene::kMaxSamples ||
           !std::isfinite(config.traceDistance) || config.traceDistance<=0 || config.traceDistance>1000 ||
           !std::isfinite(lightReach) || lightReach<0)
            return false;
        scheduler_.configure(config.origin,config.spacing,config.nx,config.ny,config.nz);
        if(!gpu_.initialize(device,scheduler_.size()))return false;
        device_=&device;config_=config;lightReach_=lightReach;return true;
    }
    // Same selection the bridge applies to a published scene; hosts use it to
    // decide between this GPU path and the CPU fallback before initialization.
    static GiGpuResidencyPlan planResidency(const GiChunkedGeometrySnapshot& scene,
                                            const GiMainCpuConfig& config,
                                            float lightReach=kDefaultLightReach) {
        return GiGpuResidencyPlanner::plan(scene,config.origin,config.spacing,
            config.nx,config.ny,config.nz,config.traceDistance,lightReach);
    }
    const GiMainCpuConfig& config() const {return config_;}
    const gfx::Device* device() const {return device_;}
    bool ready() const {return device_ && gpu_.ready();}
    size_t pendingProbes() const {return scheduler_.pending();}
    bool busy() const {return pendingFence_!=0;}
    const char* diagnostic() const {return diagnostic_;}

    bool publishGeometry(std::shared_ptr<const GiChunkedGeometrySnapshot> scene,uint64_t revision) {
        return publish(std::move(scene),revision,nullptr);
    }
    // Local edit: only probes whose rays can reach `changed` (trace distance
    // plus the widest published light) are re-traced. Needs a current scene;
    // otherwise this is a full publication.
    bool publishGeometryRegion(std::shared_ptr<const GiChunkedGeometrySnapshot> scene,uint64_t revision,
                               GiBounds changed) {
        const auto finite=[](float3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
        if(!finite(changed.min) || !finite(changed.max) || changed.min.x>changed.max.x ||
           changed.min.y>changed.max.y || changed.min.z>changed.max.z) {
            blocked_=true;geometryCurrent_=false;diagnostic_="GI geometry region rejected";return false;
        }
        if(!scene_ || !geometryCurrent_)return publish(std::move(scene),revision,nullptr);
        return publish(std::move(scene),revision,&changed);
    }
private:
    bool publish(std::shared_ptr<const GiChunkedGeometrySnapshot> scene,uint64_t revision,const GiBounds* region) {
        blocked_=true;geometryCurrent_=false;
        if(!ready() || !scene || !revision || revision<=revision_) {
            diagnostic_="GI geometry revision rejected";return false;
        }
        if(!canMutate())return false;
        try {
            const float reach=std::max(lightReach_,requestedLightReach_);
            auto plan=planResidency(*scene,config_,reach);
            residency_=plan;residency_.selected.reset();
            if(!plan.admitted) {
                diagnostic_="GI GPU resident geometry exceeds the triangle budget";return false;
            }
            if(plan.selected->chunkCount()>GiGpuPagedBvhArena::kMaxChunks) {
                diagnostic_="GI GPU resident chunk count exceeds the page table";return false;
            }
            chunkCache_.refresh(*plan.selected);
            auto pages=pages_.plan(*plan.selected,chunkCache_);
            if(!pages.accepted || !gpu_.setPackedGeometry(pages.packet,revision)) {
                diagnostic_="GI GPU geometry capacity or upload rejected";return false;
            }
            pageStats_=pages.stats;
            pages_.commit(std::move(pages));
            residentReach_=reach;
        } catch(const std::exception&) {
            diagnostic_="GI GPU geometry capacity or upload rejected";return false;
        }
        scene_=std::move(scene);revision_=revision;geometryCurrent_=true;
        if(region)scheduler_.invalidate(*region,GiDirtyReason::Geometry,config_.traceDistance+lightRadius_);
        else scheduler_.invalidateAll(GiDirtyReason::Geometry);
        blocked_=false;diagnostic_="";return true;
    }
public:
    bool publishLights(std::vector<GiMovingLight> lights) {
        blocked_=true;lightsCurrent_=false;
        float reach=0;
        for(const auto& light:lights)
            if(std::isfinite(light.radius))reach=std::max(reach,light.radius);
        requestedLightReach_=reach;
        // A light reaching past the resident set would miss excluded occluders
        // (light leaks). Republish geometry: it then plans with this reach.
        if(scene_ && residency_.excludedRemoteChunks && reach>residentReach_) {
            diagnostic_="GI light radius exceeds the GPU resident geometry reach";return false;
        }
        if(!canMutate())return false;
        if(!gpu_.setLights(std::move(lights))) {
            diagnostic_="GI GPU light capacity or source rejected";return false;
        }
        lightRadius_=reach;lightsCurrent_=true;
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
    // Last geometry selection (selected snapshot dropped) and page churn.
    const GiGpuResidencyPlan& residency() const {return residency_;}
    const GiGpuPagedBvhArena::Stats& pageStats() const {return pageStats_;}
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
        chunkCache_={};pages_=GiGpuPagedBvhArena{};residency_={};pageStats_={};
        residentReach_=requestedLightReach_=lightRadius_=0;
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
    GiGpuChunkBvhCache chunkCache_;
    GiGpuPagedBvhArena pages_;
    GiGpuResidencyPlan residency_{};
    GiGpuPagedBvhArena::Stats pageStats_{};
    float lightReach_=kDefaultLightReach,residentReach_=0,requestedLightReach_=0,lightRadius_=0;
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
