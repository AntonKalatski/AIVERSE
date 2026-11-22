// Per-source choice between STRATA clusters and the ordinary LOD/instancing
// path. STRATA wins on big unique opaque meshes (scans, buildings, cliffs):
// clusters are refined by screen error and no authored LODs are needed. The
// ordinary path wins on small or many-copy assets and on alpha-cut foliage:
// instancing draws every copy of a level in one call, and clusters cannot
// simplify cut-out cards (every tree keeps its finest leaves).
// Decided once per source when a geometry scene loads with "strata.select":
// "auto" (the default); "all" keeps the old every-source behaviour; an
// instance "strata": true/false forces its model either way.
#pragma once
#include "Model.h"
#include <cstdint>
#include <string>

namespace aiverse::render {

struct StrataAutoSelectConfig {
    uint64_t minTriangles = 60000;           // below: LOD path
    uint64_t instancedMaxTriangles = 250000; // below, with many copies or authored LODs: LOD path
    uint32_t instancedMinCount = 8;          // "many copies"
    float maxAlphaShare = 0.25f;             // cut-out/blended triangle share above: LOD path
};

struct StrataSourceFacts {
    uint64_t finestTriangles = 0;  // triangles of every mesh node at its finest level
    uint64_t alphaTriangles = 0;   // of them, alpha-masked or blended
    bool authoredLods = false;     // a mesh already has a LOD chain at load time
    size_t instances = 0;          // placements of this source in the scene
};

inline StrataSourceFacts strataSourceFacts(const Model& m, size_t instances) {
    StrataSourceFacts f;
    f.instances = instances;
    for (const auto& node : m.nodes) {
        if (node.mesh < 0 || size_t(node.mesh) >= m.meshes.size()) continue;
        const MeshDef& mesh = m.meshes[size_t(node.mesh)];
        if (!mesh.lods.empty()) f.authoredLods = true;
        for (const auto& p : mesh.primitives) {
            const uint64_t tris = p.indexCount / 3;
            f.finestTriangles += tris;
            if (p.material >= 0 && size_t(p.material) < m.materials.size()) {
                const Material& mat = m.materials[size_t(p.material)];
                if (mat.alpha != AlphaMode::Opaque || mat.transparent) f.alphaTriangles += tris;
            }
        }
    }
    return f;
}

inline bool strataPreferred(const StrataSourceFacts& f, const StrataAutoSelectConfig& c = {},
                            std::string* why = nullptr) {
    auto pick = [&](bool strata, const char* reason) { if (why) *why = reason; return strata; };
    if (f.finestTriangles == 0) return pick(false, "no triangles");
    if (double(f.alphaTriangles) > double(c.maxAlphaShare) * double(f.finestTriangles))
        return pick(false, "alpha-cut foliage: clusters cannot simplify cut-out cards");
    if (f.finestTriangles < c.minTriangles)
        return pick(false, "small mesh: an instanced LOD chain is cheaper");
    if (f.finestTriangles < c.instancedMaxTriangles && f.instances >= c.instancedMinCount)
        return pick(false, "many copies: instancing draws each LOD level once");
    if (f.finestTriangles < c.instancedMaxTriangles && f.authoredLods)
        return pick(false, "authored LOD chain");
    return pick(true, "large unique mesh: clusters refine it by screen error");
}

} // namespace aiverse::render
