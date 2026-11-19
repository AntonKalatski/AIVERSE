#pragma once
// Shared GPU geometry pool eligibility for rigid models; usable by any genre
// or asset importer before upload. Never caches camera/LOD/material decisions.
#include "Model.h"
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace aiverse::render {

// CPU-only, read-only diagnostic. No geometry page or GPU handle is allocated.
enum class RigidGeometryPoolBlock : uint8_t {
    None = 0, ClusterGeometry, Skin, AnimationClips, MaterialVariants,
    MorphTexture, DeformableMesh, RigidLodsDisabled, InvalidLodReference,
    MorphPrimitive, JointPalette, PrimitiveVariant
};

inline const char* rigidGeometryPoolBlockName(RigidGeometryPoolBlock reason) {
    switch (reason) {
    case RigidGeometryPoolBlock::None: return "eligible rigid geometry";
    case RigidGeometryPoolBlock::ClusterGeometry: return "streamed/cluster geometry";
    case RigidGeometryPoolBlock::Skin: return "skinned geometry";
    case RigidGeometryPoolBlock::AnimationClips: return "authored animation clips";
    case RigidGeometryPoolBlock::MaterialVariants: return "authored material variants";
    case RigidGeometryPoolBlock::MorphTexture: return "morph texture";
    case RigidGeometryPoolBlock::DeformableMesh: return "deformable mesh";
    case RigidGeometryPoolBlock::RigidLodsDisabled: return "rigid LOD pooling disabled";
    case RigidGeometryPoolBlock::InvalidLodReference: return "invalid rigid LOD reference/threshold";
    case RigidGeometryPoolBlock::MorphPrimitive: return "morph target primitive";
    case RigidGeometryPoolBlock::JointPalette: return "skinned primitive joint palette";
    case RigidGeometryPoolBlock::PrimitiveVariant: return "primitive material variants";
    }
    return "unknown geometry pool exclusion";
}

// Always conservative: authored clips, skins, morphs, clusters and variants
// still require dedicated buffers. Rigid LODs only change indexed triangles
// inside the original immutable GPU geometry. Renderer3D continues to select
// visibility/LOD and check every material/MDI submission independently.
inline RigidGeometryPoolBlock rigidGeometryPoolBlock(const Model& model,
                                                    bool allowRigidLods) {
    if (model.geometry) return RigidGeometryPoolBlock::ClusterGeometry;
    if (!model.skins.empty()) return RigidGeometryPoolBlock::Skin;
    if (!model.clips.empty()) return RigidGeometryPoolBlock::AnimationClips;
    if (!model.variants.empty()) return RigidGeometryPoolBlock::MaterialVariants;
    if (model.morphTex.valid()) return RigidGeometryPoolBlock::MorphTexture;
    for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
        const MeshDef& mesh = model.meshes[mi];
        if (mesh.deformable) return RigidGeometryPoolBlock::DeformableMesh;
        if (!mesh.lods.empty() && !allowRigidLods)
            return RigidGeometryPoolBlock::RigidLodsDisabled;
        for (const ModelLod& level : mesh.lods)
            if (level.mesh < 0 || size_t(level.mesh) >= model.meshes.size() ||
                size_t(level.mesh) == mi || !std::isfinite(level.maxPixels) ||
                level.maxPixels <= 0.0f)
                return RigidGeometryPoolBlock::InvalidLodReference;
        for (const Primitive& primitive : mesh.primitives) {
            if (primitive.morphTargets) return RigidGeometryPoolBlock::MorphPrimitive;
            if (!primitive.jointPalette.empty()) return RigidGeometryPoolBlock::JointPalette;
            if (!primitive.variantMaterial.empty()) return RigidGeometryPoolBlock::PrimitiveVariant;
        }
    }
    return RigidGeometryPoolBlock::None;
}

inline bool rigidGeometryPoolCandidate(const Model& model, bool allowRigidLods) {
    return rigidGeometryPoolBlock(model, allowRigidLods) == RigidGeometryPoolBlock::None;
}

} // namespace aiverse::render
