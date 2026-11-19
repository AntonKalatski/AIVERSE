#pragma once
#include <string>
// PRISMA Stage 29: conservative reactive coverage for translucent rigid models.
// A full-color postprocess uses this mask to reduce motion blur on glass,
// ghost surfaces, water-like alpha blends and other uncertain velocities.
// The pass never writes the scene's depth; it samples it and uses a separate
// scratch attachment. Vertex deformation and material-specific alpha are not
// represented by this conservative coverage pass.
namespace aiverse::render::reactive_shaders {
inline const char* kVS = R"(
layout(location=0) in vec3 a_pos;
layout(std140) uniform U0 { mat4 mvp; vec4 params; vec4 lod; };
void main() {
    gl_Position = mvp * vec4(a_pos,1.0);
}
)";
inline const char* kFS = R"(
layout(std140) uniform U0 { mat4 mvp; vec4 params; };
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
uniform sampler2D tex0;
in vec4 v_prevClip;
out vec4 o_color;
void main() {
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
fragment float4 fs_main(VOut in [[stage_in]],
    constant U0& u [[buffer(1)]],
    depth2d<float> tex0 [[texture(0)]],
    sampler tex0_s [[sampler(0)]]) {
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
