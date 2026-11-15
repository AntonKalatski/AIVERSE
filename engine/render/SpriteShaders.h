// Sprite / tile shaders (SpriteBatch), GLSL ES 3.00 bodies and MSL, same layout as Shaders.h.
//
//   vertex: pos (float3), uv (float2), color (ubyte4n, multiplies), team (ubyte4n: rgb team colour,
//           a = strength of the team remap)
//   U0:     mat4 viewProj; vec4 params (x alpha cutoff)
//   tex0:   RGBA sheet / atlas
//   U2 + tex1: dynamic lights (LightShaders.h, bound by the 2D view): day/night tint and the light
//           map at the sprite's world position. Unbound: sprites are drawn as before.
//
// Team colour: pixels painted in shades of magenta (r == b, g low) are replaced by the team colour
// times their brightness, like the remap ranges of classic palette games. One texture fetch, no
// branches besides the alpha test: cheap on weak mobile GPUs.
//
// Lit variant (kSpriteLit*, games with "sprites": {"lit": true}): the same colour times the light
// at the sprite relative to a fixed daylight reference, so a sprite in full default sun keeps its
// painted colours and darkens in shadows, at night or tints with point lights like the terrain.
//   vertex: + rpos (float4): xyz receive point (on the upright shadow caster quad, or the ground
//           for decals and tiles), w > 0: upright (offset towards the sun by w + filter size)
//   U0:     mat4 viewProj; vec4 params (x alpha cutoff, y receive sun shadows); vec4 ref (rgb ref light)
//   U1 / U2 / tex0 / tex1: as the lit shader (Renderer3D::bindLitFrame); tex2: sheet
// Shadow caster (kSpriteCaster*): alpha-tested quads into the sun shadow map (depth only).
#pragma once
#include "Shaders.h"

namespace aiverse::render::shaders {

inline const char* kSpriteVS = R"(
layout(std140) uniform U0 { mat4 u_viewProj; vec4 u_params; };
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_col;
layout(location = 3) in vec4 a_team;
out vec2 v_uv;
out vec4 v_col;
out vec4 v_team;
out vec3 v_wpos;
void main() { v_uv = a_uv; v_col = a_col; v_team = a_team; v_wpos = a_pos; gl_Position = u_viewProj * vec4(a_pos, 1.0); }
)";

inline const char* kSpriteFS = AIVERSE_GLSL_LIGHTS_AT("U2", "tex1") R"(
layout(std140) uniform U0 { mat4 u_viewProj; vec4 u_params; };
uniform sampler2D tex0;
in vec2 v_uv;
in vec4 v_col;
in vec4 v_team;
in vec3 v_wpos;
out vec4 o_color;
void main() {
    vec4 t = texture(tex0, v_uv);
    if (t.a < u_params.x) discard;
    float k = clamp((min(t.r, t.b) - t.g) * 4.0, 0.0, 1.0) * clamp(1.0 - abs(t.r - t.b) * 4.0, 0.0, 1.0) * v_team.a;
    vec3 c = mix(t.rgb, v_team.rgb * max(t.r, t.b), k);
    c *= unlitTint() + mapLight(v_wpos) * 0.8;   // lights (LightShaders.h)
    o_color = rtsDisplay(vec4(c, t.a) * v_col);
}
)";

inline const char* kSpriteMSL = "#include <metal_stdlib>\nusing namespace metal;\n" AIVERSE_MSL_LIGHTS R"(
struct U0 { float4x4 viewProj; float4 params; };
struct VIn { float3 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; float4 col [[attribute(2)]]; float4 team [[attribute(3)]]; };
struct VOut { float4 pos [[position]]; float2 uv; float4 col; float4 team; float3 wpos; };
vertex VOut vs_main(VIn in [[stage_in]], constant U0& u [[buffer(1)]]) {
    VOut o; o.pos = u.viewProj * float4(in.pos, 1.0); o.uv = in.uv; o.col = in.col; o.team = in.team; o.wpos = in.pos; return o;
}
fragment float4 fs_main(VOut in [[stage_in]], constant U0& u [[buffer(1)]],
                        texture2d<float> tex0 [[texture(0)]], sampler s0 [[sampler(0)]],
                        constant LightU& l [[buffer(3)]], texture2d<float> tex1 [[texture(1)]], sampler s1 [[sampler(1)]]) {
    float4 t = tex0.sample(s0, in.uv);
    if (t.a < u.params.x) discard_fragment();
    float k = clamp((min(t.r, t.b) - t.g) * 4.0, 0.0, 1.0) * clamp(1.0 - abs(t.r - t.b) * 4.0, 0.0, 1.0) * in.team.a;
    float3 c = mix(t.rgb, in.team.rgb * max(t.r, t.b), k);
    c *= unlitTint(l) + mapLight(l, tex1, s1, in.wpos) * 0.8;
    return rtsDisplay(float4(c, t.a) * in.col);
}
)";

