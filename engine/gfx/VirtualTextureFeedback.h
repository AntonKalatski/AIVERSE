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

// Production-ready fragment source assembly for the generic feedback pass.
// Callers provide a vertex shader that outputs vVirtualUV at location 0,
// and bind the uniforms below with their existing material binding path.
// The fragment shader is deliberately standalone and contains no genre,
// terrain, camera or ECS dependency. It computes derivatives before any
// conditional page rejection. Render with depth testing matching the
// camera visibility pass, opaque blending and linear RGBA8 output.
inline std::string virtualTextureFeedbackFragmentGLSL(bool automaticMip=true) {
    std::string shader =
        "precision highp float;\nprecision highp int;\n"
        "in vec2 vVirtualUV;\n"
        "layout(location=0) out vec4 outFeedback;\n"
        "uniform ivec2 uVirtualBaseTiles;\n"
        "uniform int uVirtualLayer;\n"
        "uniform int uVirtualMip;\n"
        "uniform int uVirtualMipLevels;\n"
        "uniform vec2 uVirtualTexels;\n"
        "uniform float uVirtualMipBias;\n";
    shader += kVirtualTextureFeedbackGLSL;
    shader += kVirtualTextureFeedbackMaterialGLSL;
    if (automaticMip) shader += kVirtualTextureFeedbackAutoMipGLSL;
    shader += automaticMip
        ? "void main(){\n"
          " int mip=aiverseFeedbackMipFromDerivatives("
          "vVirtualUV,uVirtualTexels,uVirtualMipLevels,uVirtualMipBias);\n"
          " outFeedback=aiverseMaterialFeedback(vVirtualUV,mip,"
          "uVirtualLayer,uVirtualBaseTiles);\n}\n"
        : "void main(){\n"
          " outFeedback=aiverseMaterialFeedback(vVirtualUV,uVirtualMip,"
          "uVirtualLayer,uVirtualBaseTiles);\n}\n";
    return shader;
}

// Metal source fragment entry point. Use with a vertex stage that writes
// UV at user(locn0); set one constant buffer containing the parameters
// below at fragment buffer(0). The host must match Metal's natural
// float2/int2 alignment when uploading this struct.
inline std::string virtualTextureFeedbackFragmentMSL(bool automaticMip=true) {
    std::string shader =
        "#include <metal_stdlib>\nusing namespace metal;\n"
        "struct AiverseFeedbackParams {\n"
        " int2 baseTiles; int layer; int mip;\n"
        " int mipLevels; float2 texels; float mipBias;\n"
        "};\n"
        "struct AiverseFeedbackInput {\n"
        " float4 position [[position]];\n"
        " float2 uv [[user(locn0)]];\n"
        "};\n";
    shader += kVirtualTextureFeedbackMSL;
    shader += kVirtualTextureFeedbackMaterialMSL;
    if (automaticMip) shader += kVirtualTextureFeedbackAutoMipMSL;
    shader += automaticMip
        ? "fragment float4 aiverseVirtualFeedbackFragment("
          "AiverseFeedbackInput in [[stage_in]],"
          "constant AiverseFeedbackParams& p [[buffer(0)]]) {\n"
          " int mip=aiverse_feedback_mip_from_derivatives("
          "in.uv,p.texels,p.mipLevels,p.mipBias);\n"
          " return aiverse_material_feedback(in.uv,mip,p.layer,p.baseTiles);\n}\n"
        : "fragment float4 aiverseVirtualFeedbackFragment("
          "AiverseFeedbackInput in [[stage_in]],"
          "constant AiverseFeedbackParams& p [[buffer(0)]]) {\n"
          " return aiverse_material_feedback(in.uv,p.mip,p.layer,p.baseTiles);\n}\n";
    return shader;
}

