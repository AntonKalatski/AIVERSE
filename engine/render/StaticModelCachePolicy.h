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
} // namespace aiverse::render
