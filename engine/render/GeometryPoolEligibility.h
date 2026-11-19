#pragma once
// Shared GPU geometry pool eligibility for rigid models; usable by any genre
// or asset importer before upload. A model may select a different LOD each
// frame without moving its vertex/index data or bypassing normal draw culling.
#include "Model.h"
#include <cmath>
#include <cstddef>

namespace aiverse::render {

// Keep clip/skin/morph, geometry clusters and authored material variants on
// the legacy dedicated-buffer path for now. A rigid LOD chain only reselects
// indexed triangles within the SAME immutable Model vertex/index arrays.
// The renderer still checks ModelDraw pose/material/culling separately.
inline bool rigidGeometryPoolCandidate(const Model& model,
                                       bool allowRigidLods) {
    if (model.geometry || !model.skins.empty() || !model.clips.empty() ||
        !model.variants.empty() || model.morphTex.valid())
        return false;
    for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
        const MeshDef& mesh = model.meshes[mi];
        if (mesh.deformable) return false;
        if (!mesh.lods.empty() && !allowRigidLods) return false;
        for (const ModelLod& level : mesh.lods) {
            if (level.mesh < 0 || size_t(level.mesh) >= model.meshes.size() ||
                size_t(level.mesh) == mi || !std::isfinite(level.maxPixels) ||
                level.maxPixels <= 0.0f)
                return false;
        }
        for (const Primitive& primitive : mesh.primitives)
            if (primitive.morphTargets || !primitive.jointPalette.empty() ||
                !primitive.variantMaterial.empty())
                return false;
    }
    return true;
}

} // namespace aiverse::render