// Alpha-tested feedback uses the same base-color alpha test as the
// material pass. UV transform (including rotation) is supplied as two
// affine rows so the caller can reproduce KHR_texture_transform exactly.
// Virtual page UV is deliberately independent from base-color UV.
inline std::string virtualTextureMaskedFeedbackFragmentGLSL(
    bool automaticMip=true) {
    std::string shader=virtualTextureFeedbackFragmentGLSL(automaticMip);
    const size_t main=shader.find("void main(){");
    if (main==std::string::npos) return {};
    shader.erase(main);
    shader +=
        "uniform sampler2D uBaseColor;\n"
        "uniform vec3 uAlphaUvRow0;\n"
        "uniform vec3 uAlphaUvRow1;\n"
        "uniform float uBaseAlphaFactor;\n"
        "uniform float uAlphaCutoff;\n"
        "void main(){\n";
    if (automaticMip)
        shader +=
            " int mip=aiverseFeedbackMipFromDerivatives("
            "vVirtualUV,uVirtualTexels,uVirtualMipLevels,uVirtualMipBias);\n";
    shader +=
        " vec3 uvh=vec3(vVirtualUV,1.0);\n"
        " vec2 alphaUv=vec2(dot(uAlphaUvRow0,uvh),"
        "dot(uAlphaUvRow1,uvh));\n"
        " float alpha=texture(uBaseColor,alphaUv).a*uBaseAlphaFactor;\n"
        " if(alpha<uAlphaCutoff) discard;\n"
        " outFeedback=aiverseMaterialFeedback(vVirtualUV,";
    shader += automaticMip ? "mip" : "uVirtualMip";
    shader += ",uVirtualLayer,uVirtualBaseTiles);\n}\n";
    return shader;
}

// Metal: base-color texture and sampler are explicit bindings. The
// material binder must provide identical sampler wrap/filter state and
// alpha UV transform to the primary material pass.
inline std::string virtualTextureMaskedFeedbackFragmentMSL(
    bool automaticMip=true) {
    std::string shader=virtualTextureFeedbackFragmentMSL(automaticMip);
    const size_t entry=shader.find("fragment float4 aiverseVirtualFeedbackFragment(");
    if (entry==std::string::npos) return {};
    shader.erase(entry);
    shader +=
        "struct AiverseAlphaFeedbackParams {\n"
        " float3 uvRow0; float3 uvRow1;\n"
        " float alphaFactor; float alphaCutoff;\n"
        "};\n"
        "fragment float4 aiverseVirtualFeedbackFragment(\n"
        " AiverseFeedbackInput in [[stage_in]],\n"
        " constant AiverseFeedbackParams& p [[buffer(0)]],\n"
        " constant AiverseAlphaFeedbackParams& a [[buffer(1)]],\n"
        " texture2d<float> baseColor [[texture(0)]],\n"
        " sampler baseSampler [[sampler(0)]]) {\n";
    if (automaticMip)
        shader +=
            " int mip=aiverse_feedback_mip_from_derivatives("
            "in.uv,p.texels,p.mipLevels,p.mipBias);\n";
    shader +=
        " float3 uvh=float3(in.uv,1.0);\n"
        " float2 alphaUv=float2(dot(a.uvRow0,uvh),"
        "dot(a.uvRow1,uvh));\n"
        " float alpha=baseColor.sample(baseSampler,alphaUv).a*a.alphaFactor;\n"
        " if(alpha<a.alphaCutoff) discard_fragment();\n"
        " return aiverse_material_feedback(in.uv,";
    shader += automaticMip ? "mip" : "p.mip";
    shader += ",p.layer,p.baseTiles);\n}\n";
    return shader;
}

// Matches Renderer3D ModelShaders.h LODFADE 4x4 ordered screen-space
// coverage. Supply the same threshold/invert as the visible model pass.
// The feedback viewport must use the same pixel grid as the scene, or
// coverage will differ (downsampled feedback needs explicit coordinate map).
inline std::string virtualTextureLodFeedbackFragmentGLSL(
    bool automaticMip=true) {
    std::string shader=virtualTextureFeedbackFragmentGLSL(automaticMip);
    const size_t main=shader.find("void main(){");
    if (main==std::string::npos) return {};
    shader.erase(main);
    shader +=
        "uniform float uLodThreshold;\n"
        "uniform bool uLodInvert;\n"
        "void main(){\n";
    if (automaticMip)
        shader += " int mip=aiverseFeedbackMipFromDerivatives("
                  "vVirtualUV,uVirtualTexels,uVirtualMipLevels,uVirtualMipBias);\n";
    shader +=
        " ivec2 lp=ivec2(floor(gl_FragCoord.xy)) & ivec2(3);\n"
        " int rank=((lp.x & 1) ^ (lp.y & 1))*8+(lp.y & 1)*4+"
        "((lp.x>>1) ^ (lp.y>>1))*2+(lp.y>>1);\n"
        " float noise=(float(rank)+0.5)/16.0;\n"
        " if(uLodInvert ? noise<uLodThreshold : noise>=uLodThreshold) discard;\n"
        " outFeedback=aiverseMaterialFeedback(vVirtualUV,";
    shader += automaticMip ? "mip" : "uVirtualMip";
    shader += ",uVirtualLayer,uVirtualBaseTiles);\n}\n";
    return shader;
}

