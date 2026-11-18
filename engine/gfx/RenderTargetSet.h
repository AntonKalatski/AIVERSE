// Generic offscreen color/depth render-target ownership and pass submission.
// This is a presentation-only native C++ utility, independent of game genre,
// ECS state, the built-in brush canvas and asset/file import.
#pragma once

#include "Gfx.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::gfx {

// A convenient owner for the render targets used by an offscreen camera,
// post-processing pass, shadow map or G-buffer. The device must outlive this
// object; call shutdown() before destroying the Device. Never use it for
// authoritative simulation state.
class RenderTargetSet {
public:
    struct Config {
        int width = 0, height = 0;
        TextureDimension dimension = TextureDimension::Texture2D;
        int layers = 1;       // 1 for 2D, 6 for Cube, 6*n for CubeArray
        int mipLevels = 1;    // mips are manually rendered, not auto-generated
        int sampleCount = 1;  // when >1 each color gets a single-sample resolve
        int colorCount = 1;   // 0..4; 0 requires depth=true
        PixelFormat colorFormat = PixelFormat::RGBA8;
        // When enabled, each output has its own explicitly selected format;
        // otherwise colorFormat applies to every output (legacy behavior).
        bool perAttachmentFormats = false;
        std::array<PixelFormat, kMaxRenderTargetColors> colorFormats{
            PixelFormat::RGBA8, PixelFormat::RGBA8,
            PixelFormat::RGBA8, PixelFormat::RGBA8
        };
        bool depth = false;
    };

    RenderTargetSet() = default;
    ~RenderTargetSet() { shutdown(); }
    RenderTargetSet(const RenderTargetSet&) = delete;
    RenderTargetSet& operator=(const RenderTargetSet&) = delete;
    RenderTargetSet(RenderTargetSet&&) = delete;
    RenderTargetSet& operator=(RenderTargetSet&&) = delete;

    bool init(Device& device, const Config& config) {
        shutdown();
        if (!device.supportsRenderTargetPasses()) {
            error_ = "device does not support explicit render-target passes";
            return false;
        }
        if (config.width < 1 || config.height < 1 ||
            config.mipLevels < 1 || config.colorCount < 0 ||
            config.colorCount > kMaxRenderTargetColors ||
            (config.colorCount == 0 && !config.depth) ||
            (config.dimension != TextureDimension::Texture2D &&
             config.dimension != TextureDimension::Texture2DArray &&
             config.dimension != TextureDimension::Cube &&
             config.dimension != TextureDimension::CubeArray)) {
            error_ = "invalid render-target set configuration";
            return false;
        }
        TextureDesc td;
        td.width = config.width;
        td.height = config.height;
        td.dimension = config.dimension;
        td.arrayLayers = config.layers;
        td.mipLevels = config.mipLevels;
        td.renderTarget = true;
        td.sampleCount = config.sampleCount;
        td.format = config.perAttachmentFormats ?
                    config.colorFormats[0] : config.colorFormat;
        if (config.sampleCount > 1 && (config.dimension != TextureDimension::Texture2D ||
                                       config.mipLevels != 1)) {
            error_ = "multisampled render-target sets are currently 2D with one mip";
            return false;
        }
        // Validate every requested color slot up front, before any GPU
        // allocations. Reject unsupported compressed/typed color formats
        // rather than accidentally allocating them as RGBA8.
        for (int i = 0; i < config.colorCount; ++i) {
            td.format = config.perAttachmentFormats ?
                        config.colorFormats[size_t(i)] : config.colorFormat;
            if ((td.format != PixelFormat::RGBA8 &&
                 td.format != PixelFormat::RGBA8_sRGB &&
                 td.format != PixelFormat::RGBA16F) ||
                validateTextureDesc(td) != TextureDescError::None ||
                !device.supports(td.format)) {
                error_ = "unsupported render-target color format or descriptor";
                return false;
            }
        }
        if (config.colorCount == 0) {
            td.format = PixelFormat::Depth;
            if (validateTextureDesc(td) != TextureDescError::None) {
                error_ = "invalid depth-only render target descriptor";
                return false;
            }
        }

        std::vector<TextureHandle> color;
        std::vector<TextureHandle> resolve;
        TextureHandle depth;
        auto rollback = [&] {
            for (auto h : color) device.destroyTexture(h);
            for (auto h : resolve) device.destroyTexture(h);
            if (depth.valid()) device.destroyTexture(depth);
        };
        for (int i = 0; i < config.colorCount; ++i) {
            td.format = config.perAttachmentFormats ?
                        config.colorFormats[size_t(i)] : config.colorFormat;
            TextureHandle handle = device.createTexture(td);
            if (!handle.valid()) {
                error_ = device.lastError();
                rollback();
                return false;
            }
            color.push_back(handle);
            if (config.sampleCount > 1) {
                TextureDesc single = td;
                single.sampleCount = 1;
                TextureHandle destination = device.createTexture(single);
                if (!destination.valid()) {
                    error_ = device.lastError();
                    rollback();
                    return false;
                }
                resolve.push_back(destination);
            }
        }
        if (config.depth) {
            TextureDesc z = td;
            z.format = PixelFormat::Depth;
            z.compare = false;
            depth = device.createTexture(z);
            if (!depth.valid()) {
                error_ = device.lastError();
                rollback();
                return false;
            }
        }
        device_ = &device;
        config_ = config;
        colors_ = std::move(color);
        resolves_ = std::move(resolve);
        depth_ = depth;
        error_.clear();
        return true;
    }

