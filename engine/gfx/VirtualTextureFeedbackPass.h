// AIVERSE optional virtual-texture visibility feedback render pass.
// Owns a linear RGBA8 single-sample offscreen color attachment and routes
// completed readbacks into the generic disk-backed virtual page scheduler.
// Callers render their own feedback shader: no ECS, camera, terrain or genre
// dependency, and no hidden GPU feedback work when the feature is disabled.
#pragma once

#include "RenderTargetSet.h"
#include "VirtualTextureFeedbackRuntime.h"

#include <cstddef>
#include <limits>
#include <vector>
#include <functional>
#include <cstdint>
#include <string>
#include <utility>

namespace aiverse::gfx {

// Compatible pipeline state for a feedback material draw. Callers retain
// their own vertex layout, shader and culling policy; the feedback pass
// always writes one unblended linear RGBA8 color attachment. No MSAA.
// Depth testing is opt-in and requires initialize(..., depthAttachment=true).
inline PipelineDesc virtualTextureFeedbackPipelineDesc(
    ShaderHandle shader, bool depthAttachment=false) {
    PipelineDesc desc;
    desc.shader = shader;
    desc.colorFormat = PixelFormat::RGBA8;
    desc.colorAttachmentCount = 1;
    desc.sampleCount = 1;
    desc.blend = BlendMode::Opaque;
    desc.depthTest = depthAttachment;
    desc.depthWrite = depthAttachment;
    desc.colorWrite = true;
    desc.depthOnly = false;
    desc.label = "VirtualTextureFeedback";
    return desc;
}

// A renderer can translate its existing visible mesh/material queue into
// these backend-independent draw commands. Each command may bind its own
// material uniforms using prepare(), so meshes and terrain use one API.
// The callback is invoked while the feedback render pass is active.
struct VirtualTextureFeedbackDraw {
    PipelineHandle pipeline;
    BufferHandle vertexBuffer;
    BufferHandle indexBuffer; // invalid means non-indexed draw
    uint32_t count = 0;
    uint32_t first = 0;
    int32_t baseVertex = 0;
    uint32_t instances = 1;
    size_t vertexOffset = 0;
    size_t indexOffset = 0;
    std::function<bool(Device&)> prepare;
};

// Adapter for the renderer's existing visibility queue. Implementations
// traverse their already-culled material/mesh batches and append feedback
// draw commands, without the feedback subsystem depending on scene types.
// The callback is called outside the render pass so CPU culling and command
// allocation never occur while the GPU pass is active.
class VirtualTextureFeedbackDrawSource {
public:
    virtual ~VirtualTextureFeedbackDrawSource() = default;
    virtual bool collectFeedbackDraws(
        std::vector<VirtualTextureFeedbackDraw>& out) = 0;
};

class VirtualTextureFeedbackPass {
public:
    VirtualTextureFeedbackPass() = default;
    ~VirtualTextureFeedbackPass() { shutdown(); }
    VirtualTextureFeedbackPass(const VirtualTextureFeedbackPass&) = delete;
    VirtualTextureFeedbackPass& operator=(const VirtualTextureFeedbackPass&) = delete;
    VirtualTextureFeedbackPass(VirtualTextureFeedbackPass&&) = delete;
    VirtualTextureFeedbackPass& operator=(VirtualTextureFeedbackPass&&) = delete;

    bool initialize(Device& device, int width, int height,
                    VirtualTextureFeedbackConfig feedback,
                    VirtualTexturePrefetchConfig prefetch,
                    size_t maxDeferredFrames = 8,
                    bool depthAttachment = false) {
        shutdown();
        error_.clear();
        // The feedback buffer contains one 32-bit page ID per pixel; it is
        // not a display surface, and never needs HDR, sRGB or MSAA.
        if (width < 1 || height < 1 ||
            size_t(width) > std::numeric_limits<size_t>::max() / size_t(height) ||
            size_t(width) * size_t(height) > feedback.maxReadbackPixels ||
            !device.supports(PixelFormat::RGBA8) ||
            (depthAttachment && !device.supports(PixelFormat::Depth)) ||
            !device.supportsTextureDimension(TextureDimension::Texture2D) ||
            !device.supportsRenderTargetPasses()) {
            error_ = "feedback pass needs a supported bounded RGBA8 2D render target";
            return false;
        }

        RenderTargetSet::Config cfg;
        cfg.width = width;
        cfg.height = height;
        cfg.dimension = TextureDimension::Texture2D;
        cfg.layers = 1;
        cfg.mipLevels = 1;
        cfg.sampleCount = 1;
        cfg.colorCount = 1;
        cfg.colorFormat = PixelFormat::RGBA8;
        cfg.depth = depthAttachment;
        if (!targets_.init(device,cfg)) {
            error_ = targets_.lastError();
            return false;
        }
        if (!readback_.initialize(device,feedback,prefetch,maxDeferredFrames)) {
            error_ = readback_.lastError();
            targets_.shutdown();
            return false;
        }
        device_ = &device;
        width_ = width;
        height_ = height;
        feedbackConfig_ = feedback;
        prefetchConfig_ = prefetch;
        maxDeferredFrames_ = maxDeferredFrames;
        depthAttachment_ = depthAttachment;
        return true;
    }