inline std::string virtualTextureLodFeedbackFragmentMSL(
    bool automaticMip=true) {
    std::string shader=virtualTextureFeedbackFragmentMSL(automaticMip);
    const size_t entry=shader.find("fragment float4 aiverseVirtualFeedbackFragment(");
    if (entry==std::string::npos) return {};
    shader.erase(entry);
    shader +=
        "struct AiverseLodFeedbackParams { float threshold; uint invert; };\n"
        "fragment float4 aiverseVirtualFeedbackFragment(\n"
        " AiverseFeedbackInput in [[stage_in]],\n"
        " constant AiverseFeedbackParams& p [[buffer(0)]],\n"
        " constant AiverseLodFeedbackParams& lod [[buffer(1)]]) {\n";
    if (automaticMip)
        shader += " int mip=aiverse_feedback_mip_from_derivatives("
                  "in.uv,p.texels,p.mipLevels,p.mipBias);\n";
    shader +=
        " int2 lp=int2(floor(in.position.xy)) & int2(3);\n"
        " int rank=((lp.x & 1) ^ (lp.y & 1))*8+(lp.y & 1)*4+"
        "((lp.x>>1) ^ (lp.y>>1))*2+(lp.y>>1);\n"
        " float noise=(float(rank)+0.5)/16.0;\n"
        " if(lod.invert!=0 ? noise<lod.threshold : noise>=lod.threshold)"
        " discard_fragment();\n"
        " return aiverse_material_feedback(in.uv,";
    shader += automaticMip ? "mip" : "p.mip";
    shader += ",p.layer,p.baseTiles);\n}\n";
    return shader;
}

// Combined alpha-test and ordered LOD coverage. Both rejection conditions
// must pass before a page ID is written. Derivatives are computed first.
inline std::string virtualTextureMaskedLodFeedbackFragmentGLSL(
    bool automaticMip=true) {
    std::string shader=virtualTextureMaskedFeedbackFragmentGLSL(automaticMip);
    const size_t main=shader.rfind("void main(){");
    if (main==std::string::npos) return {};
    shader.insert(main,
        "uniform float uLodThreshold;\n"
        "uniform bool uLodInvert;\n");
    const size_t alpha=shader.find(" if(alpha<uAlphaCutoff) discard;",main);
    if (alpha==std::string::npos) return {};
    const size_t after=alpha+std::string(" if(alpha<uAlphaCutoff) discard;\n").size();
    shader.insert(after,
        " ivec2 lp=ivec2(floor(gl_FragCoord.xy)) & ivec2(3);\n"
        " int rank=((lp.x & 1) ^ (lp.y & 1))*8+(lp.y & 1)*4+"
        "((lp.x>>1) ^ (lp.y>>1))*2+(lp.y>>1);\n"
        " float noise=(float(rank)+0.5)/16.0;\n"
        " if(uLodInvert ? noise<uLodThreshold : noise>=uLodThreshold) discard;\n");
    return shader;
}

inline std::string virtualTextureMaskedLodFeedbackFragmentMSL(
    bool automaticMip=true) {
    std::string shader=virtualTextureMaskedFeedbackFragmentMSL(automaticMip);
    const size_t entry=shader.find("fragment float4 aiverseVirtualFeedbackFragment(");
    if (entry==std::string::npos) return {};
    shader.insert(entry,
        "struct AiverseLodFeedbackParams { float threshold; uint invert; };\n");
    const size_t sampler=shader.find(" sampler baseSampler [[sampler(0)]]) {");
    if (sampler==std::string::npos) return {};
    shader.replace(sampler,
        std::string(" sampler baseSampler [[sampler(0)]]) {").size(),
        " sampler baseSampler [[sampler(0)]],\n"
        " constant AiverseLodFeedbackParams& lod [[buffer(2)]]) {");
    const size_t alpha=shader.find(" if(alpha<a.alphaCutoff) discard_fragment();");
    if (alpha==std::string::npos) return {};
    const size_t after=alpha+std::string(" if(alpha<a.alphaCutoff) discard_fragment();\n").size();
    shader.insert(after,
        " int2 lp=int2(floor(in.position.xy)) & int2(3);\n"
        " int rank=((lp.x & 1) ^ (lp.y & 1))*8+(lp.y & 1)*4+"
        "((lp.x>>1) ^ (lp.y>>1))*2+(lp.y>>1);\n"
        " float noise=(float(rank)+0.5)/16.0;\n"
        " if(lod.invert!=0 ? noise<lod.threshold : noise>=lod.threshold)"
        " discard_fragment();\n");
    return shader;
}

} // namespace aiverse::gfx
