#pragma once
// PRISMA Stage 29: conservative reactive coverage for translucent rigid models.
// A full-color postprocess uses this mask to reduce motion blur on glass,
// ghost surfaces, water-like alpha blends and other uncertain velocities.
// The pass never writes the scene's depth; it samples it and uses a separate
// scratch attachment. Vertex deformation and material-specific alpha are not
// represented by this conservative coverage pass.
namespace aiverse::render::reactive_shaders {
inline const char* kVS = R"(
layout(location=0) in vec3 a_pos;
layout(std140) uniform U0 { mat4 mvp; vec4 params; };
void main() {
    gl_Position = mvp * vec4(a_pos,1.0);
}
)";
inline const char* kFS = R"(
layout(std140) uniform U0 { mat4 mvp; vec4 params; };
uniform sampler2D tex0;
out vec4 o_color;
void main() {
    vec2 uv = gl_FragCoord.xy / params.xy;
    if (gl_FragCoord.z > texture(tex0,uv).r + 0.0015) discard;
    o_color = vec4(params.z,0.0,0.0,1.0);
}
)";
inline const char* kMSL = R"(
#include <metal_stdlib>
using namespace metal;
struct U0 { float4x4 mvp; float4 params; };
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
    float2 uv = in.position.xy/u.params.xy;
    if (in.position.z > tex0.sample(tex0_s,uv) + 0.0015)
        discard_fragment();
    return float4(u.params.z,0.0,0.0,1.0);
}
)";
} // namespace aiverse::render::reactive_shaders
