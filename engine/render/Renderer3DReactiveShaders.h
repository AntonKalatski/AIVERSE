#pragma once
#include <string>
// PRISMA reactive coverage for translucent rigid, animated and mesh draws.
// Stage 63's cutout variant reproduces rigid-model alpha texture cutoffs,
// UV transform, material team alpha, screen-space LOD dither and 3D dissolve.
// The animated variant reuses current-pose skin/morph/baked vertex shaders
// and exact LOD dither, but still conservatively covers animated dissolve;
// its material alpha cutout/actor aperture needs a future dedicated variant.
// The mask never writes borrowed scene depth; it samples scene depth and
// attaches Renderer3D's own private depth scratch.
namespace aiverse::render::reactive_shaders {
inline const char* kVS = R"(
layout(location=0) in vec3 a_pos;
layout(std140) uniform U0 { mat4 mvp; vec4 params; vec4 lod; };
void main() {
    gl_Position = mvp * vec4(a_pos,1.0);
}
)";
inline const char* kFS = R"(
layout(std140) uniform U0 { mat4 mvp; vec4 params; vec4 lod; };
uniform sampler2D tex0;
out vec4 o_color;
void main() {
    if (lod.z > 0.5) {
        ivec2 lp = ivec2(floor(gl_FragCoord.xy)) & ivec2(3);
        int rank = ((lp.x & 1) ^ (lp.y & 1))*8 + (lp.y & 1)*4 +
                   ((lp.x>>1) ^ (lp.y>>1))*2 + (lp.y>>1);
        float coverage = (float(rank)+0.5)/16.0;
        if (lod.y > 0.5 ? coverage < lod.x : coverage >= lod.x)
            discard;
    }
    vec2 uv = gl_FragCoord.xy / params.xy;
    if (gl_FragCoord.z > texture(tex0,uv).r + 0.0015) discard;
    o_color = vec4(params.z,0.0,0.0,1.0);
}
)";
inline const char* kMSL = R"(
#include <metal_stdlib>
using namespace metal;
struct U0 { float4x4 mvp; float4 params; float4 lod; };
struct VIn { float3 pos [[attribute(0)]]; };
struct VOut { float4 position [[position]]; };
vertex VOut vs_main(VIn in [[stage_in]],constant U0& u [[buffer(1)]]) {
    VOut out;
    out.position = u.mvp * float4(in.pos,1.0);
    return out;
}
fragment float4 fs_main(VOut in [[stage_in]],constant U0& u [[buffer(1)]],
                        depth2d<float> tex0 [[texture(0)]],
                        sampler tex0_s [[sampler(0)]]) {
    if (u.lod.z > 0.5) {
        int2 lp = int2(floor(in.position.xy)) & int2(3);
        int rank = ((lp.x & 1) ^ (lp.y & 1))*8 + (lp.y & 1)*4 +
                   ((lp.x>>1) ^ (lp.y>>1))*2 + (lp.y>>1);
        float coverage = (float(rank)+0.5)/16.0;
        if (u.lod.y > 0.5 ? coverage < u.lod.x : coverage >= u.lod.x)
            discard_fragment();
    }
    float2 uv = in.position.xy/u.params.xy;
    if (in.position.z > tex0.sample(tex0_s,uv) + 0.0015)
        discard_fragment();
    return float4(u.params.z,0.0,0.0,1.0);
}
)";