    // Reallocate feedback resolution on viewport resize. Old in-flight
    // callbacks are invalidated by shutdown(); the native Device retains
    // ownership of its outstanding staging commands. A failed allocation
    // leaves the pass cleanly uninitialized rather than half-resized.
    // This intentionally discards old camera visibility on resize.
    bool resize(int width, int height) {
        if (!ready() || active()) {
            error_ = "feedback pass must be idle and initialized to resize";
            return false;
        }
        if (width == width_ && height == height_) return true;
        Device* device = device_;
        const auto feedback = feedbackConfig_;
        const auto prefetch = prefetchConfig_;
        const size_t deferred = maxDeferredFrames_;
        const bool depth = depthAttachment_;
        return initialize(*device,width,height,feedback,prefetch,deferred,depth);
    }

    // Called inside an existing graphics frame, with no active render pass.
    // The game's feedback fragment shader must output exact unblended RGBA8
    // page IDs (VirtualTextureFeedback.h) at location 0. Use a pipeline
    // with RGBA8 color format, one output and Opaque blending.
    bool begin() {
        if (!device_ || targets_.active() ||
            readback_.pending() || readback_.readyToSubmit()) {
            error_ = "feedback target unavailable or previous sample not consumed";
            return false;
        }
        if (!targets_.begin(0,0,true,depthAttachment_,{0.f,0.f,0.f,0.f})) {
            error_ = device_->lastError();
            if (error_.empty()) error_ = "feedback render pass rejected by device";
            return false;
        }
        device_->setViewport(Rect{0,0,width_,height_});
        device_->setScissor(Rect{0,0,width_,height_});
        error_.clear();
        return true;
    }

    // Optional integration point for a renderer/material system. The caller
    // supplies its genre-independent feedback draw commands; this helper
    // owns pass ordering, clear, viewport, end and asynchronous capture.
    // Draw callback returns false if it cannot submit a compatible RGBA8
    // opaque feedback pipeline. In that case end the pass without capturing
    // incomplete visibility. Never call from inside another render pass.
    template<class DrawFeedback>
    bool renderAndCapture(DrawFeedback&& drawFeedback) {
        if (!begin()) return false;
        // A throwing renderer must not leave the Device inside an active
        // pass: the next frame must be able to render or tear down safely.
        // Preserve the caller's exception after restoring RHI pass state.
        try {
            if (!std::forward<DrawFeedback>(drawFeedback)(*device_)) {
                targets_.end();
                error_ = "feedback draw callback refused or failed";
                return false;
            }
        } catch (...) {
            targets_.end();
            error_ = "feedback draw callback threw an exception";
            throw;
        }
        return endAndCapture();
    }

    // Submit a renderer-prepared visible geometry queue through the same
    // feedback pass. Reject malformed commands before beginning the pass,
    // so an invalid item cannot leave partially rendered feedback.
    // Pipeline depth mode must match hasDepthAttachment(); renderer owns
    // pipeline creation, material uniform binding and camera transforms.
    bool renderDrawsAndCapture(
        const std::vector<VirtualTextureFeedbackDraw>& draws) {
        for (const auto& draw : draws) {
            if (!draw.pipeline.valid() || !draw.vertexBuffer.valid() ||
                draw.count == 0 || draw.instances == 0 ||
                (!draw.indexBuffer.valid() && draw.instances != 1)) {
                error_ = "invalid virtual texture feedback draw command";
                return false;
            }
        }
        return renderAndCapture([&](Device& device) {
            for (const auto& draw : draws) {
                device.setPipeline(draw.pipeline);
                device.setVertexBuffer(draw.vertexBuffer,draw.vertexOffset);
                if (draw.indexBuffer.valid())
                    device.setIndexBuffer(draw.indexBuffer,draw.indexOffset);
                if (draw.prepare && !draw.prepare(device)) return false;
                if (draw.indexBuffer.valid())
                    device.drawIndexed(draw.count,draw.first,
                                       draw.baseVertex,draw.instances);
                else device.draw(draw.count,draw.first);
            }
            return true;
        });
    }