#define AIVERSE_GLSL_SPRITE_LIT_U0 "layout(std140) uniform U0 { mat4 u_spriteVP; vec4 u_sp; vec4 u_ref; };\n"

inline const char* kSpriteLitVS =
    AIVERSE_GLSL_SPRITE_LIT_U0
    R"(
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_col;
layout(location = 3) in vec4 a_team;
layout(location = 4) in vec4 a_rpos;
out vec2 v_uv;
out vec4 v_col;
out vec4 v_team;
out vec4 v_rpos;
void main() { v_uv = a_uv; v_col = a_col; v_team = a_team; v_rpos = a_rpos; gl_Position = u_spriteVP * vec4(a_pos, 1.0); }
)";

inline const char* kSpriteLitFS =
    AIVERSE_GLSL_SPRITE_LIT_U0 AIVERSE_GLSL_FRAME AIVERSE_GLSL_SHADOW AIVERSE_GLSL_LIGHTS
    R"(
uniform sampler2D tex2;
in vec2 v_uv;
in vec4 v_col;
in vec4 v_team;
in vec4 v_rpos;
out vec4 o_color;
void main() {
    vec4 t = texture(tex2, v_uv);
    if (t.a < u_sp.x) discard;
    float k = clamp((min(t.r, t.b) - t.g) * 4.0, 0.0, 1.0) * clamp(1.0 - abs(t.r - t.b) * 4.0, 0.0, 1.0) * v_team.a;
    vec3 c = mix(t.rgb, v_team.rgb * max(t.r, t.b), k);
    vec3 L = normalize(u_sunDir.xyz);
    bool upright = v_rpos.w > 0.0;
    // upright sprites: receive at their caster quad, moved towards the sun past the filter footprint
    // (never shadowed by their own caster); ground decals / tiles: like the terrain
    vec3 n = upright ? L : vec3(0.0, 1.0, 0.0);
    vec3 p = v_rpos.xyz + (upright ? L * (v_rpos.w + u_quality.w * 3.0) : vec3(0.0));
    float sh = u_sp.y > 0.5 ? shadowAt(p, n) : 1.0;
    sh *= cloudShade(p);
    vec3 sun = u_sunColor.rgb * u_sunColor.rgb * u_sunDir.w;
    vec3 light = u_skyColor.rgb * u_skyColor.rgb + sun * (max(L.y, 0.0) * sh);
    light += dynamicLight(p, vec3(0.0, 1.0, 0.0), normalize(u_camPos.xyz - p), 0.0);
#if AIVERSE_HDR
    o_color = vec4(rtsSrgbDecode(c * v_col.rgb) * min(light / u_ref.rgb, vec3(2.56)), t.a * v_col.a);
    return;
#endif
    c *= min(sqrt(light / u_ref.rgb), vec3(1.6));   // linear ratio -> display (~sqrt)
    o_color = rtsDisplay(vec4(c, t.a) * v_col);
}
)";

inline const char* kSpriteLitMSL =
    "#include <metal_stdlib>\nusing namespace metal;\n"
    "struct SU0 { float4x4 viewProj; float4 sp; float4 ref; };\n"
    AIVERSE_MSL_FRAME AIVERSE_MSL_SHADOW AIVERSE_MSL_LIGHTS
    R"(