// Rigid alpha-cutout / dissolving geometry: unlike the conservative
// untextured fallback, replay the actual base alpha threshold, UV transform,
// vertex alpha and object-space dissolve noise. This keeps transparent holes
// out of the reactive coverage mask and matches ordinary ModelShaders.
inline const char* kCutoutVS = R"(
layout(location=0) in vec3 a_pos;
layout(location=2) in vec2 a_uv;
layout(location=4) in vec4 a_col;
layout(std140) uniform U0 {
    mat4 mvp; vec4 params; vec4 lod; vec4 uv0; vec4 uv1; vec4 maskInfo;
};
out vec2 v_uv;
out vec3 v_opos;
out float v_alpha;
void main() {
    gl_Position = mvp * vec4(a_pos,1.0);
    vec2 uv = a_uv * uv0.zw;
    float c = cos(uv1.x), si = sin(uv1.x);
    v_uv = vec2(c*uv.x+si*uv.y,-si*uv.x+c*uv.y)+uv0.xy;
    v_opos = a_pos;
    v_alpha = a_col.a;
}
)";
inline const char* kCutoutFS = R"(
layout(std140) uniform U0 {
    mat4 mvp; vec4 params; vec4 lod; vec4 uv0; vec4 uv1; vec4 maskInfo;
};
uniform sampler2D tex0;
uniform sampler2D tex1;
in vec2 v_uv;
in vec3 v_opos;
in float v_alpha;
out vec4 o_color;
float reactiveHash3(vec3 p) {
    p=fract(p*0.3183099+0.1);p*=17.0;
    return fract(p.x*p.y*p.z*(p.x+p.y+p.z));
}
float reactiveNoise3(vec3 x) {
    vec3 i=floor(x),f=fract(x);
    f=f*f*(3.0-2.0*f);
    return mix(mix(mix(reactiveHash3(i),reactiveHash3(i+vec3(1,0,0)),f.x),
                   mix(reactiveHash3(i+vec3(0,1,0)),reactiveHash3(i+vec3(1,1,0)),f.x),f.y),
               mix(mix(reactiveHash3(i+vec3(0,0,1)),reactiveHash3(i+vec3(1,0,1)),f.x),
                   mix(reactiveHash3(i+vec3(0,1,1)),reactiveHash3(i+vec3(1,1,1)),f.x),f.y),f.z);
}
void main() {
    if(lod.z>0.5) {
        ivec2 lp=ivec2(floor(gl_FragCoord.xy)) & ivec2(3);
        int rank=((lp.x&1)^(lp.y&1))*8+(lp.y&1)*4+
                 ((lp.x>>1)^(lp.y>>1))*2+(lp.y>>1);
        float noise=(float(rank)+0.5)/16.0;
        if(lod.y>0.5 ? noise<lod.x : noise>=lod.x) discard;
    }
    if(maskInfo.w>0.0) {
        float nz=reactiveNoise3(v_opos*7.0)*0.7+
                 reactiveNoise3(v_opos*19.0)*0.3;
        if(nz<maskInfo.w)discard;
    }
    if(maskInfo.z>=0.0) {
        float alpha=texture(tex1,v_uv).a*maskInfo.x;
        if(maskInfo.y>0.5)alpha*=v_alpha;
        if(alpha<maskInfo.z)discard;
    }
    vec2 uv=gl_FragCoord.xy/params.xy;
    if(gl_FragCoord.z>texture(tex0,uv).r+0.0015)discard;
    o_color=vec4(params.z,0.0,0.0,1.0);
}
)";
inline const char* kCutoutMSL = R"(
#include <metal_stdlib>
using namespace metal;
struct U0 { float4x4 mvp; float4 params,lod,uv0,uv1,maskInfo; };
struct VIn {
    float3 pos [[attribute(0)]];
    float2 uv [[attribute(2)]];
    float4 col [[attribute(4)]];
};
struct VOut {
    float4 position [[position]];
    float2 uv; float3 opos; float alpha;
};
vertex VOut vs_main(VIn in [[stage_in]],constant U0& u [[buffer(1)]]) {
    VOut out;
    out.position=u.mvp*float4(in.pos,1.0);
    float2 uv=in.uv*u.uv0.zw;
    float c=cos(u.uv1.x),si=sin(u.uv1.x);
    out.uv=float2(c*uv.x+si*uv.y,-si*uv.x+c*uv.y)+u.uv0.xy;
    out.opos=in.pos;out.alpha=in.col.a;
    return out;
}
static float reactiveHash3(float3 p) {
    p=fract(p*0.3183099+0.1);p*=17.0;
    return fract(p.x*p.y*p.z*(p.x+p.y+p.z));
}
static float reactiveNoise3(float3 x) {
    float3 i=floor(x),f=fract(x);
    f=f*f*(3.0-2.0*f);
    return mix(mix(mix(reactiveHash3(i),reactiveHash3(i+float3(1,0,0)),f.x),
                   mix(reactiveHash3(i+float3(0,1,0)),reactiveHash3(i+float3(1,1,0)),f.x),f.y),
               mix(mix(reactiveHash3(i+float3(0,0,1)),reactiveHash3(i+float3(1,0,1)),f.x),
                   mix(reactiveHash3(i+float3(0,1,1)),reactiveHash3(i+float3(1,1,1)),f.x),f.y),f.z);
}
fragment float4 fs_main(VOut in [[stage_in]],constant U0& u [[buffer(1)]],
                         depth2d<float> tex0 [[texture(0)]],sampler s0 [[sampler(0)]],
                         texture2d<float> tex1 [[texture(1)]],sampler s1 [[sampler(1)]]) {
    if(u.lod.z>0.5) {
        int2 lp=int2(floor(in.position.xy)) & int2(3);
        int rank=((lp.x&1)^(lp.y&1))*8+(lp.y&1)*4+
                 ((lp.x>>1)^(lp.y>>1))*2+(lp.y>>1);
        float noise=(float(rank)+0.5)/16.0;
        if(u.lod.y>0.5 ? noise<u.lod.x : noise>=u.lod.x)discard_fragment();
    }
    if(u.maskInfo.w>0.0) {
        float nz=reactiveNoise3(in.opos*7.0)*0.7+
                 reactiveNoise3(in.opos*19.0)*0.3;
        if(nz<u.maskInfo.w)discard_fragment();
    }
    if(u.maskInfo.z>=0.0) {
        float alpha=tex1.sample(s1,in.uv).a*u.maskInfo.x;
        if(u.maskInfo.y>0.5)alpha*=in.alpha;
        if(alpha<u.maskInfo.z)discard_fragment();
    }
    float2 uv=in.position.xy/u.params.xy;
    if(in.position.z>tex0.sample(s0,uv).r+0.0015)discard_fragment();
    return float4(u.params.z,0.0,0.0,1.0);
}
)";

