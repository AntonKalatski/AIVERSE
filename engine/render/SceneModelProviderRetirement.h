// AIVERSE — owner-thread GPU completion queue for WHOLE scene Model owners.
// Unlike a shared_ptr to an AppearanceAsset snapshot, a queued provider
// retains its ModelResourceCache, shared textures and rollback image pool.
// The native scene host owns all provider lifetimes; gameplay ECS never does.
#pragma once
#include "ModelAssetProvider.h"
#include "AsyncResourceRetirementQueue.h"
#include "RenderDeviceLifetime.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace aiverse::render {

class SceneModelProviderRetirement final {
    using Owner=std::unique_ptr<ModelAssetProvider>;
    using Queue=AsyncResourceRetirementQueue<Owner>;
public:
    explicit SceneModelProviderRetirement(RenderDeviceLifetime::Lease device)
        :device_(std::move(device)),queue_(std::make_unique<Queue>()) {}
    SceneModelProviderRetirement(const SceneModelProviderRetirement&)=delete;
    SceneModelProviderRetirement& operator=(const SceneModelProviderRetirement&)=delete;

    ~SceneModelProviderRetirement() {
        // Destruction is NEVER evidence that GPU draws completed. Quarantine
        // all pending model owners if the host did not complete shutdown.
        // The leaked queue retains entire providers, not just native handles.
        if(queue_ && !queue_->empty())(void)queue_.release();
    }

    // Call AFTER dropping ALL scene draw packets, joined animation jobs,
    // ModelReadLeases and presentation Graph/attachment generations. Any
    // submitted GPU draw must already be recorded before the next collect.
    // Takes ownership even when the backend lacks fence tokens; the queue
    // keeps the real ModelAssetProvider CPU/GPU owner alive until GPU proof.
    void retire(gfx::Device& device,Owner& owner) noexcept {
        if(!owner)return;
        if(!device_.validFor(device)) {
            quarantine(owner);
            return;
        }
        try {
            queue_->enqueueFrom(owner);
        }catch(...) {
            ++enqueueFailures_;
            // Under OOM, a genuine complete GPU-idle proof may authorize
            // synchronous safe release. Never free otherwise.
            try {
                if(device.waitForResourceIdle()) {
                    try {
                        owner->shutdown(device);
                        owner.reset();
                        return;
                    }catch(...) {++shutdownFailures_;}
                }
            }catch(...) {}
            quarantine(owner);
        }
    }

    // Poll after completed frame submission on the SAME gfx::Device owner.
    // Per-batch ordered completion tokens prevent early GPU memory reuse.
    // Without backend token support, retain the owner until the explicit
    // pending-count pressure point or a shutdown/full-idle barrier.
    // Actual provider shutdown may still require a device idle wait.
    bool collect(gfx::Device& device) noexcept {
        if(!device_.validFor(device))return false;
        if(!queue_ || queue_->empty())return true;
        try {
            return queue_->collect(
                [&]{return device.beginResourceRetirement();},
                [&](uint64_t token){return device.resourceRetirementComplete(token);},
                [&](uint64_t token){
                    if(device.resourceRetirementStatus(token)==
                       gfx::ResourceRetirementStatus::Complete)
                        device.endResourceRetirement(token);
                    else device.abandonRetirementTokenAfterIdle(token);
                },
                [&]{return queue_->pending()>=kNoTokenIdlePressure &&
                           device.waitForResourceIdle();},
                [&](Owner& owner){shutdownOrQuarantine(device,owner);},
                [&](uint64_t token){
                    return device.resourceRetirementStatus(token)==
                           gfx::ResourceRetirementStatus::Failed;
                });
        }catch(...) {
            // Never release a provider on an unproven token/poll failure.
            ++pollFailures_;
            return false;
        }
    }

    // The owner must call this BEFORE invalidating RenderDeviceLifetime or
    // destroying gfx::Device. A failed wait leaves queue owners untouched.
    bool completeForShutdown(gfx::Device& device) noexcept {
        if(!device_.validFor(device))return false;
        if(!queue_ || queue_->empty())return true;
        try {
            return queue_->drain(
                [&]{return device.waitForResourceIdle();},
                [&](uint64_t token){
                    if(token)device.abandonRetirementTokenAfterIdle(token);
                },
                [&](Owner& owner){shutdownOrQuarantine(device,owner);});
        }catch(...) {
            ++pollFailures_;
            return false;
        }
    }

    size_t pendingOwners() const noexcept {
        return queue_?queue_->pending():0;
    }
    uint64_t enqueueFailures() const noexcept{return enqueueFailures_;}
    uint64_t pollFailures() const noexcept{return pollFailures_;}
    uint64_t shutdownFailures() const noexcept{return shutdownFailures_;}
    uint64_t orphanedOwners() const noexcept{return orphanedOwners_;}

private:
    static constexpr size_t kNoTokenIdlePressure=128;
    void quarantine(Owner& owner) noexcept {
        // Preserve the CPU Model pointers and GPU handles until device
        // teardown rather than UAF; this is a diagnosed intentional leak.
        if(owner) {
            (void)owner.release();
            ++orphanedOwners_;
        }
    }
    void shutdownOrQuarantine(gfx::Device& device,Owner& owner) noexcept {
        if(!owner)return;
        try {
            if(device_.validFor(device)) {
                owner->shutdown(device);
                owner.reset();
                return;
            }
        }catch(...) {++shutdownFailures_;}
        quarantine(owner);
    }

    RenderDeviceLifetime::Lease device_;
    std::unique_ptr<Queue> queue_;
    uint64_t enqueueFailures_=0,pollFailures_=0;
    uint64_t shutdownFailures_=0,orphanedOwners_=0;
};

} // namespace aiverse::render
