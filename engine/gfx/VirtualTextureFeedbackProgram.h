// Self-contained cross-backend VT feedback pipeline for position/UV indexed meshes.
// GLSL ES 3.0 / GL 3.3 and Metal share the same U0 std140/constant-buffer
// layout and the same fragment page codec. No renderer/genre dependencies.
#pragma once
#include "Gfx.h"
#include "VirtualTextureFeedback.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace aiverse::gfx {

// One per-draw U0 block, matching GLSL std140 and MSL float4 alignment.
// clipFromLocal = camera viewProjection * model transform, column-major.
// page = (baseTileCountX,baseTileCountY,virtualLayer,explicitMip)
// texels = (logicalWidth,logicalHeight,mipCount,mipBias)
// virtualUv = (scaleX,scaleY,offsetX,offsetY) applied to source UV.
// alphaUv0/1 = affine UV transform rows in the *main material's* convention.
// coverage = (baseColorAlphaFactor,alphaCutoff,lodThreshold,lodInvert[0/1]).
// scenePixel = (feedbackToSceneScaleX,feedbackToSceneScaleY,
//               feedbackToSceneOffsetX,feedbackToSceneOffsetY).
struct alignas(16) VirtualTextureFeedbackUniforms {
    float clipFromLocal[16] = {};
    float page[4] = {1, 1, 0, 0};
    float texels[4] = {1024, 1024, 1, 0};
    float virtualUv[4] = {1, 1, 0, 0};
    float alphaUv0[4] = {1, 0, 0, 0};
    float alphaUv1[4] = {0, 1, 0, 0};
    float coverage[4] = {1, 0.5f, 1, 0};
    float scenePixel[4] = {1, 1, 0, 0};
    float addressModes[4] = {}; // U/V: clamp=0, repeat=1, mirror=2
};
static_assert(sizeof(VirtualTextureFeedbackUniforms) == 192);
static_assert(alignof(VirtualTextureFeedbackUniforms) == 16);

struct VirtualTextureFeedbackProgramSources {
    std::string glslVertex, glslFragment, msl;
};

// Unlike fragment-only helpers, these sources use the actual engine RHI
// conventions: GLSL uniform U0 and tex0; MSL vs_main/fs_main and
// [[buffer(1)]] for U0 (buffer 0 is reserved for vertices).
inline VirtualTextureFeedbackProgramSources
virtualTextureFeedbackProgramSources(bool alphaMask, bool lodFade,
                                     bool automaticMip = true,
                                     bool alphaRedChannel = false) {
    VirtualTextureFeedbackProgramSources src;
    const std::string glslBlock = R"GLSL(
layout(std140) uniform U0 {
    mat4 uClipFromLocal;
    vec4 uPage, uTexels, uVirtualUv;
    vec4 uAlphaUv0, uAlphaUv1, uCoverage, uScenePixel, uAddressModes;
};
)GLSL";
    src.glslVertex = R"GLSL(
precision highp float;
layout(location=0) in vec3 a_pos;
layout(location=2) in vec2 a_uv;
)GLSL" + glslBlock + R"GLSL(
out vec2 vVirtualUV;
out vec2 vAlphaUV;
void main() {
    gl_Position = uClipFromLocal * vec4(a_pos,1.0);
    vVirtualUV = a_uv * uVirtualUv.xy + uVirtualUv.zw;
    vAlphaUV = a_uv;
}
)GLSL";
    src.glslFragment = R"GLSL(
