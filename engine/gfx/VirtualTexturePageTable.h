// Virtual texture page-table CPU packing and bounded RHI upload.
// Each texel addresses a virtual mip-zero tile and maps it to a resident
// physical page slice plus its fallback mip. RGBA8 bytes are [slotLo,
// slotHi, physicalMip, valid]. The GPU shader must sample the page table
// with texelFetch / nearest sampling, never bilinear filtering.
#pragma once

#include "VirtualTexturePages.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace aiverse::gfx {

class VirtualTexturePageTable {
public:
    bool initialize(int mipZeroTilesX, int mipZeroTilesY,
                    int mipLevels, int virtualLayers) {
        clear();
        constexpr size_t kMaxCells = size_t(8) << 20; // <=32 MiB CPU table
        if (mipZeroTilesX < 1 || mipZeroTilesY < 1 ||
            mipLevels < 1 || mipLevels > 30 || virtualLayers < 1 ||
            size_t(mipZeroTilesX) > kMaxCells / size_t(mipZeroTilesY) ||
            size_t(mipZeroTilesX) * size_t(mipZeroTilesY) >
                kMaxCells / size_t(virtualLayers) ||
            size_t(mipZeroTilesX) > size_t(std::numeric_limits<int>::max()) / 4)
            return false;
        width_ = mipZeroTilesX;
        height_ = mipZeroTilesY;
        levels_ = mipLevels;
        layers_ = virtualLayers;
        pixels_.assign(size_t(width_) * size_t(height_) * size_t(layers_) * 4, 0);
        dirtyRows_.assign(size_t(height_) * size_t(layers_), uint8_t(1));
        return true;
    }

    TextureDesc textureDescriptor() const {
        TextureDesc d;
        if (width_ < 1) return d;
        d.width = width_;
        d.height = height_;
        d.dimension = TextureDimension::Texture2DArray;
        d.arrayLayers = layers_;
        d.mipLevels = 1;
        d.format = PixelFormat::RGBA8;
        d.filter = Filter::Nearest;
        return d;
    }

    // Opt-in convenience: create an initialized page-table texture. The GPU
    // handle is owned by the caller; the table never destroys it.
    TextureHandle createGpuTexture(Device& gpu) {
        if (pixels_.empty() || !gpu.supportsTextureDimension(TextureDimension::Texture2DArray))
            return {};
        TextureDesc d = textureDescriptor();
        d.data = pixels_.data();
        TextureHandle result = gpu.createTexture(d);
        if (result.valid()) std::fill(dirtyRows_.begin(), dirtyRows_.end(), uint8_t(0));
        return result;
    }

    // Read stable CPU page cache mappings and encode nearest resident parent
    // for every mip-zero virtual tile in each logical texture layer.
    size_t rebuild(const VirtualTexturePageCache& cache) {
        if (pixels_.empty() || cache.baseTilesX() != width_ ||
            cache.baseTilesY() != height_ ||
            cache.mipLevels() != levels_ || cache.virtualLayers() != layers_)
            return 0;
        size_t changed = 0;
        for (int layer = 0; layer < layers_; ++layer)
            for (int y = 0; y < height_; ++y)
                for (int x = 0; x < width_; ++x) {
                    VirtualPageAllocation resolved;
                    int residentMip = 0;
                    for (int mip = 0; mip < levels_; ++mip) {
                        resolved = cache.resolve({mip, x >> mip, y >> mip, layer});
                        if (resolved.valid()) { residentMip = mip; break; }
                    }
                    std::array<uint8_t,4> value{};
                    if (resolved.valid() && resolved.physicalSlice < 65536) {
                        value[0] = uint8_t(resolved.physicalSlice & 255);
                        value[1] = uint8_t((resolved.physicalSlice >> 8) & 255);
                        value[2] = uint8_t(residentMip);
                        value[3] = 255;
                    }
                    const size_t index = (size_t(layer) * size_t(height_) *
                                          size_t(width_) + size_t(y) *
                                          size_t(width_) + size_t(x)) * 4;
                    if (std::memcmp(pixels_.data() + index, value.data(), 4) != 0) {
                        std::memcpy(pixels_.data() + index, value.data(), 4);
                        dirtyRows_[size_t(layer) * size_t(height_) + size_t(y)] = 1;
                        ++changed;
                    }
                }
        return changed;
    }

