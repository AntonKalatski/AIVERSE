// AIVERSE texture mip residency planning; CPU-only presentation policy.
// Request the coarsest mip of every layer/face first. Never expose a sampler
// that can select a mip unless all its layers are resident. No GPU I/O here.
#pragma once

#include "Gfx.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace aiverse::gfx {

struct TextureMipRequest {
    int mip = -1;
    int layer = -1;
    uint64_t generation = 0;
    bool valid() const { return mip >= 0 && layer >= 0 && generation != 0; }
};

// Thread affinity: owned by the render/streaming scheduler thread.
// It does not own GPU textures or CPU bytes. Use TextureStreamQueue to stage
// actual subresource uploads, then commit each request on GPU upload success.
class TextureMipResidency {
public:
    bool initialize(const TextureDesc& texture) {
        reset();
        if (validateTextureDesc(texture) != TextureDescError::None ||
            texture.sampleCount != 1 || texture.renderTarget || texture.storage)
            return false;
        const int n = textureEffectiveMipLevels(texture);
        const int layers = texture.dimension == TextureDimension::Texture3D ?
                           1 : texture.arrayLayers;
        constexpr int kMaxTrackedSlices = 1 << 20;
        if (n < 1 || layers < 1 || n > kMaxTrackedSlices ||
            layers > kMaxTrackedSlices / n) return false;
        mipLevels_ = n;
        layers_ = layers;
        dimension_ = texture.dimension;
        states_.assign(size_t(mipLevels_) * size_t(layers_), 0);
        return true;
    }

    void reset() {
        mipLevels_ = 0;
        layers_ = 0;
        dimension_ = TextureDimension::Texture2D;
        states_.clear();
        if (generation_ == std::numeric_limits<uint64_t>::max())
            generation_ = 1;
        else ++generation_;
    }

    TextureMipRequest claimNext() {
        // Always enqueue full coarse mips before finer ones. For arrays and
        // cubemaps, every face must be uploaded before that mip is sampleable.
        for (int mip = mipLevels_ - 1; mip >= 0; --mip) {
            for (int layer = 0; layer < layers_; ++layer) {
                uint8_t& state = states_[index(mip, layer)];
                if (state != 0) continue;
                state = 1; // in flight
                return {mip, layer, generation_};
            }
        }
        return {};
    }

    bool complete(TextureMipRequest request, bool success) {
        if (!request.valid() || request.generation != generation_ ||
            !validSlice(request.mip, request.layer)) return false;
        uint8_t& state = states_[index(request.mip, request.layer)];
        if (state != 1) return false;
        state = success ? 2 : 0; // failure can be retried
        return true;
    }

    bool invalidate(int mip, int layer) {
        if (!validSlice(mip, layer)) return false;
        states_[index(mip, layer)] = 0;
        return true;
    }

    bool completeMip(int mip) const {
        if (mip < 0 || mip >= mipLevels_) return false;
        for (int layer = 0; layer < layers_; ++layer)
            if (states_[index(mip, layer)] != 2) return false;
        return true;
    }

    // Finest mip in the contiguous *resident* coarse-to-fine range.
    // Returns mipLevels() when nothing may be safely sampled yet.
    int finestResidentMip() const {
        int finest = mipLevels_;
        for (int mip = mipLevels_ - 1; mip >= 0; --mip) {
            if (!completeMip(mip)) break;
            finest = mip;
        }
        return finest;
    }
    bool sampleable() const { return mipLevels_ > 0 && finestResidentMip() < mipLevels_; }
    bool fullyResident() const { return mipLevels_ > 0 && finestResidentMip() == 0; }
    int mipLevels() const { return mipLevels_; }
    int layerCount() const { return layers_; }
    TextureDimension dimension() const { return dimension_; }

    // Clamp independent sampler LOD to exactly the complete resident range.
    // The renderer must NOT bind this texture for sampling when false.
    bool residentSampler(SamplerDesc base, SamplerDesc& out) const {
        if (!sampleable() || !validateSamplerDesc(base)) return false;
        out = base;
        out.minLod = float(finestResidentMip());
        out.maxLod = float(mipLevels_ - 1);
        return true;
    }

    size_t pendingCount() const {
        return size_t(std::count(states_.begin(), states_.end(), uint8_t(1)));
    }
    size_t residentSliceCount() const {
        return size_t(std::count(states_.begin(), states_.end(), uint8_t(2)));
    }

private:
    bool validSlice(int mip, int layer) const {
        return mip >= 0 && mip < mipLevels_ && layer >= 0 && layer < layers_;
    }
    size_t index(int mip, int layer) const {
        return size_t(mip) * size_t(layers_) + size_t(layer);
    }

    uint64_t generation_ = 1;
    int mipLevels_ = 0, layers_ = 0;
    TextureDimension dimension_ = TextureDimension::Texture2D;
    std::vector<uint8_t> states_; // 0 absent, 1 claimed/in-flight, 2 resident
};

} // namespace aiverse::gfx
