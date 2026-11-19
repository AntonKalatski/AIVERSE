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
    // Match the original material's U/V sampler modes. ClampToBorder
    // cannot be represented by the current RGBA8 page-table layout.
    gfx::SamplerAddressMode addressU = gfx::SamplerAddressMode::ClampToEdge;
    gfx::SamplerAddressMode addressV = gfx::SamplerAddressMode::ClampToEdge;
    bool valid() const {
        return physicalAtlas.valid() && pageTable.valid() &&
               layer >= 0 && layer < 16 &&
               std::isfinite(gutterPixels) && gutterPixels >= 0 &&
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

// Reuse the canonical virtual page-table shader implementation; do not
// implement another page lookup with different mip/edge semantics.
// Returns false if a shader source contract changes: never silently
// generate a broken pipeline.
inline bool injectModelVirtualTextureSampling(std::string& fragmentGLSL,
                                              std::string& msl,
                                              int atlasSlot = 13,
                                              int tableSlot = 14) {
    // Graph resources begin at slot 13. At most one graph texture can
    // coexist with the two VT arrays on GL33/GLES3/Metal (0..15).
    if (atlasSlot < 13 || tableSlot != atlasSlot+1 ||
        tableSlot > 15) return false;
    const std::string atlasName="tex"+std::to_string(atlasSlot);
    const std::string tableName="tex"+std::to_string(tableSlot);
    const std::string sourceFragment =
        "    vec4 tb = texture(tex1, v_uv);";
    const auto glSample = fragmentGLSL.find(sourceFragment);
    if (glSample == std::string::npos) return false;
    const std::string sourceMetal =
        "    float4 tb = tex1.sample(s1, in.uv);";
    const auto metalSample = msl.find(sourceMetal);
    if (metalSample == std::string::npos) return false;
    // ModelShaders.h has optional VIRTUAL_ALPHA_MASK arguments between
    // GI_FARGS and the function body. Match the stable GI_FARGS line
    // rather than the obsolete adjacent "GI_FARGS) {" sequence.
    const std::string parameterAnchor = "                        GI_FARGS\n";
    const auto fragEntry=msl.find("fragment float4 fs_main(");
    const auto signature = fragEntry==std::string::npos ?
        std::string::npos : msl.find(parameterAnchor,fragEntry);
    const auto bodyAnchor = ") {\n#if defined(AIVERSE_ICB_PBR)\n";
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
        "    vec4 tb = aiverseSampleVirtual("+atlasName+","+tableName+
        ",v_uv,int(uModelVt.x),uModelVt.y,texture(tex1,v_uv),"
        "int(uModelVt.z),int(uModelVt.w));");
    msl.replace(metalSample,sourceMetal.size(),
        "    float4 tb = aiverse_sample_virtual("+atlasName+","+
        tableName+",s1,in.uv,uint(uModelVt.x),uModelVt.y,"
        "tex1.sample(s1,in.uv),uint(uModelVt.z),uint(uModelVt.w));");

    fragmentGLSL.insert(0,
        std::string("layout(std140) uniform U6 { vec4 uModelVt; };\n")
        + "uniform highp sampler2DArray "+atlasName+";\n"
        + "uniform highp sampler2DArray "+tableName+";\n"
        + gfx::kVirtualTextureGLSL + "\n");

    // Metal fragment resource arguments are appended after GI_FARGS so
    // the original texture slots / vertex ABI do not change. Sampler s1
    // is already bound by the regular material shader.
    msl.replace(signature,parameterAnchor.size(),
        "                        GI_FARGS\n"
        "                        , constant float4& uModelVt [[buffer(7)]]\n"
        "#if !defined(AIVERSE_ICB_PBR)\n"
        "                        , texture2d_array<float, access::sample> "+
            atlasName+" [[texture("+std::to_string(atlasSlot)+")]]\n"
        "                        , texture2d_array<float, access::read> "+
            tableName+" [[texture("+std::to_string(tableSlot)+")]]\n"
        "#endif\n");
    const auto bodyAfter=msl.find(bodyAnchor,signature);
    if (bodyAfter==std::string::npos) return false;
    msl.insert(bodyAfter+bodyAnchor.size(),
        "#define "+atlasName+" icbTextures."+atlasName+"\n"
        "#define "+tableName+" icbTextures."+tableName+"\n");
    msl.insert(metalNamespace+ns.size(),"\n"+std::string(gfx::kVirtualTextureMSL));
    return true;
}

} // namespace aiverse::render
