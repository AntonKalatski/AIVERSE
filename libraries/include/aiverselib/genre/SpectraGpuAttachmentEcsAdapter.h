// SpectraFX Stage 35 — persistent, genre-independent read-only ECS bindings
// for the pooled GPU/CPU visual presenter. Never place PoolSourceId, Metal
// handles, particle state, or render timing in authoritative ECS World.
#pragma once

#include "EcsVfxPresentation.h"
#include "engine/fx/SpectraGpuPoolWorld.h"
#include <cstdint>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::spectra {

// Host owns this adapter AND the GpuPoolWorld. The pool must outlive it.
// Use an isolated rollback scope: resetForRollback resets the WHOLE pool.
// sync() is called AFTER pool.beginPresentationFrame(), BEFORE pool.step().
// World reads are const; each Entity::packed() includes its generation.
class GpuEcsAttachmentPresenter final {
public:
    using ResolvePose = lib::genre::SpectraPresentationSystem::ResolvePose;

    explicit GpuEcsAttachmentPresenter(GpuPoolWorld& pool) : pool_(pool) {}
    GpuEcsAttachmentPresenter(const GpuEcsAttachmentPresenter&) = delete;
    GpuEcsAttachmentPresenter& operator=(const GpuEcsAttachmentPresenter&) = delete;

    // Pooled continuous effects MUST be explicitly configured by native game
    // presentation code. No genre-specific ECS fields or implicit emitters.
    bool bindEffect(std::string effect,PoolContinuousEmission emission) {
        if(effect.empty() || !std::isfinite(emission.burstsPerSecond) ||
           !std::isfinite(emission.metresPerBurst) ||
           !std::isfinite(emission.lifetimeSeconds) ||
           emission.burstsPerSecond<0.f ||
           emission.burstsPerSecond>120.f ||
           emission.metresPerBurst<0.f || emission.metresPerBurst>1.e5f ||
           emission.lifetimeSeconds<0.f ||
           emission.lifetimeSeconds>86400.f ||
           emission.maxBurstsPerFrame==0 ||
           emission.maxBurstsPerFrame>64u)return false;
        const uint64_t revision=nextPolicyRevision_++;
        bindings_[std::move(effect)]={emission,revision};
        return true;
    }

