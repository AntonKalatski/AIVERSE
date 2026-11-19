// AIVERSE virtual texture feedback color-buffer contract.
// A separate app shader writes lossless packed IDs into an RGBA8 UNORM
// offscreen target; Device::readTextureAsync returns the pixels later.
// CPU-only decoding is genre-independent and never calls gfx::Device.
#pragma once

#include "VirtualTexturePrefetch.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace aiverse::gfx {

// Fixed 32-bit feedback ID layout: bit 31 valid, bits 0..10 x,
// 11..21 y, 22..26 mip, 27..30 virtual layer. Supports at most
// 2048x2048 base tiles, 16 virtual layers and 31 mip levels.
struct VirtualTextureFeedbackConfig {
    int tilesX = 0, tilesY = 0, mipLevels = 0, virtualLayers = 1;
    size_t maxReadbackPixels = size_t(4) << 20;
    size_t maxUniquePages = 4096;
    size_t maxVisibleRegions = 256;
};
struct VirtualTextureFeedbackStats {
    size_t sampledPixels = 0;
    size_t invalidIds = 0;
    size_t uniquePages = 0;
    size_t omittedUniquePages = 0;
    size_t outputRegions = 0;
    size_t omittedRegions = 0;
};

// Zero marks an unused fragment: initialize the feedback target to RGBA(0).
// Clear blend, MSAA, color grading, sRGB, dithering, and postprocessing.
inline uint32_t encodeVirtualTextureFeedback(VirtualPageKey k) {
    if (k.x < 0 || k.x >= 2048 || k.y < 0 || k.y >= 2048 ||
        k.mip < 0 || k.mip >= 31 || k.layer < 0 || k.layer >= 16)
        return 0;
    return 0x80000000u | uint32_t(k.x) | (uint32_t(k.y)<<11) |
           (uint32_t(k.mip)<<22) | (uint32_t(k.layer)<<27);
}
inline VirtualPageKey decodeVirtualTextureFeedbackId(uint32_t word) {
    if ((word & 0x80000000u) == 0) return {};
    return {int((word>>22)&31u),int(word&2047u),
            int((word>>11)&2047u),int((word>>27)&15u)};
}

class VirtualTextureFeedbackDecoder {
public:
    bool initialize(VirtualTextureFeedbackConfig config) {
        reset();
        if (config.tilesX < 1 || config.tilesY < 1 ||
            config.tilesX > 2048 || config.tilesY > 2048 ||
            config.mipLevels < 1 || config.mipLevels > 31 ||
            config.virtualLayers < 1 || config.virtualLayers > 16 ||
            config.maxReadbackPixels < 1 ||
            config.maxReadbackPixels > (size_t(16) << 20) ||
            config.maxUniquePages < 1 || config.maxUniquePages > 65536 ||
            config.maxVisibleRegions < 1 ||
            config.maxVisibleRegions > config.maxUniquePages)
            return false;
        config_ = config;
        ready_ = true;
        return true;
    }
    void reset() { ready_=false;config_={};stats_={}; }
    bool ready() const {return ready_;}
    const VirtualTextureFeedbackStats& stats() const {return stats_;}