precision highp float;
precision highp int;
in vec2 vVirtualUV;
in vec2 vAlphaUV;
layout(location=0) out vec4 outFeedback;
)GLSL" + glslBlock;
    if (alphaMask)
        src.glslFragment += "uniform sampler2D tex0;\n";
    src.glslFragment += kVirtualTextureFeedbackGLSL;
    src.glslFragment += kVirtualTextureFeedbackMaterialGLSL;
    if (automaticMip) src.glslFragment += kVirtualTextureFeedbackAutoMipGLSL;
    src.glslFragment += "void main() {\n";
    if (automaticMip)
        src.glslFragment +=
            " int mip=aiverseFeedbackMipFromDerivatives(vVirtualUV,"
            "uTexels.xy,int(uTexels.z),uTexels.w);\n";
    else
        src.glslFragment += " int mip=int(uPage.w);\n";
    if (alphaMask) {
        src.glslFragment +=
            " vec3 auv=vec3(vAlphaUV,1.0);\n"
            " vec2 sampleUv=vec2(dot(uAlphaUv0.xyz,auv),"
            "dot(uAlphaUv1.xyz,auv));\n";
        src.glslFragment += std::string(" if(texture(tex0,sampleUv)") +
            (alphaRedChannel ? ".r" : ".a") +
            "*uCoverage.x<uCoverage.y) discard;\n";
    }
    if (lodFade)
        src.glslFragment +=
            " vec2 pixel=gl_FragCoord.xy*uScenePixel.xy+uScenePixel.zw;\n"
            " ivec2 lp=ivec2(floor(pixel)) & ivec2(3);\n"
            " int rank=((lp.x & 1) ^ (lp.y & 1))*8+(lp.y & 1)*4+"
            "((lp.x>>1) ^ (lp.y>>1))*2+(lp.y>>1);\n"
            " float noise=(float(rank)+0.5)/16.0;\n"
            " if(uCoverage.w>0.5 ? noise<uCoverage.z : noise>=uCoverage.z)"
            " discard;\n";
    src.glslFragment +=
        " outFeedback=aiverseMaterialFeedback(vVirtualUV,mip,"
        "int(uPage.z),ivec2(uPage.xy),"
        "int(uAddressModes.x),int(uAddressModes.y));\n}\n";

    src.msl = R"MSL(
#include <metal_stdlib>
using namespace metal;
struct FeedbackU0 {
    float4x4 clipFromLocal;
    float4 page, texels, virtualUv, alphaUv0, alphaUv1, coverage,
           scenePixel, addressModes;
};
struct FeedbackIn {
    float3 pos [[attribute(0)]];
    float2 uv [[attribute(2)]];
};
struct FeedbackOut {
    float4 position [[position]];
    float2 uv [[user(locn0)]];
    float2 alphaUv [[user(locn1)]];
};
vertex FeedbackOut vs_main(FeedbackIn in [[stage_in]],
                           constant FeedbackU0& u [[buffer(1)]]) {
    FeedbackOut out;
    out.position=u.clipFromLocal*float4(in.pos,1.0);
    out.uv=in.uv*u.virtualUv.xy+u.virtualUv.zw;
    out.alphaUv=in.uv;
    return out;
}
)MSL";
    src.msl += kVirtualTextureFeedbackMSL;
    src.msl += kVirtualTextureFeedbackMaterialMSL;
    if (automaticMip) src.msl += kVirtualTextureFeedbackAutoMipMSL;
    src.msl +=
        "fragment float4 fs_main(FeedbackOut in [[stage_in]],"
        "constant FeedbackU0& u [[buffer(1)]]";
    if (alphaMask)
        src.msl +=
            ", texture2d<float> tex0 [[texture(0)]],"
            "sampler s0 [[sampler(0)]]";
    src.msl += ") {\n";
    if (automaticMip)
        src.msl +=
            " int mip=aiverse_feedback_mip_from_derivatives(in.uv,"
            "u.texels.xy,int(u.texels.z),u.texels.w);\n";
    else
        src.msl += " int mip=int(u.page.w);\n";
    if (alphaMask) {
        src.msl +=
            " float3 auv=float3(in.alphaUv,1.0);\n"
            " float2 sampleUv=float2(dot(u.alphaUv0.xyz,auv),"
            "dot(u.alphaUv1.xyz,auv));\n"
            " float4 alphaSample = tex0.sample(s0,sampleUv);\n";
        src.msl += std::string(" if(alphaSample.") +
            (alphaRedChannel ? "r" : "a") +
            "*u.coverage.x<u.coverage.y) discard_fragment();\n";
    }
    if (lodFade)
        src.msl +=
            " float2 pixel=in.position.xy*u.scenePixel.xy+u.scenePixel.zw;\n"
            " int2 lp=int2(floor(pixel)) & int2(3);\n"
            " int rank=((lp.x & 1) ^ (lp.y & 1))*8+(lp.y & 1)*4+"
            "((lp.x>>1) ^ (lp.y>>1))*2+(lp.y>>1);\n"
            " float noise=(float(rank)+0.5)/16.0;\n"
            " if(u.coverage.w>0.5 ? noise<u.coverage.z : noise>=u.coverage.z)"
            " discard_fragment();\n";
    src.msl +=
        " return aiverse_material_feedback(in.uv,mip,"
        "int(u.page.z),int2(u.page.xy),"
        "uint(u.addressModes.x),uint(u.addressModes.y));\n}\n";
    return src;
}

