// Screen-space exterior selection outline. Samples only the depth-tested
// selection mask: no displaced vertices, no internal model/submesh edges.
#pragma once

namespace aiverse::render::selection_outline {

inline constexpr const char* kVertex = R"(
layout(location = 0) in vec2 a_pos;
out vec2 v_uv;
void main() {
    v_uv = a_pos * 0.5 + 0.5;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

inline constexpr const char* kFragment = R"(
layout(std140) uniform U0 { vec4 u_px; };
uniform sampler2D tex0;
in vec2 v_uv;
out vec4 o_color;
void main() {
    vec2 uv = vec2(v_uv.x, u_px.z > 0.5 ? 1.0 - v_uv.y : v_uv.y);
    vec3 centre = texture(tex0, uv).rgb;
    // Only paint OUTSIDE the object's opaque mask, never inside its mesh.
    if (max(centre.r, max(centre.g, centre.b)) > 0.03) discard;
    vec3 nearest = vec3(0.0);
    float weight = 0.0;
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            vec3 candidate = texture(tex0,
                uv + vec2(float(x), float(y)) * u_px.xy).rgb;
            float power = max(candidate.r, max(candidate.g, candidate.b));
            if (power > weight) { weight = power; nearest = candidate; }
        }
    }
    if (weight < 0.04) discard;
    o_color = vec4(nearest, 0.94);
}
)";

inline constexpr const char* kMetal = R"(
#include <metal_stdlib>
using namespace metal;
struct VIn { float2 pos [[attribute(0)]]; };
struct VOut { float4 position [[position]]; float2 uv; };
struct U0 { float4 px; };
vertex VOut vs_main(VIn in [[stage_in]]) {
    VOut out;
    out.position = float4(in.pos, 0.0, 1.0);
    out.uv = in.pos * 0.5 + 0.5;
    return out;
}
fragment float4 fs_main(VOut in [[stage_in]],
                        constant U0& u [[buffer(1)]],
                        texture2d<float> tex0 [[texture(0)]],
                        sampler samp [[sampler(0)]]) {
    float2 uv = float2(in.uv.x, u.px.z > 0.5 ? 1.0 - in.uv.y : in.uv.y);
    float3 centre = tex0.sample(samp, uv).rgb;
    if (max(centre.r, max(centre.g, centre.b)) > 0.03) discard_fragment();
    float3 nearest = float3(0.0);
    float weight = 0.0;
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            float3 candidate = tex0.sample(samp,
                uv + float2(float(x), float(y)) * u.px.xy).rgb;
            float power = max(candidate.r, max(candidate.g, candidate.b));
            if (power > weight) { weight = power; nearest = candidate; }
        }
    }
    if (weight < 0.04) discard_fragment();
    return float4(nearest, 0.94);
}
)";
} // namespace aiverse::render::selection_outline
