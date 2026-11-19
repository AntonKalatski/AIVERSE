#pragma once
// Stage 26 — rigid object motion overlay for PRISMA.
// ModelVertex position occupies location 0, stride 60.
// Velocity RG is signed current-minus-previous normalized screen UV; B=1 valid.
// Opaque rigid primitives only. Occlusion comes from sampled scene depth,
// not from writing to the original depth attachment.
namespace aiverse::render::motion_shaders {
inline const char* kVS = R"(
layout(location=0) in vec3 a_pos;
layout(std140) uniform U0 {
    mat4 currentMvp;
    mat4 previousMvp;
    vec4 params;
};
out vec4 v_prevClip;
void main() {
    vec4 local = vec4(a_pos,1.0);
    gl_Position = currentMvp * local;
    v_prevClip = previousMvp * local;
}
)";
inline const char* kFS = R"(
layout(std140) uniform U0 {
    mat4 currentMvp;
    mat4 previousMvp;
    vec4 params;
};
uniform sampler2D tex0;
in vec4 v_prevClip;
out vec4 o_color;
void main() {
    vec2 s = gl_FragCoord.xy / params.xy;
    vec2 uv = params.z > 0.5 ? vec2(s.x,1.0-s.y) : s;
    float sceneZ = texture(tex0, uv).r;
    if (gl_FragCoord.z > sceneZ + 0.0015) discard;
    if (v_prevClip.w <= 0.00001) discard;
    vec2 prior = v_prevClip.xy / v_prevClip.w * 0.5 + 0.5;
    o_color = vec4(s-prior,1.0,1.0);
}
)";
inline const char* kMSL = R"(
#include <metal_stdlib>
using namespace metal;
struct U0 { float4x4 currentMvp; float4x4 previousMvp; float4 params; };
struct VIn { float3 pos [[attribute(0)]]; };
struct VOut {
    float4 position [[position]];
    float4 previous;
};
vertex VOut vs_main(VIn in [[stage_in]], constant U0& u [[buffer(1)]]) {
    VOut o;
    const float4 local=float4(in.pos,1.0);
    o.position=u.currentMvp*local;
    o.previous=u.previousMvp*local;
    return o;
}
fragment float4 fs_main(
    VOut in [[stage_in]],
    constant U0& u [[buffer(1)]],
    texture2d<float> tex0 [[texture(0)]],
    sampler tex0_s [[sampler(0)]]) {
    float2 s=in.position.xy/u.params.xy;
    float2 uv=u.params.z>0.5 ? float2(s.x,1.0-s.y):s;
    const float sceneZ=tex0.sample(tex0_s,uv).r;
    if (in.position.z > sceneZ+0.0015) discard_fragment();
    if (in.previous.w <= 0.00001) discard_fragment();
    const float2 prior=in.previous.xy/in.previous.w*0.5+0.5;
    // Metal raster position has top-left origin. Convert to NDC y-up.
    const float2 current=float2(s.x,1.0-s.y);
    return float4(current-prior,1.0,1.0);
}
)";
} // namespace aiverse::render::motion_shaders
