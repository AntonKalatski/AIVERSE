#pragma once
// CPU-only immutable model eligibility. This describes the *asset*, not an
// instance: active poses, visibility and material displacement are evaluated
// separately for each ModelDraw. No renderer, backend or genre dependencies.
#include "Model.h"
#include <cstddef>

namespace aiverse::render {

struct ModelStaticEligibility {
    bool transforms = false;
    bool packets = false;
};

// Called on import/revision changes and memoized per Renderer3D. Native code
// mutating Model nodes, skins, LODs, mesh topology or bind matrices in place
// must call Model::markGeometryChanged() before the next presentation draw.
// The Model pointer must also be invalidated before asset destruction.
inline ModelStaticEligibility classifyModelStaticEligibility(const Model& m) {
    ModelStaticEligibility result{};
    if (m.nodes.empty() || m.bindWorld.size() != m.nodes.size() ||
        !m.skins.empty() || m.geometry)
        return result;

    bool packets = !m.materials.empty() && !m.meshes.empty();
    for (const Node& node : m.nodes) {
        if (node.runtimeMutable) return result;
        if (node.skin >= 0 ||
            (node.mesh >= 0 && size_t(node.mesh) >= m.meshes.size()))
            packets = false;
    }
    result.transforms = true;
    if (!packets) return result;

    for (const MeshDef& mesh : m.meshes) {
        if (mesh.deformable) return result;
        for (const Primitive& p : mesh.primitives)
            if (p.morphTargets || !p.jointPalette.empty())
                return result;
    }
    result.packets = true;
    return result;
}

} // namespace aiverse::render
