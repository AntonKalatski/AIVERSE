// AIVERSE opt-in virtual texture GPU feedback runtime coordinator.
// Requests a completed RGBA8 feedback render target through the existing
// async Device readback contract and forwards decoded tile visibility into
// the genre-agnostic file-backed prefetch planner. Render-thread only.
#pragma once

#include "VirtualTextureFeedback.h"
#include "VirtualTexturePrefetchAdapter.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::gfx {

class VirtualTextureFeedbackRuntime {
public:
    VirtualTextureFeedbackRuntime()=default;
    ~VirtualTextureFeedbackRuntime() { shutdown(); }
    VirtualTextureFeedbackRuntime(const VirtualTextureFeedbackRuntime&)=delete;
    VirtualTextureFeedbackRuntime& operator=(const VirtualTextureFeedbackRuntime&)=delete;
    VirtualTextureFeedbackRuntime(VirtualTextureFeedbackRuntime&&)=delete;
    VirtualTextureFeedbackRuntime& operator=(VirtualTextureFeedbackRuntime&&)=delete;

    bool initialize(Device& device, VirtualTextureFeedbackConfig feedback,
                    VirtualTexturePrefetchConfig prefetch) {
        shutdown();
        if (feedback.tilesX!=prefetch.tilesX ||
            feedback.tilesY!=prefetch.tilesY ||
            feedback.mipLevels!=prefetch.mipLevels ||
            feedback.virtualLayers!=prefetch.virtualLayers ||
            !planner_.initialize(prefetch)) {
            error_="GPU feedback and prefetch virtual tile geometry differ";
            return false;
        }
        auto state=std::make_shared<State>();
        if (!state->decoder.initialize(feedback)) {
            planner_.reset();
            error_="GPU feedback decoder rejected config or resource budget";
            return false;
        }
        state_=std::move(state);
        device_=&device;
        error_.clear();
        return true;
    }

    // Call only outside a render/compute pass after the game's own feedback
    // shader has rendered a linear, unblended RGBA8 target.
    // No implicit beginFrame/endFrame, no synchronous glReadPixels/fence wait.
    // A second capture is refused until the first completes.
    bool capture(TextureHandle target, TextureReadbackDesc subresource={}) {
        if (!device_ || !state_ || !target.valid() ||
            state_->pending || state_->ready) {
            error_="GPU feedback unavailable, pending or awaiting consumption";
            return false;
        }
        auto state=state_;
        state->pending=true;
        state->views.clear();
        state->error.clear();
        std::weak_ptr<State> weak=state;
        if (!device_->readTextureAsync(target,subresource,
            [weak](TextureReadbackResult image) {
                // The owner object may have been shutdown or destroyed while
                // this GPU staging command remained in flight.
                const auto current=weak.lock();
                if (!current) return;
                current->pending=false;
                current->ready=current->decoder.decode(
                    image,current->views,current->error);
            })) {
            state->pending=false;
            error_=device_->lastError();
            if(error_.empty())
                error_="native backend refused async feedback readback";
            return false;
        }
        error_.clear();
        return true;
    }

    // Call on the graphics/streamer owner thread, normally once per frame
    // after beginFrame has delivered ready async readback callbacks.
    // Then call stream.pump() to transfer staged physical pages.
    VirtualTexturePrefetchDispatch submit(VirtualTextureFileStreamer& stream) {
        if (!state_ || !state_->ready || !stream.ready()) return {};
        state_->ready=false;
        auto visible=std::move(state_->views);
        state_->views.clear();
        const auto dispatch=prefetchVisibleVirtualPages(stream,planner_,visible);
        return dispatch;
    }

    bool pending() const {return state_ && state_->pending;}
    bool readyToSubmit() const {return state_ && state_->ready;}
    VirtualTextureFeedbackStats lastFeedbackStats() const {
        return state_?state_->decoder.stats():VirtualTextureFeedbackStats{};
    }
    VirtualTexturePrefetchStats lastPrefetchStats() const {
        return planner_.stats();
    }
    const std::string& lastError() const {
        if (state_ && !state_->error.empty()) return state_->error;
        return error_;
    }

    // Invalidate the captured weak callback state without stopping GPU
    // work already submitted to the Device. The native backend still owns
    // the staging allocation and frees it after completion.
    void shutdown() {
        state_.reset();
        device_=nullptr;
        planner_.reset();
        error_.clear();
    }
private:
    struct State {
        VirtualTextureFeedbackDecoder decoder;
        std::vector<VirtualTextureVisibleRegion> views;
        std::string error;
        bool pending=false;
        bool ready=false;
    };
    Device* device_=nullptr; // must outlive this helper
    VirtualTexturePrefetchPlanner planner_;
    std::shared_ptr<State> state_;
    std::string error_;
};

} // namespace aiverse::gfx