// Animated reactive coverage shares the precise CURRENT deformation vertex
// stage from Renderer3DMotionShaders, but replaces its velocity fragment.
// No prior animation pose is necessary because this mask only suppresses
// unreliable blur on the current visible translucent silhouette.
inline const char* kAnimatedFS = R"(
layout(std140) uniform U0 {
    mat4 currentMvp;
    mat4 previousMvp;
    vec4 params;
};
layout(std140) uniform U1 { vec4 reactiveLod; };
uniform sampler2D tex0;
in vec4 v_prevClip;
out vec4 o_color;
void main() {
    // Stage 63 exact screen-space LOD dither for skinned/baked coverage,
    // even on the first frame when no previous pose exists.
    if (reactiveLod.z > 0.5) {
        ivec2 lp=ivec2(floor(gl_FragCoord.xy)) & ivec2(3);
        int rank=((lp.x&1)^(lp.y&1))*8+(lp.y&1)*4+
                 ((lp.x>>1)^(lp.y>>1))*2+(lp.y>>1);
        float noise=(float(rank)+0.5)/16.0;
        if(reactiveLod.y>0.5 ? noise<reactiveLod.x : noise>=reactiveLod.x)
            discard;
    }
    vec2 uv = gl_FragCoord.xy / params.xy;
    if (gl_FragCoord.z > texture(tex0,uv).r + 0.0015)
        discard;
    o_color = vec4(1.0,0.0,0.0,1.0);
}
)";

// The motion MSL source already defines its vertex entry, deformation
// helpers, U0 and VOut types. Replace only the end fragment: it is a
// PRESENTATION-only reactive path that emits full current-frame coverage.
inline std::string animatedMsl(const char* motionMsl) {
    if (!motionMsl) return {};
    std::string code(motionMsl);
    const auto fragment = code.find("fragment float4 fs_main");
    if (fragment == std::string::npos) return {};
    code.resize(fragment);
    code += R"(
struct ReactiveCoverageU1 {float4 lod;};
fragment float4 fs_main(VOut in [[stage_in]],
    constant U0& u [[buffer(1)]],
    constant ReactiveCoverageU1& coverage [[buffer(2)]],
    depth2d<float> tex0 [[texture(0)]],
    sampler tex0_s [[sampler(0)]]) {
    if (coverage.lod.z > 0.5) {
        int2 lp=int2(floor(in.position.xy)) & int2(3);
        int rank=((lp.x&1)^(lp.y&1))*8+(lp.y&1)*4+
                 ((lp.x>>1)^(lp.y>>1))*2+(lp.y>>1);
        float noise=(float(rank)+0.5)/16.0;
        if(coverage.lod.y>0.5 ? noise<coverage.lod.x :
                                  noise>=coverage.lod.x)
            discard_fragment();
    }
    const float2 uv = in.position.xy/u.params.xy;
    if (in.position.z > tex0.sample(tex0_s,uv)+0.0015)
        discard_fragment();
    return float4(1.0,0.0,0.0,1.0);
}
)";
    return code;
}

// Dither rule copied from the normal opaque ModelShaders LODFADE branch.
// lod=(threshold,invert,enabled,unused); params.z is mask strength.
// The reactive fallback is rendered only for pixels actually covered by
// the fading geometry, not its bounding triangle.
} // namespace aiverse::render::reactive_shaders