// The application chooses a vertex layout matching its native mesh:
// ModelVertex: stride 60, position offset 0, UV offset 24, 32-bit indices.
// A different terrain mesh can use any stride/offsets and index width.
// Position attribute is location 0, UV is location 2, matching ModelVertex.
struct VirtualTextureFeedbackVertexLayout {
    uint32_t stride = 60;
    uint32_t positionOffset = 0;
    uint32_t uvOffset = 24;
    IndexType indexType = IndexType::UInt32;
    CullMode cull = CullMode::Back;
};

// Synchronous RHI shader/pipeline creation. Keep shader sources alive until
// createShader returns; backend implementations retain compiled resources.
// Never reuse this program across incompatible depth formats or vertex layouts.
class VirtualTextureFeedbackProgram {
public:
    bool initialize(Device& dev, bool alphaMask, bool lodFade,
                    bool depthAttachment,
                    VirtualTextureFeedbackVertexLayout layout = {},
                    bool automaticMip = true,
                    bool alphaRedChannel = false) {
        if (shader_.valid() || pipeline_.valid() ||
            layout.stride == 0 ||
            layout.positionOffset + 12 > layout.stride ||
            layout.uvOffset + 8 > layout.stride ||
            layout.indexType == IndexType::None)
            return false;
        const auto sources = virtualTextureFeedbackProgramSources(
            alphaMask, lodFade, automaticMip, alphaRedChannel);
        ShaderDesc sd;
        sd.glslVertex = sources.glslVertex.c_str();
        sd.glslFragment = sources.glslFragment.c_str();
        sd.msl = sources.msl.c_str();
        sd.textureSlots = alphaMask ? 1 : 0;
        sd.uniformSlots = 1;
        sd.label = "VirtualTextureFeedbackMaterial";
        shader_ = dev.createShader(sd);
        if (!shader_.valid()) return false;
        PipelineDesc pd;
        pd.shader = shader_;
        pd.attrs[0] = {0,VertexFormat::Float3,layout.positionOffset};
        pd.attrs[1] = {2,VertexFormat::Float2,layout.uvOffset};
        pd.attrCount = 2;
        pd.stride = layout.stride;
        pd.indexType = layout.indexType;
        pd.cull = layout.cull;
        pd.depthTest = depthAttachment;
        pd.depthWrite = depthAttachment;
        pd.blend = BlendMode::Opaque;
        pd.colorFormat = PixelFormat::RGBA8;
        pd.colorAttachmentCount = 1;
        pd.sampleCount = 1;
        pd.label = "VirtualTextureFeedbackMaterial";
        pipeline_ = dev.createPipeline(pd);
        if (!pipeline_.valid()) {
            dev.destroyShader(shader_);
            shader_ = {};
            return false;
        }
        alphaMask_ = alphaMask;
        return true;
    }
    bool ready() const { return shader_.valid() && pipeline_.valid(); }
    PipelineHandle pipeline() const { return pipeline_; }
    bool bind(Device& dev, const VirtualTextureFeedbackUniforms& u,
              TextureHandle baseColor = {}) const {
        if (!ready() || (alphaMask_ && !baseColor.valid())) return false;
        dev.setPipeline(pipeline_);
        dev.setUniforms(0, &u, sizeof(u));
        if (alphaMask_) dev.setTexture(0,baseColor);
        return true;
    }
    void shutdown(Device& dev) {
        if (pipeline_.valid()) dev.destroyPipeline(pipeline_);
        if (shader_.valid()) dev.destroyShader(shader_);
        pipeline_ = {};
        shader_ = {};
        alphaMask_ = false;
    }
private:
    ShaderHandle shader_;
    PipelineHandle pipeline_;
    bool alphaMask_ = false;
};

} // namespace aiverse::gfx
