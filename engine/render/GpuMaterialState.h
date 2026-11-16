// GPU material equivalence for batching. Material object identity is irrelevant:
// only shader programs, pipeline state, uniforms and resolved texture resources matter.
#pragma once
#include "Model.h"
#include <tuple>

namespace aiverse::render {

inline bool equivalentGpuMaterial(const Model& ma, const Material& a,
                                  const Model& mb, const Material& b) {
    if (a.shaderGraph != b.shaderGraph) {
        if (!a.shaderGraph || !b.shaderGraph) return false;
        const auto& g = *a.shaderGraph;
        const auto& h = *b.shaderGraph;
        if (g.domain != h.domain || g.vertexGlsl != h.vertexGlsl ||
            g.vertexMsl != h.vertexMsl || g.glsl != h.glsl ||
            g.msl != h.msl || g.outputs != h.outputs ||
            g.textures != h.textures || g.minQuality != h.minQuality) return false;
    }
    auto state = [](const Material& m) {
        return std::tie(m.shading, m.alpha, m.alphaCutoff, m.doubleSided,
            m.vertexOcclusion, m.backOcclusion, m.vertexColors,
            m.castShadow, m.receiveShadow, m.transparent,
            m.team, m.teamStrength, m.normalScale, m.occlusionStrength,
            m.baseColor.x, m.baseColor.y, m.baseColor.z, m.baseColor.w,
            m.metallic, m.roughness, m.emissive.x, m.emissive.y,
            m.emissive.z, m.emissiveStrength, m.uvOffset.x,
            m.uvOffset.y, m.uvScale.x, m.uvScale.y, m.uvRotation,
            m.fixedLight.x, m.fixedLight.y, m.fixedLight.z, m.fixedLight.w,
            m.fixedShade.x, m.fixedShade.y);
    };
    if (state(a) != state(b)) return false;
    auto texture = [](const Model& model, int slot) -> uint32_t {
        return slot >= 0 && size_t(slot) < model.images.size()
            ? model.images[size_t(slot)].tex.id : 0;
    };
    const int ta[] = {a.baseTex, a.mrTex, a.normalTex, a.occlusionTex, a.emissiveTex, a.teamTex};
    const int tb[] = {b.baseTex, b.mrTex, b.normalTex, b.occlusionTex, b.emissiveTex, b.teamTex};
    for (size_t i = 0; i < 6; ++i)
        if (texture(ma, ta[i]) != texture(mb, tb[i])) return false;
    if (a.shaderGraph) {
        for (size_t i = 0; i < a.shaderGraph->textures.size() && i < 4; ++i)
            if (texture(ma, a.graphTextures[i]) != texture(mb, b.graphTextures[i])) return false;
    }
    return true;
}

} // namespace aiverse::render