    // Convert an accepted (asynchronous) RHI color readback into per-page
    // visible regions for VirtualTexturePrefetchPlanner. Each unique page's
    // pixel frequency becomes its stable priority. The next step is:
    // prefetchVisibleVirtualPages(streamer,planner,regions).
    bool decode(const TextureReadbackResult& image,
                std::vector<VirtualTextureVisibleRegion>& out,
                std::string& error) {
        out.clear();
        stats_={};
        if (!ready_) {
            error="virtual texture feedback decoder was not initialized";
            return false;
        }
        if (image.format != PixelFormat::RGBA8 || image.width < 1 ||
            image.height < 1 ||
            size_t(image.width) >
                std::numeric_limits<size_t>::max()/size_t(image.height) ||
            size_t(image.width)*size_t(image.height)>config_.maxReadbackPixels ||
            image.pixels.size() !=
                size_t(image.width)*size_t(image.height)*4u) {
            error="feedback readback must be exact, bounded RGBA8 UNORM pixels";
            return false;
        }
        std::map<VirtualPageKey,uint32_t> counts;
        for(size_t i=0;i<image.pixels.size();i+=4) {
            ++stats_.sampledPixels;
            const uint32_t id=uint32_t(image.pixels[i]) |
                              (uint32_t(image.pixels[i+1])<<8) |
                              (uint32_t(image.pixels[i+2])<<16) |
                              (uint32_t(image.pixels[i+3])<<24);
            const VirtualPageKey key=decodeVirtualTextureFeedbackId(id);
            if (key.mip<0 || key.mip>=config_.mipLevels ||
                key.layer<0 || key.layer>=config_.virtualLayers) {
                ++stats_.invalidIds;
                continue;
            }
            const int stride=1 << key.mip;
            const int xCount=(config_.tilesX+stride-1)/stride;
            const int yCount=(config_.tilesY+stride-1)/stride;
            if (key.x>=xCount || key.y>=yCount) {
                ++stats_.invalidIds;
                continue;
            }
            auto it=counts.find(key);
            if (it != counts.end()) {
                if (it->second != std::numeric_limits<uint32_t>::max())
                    ++it->second;
            } else if (counts.size()<config_.maxUniquePages) {
                counts.emplace(key,1);
            } else ++stats_.omittedUniquePages;
        }
        stats_.uniquePages=counts.size();
        struct Ranked {VirtualPageKey key;uint32_t count;};
        std::vector<Ranked> ranked;
        ranked.reserve(counts.size());
        for(const auto& [key,count]:counts) ranked.push_back({key,count});
        std::sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b) {
            if(a.count!=b.count) return a.count>b.count;
            if(a.key.mip!=b.key.mip) return a.key.mip>b.key.mip;
            if(a.key.layer!=b.key.layer) return a.key.layer<b.key.layer;
            if(a.key.y!=b.key.y) return a.key.y<b.key.y;
            return a.key.x<b.key.x;
        });
        out.reserve(std::min(ranked.size(),config_.maxVisibleRegions));
        for(const auto& entry:ranked) {
            if(out.size()==config_.maxVisibleRegions) {
                ++stats_.omittedRegions;
                continue;
            }
            const int stride=1 << entry.key.mip;
            const int w=(config_.tilesX+stride-1)/stride;
            const int h=(config_.tilesY+stride-1)/stride;
            VirtualTextureVisibleRegion v;
            v.u0=double(entry.key.x)/double(w);
            v.v0=double(entry.key.y)/double(h);
            v.u1=double(entry.key.x+1)/double(w);
            v.v1=double(entry.key.y+1)/double(h);
            v.layer=entry.key.layer;
            v.desiredMip=entry.key.mip;
            v.priority=int(std::min<uint32_t>(
                entry.count,uint32_t(std::numeric_limits<int>::max())));
            out.push_back(v);
        }
        stats_.outputRegions=out.size();
        error.clear();
        return true;
    }
private:
    bool ready_=false;
    VirtualTextureFeedbackConfig config_;
    VirtualTextureFeedbackStats stats_;
};

// Match the shader page selection for material/terrain feedback. UVs are
// normalized, with the upper edge clamped to the last valid page. Reject
// invalid topology/coordinates rather than aliasing a different page.
inline VirtualPageKey virtualTextureFeedbackPageAtUV(
    double u, double v, int mip, int layer, int tilesX, int tilesY) {
    if (!std::isfinite(u) || !std::isfinite(v) ||
        u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0 ||
        mip < 0 || mip >= 31 || layer < 0 || layer >= 16 ||
        tilesX < 1 || tilesX > 2048 || tilesY < 1 || tilesY > 2048)
        return {};
    const int scale=1 << mip;
    const int nx=(tilesX+scale-1)/scale;
    const int ny=(tilesY+scale-1)/scale;
    return {mip,std::min(int(u*nx),nx-1),std::min(int(v*ny),ny-1),layer};
}

// Fragment helper functions: the caller must write the returned encoded
// RGBA bytes to a *linear RGBA8 UNORM* render target at location 0. An
// invalid pixel is vec4(0), not a decodable page. No blending/tone mapping.
// Append to a material feedback fragment shader along with the encoder.
// Explicit mip is supplied by the renderer/material LOD policy.
inline constexpr const char* kVirtualTextureFeedbackMaterialGLSL = R"VTF(
vec4 aiverseMaterialFeedback(vec2 uv, int mip, int layer, ivec2 baseTiles) {
    if (any(isnan(uv)) || any(isinf(uv)) ||
        any(lessThan(uv,vec2(0.0))) ||
        any(greaterThan(uv,vec2(1.0))) ||
        any(lessThanEqual(baseTiles,ivec2(0))) ||
        any(greaterThan(baseTiles,ivec2(2048))) ||
        mip<0 || mip>=31 || layer<0 || layer>=16)
        return vec4(0.0);
    int scale=1 << mip;
    ivec2 counts=(baseTiles+ivec2(scale-1))/scale;
    ivec2 tile=min(ivec2(floor(uv*vec2(counts))),counts-ivec2(1));
    return aiverseEncodeFeedback(ivec4(tile,mip,layer));
}
)VTF";