struct VIn { float3 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; float4 col [[attribute(2)]]; float4 team [[attribute(3)]]; float4 rpos [[attribute(4)]]; };
struct VOut { float4 pos [[position]]; float2 uv; float4 col; float4 team; float4 rpos; };
vertex VOut vs_main(VIn in [[stage_in]], constant SU0& u [[buffer(1)]]) {
    VOut o; o.pos = u.viewProj * float4(in.pos, 1.0); o.uv = in.uv; o.col = in.col; o.team = in.team; o.rpos = in.rpos; return o;
}
fragment float4 fs_main(VOut in [[stage_in]], constant SU0& u [[buffer(1)]], constant U1& f [[buffer(2)]],
                        depth2d<float> tex0 [[texture(0)]], sampler s0 [[sampler(0)]],
                        constant LightU& l [[buffer(3)]], texture2d<float> tex1 [[texture(1)]], sampler s1 [[sampler(1)]],
                        texture2d<float> tex2 [[texture(2)]], sampler s2 [[sampler(2)]]) {
    float4 t = tex2.sample(s2, in.uv);
    if (t.a < u.sp.x) discard_fragment();
    float k = clamp((min(t.r, t.b) - t.g) * 4.0, 0.0, 1.0) * clamp(1.0 - abs(t.r - t.b) * 4.0, 0.0, 1.0) * in.team.a;
    float3 c = mix(t.rgb, in.team.rgb * max(t.r, t.b), k);
    float3 L = normalize(f.sunDir.xyz);
    bool upright = in.rpos.w > 0.0;
    float3 n = upright ? L : float3(0.0, 1.0, 0.0);
    float3 p = in.rpos.xyz + (upright ? L * (in.rpos.w + f.quality.w * 3.0) : float3(0.0));
    float sh = u.sp.y > 0.5 ? shadowAt(f, p, n, in.pos.xy, tex0, s0) : 1.0;
    sh *= cloudShade(l, p);
    float3 sun = f.sunColor.rgb * f.sunColor.rgb * f.sunDir.w;
    float3 light = f.skyColor.rgb * f.skyColor.rgb + sun * (max(L.y, 0.0) * sh);
    light += dynamicLight(l, tex1, s1, p, float3(0.0, 1.0, 0.0), normalize(f.camPos.xyz - p), 0.0);
#if AIVERSE_HDR
    return float4(rtsSrgbDecode(c * in.col.rgb) * min(light / u.ref.rgb, float3(2.56)), t.a * in.col.a);
#endif
    c *= min(sqrt(light / u.ref.rgb), float3(1.6));
    return rtsDisplay(float4(c, t.a) * in.col);
}
)";

// Standing water decals (SpriteBatch::drawMesh(..., kSpriteWater)): puddles on roads. The sheet's alpha is the
// pool's shape, its colour the water's own dark tint; on top the water mirrors what the lit shader's water mirrors:
// the sky (by the angle), the sun and the lights (sharp highlights, the light map's pools glittering on small
// ripples) and, when the High light tier has drawn the planar reflection this frame, the cars, buildings and lamp
// glows in it (LightShaders.h waterReflect; the reflection plane is the water level, puddles lie within 0.04 of it).
//   same vertices / U0 / U1 / U2 / tex0..tex2 as the lit sprites; tex3: the planar reflection (black: none)
inline const char* kSpriteWaterFS =
    AIVERSE_GLSL_SPRITE_LIT_U0 AIVERSE_GLSL_FRAME AIVERSE_GLSL_SHADOW AIVERSE_GLSL_LIGHTS AIVERSE_GLSL_WATER_REFLECT("tex3")
    R"(
uniform sampler2D tex2;
in vec2 v_uv;
in vec4 v_col;
in vec4 v_team;
in vec4 v_rpos;
out vec4 o_color;
void main() {
    vec4 t = texture(tex2, v_uv);
    float a = t.a * v_col.a;
    if (a < u_sp.x) discard;
    vec3 p = v_rpos.xyz;
    // small ripples drifting with the wind (the cloud offset), stronger where the pool is deep
    vec3 n = normalize(vec3(sin(p.z * 19.0 + l_cloud.x * 13.0) * 0.03, 1.0, sin(p.x * 17.0 - l_cloud.y * 11.0) * 0.03));
    vec3 V = normalize(u_camPos.xyz - p);
    vec3 L = normalize(u_sunDir.xyz);
    float sh = u_sp.y > 0.5 ? shadowAt(p, vec3(0.0, 1.0, 0.0)) : 1.0;
    sh *= cloudShade(p);
    vec3 sun = rtsDecodeScene(u_sunColor.rgb) * u_sunDir.w;
    vec3 sky = rtsDecodeScene(u_skyColor.rgb);
    vec3 base = rtsDecodeScene(t.rgb * v_col.rgb);
    float fres = 0.10 + 0.55 * pow(1.0 - max(dot(n, V), 0.0), 4.0);
    vec3 H = normalize(L + V);
    vec3 col = base * (sky + sun * max(L.y, 0.0) * sh) * 0.35;         // the bed seen through the water
    col += sky * fres * 0.55;                                          // the sky
    col += sun * sh * (pow(max(dot(n, H), 0.0), 220.0) * 2.0 + pow(max(dot(n, H), 0.0), 40.0) * 0.08);
    col += dynamicLight(p, n, V, 0.95) * 0.7;                          // lamps: highlights and glitter
    float dist = length(u_camPos.xyz - p);
    col = mix(col, rtsDecodeScene(u_fog.rgb), 1.0 - exp(-dist * u_fog.w));
    col = rtsToneScene(col, u_skyColor.w > 0.5);
    vec3 outc = waterReflect(rtsEncodeScene(col), vec3(p.x, 0.0, p.z), n, V, 0.95);
    o_color = vec4(outc, a);
}
)";

inline const char* kSpriteWaterMSL =
    "#include <metal_stdlib>\nusing namespace metal;\n"
    "struct SU0 { float4x4 viewProj; float4 sp; float4 ref; };\n"
    AIVERSE_MSL_FRAME AIVERSE_MSL_SHADOW AIVERSE_MSL_LIGHTS AIVERSE_MSL_WATER_REFLECT
    R"(
