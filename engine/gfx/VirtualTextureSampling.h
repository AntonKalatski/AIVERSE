// AIVERSE virtual-page sampling snippets. Append to GLSL/MSL shaders that
// bind an RGBA8 Texture2DArray page-table and a sampled Texture2DArray atlas.
// Physical page encoding: R/G = 16-bit physical slice, B = fallback mip,
// A=255 = resident. A=0 means use fallback color.
#pragma once
namespace aiverse::gfx {

inline constexpr const char* kVirtualTextureGLSL = R"AVT(
struct AiverseVirtualPage {
    vec2 uv;
    int layer;
    bool valid;
};
AiverseVirtualPage aiverseLookupVirtual(
    highp sampler2DArray atlas, highp sampler2DArray pageTable,
    vec2 uv, int virtualLayer, float gutterPixels,
    int addressU, int addressV) {
    AiverseVirtualPage p;
    p.uv = vec2(0.5); p.layer = 0; p.valid = false;
    ivec3 tableSize = textureSize(pageTable, 0);
    if (virtualLayer < 0 || virtualLayer >= tableSize.z ||
        tableSize.x <= 0 || tableSize.y <= 0) return p;
    // Match the source sampler's per-axis wrap policy. The physical
    // atlas still uses edge-safe gutter addressing after page lookup.
    vec2 addressed = vec2(
        addressU == 1 ? fract(uv.x) :
            (addressU == 2 ? 1.0 - abs(fract(uv.x * 0.5) * 2.0 - 1.0) : uv.x),
        addressV == 1 ? fract(uv.y) :
            (addressV == 2 ? 1.0 - abs(fract(uv.y * 0.5) * 2.0 - 1.0) : uv.y));
    vec2 bounded = clamp(addressed, vec2(0.0), vec2(0.99999994));
    ivec2 tile = clamp(ivec2(bounded * vec2(tableSize.xy)),
                       ivec2(0), tableSize.xy - ivec2(1));
    vec4 rgba = texelFetch(pageTable, ivec3(tile, virtualLayer), 0);
    ivec4 bytes = ivec4(floor(rgba * 255.0 + 0.5));
    if (bytes.a < 128) return p;
    int physicalSlice = bytes.r + 256 * bytes.g;
    ivec3 atlasSize = textureSize(atlas, 0);
    if (physicalSlice < 0 || physicalSlice >= atlasSize.z ||
        atlasSize.x <= 0 || atlasSize.y <= 0) return p;
    vec2 tileUv = fract(bounded * vec2(tableSize.xy) / exp2(float(bytes.b)));
    vec2 inset = (vec2(max(0.0, gutterPixels)) + vec2(0.5)) / vec2(atlasSize.xy);
    if (any(greaterThanEqual(inset, vec2(0.5)))) return p;
    p.uv = mix(inset, vec2(1.0) - inset, tileUv);
    p.layer = physicalSlice; p.valid = true;
    return p;
}
// Legacy callers retain ClampToEdge on both axes.
AiverseVirtualPage aiverseLookupVirtual(
    highp sampler2DArray atlas, highp sampler2DArray table,
    vec2 uv, int layer, float gutterPixels) {
    return aiverseLookupVirtual(atlas, table, uv, layer, gutterPixels, 0, 0);
}
vec4 aiverseSampleVirtual(
    highp sampler2DArray atlas, highp sampler2DArray table,
    vec2 uv, int layer, float gutterPixels, vec4 fallbackColor,
    int addressU, int addressV) {
    AiverseVirtualPage p = aiverseLookupVirtual(atlas, table, uv, layer, gutterPixels, addressU, addressV);
    return p.valid ? texture(atlas, vec3(p.uv, float(p.layer))) : fallbackColor;
}
vec4 aiverseSampleVirtual(
    highp sampler2DArray atlas, highp sampler2DArray table,
    vec2 uv, int layer, float gutterPixels, vec4 fallbackColor) {
    return aiverseSampleVirtual(atlas, table, uv, layer, gutterPixels,
                                fallbackColor, 0, 0);
}
)AVT";

inline constexpr const char* kVirtualTextureMSL = R"AVT(
struct AiverseVirtualPage {
    float2 uv;
    uint layer;
    bool valid;
};
inline AiverseVirtualPage aiverse_lookup_virtual(
    texture2d_array<float, access::sample> atlas,
    texture2d_array<float, access::read> pageTable,
    float2 uv, uint virtualLayer, float gutterPixels,
    uint addressU, uint addressV) {
    AiverseVirtualPage p;
    p.uv = float2(0.5); p.layer = 0; p.valid = false;
    uint3 tableSize = uint3(pageTable.get_width(),pageTable.get_height(),
                            pageTable.get_array_size());
    if (virtualLayer >= tableSize.z || !tableSize.x || !tableSize.y) return p;
    float2 addressed = float2(
        addressU == 1u ? fract(uv.x) :
            (addressU == 2u ? 1.0 - abs(fract(uv.x * 0.5) * 2.0 - 1.0) : uv.x),
        addressV == 1u ? fract(uv.y) :
            (addressV == 2u ? 1.0 - abs(fract(uv.y * 0.5) * 2.0 - 1.0) : uv.y));
    float2 bounded = clamp(addressed, float2(0.0), float2(0.99999994));
    uint2 tile = min(uint2(bounded * float2(tableSize.xy)), tableSize.xy-uint2(1));
    float4 rgba = pageTable.read(tile,virtualLayer,0);
    uint4 bytes = uint4(floor(clamp(rgba,0.0,1.0)*255.0+0.5));
    if (bytes.a < 128) return p;
    uint physicalSlice = bytes.r + 256u*bytes.g;
    uint3 atlasSize = uint3(atlas.get_width(),atlas.get_height(),
                            atlas.get_array_size());
    if (physicalSlice >= atlasSize.z || !atlasSize.x || !atlasSize.y) return p;
    float2 tileUv = fract(bounded * float2(tableSize.xy) / exp2(float(bytes.b)));
    float2 inset = (float2(max(0.0,gutterPixels))+0.5)/float2(atlasSize.xy);
    if (any(inset >= float2(0.5))) return p;
    p.uv = mix(inset,float2(1.0)-inset,tileUv);
    p.layer = physicalSlice; p.valid = true;
    return p;
}
inline AiverseVirtualPage aiverse_lookup_virtual(
    texture2d_array<float, access::sample> atlas,
    texture2d_array<float, access::read> table,
    float2 uv, uint layer, float gutterPixels) {
    return aiverse_lookup_virtual(atlas, table, uv, layer, gutterPixels, 0u, 0u);
}
inline float4 aiverse_sample_virtual(
    texture2d_array<float, access::sample> atlas,
    texture2d_array<float, access::read> table, sampler atlasSampler,
    float2 uv, uint layer, float gutterPixels, float4 fallbackColor,
    uint addressU, uint addressV) {
    AiverseVirtualPage p = aiverse_lookup_virtual(atlas,table,uv,layer,gutterPixels,addressU,addressV);
    return p.valid ? atlas.sample(atlasSampler,p.uv,p.layer) : fallbackColor;
}
inline float4 aiverse_sample_virtual(
    texture2d_array<float, access::sample> atlas,
    texture2d_array<float, access::read> table, sampler atlasSampler,
    float2 uv, uint layer, float gutterPixels, float4 fallbackColor) {
    return aiverse_sample_virtual(atlas, table, atlasSampler, uv, layer,
                                  gutterPixels, fallbackColor, 0u, 0u);
}
)AVT";

} // namespace aiverse::gfx
