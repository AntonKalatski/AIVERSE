// SpectraFX GPU billboard renderer. Samples position/lifetime from the
// compute-written RGBA32F texture, one instanced draw for a single batch.
// This is opt-in. The CPU ParticleSystem remains the complete fallback.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include "../gfx/Gfx.h"
#include "../render/Math3D.h"
#include "SpectraFxGpuEvolution.h"
#include "SpectraFxGpuCompaction.h"

namespace aiverse::fx {
namespace gpu_render_shaders {
inline constexpr const char* kVS=R"GLSL(
layout(location=0) in vec2 a_corner;
layout(std140) uniform U0 {
    mat4 u_viewProj;
    vec4 u_cameraRight;
    vec4 u_cameraUp;
    vec4 u_colorSize;
    vec4 u_meta;
};
uniform highp sampler2D tex0;
uniform highp sampler2D tex1;
out mediump vec2 v_uv;
out lowp vec4 v_color;
void main() {
    int instance=gl_InstanceID;
    int width=int(u_meta.x);
    // GPU compaction maps packed instance indices to live particle slots.
    // The fallback uses the original full-capacity slot ID.
    int slot=u_meta.y>0.5
        ? int(texelFetch(tex1,ivec2(instance%width,instance/width),0).x+0.5)
        : instance;
    vec4 p=texelFetch(tex0,ivec2(slot%width,slot/width),0);
    vec3 world=p.xyz+(u_cameraRight.xyz*a_corner.x+
                       u_cameraUp.xyz*a_corner.y)*u_colorSize.w;
    gl_Position=u_viewProj*vec4(world,1.0);
    v_uv=a_corner*0.5+0.5;
    v_color=vec4(u_colorSize.rgb,p.w);
}
)GLSL";
inline constexpr const char* kFS=R"GLSL(
precision mediump float;
in mediump vec2 v_uv;
in lowp vec4 v_color;
out lowp vec4 o_color;
void main() {
    vec2 uv=v_uv*2.0-1.0;
    float alpha=clamp(1.0-dot(uv,uv),0.0,1.0);
    o_color=vec4(v_color.rgb,v_color.a*alpha);
}
)GLSL";
inline constexpr const char* kMetal=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct U0 {
    float4x4 viewProj;
    float4 cameraRight;
    float4 cameraUp;
    float4 colorSize;
    float4 meta;
};
struct VIn { float2 corner [[attribute(0)]]; };
struct VOut { float4 position [[position]]; float2 uv; half4 color; };
vertex VOut vs_main(VIn in [[stage_in]],
                    uint instance [[instance_id]],
                    constant U0& u [[buffer(1)]],
                    texture2d<float> tex0 [[texture(0)]],
                    texture2d<float> tex1 [[texture(1)]]) {
    const uint width=uint(u.meta.x);
    const uint slot=u.meta.y>0.5f
        ? uint(tex1.read(uint2(instance%width,instance/width)).x+0.5f)
        : instance;
    const float4 p=tex0.read(uint2(slot%width,slot/width));
    const float3 world=p.xyz+(u.cameraRight.xyz*in.corner.x+
                               u.cameraUp.xyz*in.corner.y)*u.colorSize.w;
    VOut o;
    o.position=u.viewProj*float4(world,1.0);
    o.uv=in.corner*0.5+0.5;
    o.color=half4(u.colorSize.rgb,p.w);
    return o;
}
fragment half4 fs_main(VOut in [[stage_in]]) {
    const float2 xy=in.uv*2.0-1.0;
    const float alpha=clamp(1.0-dot(xy,xy),0.0,1.0);
    return half4(in.color.rgb,in.color.a*half(alpha));
}
)MSL";
} // namespace gpu_render_shaders

class SpectraFxGpuBillboardRenderer {
public:
    struct Appearance {
        render::float3 color{1,1,1};
        float radius=0.2f;
        bool additive=false;
    };