    // A temporary pose failure should NOT silently kill existing effects.
    // The affected source is paused and can resume when its pose returns.
    // Disappearing/disabled components are detached using killOnDetach.
    bool sync(const World& world,const ResolvePose& resolver) {
        reason_.clear();
        if(!pool_.ready() || !resolver) {
            reason_="SpectraFX ECS attachments need initialized pool and pose resolver";
            return false;
        }
        struct Wanted {
            std::string effect;
            fx::float3 position{},direction{0,1,0};
            uint32_t revision=0;
            int64_t scaleRaw=0;
            bool killOnDetach=false, hasPose=false;
        };
        std::map<uint64_t,Wanted> desired;
        world.eachExistingConst<lib::genre::SpectraSourceComponent>(
            [&](Entity entity,const lib::genre::SpectraSourceComponent& component) {
                if(!world.alive(entity) || !component.enabled ||
                   component.effect.empty())return;
                Wanted w;
                w.effect=component.effect;
                w.revision=component.revision;
                w.scaleRaw=component.scale.raw;
                w.killOnDetach=component.killOnDetach;
                // Keep an entry even if rendering pose is temporarily missing.
                w.hasPose=resolver(world,entity,w.position,w.direction);
                desired[entity.packed()]=std::move(w);
            });

        // Release stale bindings first, freeing their bounded source slots for
        // new entities. On failure retain tracking to permit retry next frame.
        for(auto it=attached_.begin();it!=attached_.end();) {
            auto want=desired.find(it->first);
            const auto policy=want!=desired.end()?bindings_.find(want->second.effect):bindings_.end();
            const bool equivalent=want!=desired.end() &&
                policy!=bindings_.end() &&
                want->second.effect==it->second.effect &&
                want->second.revision==it->second.revision &&
                want->second.scaleRaw==it->second.scaleRaw &&
                policy->second.revision==it->second.policyRevision;
            if(equivalent) {++it;continue;}
            // An obsolete source must not be allowed to generate future
            // particles, even if the next source cannot yet be created.
            if(!pool_.stopSource(it->second.handle,it->second.killOnDetach) ||
               !pool_.retireSource(it->second.handle)) {
                reason_="SpectraFX ECS attachment detach failed: "+pool_.reason();
                return false;
            }
            it=attached_.erase(it);
        }

        for(const auto& [key,want]:desired) {
            auto policy=bindings_.find(want.effect);
            if(policy==bindings_.end()) {
                reason_="SpectraFX ECS effect has no bound continuous policy: "+want.effect;
                return false;
            }
            // The old CPU SourceComponent supports scale, but the strict pooled
            // GPU billboard subset does not. Refuse silent size/velocity parity
            // loss rather than ignoring a non-unit authored multiplier.
            if(want.scaleRaw!=(1_fx).raw) {
                reason_="SpectraFX pooled ECS attachment requires unit scale: "+want.effect;
                return false;
            }
            auto it=attached_.find(key);
            if(it==attached_.end()) {
                const PoolSourceId handle=pool_.createSource(want.effect);
                if(!handle.valid()) {
                    reason_="SpectraFX ECS source admission: "+pool_.reason();
                    return false;
                }
                if(!pool_.configureContinuous(handle,policy->second.emission)) {
                    (void)pool_.retireSource(handle);
                    reason_="SpectraFX ECS continuous policy rejected: "+want.effect;
                    return false;
                }
                Attached a;
                a.handle=handle;
                a.effect=want.effect;
                a.revision=want.revision;
                a.scaleRaw=want.scaleRaw;
                a.policyRevision=policy->second.revision;
                a.killOnDetach=want.killOnDetach;
                it=attached_.emplace(key,std::move(a)).first;
            }
            it->second.killOnDetach=want.killOnDetach;
            if(!want.hasPose) {
                // Do not infer a pose from the previous frame; no spawns.
                if(!pool_.pauseSource(it->second.handle)) {
                    reason_="SpectraFX ECS attachment pause failed";
                    return false;
                }
                it->second.pausedForMissingPose=true;
                continue;
            }
            if(!pool_.setSourceTransform(it->second.handle,
                                         want.position,want.direction)) {
                reason_="SpectraFX ECS attachment transform rejected";
                return false;
            }
            if(it->second.pausedForMissingPose &&
               !pool_.resumeSource(it->second.handle)) {
                reason_="SpectraFX ECS attachment resume failed";
                return false;
            }
            it->second.pausedForMissingPose=false;
        }
        return true;
    }

    // Call outside any active render/compute pass and before replaying ticks.
    // Rejects without clearing tracking if GPU pool cannot reset safely.
    bool resetForRollback() {
        if(!pool_.resetForRollback()) {
            reason_="SpectraFX ECS rollback reset: "+pool_.reason();
            return false;
        }
        attached_.clear();
        reason_.clear();
        return true;
    }

    // Use when presenter stops but the pool remains alive. Existing particles
    // may age naturally (killLiving=false) or be explicitly invalidated.
    // Must be called at a safe graphics phase for GPU selective kills.
    bool detachAll(bool killLiving=false) {
        for(auto it=attached_.begin();it!=attached_.end();) {
            if(!pool_.stopSource(it->second.handle,killLiving) ||
               !pool_.retireSource(it->second.handle)) {
                reason_="SpectraFX ECS detachAll failed: "+pool_.reason();
                return false;
            }
            it=attached_.erase(it);
        }
        reason_.clear();
        return true;
    }

    size_t activeAttachments() const {return attached_.size();}
    const std::string& reason() const {return reason_;}

private:
    struct Policy {
        PoolContinuousEmission emission{};
        uint64_t revision=0;
    };
    struct Attached {
        PoolSourceId handle{};
        std::string effect;
        uint64_t policyRevision=0;
        uint32_t revision=0;
        int64_t scaleRaw=0;
        bool killOnDetach=false, pausedForMissingPose=false;
    };

    GpuPoolWorld& pool_;
    std::map<std::string,Policy> bindings_;
    std::map<uint64_t,Attached> attached_;
    uint64_t nextPolicyRevision_=1;
    std::string reason_;
};

} // namespace aiverse::spectra
