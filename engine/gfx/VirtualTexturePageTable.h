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
                    int mipLevels, int virtualLayers,
                    bool indexVirtualMipLevels = false) {
        clear();
        constexpr size_t kMaxCells = size_t(8) << 20; // <=32 MiB CPU table
        if (mipZeroTilesX < 1 || mipZeroTilesY < 1 ||
            mipLevels < 1 || mipLevels > 30 || virtualLayers < 1)
            return false;
        const int selectors=indexVirtualMipLevels ? mipLevels : 1;
        if (virtualLayers>std::numeric_limits<int>::max()/selectors ||
            size_t(mipZeroTilesX)>kMaxCells/size_t(mipZeroTilesY) ||
            size_t(mipZeroTilesX)*size_t(mipZeroTilesY)>
                kMaxCells/(size_t(virtualLayers)*size_t(selectors)) ||
            size_t(mipZeroTilesX)>size_t(std::numeric_limits<int>::max())/4)
            return false;
        width_ = mipZeroTilesX;
        height_ = mipZeroTilesY;
        levels_ = mipLevels;
        layers_ = virtualLayers;
        selectors_ = selectors;
        tableLayers_ = virtualLayers*selectors;
        pixels_.assign(size_t(width_) * size_t(height_) * size_t(tableLayers_) * 4, 0);
        dirtyRows_.assign(size_t(height_) * size_t(tableLayers_), uint8_t(1));
        return true;
    }

    TextureDesc textureDescriptor() const {
        TextureDesc d;
        if (width_ < 1) return d;
        d.width = width_;
        d.height = height_;
        d.dimension = TextureDimension::Texture2DArray;
        d.arrayLayers = tableLayers_;
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

    // Fast no-op on unchanged residency. On a few changed pages, update only
    // the affected mip-zero virtual rectangles. Excess changes/large coarse
    // roots transparently fall back to a bounded full rebuild.
    // Each visited base tile scans its nearest resident parents once,
    // from coarse to fine, updating every selectable mip slice.
    size_t rebuild(const VirtualTexturePageCache& cache) {
        if(pixels_.empty() || cache.baseTilesX()!=width_ ||
           cache.baseTilesY()!=height_ || cache.mipLevels()!=levels_ ||
           cache.virtualLayers()!=layers_) return 0;
        if(lastRevision_==cache.mappingRevision()) return 0;
        size_t changed=0;
        const auto visit=[&](int x,int y,int logicalLayer) {
            VirtualPageAllocation nearest;
            int residentMip=0;
            for(int requestedMip=levels_-1;requestedMip>=0;--requestedMip) {
                const auto candidate=cache.resolve(
                    {requestedMip,x>>requestedMip,y>>requestedMip,logicalLayer});
                if(candidate.valid()) {
                    nearest=candidate;
                    residentMip=requestedMip;
                }
                if(requestedMip>=selectors_) continue;
                const int physicalLayer=logicalLayer*selectors_+requestedMip;
                std::array<uint8_t,4> value{};
                if(nearest.valid() && nearest.physicalSlice<65536) {
                    value[0]=uint8_t(nearest.physicalSlice&255);
                    value[1]=uint8_t((nearest.physicalSlice>>8)&255);
                    value[2]=uint8_t(residentMip);
                    value[3]=255;
                }
                const size_t row=size_t(physicalLayer)*size_t(height_)+size_t(y);
                const size_t offset=(row*size_t(width_)+size_t(x))*4u;
                if(std::memcmp(pixels_.data()+offset,value.data(),4)!=0) {
                    std::memcpy(pixels_.data()+offset,value.data(),4);
                    dirtyRows_[row]=1;
                    ++changed;
                }
            }
        };
        struct Rect {
            int x0=0,y0=0,x1=0,y1=0,layer=0;
        };
        std::vector<VirtualPageKey> recent;
        std::vector<Rect> rectangles;
        bool partial=lastRevision_!=std::numeric_limits<uint64_t>::max() &&
            cache.mappingChangesSince(lastRevision_,recent,128);
        const size_t fullCells=size_t(width_)*size_t(height_)*size_t(layers_);
        size_t visitCount=0;
        if(partial) {
            rectangles.reserve(recent.size());
            for(const auto& key:recent) {
                const int64_t stride=int64_t(1)<<key.mip;
                const int64_t x0=int64_t(key.x)*stride;
                const int64_t y0=int64_t(key.y)*stride;
                const int64_t x1=(int64_t(key.x)+1)*stride;
                const int64_t y1=(int64_t(key.y)+1)*stride;
                if(key.layer<0 || key.layer>=layers_ || key.mip<0 ||
                   key.mip>=levels_ || x0>=width_ || y0>=height_ ||
                   x1<=0 || y1<=0) {partial=false;break;}
                Rect r;
                r.x0=int(std::max<int64_t>(0,x0));r.y0=int(std::max<int64_t>(0,y0));
                r.x1=int(std::min<int64_t>(width_,x1));
                r.y1=int(std::min<int64_t>(height_,y1));r.layer=key.layer;
                const size_t count=size_t(r.x1-r.x0)*size_t(r.y1-r.y0);
                if(count>fullCells/2 || visitCount>fullCells/2-count) {
                    partial=false;break;
                }
                visitCount+=count;
                rectangles.push_back(r);
            }
        }
        if(partial) {
            for(const Rect& r:rectangles)
                for(int y=r.y0;y<r.y1;++y)
                    for(int x=r.x0;x<r.x1;++x)
                        visit(x,y,r.layer);
        } else {
            for(int layer=0;layer<layers_;++layer)
                for(int y=0;y<height_;++y)
                    for(int x=0;x<width_;++x)
                        visit(x,y,layer);
        }
        lastRevision_=cache.mappingRevision();
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
        // TextureUploadDesc.slicePitchBytes is an int; keep every encoded
        // GPU upload within both the caller's budget and the API pitch range.
        const size_t limitRows = std::min(maxBytes / bytesPerRow,
            size_t(std::numeric_limits<int>::max()) / bytesPerRow);
        if (!limitRows) {
            error_ = "page-table slice pitch exceeds the RHI integer range";
            return 0;
        }
        for (int layer = 0; layer < tableLayers_; ++layer) {
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
    std::array<uint8_t,4> entry(int x,int y,int layer) const {
        return entryForMip(x,y,layer,0);
    }
    std::array<uint8_t,4> entryForMip(int x,int y,int logicalLayer,
                                     int requestedMip) const {
        if (x<0 || y<0 || logicalLayer<0 || requestedMip<0 ||
            x>=width_ || y>=height_ || logicalLayer>=layers_ ||
            requestedMip>=selectors_) return {};
        const int tableLayer=logicalLayer*selectors_+requestedMip;
        const size_t offset=(size_t(tableLayer)*size_t(height_)*size_t(width_)+
                             size_t(y)*size_t(width_)+size_t(x))*4u;
        return {pixels_[offset],pixels_[offset+1],pixels_[offset+2],pixels_[offset+3]};
    }
    size_t pendingRows() const {
        return size_t(std::count(dirtyRows_.begin(), dirtyRows_.end(), uint8_t(1)));
    }
    int width() const { return width_; }
    int height() const { return height_; }
    int layers() const { return layers_; }
    int selectableMipLevels() const { return selectors_; }
    int tableArrayLayers() const { return tableLayers_; }
    const std::string& lastError() const { return error_; }
    void clear() {
        pixels_.clear();
        dirtyRows_.clear();
        width_ = height_ = levels_ = layers_ = tableLayers_ = 0;
        selectors_ = 1;
        lastRevision_=std::numeric_limits<uint64_t>::max();
        error_.clear();
    }

private:
    int width_ = 0, height_ = 0, levels_ = 0, layers_ = 0;
    int selectors_ = 1, tableLayers_ = 0;
    uint64_t lastRevision_=std::numeric_limits<uint64_t>::max();
    std::vector<uint8_t> pixels_;
    std::vector<uint8_t> dirtyRows_;
    std::string error_;
};

} // namespace aiverse::gfx