    bool init(gfx::Device& device) {
        release();
        device_=&device;
        if(!device.supportsCompute()) {
            error_="GPU billboard rendering requires a compute-capable RHI";
            device_=nullptr;return false;
        }
        gfx::ShaderDesc shader{};
        shader.glslVertex=gpu_render_shaders::kVS;
        shader.glslFragment=gpu_render_shaders::kFS;
        shader.msl=gpu_render_shaders::kMetal;
        shader.textureSlots=2;
        shader.uniformSlots=1;
        shader.label="SpectraFX GPU billboards";
        vertexShader_=device.createShader(shader);
        if(!vertexShader_.valid()) {
            error_=device.lastError();release();return false;
        }
        struct Corner {float x,y;};
        const Corner vertices[]={{-1,-1},{1,-1},{1,1},{-1,1}};
        const uint16_t indices[]={0,1,2,0,2,3};
        vertex_=device.createBuffer({gfx::BufferType::Vertex,gfx::Usage::Immutable,
                                     sizeof(vertices),vertices,"SpectraFX GPU quad"});
        index_=device.createBuffer({gfx::BufferType::Index,gfx::Usage::Immutable,
                                    sizeof(indices),indices,"SpectraFX GPU indices"});
        gfx::PipelineDesc pipeline{};
        pipeline.shader=vertexShader_;
        pipeline.attrs[0]={0,gfx::VertexFormat::Float2,0};
        pipeline.attrCount=1;
        pipeline.stride=sizeof(Corner);
        pipeline.indexType=gfx::IndexType::UInt16;
        pipeline.primitive=gfx::Primitive::Triangles;
        pipeline.cull=gfx::CullMode::None;
        pipeline.depthTest=true;
        pipeline.depthWrite=false;
        pipeline.blend=gfx::BlendMode::Alpha;
        pipeline.label="SpectraFX GPU alpha quads";
        alpha_=device.createPipeline(pipeline);
        pipeline.blend=gfx::BlendMode::Additive;
        pipeline.label="SpectraFX GPU additive quads";
        additive_=device.createPipeline(pipeline);
        if(!vertex_.valid()||!index_.valid()||!alpha_.valid()||!additive_.valid()) {
            error_="SpectraFX GPU draw buffers/pipeline unavailable: "+device.lastError();
            release();return false;
        }
        error_.clear();
        return true;
    }

    // Call only inside an open compatible render pass, after the compute
    // stage ended. The supplied orientation axes are world-space camera right
    // and up (normalized by caller). No CPU readback of positions.
    bool draw(const SpectraFxGpuEvolution& particles,
              const render::mat4& viewProj,render::float3 cameraRight,
              render::float3 cameraUp,const Appearance& appearance,
              const SpectraFxGpuCompaction* compaction=nullptr) {
        if(!ready()||!particles.ready()||!particles.count()||
           !particles.positionTexture().valid()) return false;
        auto finite=[](render::float3 v) {
            return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
        };
        if(!finite(cameraRight)||!finite(cameraUp)||!finite(appearance.color)||
           !std::isfinite(appearance.radius)||appearance.radius<=0||
           appearance.radius>1000) return false;
        struct alignas(16) Uniforms {
            float viewProj[16];
            render::float4 cameraRight;
            render::float4 cameraUp;
            render::float4 colorSize;
            render::float4 metadata;
        } u{};
        static_assert(sizeof(Uniforms)==128);
        std::memcpy(u.viewProj,viewProj.m,sizeof(u.viewProj));
        u.cameraRight={cameraRight.x,cameraRight.y,cameraRight.z,0};
        u.cameraUp={cameraUp.x,cameraUp.y,cameraUp.z,0};
        u.colorSize={appearance.color.x,appearance.color.y,
                     appearance.color.z,appearance.radius};
        const bool compacted=compaction && compaction->preparedFor(particles);
        u.metadata={float(SpectraFxGpuEvolution::kTextureWidth),
                    compacted?1.0f:0.0f,0,0};
        device_->setPipeline(appearance.additive?additive_:alpha_);
        device_->setVertexBuffer(vertex_);
        device_->setIndexBuffer(index_);
        device_->setTexture(0,particles.positionTexture());
        // Bind tex1 even in legacy draw mode: native shader resources
        // are declared unconditionally, while the selection branch is not.
        device_->setTexture(1,compacted?compaction->denseIndexTexture()
                                   :particles.positionTexture());
        device_->setUniforms(0,&u,sizeof(u));
        if(compacted && device_->drawIndexedGpuIndirect(
                          compaction->indirectArguments(),0)) return true;
        // Failed/unsupported Metal indirect submission: restore original
        // full-capacity indexing before issuing the existing safe draw.
        if(compacted) {
            u.metadata.y=0;
            device_->setTexture(1,particles.positionTexture());
            device_->setUniforms(0,&u,sizeof(u));
        }
        device_->drawIndexed(6,0,0,particles.count());
        return true;
    }

    bool ready() const {
        return device_&&vertexShader_.valid()&&vertex_.valid()&&
               index_.valid()&&alpha_.valid()&&additive_.valid();
    }
    const std::string& lastError() const {return error_;}
    void release() {
        if(device_) {
            if(alpha_.valid()) device_->destroyPipeline(alpha_);
            if(additive_.valid()) device_->destroyPipeline(additive_);
            if(vertex_.valid()) device_->destroyBuffer(vertex_);
            if(index_.valid()) device_->destroyBuffer(index_);
            if(vertexShader_.valid()) device_->destroyShader(vertexShader_);
        }
        device_=nullptr;alpha_={};additive_={};vertex_={};index_={};vertexShader_={};
    }
private:
    gfx::Device* device_=nullptr;
    gfx::ShaderHandle vertexShader_;
    gfx::PipelineHandle alpha_,additive_;
    gfx::BufferHandle vertex_,index_;
    std::string error_;
};
} // namespace aiverse::fx
