// Opt-in native model-material VT colour sampling, independent of genre.
// Slots 13/14 are free ONLY for built-in non-graph material variants;
// custom shader graphs already allocate tex13 and upwards.
// No effect on shadow, selection, postprocess or other shader pipelines.
#pragma once
#include "../gfx/Gfx.h"
#include "../gfx/VirtualTextureSampling.h"
#include "Model.h"
#include <cmath>
#include <bit>
#include <cstdint>
#include <string>

namespace aiverse::render {

// One VT domain per draw/material. The caller owns both textures and must
// keep them alive until in-flight GPU work is complete.
struct ModelVirtualTextureBinding {
    gfx::TextureHandle physicalAtlas;
    gfx::TextureHandle pageTable;
    int layer = 0;
    float gutterPixels = 2.0f;
    // Opt-in indexed virtual mip table. The resolver supplies original
    // logical virtual texture resolution (excluding page gutters), so
    // fragment derivatives select correct virtual LOD.
    bool indexedMipTable = false;
    int virtualMipLevels = 1;
    float virtualWidthPixels = 0, virtualHeightPixels = 0;
    float virtualMipBias = 0;
    int virtualAnisotropyTaps = 1; // 1/2/4/8, each tap resolves page independently
    // Match the original material's U/V sampler modes. ClampToBorder
    // cannot be represented by the current RGBA8 page-table layout.
    gfx::SamplerAddressMode addressU = gfx::SamplerAddressMode::ClampToEdge;
    gfx::SamplerAddressMode addressV = gfx::SamplerAddressMode::ClampToEdge;
    bool valid() const {
        return physicalAtlas.valid() && pageTable.valid() &&
               layer >= 0 && layer < 16 &&
               std::isfinite(gutterPixels) && gutterPixels >= 0 &&
               (!indexedMipTable ||
                (virtualMipLevels >= 1 && virtualMipLevels <= 30 &&
                 std::isfinite(virtualWidthPixels) && virtualWidthPixels > 0 &&
                 std::isfinite(virtualHeightPixels) && virtualHeightPixels > 0 &&
                 std::isfinite(virtualMipBias) &&
                 (virtualAnisotropyTaps==1 || virtualAnisotropyTaps==2 ||
                  virtualAnisotropyTaps==4 || virtualAnisotropyTaps==8))) &&
               (indexedMipTable || virtualAnisotropyTaps==1) &&
               addressU <= gfx::SamplerAddressMode::MirrorRepeat &&
               addressV <= gfx::SamplerAddressMode::MirrorRepeat;
    }
    bool operator==(const ModelVirtualTextureBinding&) const = default;
};

// External atlas identity is part of a GPU draw group. Source Material
// equivalence alone cannot compare archive generations or VT layers.
inline uint64_t modelVirtualTextureBindingHash(const ModelVirtualTextureBinding& b) {
    if (!b.valid()) return 0;
    uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](uint32_t n) {
        hash ^= uint64_t(n); hash *= 1099511628211ULL;
    };
    mix(b.physicalAtlas.id);
    mix(b.pageTable.id);
    mix(uint32_t(b.layer));
    mix(b.gutterPixels == 0 ? 0u : std::bit_cast<uint32_t>(b.gutterPixels));
    mix(uint32_t(b.addressU));
    mix(uint32_t(b.addressV));
    mix(uint32_t(b.indexedMipTable));
    if(b.indexedMipTable) {
        mix(uint32_t(b.virtualMipLevels));
        mix(std::bit_cast<uint32_t>(b.virtualWidthPixels));
        mix(std::bit_cast<uint32_t>(b.virtualHeightPixels));
        mix(b.virtualMipBias == 0 ? 0u : std::bit_cast<uint32_t>(b.virtualMipBias));
        mix(uint32_t(b.virtualAnisotropyTaps));
    }
    return hash;
}
inline bool compatibleModelVirtualTextureBinding(
    bool leftEnabled, const ModelVirtualTextureBinding& left,
    bool rightEnabled, const ModelVirtualTextureBinding& right) {
    return leftEnabled == rightEnabled &&
           (!leftEnabled || left == right);
}