    // Upload only dirty rows with a caller-controlled byte budget. If the
    // backend rejects an update, the corresponding rows remain dirty for
    // retry. Must be called on the graphics device thread.
    size_t flush(Device& gpu, TextureHandle texture, size_t maxBytes) {
        if (!texture.valid() || width_ < 1 || height_ < 1) {
            error_ = "invalid page-table resource or dimensions";
            return 0;
        }
        const size_t bytesPerRow = size_t(width_) * 4;
        if (maxBytes < bytesPerRow) {
            error_ = "page-table budget smaller than a complete row";
            return 0;
        }
        size_t transferred = 0;
        const size_t limitRows = maxBytes / bytesPerRow;
        for (int layer = 0; layer < layers_; ++layer) {
            int row = 0;
            while (row < height_) {
                const size_t idx = size_t(layer) * size_t(height_) + size_t(row);
                if (!dirtyRows_[idx]) { ++row; continue; }
                const int begin = row;
                while (row < height_ &&
                       dirtyRows_[size_t(layer) * size_t(height_) + size_t(row)] &&
                       size_t(row - begin) < limitRows - transferred / bytesPerRow)
                    ++row;
                const int count = row - begin;
                if (count == 0) return transferred;
                TextureUploadDesc upload;
                upload.mipLevel = 0;
                upload.arrayLayer = layer;
                upload.x = 0;
                upload.y = begin;
                upload.width = width_;
                upload.height = count;
                upload.rowPitchBytes = int(bytesPerRow);
                upload.slicePitchBytes = int(bytesPerRow * size_t(count));
                upload.data = pixels_.data() +
                    (size_t(layer) * size_t(height_) + size_t(begin)) * bytesPerRow;
                if (!gpu.updateTextureRegion(texture, upload)) {
                    error_ = gpu.lastError();
                    if (error_.empty()) error_ = "page-table GPU update failed";
                    return transferred;
                }
                for (int y = begin; y < row; ++y)
                    dirtyRows_[size_t(layer) * size_t(height_) + size_t(y)] = 0;
                transferred += size_t(count) * bytesPerRow;
                if (transferred == maxBytes ||
                    limitRows == transferred / bytesPerRow) {
                    error_.clear();
                    return transferred;
                }
            }
        }
        error_.clear();
        return transferred;
    }

    // CPU-only inspection, useful for editor debugging and test fixtures.
    std::array<uint8_t,4> entry(int x, int y, int layer) const {
        if (x < 0 || y < 0 || layer < 0 ||
            x >= width_ || y >= height_ || layer >= layers_) return {};
        const size_t index = (size_t(layer) * size_t(height_) *
                              size_t(width_) + size_t(y) *
                              size_t(width_) + size_t(x)) * 4;
        return {pixels_[index], pixels_[index+1], pixels_[index+2], pixels_[index+3]};
    }
    size_t pendingRows() const {
        return size_t(std::count(dirtyRows_.begin(), dirtyRows_.end(), uint8_t(1)));
    }
    int width() const { return width_; }
    int height() const { return height_; }
    int layers() const { return layers_; }
    const std::string& lastError() const { return error_; }
    void clear() {
        pixels_.clear();
        dirtyRows_.clear();
        width_ = height_ = levels_ = layers_ = 0;
        error_.clear();
    }

private:
    int width_ = 0, height_ = 0, levels_ = 0, layers_ = 0;
    std::vector<uint8_t> pixels_;
    std::vector<uint8_t> dirtyRows_;
    std::string error_;
};

} // namespace aiverse::gfx