inline constexpr const char* kVirtualTextureFeedbackMaterialMSL = R"VTF(
inline float4 aiverse_material_feedback(float2 uv, int mip, int layer,
                                        int2 baseTiles) {
    if (!all(isfinite(uv)) ||
        any(uv<float2(0.0)) || any(uv>float2(1.0)) ||
        any(baseTiles<=int2(0)) || any(baseTiles>int2(2048)) ||
        mip<0 || mip>=31 || layer<0 || layer>=16)
        return float4(0.0);
    int scale=1 << mip;
    int2 counts=(baseTiles+int2(scale-1))/scale;
    int2 tile=min(int2(floor(uv*float2(counts))),counts-int2(1));
    return aiverse_encode_feedback(int4(tile,mip,layer));
}
)VTF";

// Optional fragment-only automatic mip policy. The caller supplies the
// virtual texture's logical texel extent, not the feedback target extent.
// The derivatives must be evaluated in uniform fragment control flow
// (before conditional discard/early return) to avoid undefined LOD.
// Bias permits coarser/finer streaming without changing the page codec.
inline constexpr const char* kVirtualTextureFeedbackAutoMipGLSL = R"VTF(
int aiverseFeedbackMipFromDerivatives(
    vec2 uv, vec2 virtualTexels, int mipLevels, float mipBias) {
    if (any(isnan(uv)) || any(isinf(uv)) ||
        any(isnan(virtualTexels)) || any(isinf(virtualTexels)) ||
        any(lessThanEqual(virtualTexels,vec2(0.0))) ||
        isnan(mipBias) || isinf(mipBias) ||
        mipLevels<1 || mipLevels>30) return -1;
    vec2 dx=dFdx(uv)*virtualTexels;
    vec2 dy=dFdy(uv)*virtualTexels;
    float footprint=max(dot(dx,dx),dot(dy,dy));
    if (isnan(footprint) || isinf(footprint)) return -1;
    float lod=0.5*log2(max(footprint,1.0))+mipBias;
    return int(floor(clamp(lod,0.0,float(mipLevels-1))));
}
)VTF";

inline constexpr const char* kVirtualTextureFeedbackAutoMipMSL = R"VTF(
inline int aiverse_feedback_mip_from_derivatives(
    float2 uv, float2 virtualTexels, int mipLevels, float mipBias) {
    if (!all(isfinite(uv)) || !all(isfinite(virtualTexels)) ||
        any(virtualTexels<=float2(0.0)) || !isfinite(mipBias) ||
        mipLevels<1 || mipLevels>30) return -1;
    float2 dx=dfdx(uv)*virtualTexels;
    float2 dy=dfdy(uv)*virtualTexels;
    float footprint=max(dot(dx,dx),dot(dy,dy));
    if (!isfinite(footprint)) return -1;
    float lod=0.5*log2(max(footprint,1.0))+mipBias;
    return int(floor(clamp(lod,0.0,float(mipLevels-1))));
}
)VTF";

inline constexpr const char* kVirtualTextureFeedbackGLSL = R"VTF(
vec4 aiverseEncodeFeedback(ivec4 page) {
    // page = (x,y,mip,virtualLayer); negatives mean invalid.
    if (any(lessThan(page,ivec4(0))) ||
        page.x >= 2048 || page.y >= 2048 || page.z >= 31 || page.w >= 16)
        return vec4(0.0);
    highp uint packed = 0x80000000u |
        uint(page.x) | (uint(page.y)<<11) |
        (uint(page.z)<<22) | (uint(page.w)<<27);
    return vec4(float(packed & 255u),
                float((packed>>8)&255u),
                float((packed>>16)&255u),
                float((packed>>24)&255u)) / 255.0;
}
)VTF";

inline constexpr const char* kVirtualTextureFeedbackMSL = R"VTF(
inline float4 aiverse_encode_feedback(int4 page) {
    if (any(page<0) || page.x>=2048 || page.y>=2048 ||
        page.z>=31 || page.w>=16) return float4(0.0);
    uint p = 0x80000000u | uint(page.x) | (uint(page.y)<<11) |
             (uint(page.z)<<22) | (uint(page.w)<<27);
    return float4(float(p&255u), float((p>>8)&255u),
                  float((p>>16)&255u), float((p>>24)&255u)) / 255.0;
}
)VTF";

} // namespace aiverse::gfx