    // Entry point for any renderer with a visibility queue. Collect first,
    // then validate and render, then request asynchronous readback. No
    // coupling to the engine's optional terrain, RTS, FPS or editor modules.
    // The source owns visibility ordering, compatible pipelines and
    // per-material uniforms. Empty visibility is a valid cleared capture.
    bool renderVisibleAndCapture(VirtualTextureFeedbackDrawSource& source) {
        if (!ready() || active() || pending() || readyToSubmit()) {
            error_ = "feedback pass unavailable for visible draw collection";
            return false;
        }
        std::vector<VirtualTextureFeedbackDraw> draws;
        if (!source.collectFeedbackDraws(draws)) {
            error_ = "feedback visible draw collection failed";
            return false;
        }
        return renderDrawsAndCapture(draws);
    }

    // End the current render pass, then stage a nonblocking texture readback.
    // Returns false if the backend doesn't implement async texture readback.
    // No immediate CPU pixels are returned here.
    bool endAndCapture() {
        if (!device_ || !targets_.active()) {
            error_ = "no active feedback render pass";
            return false;
        }
        targets_.end();
        if (!readback_.capture(targets_.sampledColor(0))) {
            error_ = readback_.lastError();
            return false;
        }
        error_.clear();
        return true;
    }

    // On a later graphics frame, after the RHI has delivered its readback
    // completion at Device::beginFrame, submit only missing/pending-safe
    // virtual pages to the existing worker queue. The caller still calls
    // VirtualTextureFileStreamer::pump for bounded disk->GPU uploads.
    VirtualTexturePrefetchDispatch submitVisiblePages(
        VirtualTextureFileStreamer& streamer) {
        return readback_.submit(streamer);
    }

    // One owner-thread integration point for a game/editor frame after
    // Device::beginFrame has delivered asynchronous readback callbacks.
    // The caller remains responsible for feedback geometry submission and
    // for ensuring no other render/compute pass is active. The file streamer
    // is independently owned and must outlive this call.
    struct FrameResult {
        VirtualTexturePrefetchDispatch visibility;
        size_t stagedGpuPages = 0;
        bool streamingReady = false;
    };
    FrameResult updateStreaming(
        VirtualTextureFileStreamer& streamer,
        size_t maxNewGpuPages = 8,
        const VirtualTextureFileStreamer::CompletionFn& completion = {}) {
        FrameResult result;
        if (!ready() || active() || !streamer.ready()) return result;
        result.visibility = submitVisiblePages(streamer);
        result.stagedGpuPages = streamer.pump(maxNewGpuPages,completion);
        result.streamingReady = streamer.ready();
        return result;
    }

    bool ready() const { return device_ != nullptr && targets_.color(0).valid(); }
    bool active() const { return targets_.active(); }
    bool pending() const { return readback_.pending(); }
    bool readyToSubmit() const { return readback_.readyToSubmit(); }
    TextureHandle texture() const { return ready() ? targets_.sampledColor(0) : TextureHandle{}; }
    bool hasDepthAttachment() const { return ready() && depthAttachment_; }
    TextureHandle depthTexture() const { return hasDepthAttachment() ? targets_.depth() : TextureHandle{}; }
    int width() const { return width_; }
    int height() const { return height_; }
    const std::string& lastError() const { return error_; }

    void shutdown() {
        // A late backend callback holds only a weak_ptr to the discarded
        // decoder state, not the render-target owner or a Device pointer.
        // Device must outlive this object while its GPU commands are in flight.
        readback_.shutdown();
        targets_.shutdown();
        device_ = nullptr;
        width_ = height_ = 0;
        feedbackConfig_ = {};
        prefetchConfig_ = {};
        maxDeferredFrames_ = 8;
        depthAttachment_ = false;
    }

private:
    Device* device_ = nullptr;
    RenderTargetSet targets_;
    VirtualTextureFeedbackRuntime readback_;
    int width_ = 0, height_ = 0;
    VirtualTextureFeedbackConfig feedbackConfig_{};
    VirtualTexturePrefetchConfig prefetchConfig_{};
    size_t maxDeferredFrames_ = 8;
    bool depthAttachment_ = false;
    std::string error_;
};

} // namespace aiverse::gfx
