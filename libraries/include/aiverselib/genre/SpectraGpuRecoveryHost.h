// SpectraFX Stage 39 — host-owned recovery workflow for a native game.
// This lives in the native presentation integration layer, not gameplay ECS.
// The host owns BOTH graphics Devices and must keep the OLD Device alive
// through pool.shutdown() while it recreates graphics.
#pragma once

#include "SpectraGpuAttachmentEcsAdapter.h"
#include "SpectraEcsRenderPose.h"
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace aiverse::spectra {

enum class PoolRecoveryPhase : uint8_t {
    Uninitialized, Ready, ResetRetryRequired, DeviceRecreationRequired,
    BlockedUnknown
};
enum class PoolRecoveryAction : uint8_t {
    None, InitPool, RetryResetInGraphicsFrame, RecreateGraphicsDevice,
    InspectFailure
};
struct PoolRecoveryEvent {
    PoolRecoveryPhase phase=PoolRecoveryPhase::Uninitialized;
    PoolRecoveryAction next=PoolRecoveryAction::InitPool;
    uint32_t resetGroupsAccepted=0, resetGroupsTotal=0;
    uint64_t resetAttempts=0;
    bool deviceHealthKnown=false, deviceHealthy=false;
    bool presentationBlocked=false;
    std::string reason;
};

class GpuPoolRecoveryHost final {
public:
    using OnChange=std::function<void(const PoolRecoveryEvent&)>;

    GpuPoolRecoveryHost(GpuPoolWorld& pool,
                        GpuEcsAttachmentPresenter& presenter,
                        GpuEcsPoseInterpolator* poses=nullptr)
        : pool_(pool),presenter_(presenter),poses_(poses) {}

    // Called by native frame host at a presentation safe point. Emits a
    // callback ONLY when the phase/progress/health changed; never starts
    // asynchronous work, destroys a Device, or performs GPU commands.
    PoolRecoveryEvent poll() {
        PoolRecoveryEvent result=inspect();
        if(!observed_ || changed(last_,result)) {
            last_=result;
            observed_=true;
            if(onChange_)onChange_(result);
        }
        return result;
    }
    void onChange(OnChange callback) {onChange_=std::move(callback);}

    // Retry at a graphics safe point AFTER Device::beginFrame (Metal has an
    // active ordered command buffer), BEFORE any scene encoder/pass.
    // A false return leaves all sources paused/blocked for another retry.
    bool retryPresentationReset() {
        const PoolRecoveryEvent state=inspect();
        if(state.phase==PoolRecoveryPhase::DeviceRecreationRequired ||
           state.phase==PoolRecoveryPhase::Uninitialized) {
            reason_="SpectraFX reset needs an initialized recoverable GPU pool";
            poll();
            return false;
        }
        if(!presenter_.resetForRollback()) {
            reason_="SpectraFX presentation reset: "+presenter_.reason();
            poll();
            return false;
        }
        if(poses_)poses_->resetForRollback();
        reason_.clear();
        poll();
        return true;
    }

    // Host has already constructed a healthy NEW gfx::Device.
    // Call outside render/compute encoders and with the OLD Device alive.
    // Physical GPU particles and all render-source IDs are intentionally
    // lost. Effects are re-declared from original CPU-side definitions.
    // A failed import shuts down its partial new pool and does not claim
    // that any GPU data was restored.
    bool replaceDevice(gfx::Device& healthyReplacement) {
        if(!healthyReplacement.gpuHealthy()) {
            reason_="SpectraFX graphics Device replacement is not healthy";
            poll();
            return false;
        }
        if(!pool_.ready()) {
            reason_="SpectraFX replacement requires the original pool alive";
            poll();
            return false;
        }
        auto catalog=pool_.exportRecoveryManifest();
        pool_.shutdown(); // requires old gfx Device still valid
        presenter_.discardTrackingAfterPoolRecreation();
        if(poses_)poses_->resetForRollback();
        const bool rebuilt=pool_.importRecoveryManifest(
            &healthyReplacement,catalog);
        if(!rebuilt) {
            reason_="SpectraFX Device replacement: "+pool_.reason();
            poll();
            return false;
        }
        reason_.clear();
        poll();
        return true;
    }

    PoolRecoveryEvent inspect() const {
        PoolRecoveryEvent out;
        if(!pool_.ready()) {
            out.phase=PoolRecoveryPhase::Uninitialized;
            out.next=PoolRecoveryAction::InitPool;
            out.reason=reason_.empty()?pool_.reason():reason_;
            return out;
        }
        const auto& reset=pool_.resetReport();
        const auto stats=pool_.checkpointStats();
        out.resetGroupsAccepted=reset.groupsAccepted;
        out.resetGroupsTotal=reset.groupCount;
        out.resetAttempts=reset.attempts;
        out.deviceHealthKnown=stats.gpuDeviceHealthKnown;
        out.deviceHealthy=stats.gpuDeviceHealthy;
        out.presentationBlocked=pool_.presentationBlocked();
        if(reset.deviceLost ||
           (stats.gpuDeviceHealthKnown && !stats.gpuDeviceHealthy)) {
            out.phase=PoolRecoveryPhase::DeviceRecreationRequired;
            out.next=PoolRecoveryAction::RecreateGraphicsDevice;
        } else if(reset.retryRequired) {
            out.phase=PoolRecoveryPhase::ResetRetryRequired;
            out.next=PoolRecoveryAction::RetryResetInGraphicsFrame;
        } else if(out.presentationBlocked) {
            out.phase=PoolRecoveryPhase::BlockedUnknown;
            out.next=PoolRecoveryAction::InspectFailure;
        } else {
            out.phase=PoolRecoveryPhase::Ready;
            out.next=PoolRecoveryAction::None;
        }
        out.reason=reason_.empty()?pool_.reason():reason_;
        return out;
    }

    const std::string& reason() const {return reason_;}

private:
    static bool changed(const PoolRecoveryEvent& a,
                        const PoolRecoveryEvent& b) {
        return a.phase!=b.phase || a.next!=b.next ||
            a.resetGroupsAccepted!=b.resetGroupsAccepted ||
            a.resetGroupsTotal!=b.resetGroupsTotal ||
            a.resetAttempts!=b.resetAttempts ||
            a.deviceHealthKnown!=b.deviceHealthKnown ||
            a.deviceHealthy!=b.deviceHealthy ||
            a.presentationBlocked!=b.presentationBlocked ||
            a.reason!=b.reason;
    }
    GpuPoolWorld& pool_;
    GpuEcsAttachmentPresenter& presenter_;
    GpuEcsPoseInterpolator* poses_=nullptr;
    OnChange onChange_;
    PoolRecoveryEvent last_{};
    std::string reason_;
    bool observed_=false;
};

} // namespace aiverse::spectra