struct VIn { float3 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; float4 col [[attribute(2)]]; float4 team [[attribute(3)]]; float4 rpos [[attribute(4)]]; };
struct VOut { float4 pos [[position]]; float2 uv; float4 col; float4 team; float4 rpos; };
vertex VOut vs_main(VIn in [[stage_in]], constant SU0& u [[buffer(1)]]) {
    VOut o; o.pos = u.viewProj * float4(in.pos, 1.0); o.uv = in.uv; o.col = in.col; o.team = in.team; o.rpos = in.rpos; return o;
}
fragment float4 fs_main(VOut in [[stage_in]], constant SU0& u [[buffer(1)]], constant U1& f [[buffer(2)]],
                        depth2d<float> tex0 [[texture(0)]], sampler s0 [[sampler(0)]],
                        constant LightU& l [[buffer(3)]], texture2d<float> tex1 [[texture(1)]], sampler s1 [[sampler(1)]],
                        texture2d<float> tex2 [[texture(2)]], sampler s2 [[sampler(2)]],
                        texture2d<float> tex3 [[texture(3)]], sampler s3 [[sampler(3)]]) {
    float4 t = tex2.sample(s2, in.uv);
    float a = t.a * in.col.a;
    if (a < u.sp.x) discard_fragment();
    float3 p = in.rpos.xyz;
    float3 n = normalize(float3(sin(p.z * 19.0 + l.cloud.x * 13.0) * 0.03, 1.0, sin(p.x * 17.0 - l.cloud.y * 11.0) * 0.03));
    float3 V = normalize(f.camPos.xyz - p);
    float3 L = normalize(f.sunDir.xyz);
    float sh = u.sp.y > 0.5 ? shadowAt(f, p, float3(0.0, 1.0, 0.0), in.pos.xy, tex0, s0) : 1.0;
    sh *= cloudShade(l, p);
    float3 sun = rtsDecodeScene(f.sunColor.rgb) * f.sunDir.w;
    float3 sky = rtsDecodeScene(f.skyColor.rgb);
    float3 base = rtsDecodeScene(t.rgb * in.col.rgb);
    float fres = 0.10 + 0.55 * pow(1.0 - max(dot(n, V), 0.0), 4.0);
    float3 H = normalize(L + V);
    float3 col = base * (sky + sun * max(L.y, 0.0) * sh) * 0.35;
    col += sky * fres * 0.55;
    col += sun * sh * (pow(max(dot(n, H), 0.0), 220.0) * 2.0 + pow(max(dot(n, H), 0.0), 40.0) * 0.08);
    col += dynamicLight(l, tex1, s1, p, n, V, 0.95) * 0.7;
    float dist = length(f.camPos.xyz - p);
    col = mix(col, rtsDecodeScene(f.fog.rgb), 1.0 - exp(-dist * f.fog.w));
    col = rtsToneScene(col, f.skyColor.w > 0.5);
    float3 outc = waterReflect(l, tex3, s3, in.pos.xy, rtsEncodeScene(col), float3(p.x, 0.0, p.z), n, V, 0.95);
    return float4(outc, a);
}
)";

// Shadow caster: depth only, alpha-tested against the sheet (same vertex layout as the sprites).
inline const char* kSpriteCasterVS = R"(
layout(std140) uniform U0 { mat4 u_viewProj; vec4 u_params; };
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
out vec2 v_uv;
void main() { v_uv = a_uv; gl_Position = u_viewProj * vec4(a_pos, 1.0); }
)";

inline const char* kSpriteCasterFS = R"(
layout(std140) uniform U0 { mat4 u_viewProj; vec4 u_params; };
uniform sampler2D tex0;
in vec2 v_uv;
void main() { if (texture(tex0, v_uv).a < u_params.x) discard; }
)";

inline const char* kSpriteCasterMSL = R"(
#include <metal_stdlib>
using namespace metal;
struct U0 { float4x4 viewProj; float4 params; };
struct VIn { float3 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; };
struct VOut { float4 pos [[position]]; float2 uv; };
vertex VOut vs_main(VIn in [[stage_in]], constant U0& u [[buffer(1)]]) {
    VOut o; o.pos = u.viewProj * float4(in.pos, 1.0); o.uv = in.uv; return o;
}
fragment void fs_main(VOut in [[stage_in]], constant U0& u [[buffer(1)]], texture2d<float> tex0 [[texture(0)]], sampler s0 [[sampler(0)]]) {
    if (tex0.sample(s0, in.uv).a < u.params.x) discard_fragment();
}
)";

} // namespace aiverse::render::shaders
