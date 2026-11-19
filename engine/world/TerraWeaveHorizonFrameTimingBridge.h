// TerraWeave: host-side frame timing bridge for native RTS/FPS clients.
// Uses AIVERSE's existing gfx::Device tagged asynchronous GPU timings and
// measured CPU wall time. Never invents a GPU sample or stalls a frame.
// Only register this bridge with a Device whose takeGpuTimings() collection
// is not simultaneously owned by another benchmark/consumer.
#pragma once
#include "engine/world/WorldScene.h"
#include "engine/gfx/Gfx.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>

namespace aiverse::terraweave {

class HorizonFrameTimingBridge {
public:
    using Clock=std::chrono::steady_clock;

    // Called after gfx::Device::beginFrame() and before its endFrame().
    // The caller retains the REAL CPU frame start timestamp for finish().
    // Return false when the optional horizon/adaptation is absent.
    bool begin(gfx::Device& device, world::WorldScene* scene) {
        if(!scene || !scene->vegetationHorizonAdaptiveQualityConfig().enabled) {
            discard();
            return false;
        }
        if(capturing_)return false;
        collectGpu(device);
        deliver(*scene);
        ++frameId_;
        Frame pending;
        pending.id=frameId_;
        pending.gpuRequested=device.timeGpuFrame(tag(frameId_));
        frames_.push_back(pending);
        capturing_=true;
        return true;
    }

    // Call once after the host has finished submitting this frame's
    // rendering. The CPU timer is measured (not the clamped sim 'dt').
    // GPU results are paired later by their original tagged frame ID.
    void finish(world::WorldScene* scene,Clock::time_point frameStarted) {
        if(!capturing_)return;
        capturing_=false;
        if(!scene || frames_.empty()){discard();return;}
        const auto micros=std::chrono::duration_cast<
            std::chrono::microseconds>(Clock::now()-frameStarted).count();
        Frame& frame=frames_.back();
        frame.cpuUs=micros>0 && micros<=10000000 ?
            static_cast<uint32_t>(micros) : 0u;
        frame.cpuReady=true;
        deliver(*scene);
    }

    // Does not reset monotonically assigned IDs; old GPU tags can never
    // accidentally match a newly enabled WorldScene/controller.
    void discard() {
        frames_.clear();
        capturing_=false;
    }

    uint64_t lastFrameId() const {return frameId_;}
    size_t pendingFrames() const {return frames_.size();}

private:
    struct Frame {
        uint64_t id=0;
        uint32_t cpuUs=0,gpuUs=0;
        bool gpuRequested=false,gpuReady=false,cpuReady=false;
    };
    static constexpr uint64_t kPrefix=0x7E41000000000000ull;
    static constexpr uint64_t kIdMask=0x0000ffffffffffffull;
    static constexpr uint64_t kMaxGpuLagFrames=8;
    static uint64_t tag(uint64_t id) {
        return kPrefix|(id&kIdMask);
    }
    void collectGpu(gfx::Device& device) {
        bool waiting=false;
        for(const auto& frame:frames_)
            if(frame.gpuRequested && !frame.gpuReady){waiting=true;break;}
        if(!waiting)return;
        // This is a destructive device-level drain. Native game clients
        // use one timing collector; other tools must coordinate if sharing
        // the same Device with independent tagged benchmark consumers.
        for(const auto& timing:device.takeGpuTimings()) {
            if((timing.tag & ~kIdMask)!=kPrefix)continue;
            for(auto& frame:frames_) {
                if(tag(frame.id)!=timing.tag || !frame.gpuRequested ||
                   frame.gpuReady)continue;
                frame.gpuReady=true;
                if(timing.valid && std::isfinite(timing.milliseconds) &&
                   timing.milliseconds>0.0 && timing.milliseconds<=10000.0) {
                    const auto rounded=std::llround(
                        timing.milliseconds*1000.0);
                    if(rounded>0 && rounded<=10000000)
                        frame.gpuUs=static_cast<uint32_t>(rounded);
                }
                break;
            }
        }
    }
    void deliver(world::WorldScene& scene) {
        while(!frames_.empty()) {
            const auto& first=frames_.front();
            if(!first.cpuReady)break;
            if(first.gpuRequested && !first.gpuReady &&
               frameId_-first.id<kMaxGpuLagFrames)
                break;
            // CPU-only fallback on unsupported/timed-out GPU providers.
            // Emit each frame ID only once, strictly in increasing order.
            scene.submitVegetationHorizonFrameTiming({
                first.id,first.cpuUs,first.gpuReady?first.gpuUs:0u});
            frames_.pop_front();
        }
    }
    uint64_t frameId_=0;
    bool capturing_=false;
    std::deque<Frame> frames_;
};
} // namespace aiverse::terraweave
