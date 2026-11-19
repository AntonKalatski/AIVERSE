// SpectraFX Stage 41 — one-at-a-time, bounded GPU diagnostic captures.
// Runs ONLY on the native presentation Device owner thread. Never starts
// background workers, blocks on GPU buffers, or touches authoritative ECS.
// Call pollBeforeStep() after Device.beginFrame, submitAfterStepBeforePass()
// after pool.step and before gfx::Device::beginPass.
#pragma once

#include "engine/fx/SpectraGpuPoolWorld.h"
#include "engine/fx/SpectraCheckpointInspector.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::spectra {

enum class GpuCapturePhase : uint8_t {
    Idle, Queued, InFlight, Ready, Failed
};

struct GpuCaptureStatus {
    GpuCapturePhase phase=GpuCapturePhase::Idle;
    uint64_t requestedPoolEpoch=0;
    size_t groupIndex=0;
    uint64_t requestId=0;
    size_t payloadBytes=0;
    uint64_t capturedRevision=0;
    uint64_t gpuFrameSerial=0;
    std::string reason;
};

class GpuPoolCaptureHost final {
public:
    explicit GpuPoolCaptureHost(GpuPoolWorld& pool)
        : pool_(pool),shared_(std::make_shared<CallbackState>()) {}
    GpuPoolCaptureHost(const GpuPoolCaptureHost&)=delete;
    GpuPoolCaptureHost& operator=(const GpuPoolCaptureHost&)=delete;
    ~GpuPoolCaptureHost() {cancel();}

    // Caller may enqueue ONE request, then use the normal render loop to
    // deliver it. A fresh request never overwrites an in-flight capture.
    bool request(size_t groupIndex,
                 size_t maxBytes=kGpuCheckpointExportMaxBytes) {
        if(status_.phase==GpuCapturePhase::Queued ||
           status_.phase==GpuCapturePhase::InFlight)return false;
        result_.clear();
        status_=GpuCaptureStatus{};
        if(!pool_.ready() || pool_.presentationBlocked() ||
           groupIndex>=pool_.stats().gpuGroups || maxBytes==0 ||
           maxBytes>kGpuCheckpointExportMaxBytes) {
            fail("Capture request: invalid group, byte cap, or unavailable GPU pool");
            return false;
        }
        status_.phase=GpuCapturePhase::Queued;
        status_.requestedPoolEpoch=pool_.lifecycleEpoch();
        status_.groupIndex=groupIndex;
        status_.requestId=++nextRequestId_;
        shared_->requestId=status_.requestId;
        maxBytes_=maxBytes;
        shared_->received=false;
        shared_->completed=false;
        return true;
    }

    // Invoke in a safe graphics frame, AFTER pool.step(dt) and BEFORE the
    // render encoder. This is the only function that initiates GPU readback.
    bool submitAfterStepBeforePass() {
        if(status_.phase!=GpuCapturePhase::Queued)return false;
        if(!sameLivePool()) {
            fail("Capture submit: pool was lost or re-created");
            return false;
        }
        const uint64_t requestId=status_.requestId;
        std::weak_ptr<CallbackState> callback=shared_;
        status_.phase=GpuCapturePhase::InFlight;
        const bool started=pool_.requestGpuCheckpoint(
            status_.groupIndex,
            [callback,requestId](bool completed) {
                // GPU callback lifetime is independent of host destruction.
                if(auto state=callback.lock()) {
                    if(state->requestId!=requestId)return;
                    state->completed=completed;
                    state->received=true;
                }
            });
        if(!started) {
            fail("Capture submit: "+pool_.reason());
            return false;
        }
        return true;
    }

    // Invoke after Device.beginFrame delivers callbacks, BEFORE the next
    // pool.step can advance the captured GPU revision. Export runs only on
    // the calling owner thread and has no synchronous GPU reads.
    const GpuCaptureStatus& pollBeforeStep() {
        if(status_.phase!=GpuCapturePhase::InFlight)return status_;
        if(!sameLivePool()) {
            fail("Capture cancelled by graphics Device loss or pool recreation");
            return status_;
        }
        if(!shared_->received)return status_;
        if(!shared_->completed) {
            fail("Capture readback failed or returned incomplete chunks");
            return status_;
        }
        std::vector<uint8_t> fresh;
        std::string why;
        if(!pool_.exportFreshCheckpoint(
               status_.groupIndex,fresh,maxBytes_,&why)) {
            fail("Capture completed but became stale: "+why);
            return status_;
        }
        const GpuDiagnosticInspection checked=
            inspectGpuCheckpointDiagnostic(fresh);
        if(!checked.structurallyValid()) {
            fail("Captured checkpoint failed integrity inspection");
            return status_;
        }
        status_.phase=GpuCapturePhase::Ready;
        status_.payloadBytes=fresh.size();
        status_.capturedRevision=checked.header.capturedRevision;
        status_.gpuFrameSerial=checked.header.gpuFrameSerial;
        status_.reason.clear();
        result_=std::move(fresh);
        return status_;
    }

    // Cancel BEFORE pool reset, shutdown, or Device recreation.
    // The shared callback state is replaced; already in-flight RHI closures
    // retain only weak references and cannot publish into the new request.
    void cancel() {
        shared_=std::make_shared<CallbackState>();
        result_.clear();
        status_=GpuCaptureStatus{};
    }
    const GpuCaptureStatus& status() const {return status_;}
    bool takeReady(std::vector<uint8_t>& output) {
        if(status_.phase!=GpuCapturePhase::Ready)return false;
        output=std::move(result_);
        status_=GpuCaptureStatus{};
        return true;
    }

private:
    struct CallbackState {
        uint64_t requestId=0;
        bool received=false,completed=false;
    };
    bool sameLivePool() const {
        return status_.requestedPoolEpoch!=0 &&
               pool_.ready() && !pool_.presentationBlocked() &&
               pool_.lifecycleEpoch()==status_.requestedPoolEpoch;
    }
    void fail(std::string reason) {
        status_.phase=GpuCapturePhase::Failed;
        status_.reason=std::move(reason);
        result_.clear();
    }

    GpuPoolWorld& pool_;
    std::shared_ptr<CallbackState> shared_;
    GpuCaptureStatus status_{};
    std::vector<uint8_t> result_;
    size_t maxBytes_=kGpuCheckpointExportMaxBytes;
    uint64_t nextRequestId_=0;
};

} // namespace aiverse::spectra
