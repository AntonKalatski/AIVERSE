// Opt-in native model-material VT colour sampling, independent of genre.
// Slots 13/14 are free ONLY for built-in non-graph material variants;
// custom shader graphs already allocate tex13 and upwards.
// No effect on shadow, selection, postprocess or other shader pipelines.
#pragma once
#include "../gfx/Gfx.h"
#include "../gfx/VirtualTextureSampling.h"
#include "Model.h"
#include <cmath>
#include <string>

namespace aiverse::render {

// One VT domain per draw/material. The caller owns both textures and must
// keep them alive until in-flight GPU work is complete.
struct ModelVirtualTextureBinding {
    gfx::TextureHandle physicalAtlas;
    gfx::TextureHandle pageTable;
    int layer = 0;
    float gutterPixels = 2.0f;
    bool valid() const {
        return physicalAtlas.valid() && pageTable.valid() &&
               layer >= 0 && layer < 16 &&
               std::isfinite(gutterPixels) && gutterPixels >= 0;
    }
    bool operator==(const ModelVirtualTextureBinding&) const = default;
};

struct ModelVirtualTextureUniforms {
    float layer = 0, gutterPixels = 2, unused0 = 0, unused1 = 0;
};
static_assert(sizeof(ModelVirtualTextureUniforms) == 16);

// Reuse the canonical virtual page-table shader implementation; do not
// implement another page lookup with different mip/edge semantics.
// Returns false if a shader source contract changes: never silently
// generate a broken pipeline.
inline bool injectModelVirtualTextureSampling(std::string& fragmentGLSL,
                                              std::string& msl) {
    const std::string sourceFragment =
        "    vec4 tb = texture(tex1, v_uv);";
    const auto glSample = fragmentGLSL.find(sourceFragment);
    if (glSample == std::string::npos) return false;
    const std::string sourceMetal =
        "    float4 tb = tex1.sample(s1, in.uv);";
    const auto metalSample = msl.find(sourceMetal);
    if (metalSample == std::string::npos) return false;
    const std::string parameterEnd = "GI_FARGS) {";
    const auto signature = msl.find(parameterEnd,msl.find("fragment float4 fs_main("));
    if (signature == std::string::npos) return false;
    const std::string ns = "using namespace metal;";
    const auto metalNamespace = msl.find(ns);
    if (metalNamespace == std::string::npos) return false;

    // Stage 1: replace base-color texture fetch for the lit surface.
    // Preserve the existing tex1 fallback when a physical page is not
    // resident yet (also preserves alpha-mask semantics). VT is enabled
    // only in a dedicated shader permutation and after both resources
    // are validated. Studio debug and shadow paths stay unchanged.
    fragmentGLSL.replace(glSample,sourceFragment.size(),
        "    vec4 tb = aiverseSampleVirtual(tex13,tex14,v_uv,"
        "int(uModelVt.x),uModelVt.y,texture(tex1,v_uv));");
    msl.replace(metalSample,sourceMetal.size(),
        "    float4 tb = aiverse_sample_virtual("
        "tex13,tex14,s1,in.uv,uint(uModelVt.x),uModelVt.y,"
        "tex1.sample(s1,in.uv));");

    fragmentGLSL.insert(0,
        std::string("layout(std140) uniform U6 { vec4 uModelVt; };\n")
        + "uniform highp sampler2DArray tex13;\n"
        + "uniform highp sampler2DArray tex14;\n"
        + gfx::kVirtualTextureGLSL + "\n");

    // Metal fragment resource arguments are appended after GI_FARGS so
    // the original texture slots / vertex ABI do not change. Sampler s1
    // is already bound by the regular material shader.
    const auto mslSignature = msl.find(parameterEnd,msl.find("fragment float4 fs_main("));
    msl.replace(mslSignature,parameterEnd.size(),
        "GI_FARGS, constant float4& uModelVt [[buffer(7)]],"
        "texture2d_array<float, access::sample> tex13 [[texture(13)]],"
        "texture2d_array<float, access::read> tex14 [[texture(14)]]) {");
    msl.insert(metalNamespace+ns.size(),"\n"+std::string(gfx::kVirtualTextureMSL));
    return true;
}

} // namespace aiverse::render