struct ModelVirtualTextureUniforms {
    float layer = 0, gutterPixels = 2, addressU = 0, addressV = 0;
};
static_assert(sizeof(ModelVirtualTextureUniforms) == 16);
struct ModelVirtualTextureTrilinearUniforms {
    ModelVirtualTextureUniforms base;
    float widthPixels = 0, heightPixels = 0, mipLevels = 0, mipBias = 0;
    float anisotropyTaps = 1, reserved0 = 0, reserved1 = 0, reserved2 = 0;
};
static_assert(sizeof(ModelVirtualTextureTrilinearUniforms) == 48);

// Shared expression generator for built-in base colour and Shader Graph
// SG_SAMPLE_VIRTUAL. Do not diverge the two paths' page-table/mip semantics.
inline std::string modelVirtualSampleExpression(
    bool metal,bool trilinear,const std::string& atlas,
    const std::string& table,const std::string& uv,
    const std::string& fallback) {
    const std::string coord="("+uv+")";
    if(!trilinear) {
        return metal ?
            "aiverse_sample_virtual("+atlas+","+table+",s1,"+coord+
              ",uint(uModelVt.x),uModelVt.y,"+fallback+
              ",uint(uModelVt.z),uint(uModelVt.w))" :
            "aiverseSampleVirtual("+atlas+","+table+","+coord+
              ",int(uModelVt.x),uModelVt.y,"+fallback+
              ",int(uModelVt.z),int(uModelVt.w))";
    }
    const std::string dx=(metal?"dfdx(":"dFdx(")+coord+")";
    const std::string dy=(metal?"dfdy(":"dFdy(")+coord+")";
    return metal ?
       "aiverse_sample_virtual_anisotropic("+atlas+","+table+",s1,"+coord+
         ",uint(uModelVt.x),uModelVt.y,"+fallback+","+dx+","+dy+
         ",uModelVtExtra.xy,uModelVtExtra.w,uint(uModelVtExtra.z),"+
         "uint(uModelVtQuality.x),uint(uModelVt.z),uint(uModelVt.w))" :
       "aiverseSampleVirtualAnisotropic("+atlas+","+table+","+coord+
         ",int(uModelVt.x),uModelVt.y,"+fallback+","+dx+","+dy+
         ",uModelVtExtra.xy,uModelVtExtra.w,int(uModelVtExtra.z),"+
         "int(uModelVtQuality.x),int(uModelVt.z),int(uModelVt.w))";
}

// Source graph samplers occupy slots 13..16. Metal's RHI has 32 fragment
// texture slots (but 16 dynamic samplers); graph slot 16 already uses a
// constexpr sampler. The VT atlas/table use the existing s1 sampler and
// a texel read, respectively, so they can occupy slots 17/18 on Metal.
// GL33/GLES3/Vulkan remain limited to resource indices 0..15.
inline bool supportsModelGraphSourceTextures(const gfx::Device& dev,
                                             size_t sources) {
    return sources <= 4 &&
           13 + int(sources) <= dev.maxFragmentTextureSlots();
}
inline bool modelGraphVirtualTextureSlots(const gfx::Device& dev,
                                          size_t sources,
                                          int& atlas, int& table) {
    atlas = 0;
    table = 0;
    if (!supportsModelGraphSourceTextures(dev, sources)) return false;
    const int next = 13+int(sources);
    if (next+1 >= dev.maxFragmentTextureSlots()) return false;
    atlas = next;
    table = next+1;
    return true;
}

