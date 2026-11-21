#pragma once
#include "GiMainCpuBridge.h"
#include "GiGpuSmallScene.h"
#include "GiGpuChunkBvhCache.h"
#include "GiGpuPagedBvhArena.h"
#include "GiGpuResidencyPlanner.h"
#include <algorithm>
#include <array>
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
//
// Cascades (config.cascades 1..3): nested lattices around one center, cascade
// k with spacing * 2^k and rays traceDistance * 2^k long. All of them live in
// one atlas (probe offsets); each frame dispatches one cascade (finest first)
// so the one job buffer is never rewritten while the GPU still reads it.
//
// Scrolling (config.scroll): each cascade follows the updateFrame focus in
// whole cells (one cell of dead band). Probes keep their world cell through a
// toroidal ring offset (GiUpdateScheduler::scroll, U6 ring.xyz); cells that
// newly enter a cascade are zeroed on the GPU (GiGpuSmallScene::clear) and
// re-traced, so they show no indirect light until traced, never stale light
// from another place. The resident geometry is planned for the outer cascade
// widened by half its size on each side and re-planned when the cascade leaves
// that box. After the first full convergence the atlas stays presented while
// probes update.
class GiMainGpuBridge {
public:
    static constexpr float kDefaultLightReach=64;
    static constexpr int kMaxCascades=3;
    // Lattice of cascade `level` for a base configuration.
    static GiMainCpuConfig cascadeConfig(const GiMainCpuConfig& base,int level) {
        GiMainCpuConfig out=base;
        const float scale=float(1<<level);
        const float3 extent{base.spacing*float(base.nx),base.spacing*float(base.ny),base.spacing*float(base.nz)};
        const float3 center=base.origin+extent*0.5f;
        out.spacing=base.spacing*scale;
        out.traceDistance=base.traceDistance*scale;
        out.origin=center-extent*(0.5f*scale);
        out.cascades=1;
        return out;
    }
    // lightReach: largest point/spot radius the resident set must cover.
    bool initialize(gfx::Device& device,GiMainCpuConfig config,float lightReach=kDefaultLightReach) {
        if(device_ || config.samples<8 || config.samples>GiGpuSmallScene::kMaxSamples ||
           config.cascades<1 || config.cascades>kMaxCascades ||
           !std::isfinite(config.traceDistance) || config.traceDistance<=0 ||
           config.traceDistance*float(1<<(config.cascades-1))>1000 ||
           !std::isfinite(lightReach) || lightReach<0)
            return false;
        size_t total=0;
        try {
            for(int k=0;k<config.cascades;++k) {
                const auto c=cascadeConfig(config,k);
                levels_[size_t(k)].configure(c.origin,c.spacing,c.nx,c.ny,c.nz);
                offsets_[size_t(k)]=uint32_t(total);
                total+=levels_[size_t(k)].size();
            }
        } catch(const std::invalid_argument&) {levels_={};return false;}
        if(!gpu_.initialize(device,total)) {levels_={};return false;}
        levelCount_=config.cascades;converged_=false;scrolls_=recycled_=replans_=0;
        device_=&device;config_=config;lightReach_=lightReach;return true;
    }
    int cascadeCount() const {return levelCount_;}
    // Current lattice of a cascade (its origin moves when scrolling).
    GiMainCpuConfig cascade(int level) const {
        auto c=cascadeConfig(config_,level);
        if(config_.scroll && levels_.at(size_t(level)).configured())c.origin=levels_[size_t(level)].origin();
        return c;
    }
    // Toroidal slot offset of a cascade per axis (U6 ring.xyz).
    std::array<int,3> cascadeRing(int level) const {
        const auto& l=levels_.at(size_t(level));
        return {l.ring(0),l.ring(1),l.ring(2)};
    }
    struct ScrollStats {size_t scrolls=0,recycledProbes=0,residencyReplans=0;};
    ScrollStats scrollStats() const {return {scrolls_,recycled_,replans_};}
    uint32_t cascadeProbeOffset(int level) const {return offsets_.at(size_t(level));}
    // Same selection the bridge applies to a published scene; hosts use it to
    // decide between this GPU path and the CPU fallback before initialization.
    static GiGpuResidencyPlan planResidency(const GiChunkedGeometrySnapshot& scene,
                                            const GiMainCpuConfig& config,
                                            float lightReach=kDefaultLightReach) {
        // The outermost cascade covers every inner one and has the longest rays.
        return planLattice(scene,cascadeConfig(config,std::clamp(config.cascades,1,kMaxCascades)-1),
                           config.scroll,lightReach);
    }
    // A scrolling volume plans for the lattice widened by half its size per side.
    static GiGpuResidencyPlan planLattice(const GiChunkedGeometrySnapshot& scene,GiMainCpuConfig outer,
                                          bool scroll,float lightReach) {
        if(scroll) {
            const int mx=(outer.nx+1)/2,my=(outer.ny+1)/2,mz=(outer.nz+1)/2;
            outer.origin=outer.origin-float3{float(mx),float(my),float(mz)}*outer.spacing;
            outer.nx+=2*mx;outer.ny+=2*my;outer.nz+=2*mz;
        }
        return GiGpuResidencyPlanner::plan(scene,outer.origin,outer.spacing,
            outer.nx,outer.ny,outer.nz,outer.traceDistance,lightReach);
    }
    const GiMainCpuConfig& config() const {return config_;}
    const gfx::Device* device() const {return device_;}
    bool ready() const {return device_ && gpu_.ready();}
    size_t pendingProbes() const {
        size_t n=0;
        for(int k=0;k<levelCount_;++k)n+=levels_[size_t(k)].pending();
        return n;
    }
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
            if(!uploadResident(*scene))return false;
        } catch(const std::exception&) {
            diagnostic_="GI GPU geometry capacity or upload rejected";return false;
        }
        scene_=std::move(scene);revision_=revision;geometryCurrent_=true;
        for(int k=0;k<levelCount_;++k) {
            if(region)levels_[size_t(k)].invalidate(*region,GiDirtyReason::Geometry,
                                                    cascade(k).traceDistance+lightRadius_);
            else levels_[size_t(k)].invalidateAll(GiDirtyReason::Geometry);
        }
        blocked_=false;diagnostic_="";return true;
    }
    // Plans and uploads the resident set for the current outer lattice.
    bool uploadResident(const GiChunkedGeometrySnapshot& scene) {
        // GPU geometry revisions count uploads (a re-plan reuses the scene revision).
        const uint64_t revision=gpuRevision_+1;
        const float reach=std::max(lightReach_,requestedLightReach_);
        const auto outer=cascade(levelCount_-1);
        auto plan=planLattice(scene,outer,config_.scroll,reach);
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
        residentReach_=reach;gpuRevision_=revision;
        // Box of probe positions the plan covers (scrolling re-plans past it).
        const int mx=config_.scroll?(outer.nx+1)/2:0,my=config_.scroll?(outer.ny+1)/2:0,
                  mz=config_.scroll?(outer.nz+1)/2:0;
        plannedLo_=outer.origin-float3{float(mx),float(my),float(mz)}*outer.spacing;
        plannedHi_=plannedLo_+float3{float(outer.nx+2*mx),float(outer.ny+2*my),float(outer.nz+2*mz)}*outer.spacing;
        return true;
    }
    // Scrolls every cascade toward `focus` and zeroes the recycled probes.
    // Only called when no GPU work reads or writes the atlas (canMutate).
    bool recenter(float3 focus) {
        std::vector<uint32_t> cleared;
        for(int k=0;k<levelCount_;++k) {
            const auto c=cascade(k);
            const float3 extent{c.spacing*float(c.nx),c.spacing*float(c.ny),c.spacing*float(c.nz)};
            const float3 f=(focus-extent*0.5f-c.origin)*(1.0f/c.spacing);
            // Truncation toward zero: one cell of dead band, no flicker at a cell edge.
            const auto cells=[](float v){return std::isfinite(v)?int(std::clamp(v,-1.0e6f,1.0e6f)):0;};
            const int dx=cells(f.x),dy=cells(f.y),dz=cells(f.z);
            if(!dx && !dy && !dz)continue;
            const auto ids=levels_[size_t(k)].scroll(dx,dy,dz,GiDirtyReason::Geometry);
            for(const auto id:ids)cleared.push_back(id+offsets_[size_t(k)]);
            ++scrolls_;
        }
        if(cleared.empty())return true;
        recycled_+=cleared.size();
        const auto outer=cascade(levelCount_-1);
        const float3 hi=outer.origin+float3{float(outer.nx),float(outer.ny),float(outer.nz)}*outer.spacing;
        if(outer.origin.x<plannedLo_.x || outer.origin.y<plannedLo_.y || outer.origin.z<plannedLo_.z ||
           hi.x>plannedHi_.x || hi.y>plannedHi_.y || hi.z>plannedHi_.z) {
            // Probes still inside the old box were traced with every chunk they
            // reach; only the recycled ones need the new resident set.
            bool ok=false;
            try {ok=uploadResident(*scene_);} catch(const std::exception&) {}
            if(!ok) {
                blocked_=true;geometryCurrent_=false;
                if(!*diagnostic_)diagnostic_="GI GPU geometry capacity or upload rejected";
                return false;
            }
            ++replans_;
        }
        const uint64_t fence=device_->gpuSubmissionFence();
        if(!gpu_.clear(cleared).submitted) {
            blocked_=true;diagnostic_="GI GPU probe clear rejected";return false;
        }
        pendingFence_=fence;
        return true;
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
        invalidateAll(GiDirtyReason::Lighting);
        blocked_=false;diagnostic_="";return true;
    }
    bool publishEnvironment(GiEnvironmentLight environment) {
        blocked_=true;environmentCurrent_=false;
        if(!canMutate() || !gpu_.setEnvironment(environment))return false;
        environmentCurrent_=true;
        invalidateAll(GiDirtyReason::Environment);
        blocked_=false;diagnostic_="";return true;
    }
    // Poll even when a newer source was deferred. Completion of old tickets
    // never by itself reopens a rejected source publication.
    void poll() {
        if(!device_)return;
        gpu_.collectRetirements();
        if(!pendingFence_)return;
        if(!device_->gpuHealthy()) {
            for(const auto& job:inFlight_)levels_[inFlightLevel_].abort(job);
            inFlight_.clear();pendingFence_=0;blocked_=true;
            diagnostic_="GI GPU device lost";return;
        }
        if(device_->gpuCompletedFence()<pendingFence_)return;
        for(const auto& job:inFlight_)levels_[inFlightLevel_].complete(job);
        inFlight_.clear();pendingFence_=0;
        if(!pendingProbes())converged_=true;
    }
    bool updateFrame(float3 focus,size_t maxProbes=32) {
        poll();
        if(!ready() || !scene_ || !geometryCurrent_ || !lightsCurrent_ || !environmentCurrent_ || blocked_ || !canMutate())return false;
        if(config_.scroll && !recenter(focus))return false;
        int level=-1;
        for(int k=0;k<levelCount_;++k)if(levels_[size_t(k)].pending()){level=k;break;}
        if(level<0)return true;
        // A clear submitted above shares this frame's fence and job-free
        // completion; the trace uses its own job buffer.
        auto& scheduler=levels_[size_t(level)];
        auto jobs=scheduler.claim(std::min(maxProbes,GiGpuSmallScene::kMaxJobs),focus);
        if(jobs.empty())return false;
        auto atlasJobs=jobs;
        for(auto& job:atlasJobs)job.index+=offsets_[size_t(level)];
        const uint64_t fence=device_->gpuSubmissionFence();
        bool submitted=false;
        try {submitted=gpu_.dispatch(atlasJobs,uint32_t(config_.samples),cascade(level).traceDistance).submitted;}
        catch(...) {
            for(const auto& job:jobs)scheduler.abort(job);
            blocked_=true;throw;
        }
        if(!submitted) {
            for(const auto& job:jobs)scheduler.abort(job);
            blocked_=true;diagnostic_="GI GPU dispatch rejected";return false;
        }
        pendingFence_=fence;inFlight_=std::move(jobs);inFlightLevel_=size_t(level);
        return true;
    }
    // Last geometry selection (selected snapshot dropped) and page churn.
    const GiGpuResidencyPlan& residency() const {return residency_;}
    const GiGpuPagedBvhArena::Stats& pageStats() const {return pageStats_;}
    bool canPresent() const {
        if(!(ready() && scene_ && geometryCurrent_ && lightsCurrent_ && environmentCurrent_ && !blocked_ &&
             device_->gpuHealthy()))
            return false;
        // Scrolling: once converged, recycled probes are zero and the others
        // hold light of their own cell, so updates never hide the GI.
        if(config_.scroll && converged_)return true;
        return !pendingFence_ && !pendingProbes();
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
        scene_.reset();inFlight_.clear();levels_={};levelCount_=1;offsets_={};inFlightLevel_=0;
        chunkCache_={};pages_=GiGpuPagedBvhArena{};residency_={};pageStats_={};
        residentReach_=requestedLightReach_=lightRadius_=0;converged_=false;
        scrolls_=recycled_=replans_=0;plannedLo_=plannedHi_={};
        pendingFence_=sampledFence_=revision_=gpuRevision_=0;device_=nullptr;return true;
    }
private:
    void invalidateAll(GiDirtyReason reason) {
        for(int k=0;k<levelCount_;++k)levels_[size_t(k)].invalidateAll(reason);
    }
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
    std::array<GiUpdateScheduler,kMaxCascades> levels_{};
    std::array<uint32_t,kMaxCascades> offsets_{};
    int levelCount_=1;
    size_t inFlightLevel_=0;
    GiMainCpuConfig config_{};
    gfx::Device* device_=nullptr;
    std::shared_ptr<const GiChunkedGeometrySnapshot> scene_;
    std::vector<GiProbeTicket> inFlight_;
    uint64_t revision_=0,gpuRevision_=0,pendingFence_=0;
    mutable uint64_t sampledFence_=0;
    bool blocked_=true,geometryCurrent_=false,converged_=false;
    size_t scrolls_=0,recycled_=0,replans_=0;
    float3 plannedLo_{},plannedHi_{};
    bool lightsCurrent_=true,environmentCurrent_=true;
    const char* diagnostic_="";
};
}