    bool begin(int mipLevel = 0, int arrayLayer = 0,
               bool clearColor = true, bool clearDepth = true,
               std::array<float, 4> rgba = {0, 0, 0, 0},
               float depthClear = 1.0f) {
        if (!device_ || active_ || mipLevel < 0 || mipLevel >= config_.mipLevels ||
            arrayLayer < 0 || arrayLayer >= config_.layers) return false;
        RenderTargetPassDesc pass;
        pass.colorCount = int(colors_.size());
        for (int i = 0; i < pass.colorCount; ++i) {
            auto& a = pass.colors[size_t(i)];
            a.texture = colors_[size_t(i)];
            a.mipLevel = mipLevel;
            a.arrayLayer = arrayLayer;
            a.clear = clearColor;
            for (int k = 0; k < 4; ++k) a.clearColor[k] = rgba[size_t(k)];
            if (!resolves_.empty()) {
                a.resolveTexture = resolves_[size_t(i)];
                a.resolveMipLevel = 0;
                a.resolveArrayLayer = arrayLayer;
            }
        }
        if (depth_.valid()) {
            pass.depth.texture = depth_;
            pass.depth.mipLevel = mipLevel;
            pass.depth.arrayLayer = arrayLayer;
            pass.depth.clear = clearDepth;
            pass.clearDepth = depthClear;
        }
        // No partially active state is published when the device rejects an MRT
        // or MSAA request (e.g. GLES3 with no multisample texture support).
        if (!device_->beginRenderTargets(pass)) return false;
        active_ = true;
        return true;
    }

    void end() {
        if (active_ && device_) device_->endPass();
        active_ = false;
    }
    void shutdown() {
        end();
        if (device_) {
            for (TextureHandle h : colors_) device_->destroyTexture(h);
            for (TextureHandle h : resolves_) device_->destroyTexture(h);
            if (depth_.valid()) device_->destroyTexture(depth_);
        }
        colors_.clear();
        resolves_.clear();
        depth_ = {};
        device_ = nullptr;
        config_ = {};
    }

    TextureHandle color(int index) const {
        return index >= 0 && size_t(index) < colors_.size() ? colors_[size_t(index)] : TextureHandle{};
    }
    // MSAA outputs are resolved automatically at endPass; sample this handle.
    TextureHandle sampledColor(int index) const {
        if (index < 0 || size_t(index) >= colors_.size()) return {};
        return resolves_.empty() ? colors_[size_t(index)] : resolves_[size_t(index)];
    }
    TextureHandle depth() const { return depth_; }
    PixelFormat colorFormat(int index) const {
        if (index < 0 || index >= config_.colorCount) return PixelFormat::Depth;
        return config_.perAttachmentFormats ?
               config_.colorFormats[size_t(index)] : config_.colorFormat;
    }
    const Config& config() const { return config_; }
    const std::string& lastError() const { return error_; }
    bool active() const { return active_; }

private:
    Device* device_ = nullptr;
    Config config_;
    std::vector<TextureHandle> colors_;
    std::vector<TextureHandle> resolves_;
    TextureHandle depth_;
    bool active_ = false;
    std::string error_;
};

} // namespace aiverse::gfx