// Reuse the canonical virtual page-table shader implementation; do not
// implement another page lookup with different mip/edge semantics.
// Returns false if a shader source contract changes: never silently
// generate a broken pipeline.
inline bool injectModelVirtualTextureSampling(std::string& fragmentGLSL,
                                              std::string& msl,
                                              int atlasSlot = 13,
                                              int tableSlot = 14,
                                              bool extendedMetalSlots = false,
                                              bool trilinear = false) {
    // Only Metal can use the graph's tex16 followed by tex17/18
    // for VT resources; other backends must not overflow 16 slots.
    if (atlasSlot < 13 || tableSlot != atlasSlot+1 ||
        tableSlot > (extendedMetalSlots ? 18 : 15)) return false;
    const std::string atlasName="tex"+std::to_string(atlasSlot);
    const std::string tableName="tex"+std::to_string(tableSlot);
    // The base colour fetch uses the (optionally parallax-shifted) opticalUv
    // of ModelShaders.h; VT must sample the same coordinate.
    const std::string sourceFragment =
        "    vec4 tb = texture(tex1, opticalUv);";
    const auto glSample = fragmentGLSL.find(sourceFragment);
    if (glSample == std::string::npos) return false;
    const std::string sourceMetal =
        "    float4 tb = tex1.sample(s1, opticalUv);";
    const auto metalSample = msl.find(sourceMetal);
    if (metalSample == std::string::npos) return false;
    // ModelShaders.h has optional VIRTUAL_ALPHA_MASK arguments between
    // GI_FARGS and the function body. Match the stable GI_FARGS line
    // rather than the obsolete adjacent "GI_FARGS) {" sequence.
    const std::string parameterAnchor = "                        GI_FARGS\n";
    const auto fragEntry=msl.find("fragment float4 fs_main(");
    const auto signature = fragEntry==std::string::npos ?
        std::string::npos : msl.find(parameterAnchor,fragEntry);
    const std::string bodyAnchor = ") {\n#if defined(AIVERSE_ICB_PBR)\n";
    const auto body = signature==std::string::npos ?
        std::string::npos : msl.find(bodyAnchor,signature);
    const std::string ns = "using namespace metal;";
    const auto metalNamespace = msl.find(ns);
    if (signature==std::string::npos || body==std::string::npos ||
        metalNamespace==std::string::npos) return false;

    // Stage 1: replace base-color texture fetch for the lit surface.
    // Preserve the existing tex1 fallback when a physical page is not
    // resident yet (also preserves alpha-mask semantics). VT is enabled
    // only in a dedicated shader permutation and after both resources
    // are validated. Studio debug and shadow paths stay unchanged.
    fragmentGLSL.replace(glSample,sourceFragment.size(),
        "    vec4 tb = "+modelVirtualSampleExpression(false,trilinear,
           atlasName,tableName,"opticalUv","texture(tex1,opticalUv)")+";");
    msl.replace(metalSample,sourceMetal.size(),
        "    float4 tb = "+modelVirtualSampleExpression(true,trilinear,
           atlasName,tableName,"opticalUv","tex1.sample(s1,opticalUv)")+";");

    fragmentGLSL.insert(0,
        std::string(trilinear ?
            "layout(std140) uniform U6 { vec4 uModelVt; vec4 uModelVtExtra; vec4 uModelVtQuality; };\n" :
            "layout(std140) uniform U6 { vec4 uModelVt; };\n")
        + "uniform highp sampler2DArray "+atlasName+";\n"
        + "uniform highp sampler2DArray "+tableName+";\n"
        + gfx::kVirtualTextureGLSL + "\n");

    // Metal fragment resource arguments are appended after GI_FARGS so
    // the original texture slots / vertex ABI do not change. Sampler s1
    // is already bound by the regular material shader.
    msl.replace(signature,parameterAnchor.size(),
        std::string("                        GI_FARGS\n") +
        (trilinear ?
          "                        , constant AiverseModelVtUniforms& uModelVtParams [[buffer(7)]]\n" :
          "                        , constant float4& uModelVt [[buffer(7)]]\n") +
        "#if !defined(AIVERSE_ICB_PBR)\n"
        "                        , texture2d_array<float, access::sample> "+
            atlasName+" [[texture("+std::to_string(atlasSlot)+")]]\n"
        "                        , texture2d_array<float, access::read> "+
            tableName+" [[texture("+std::to_string(tableSlot)+")]]\n"
        "#endif\n");
    const auto bodyAfter=msl.find(bodyAnchor,signature);
    if (bodyAfter==std::string::npos) return false;
    // ICB resource aliases exist only in the ICB-specific branch.
    msl.insert(bodyAfter+bodyAnchor.size(),
        "#define "+atlasName+" icbTextures."+atlasName+"\n"
        "#define "+tableName+" icbTextures."+tableName+"\n");
    // Uniform aliases must be visible to BOTH direct draws and ICB draws.
    // Insert immediately after the opening brace, BEFORE the ICB #if.
    if(trilinear)
        msl.insert(bodyAfter+std::string(") {\n").size(),
            "#define uModelVt uModelVtParams.base\n"
            "#define uModelVtExtra uModelVtParams.extra\n"
            "#define uModelVtQuality uModelVtParams.quality\n");
    msl.insert(metalNamespace+ns.size(),
        "\n"+std::string(trilinear ?
            "struct AiverseModelVtUniforms { float4 base; float4 extra; float4 quality; };\n" :
            "")+std::string(gfx::kVirtualTextureMSL));
    return true;
}

} // namespace aiverse::render
