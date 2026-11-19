#pragma once
// CPU-only eligibility contract for an optional per-entity rigid transform cache.
// The renderer still performs per-view culling, LOD, material selection and submission.
#include "Renderer3D.h"
#include <algorithm>
#include <cstring>

namespace aiverse::render {

// An explicit stable identity is mandatory. Never cache animation, morph, skin,
// node-edited or runtime-mutable hierarchy transforms under an entity ID.
inline bool staticModelCacheCandidate(const ModelDraw& d, bool activeDisplacement) {
    const Model* m = d.model;
    if (!m || !d.persistentId || !(d.flags & kDrawStatic) || activeDisplacement ||
        d.nodeWorld || d.pose || d.baked || !m->skins.empty() ||
        !m->clips.empty() || m->geometry || m->nodes.empty())
        return false;
    return std::none_of(m->nodes.begin(), m->nodes.end(),
                        [](const Node& node) { return node.runtimeMutable; });
}

// The cache contains only geometry-derived transforms, bounds and offsets.
// Material copies / per-node material overrides do not invalidate transforms;
// *active vertex displacement* does, and must bypass cached bounds as well.
inline bool staticModelCacheGeometryCompatible(const StaticModelCache& cache,
                                                const ModelDraw& d,
                                                bool activeDisplacement) {
    const Model* m = d.model;
    return m && cache.model == m && !activeDisplacement &&
        !d.nodeWorld && !d.pose && !d.baked && m->skins.empty() &&
        m->clips.empty() && cache.transforms.size() == m->nodes.size() &&
        cache.primitiveOffsets.size() == m->meshes.size() &&
        std::memcmp(cache.world.m, d.world.m, sizeof(d.world.m)) == 0;
}

// Packet replay is an additional explicit contract. Because the source can be
// changed in-place by a native project, caller-controlled staticPacketRevision
// must be nonzero and advance whenever its appearance/material data changes.
// Source meshes with runtime LOD/skin/morph require live per-frame assembly.
inline bool staticRenderPacketCandidate(const ModelDraw& d,
                                        bool activeDisplacement, bool bakedLighting,
                                        bool lodDiagnostics) {
    const Model* m = d.model;
    if (!d.staticPacketRevision || d.materials || d.nodeMaterial ||
        d.variant >= 0 || bakedLighting || lodDiagnostics ||
        !staticModelCacheCandidate(d, activeDisplacement)) return false;
    for (const auto& node : m->nodes)
        if (node.skin >= 0 || (node.mesh >= 0 &&
            size_t(node.mesh) >= m->meshes.size())) return false;
    for (const auto& mesh : m->meshes) {
        if (mesh.deformable || !mesh.lods.empty()) return false;
        for (const auto& p : mesh.primitives)
            if (p.morphTargets || !p.jointPalette.empty()) return false;
    }
    return !m->materials.empty() && !m->meshes.empty();
}

inline bool equivalentStaticRenderPacketDraw(const ModelDraw& a,
                                              const ModelDraw& b) {
    const auto eq4 = [](const float4& x, const float4& y) {
        return x.x == y.x && x.y == y.y && x.z == y.z && x.w == y.w;
    };
    const auto eq3 = [](const float3& x, const float3& y) {
        return x.x == y.x && x.y == y.y && x.z == y.z;
    };
    const auto& g = a.geometry;
    const auto& h = b.geometry;
    return a.model == b.model && a.persistentId == b.persistentId &&
        a.staticPacketRevision == b.staticPacketRevision &&
        a.flags == b.flags && a.variant == b.variant &&
        a.flash == b.flash && a.teamGlow == b.teamGlow &&
        a.dissolve == b.dissolve &&
        eq4(a.tint, b.tint) && eq4(a.outline, b.outline) &&
        eq3(a.team, b.team) &&
        g.clusters == h.clusters && g.occlusion == h.occlusion &&
        g.occluder == h.occluder && g.shadowClusters == h.shadowClusters &&
        g.errorPixels == h.errorPixels &&
        g.shadowErrorPixels == h.shadowErrorPixels &&
        std::memcmp(a.world.m, b.world.m, sizeof(a.world.m)) == 0;
}

} // namespace aiverse::render
