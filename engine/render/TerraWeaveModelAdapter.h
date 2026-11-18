// TerraWeave Arbor -> standard AIVERSE render::Model adapter.
// Render-only conversion. CPU-safe; call render::uploadModel(dev, model) on
// the owning render thread. Uses existing model LOD/instancing/RHI pipeline.
#pragma once
#include "engine/world/TerraWeaveArborMesh.h"
#include "engine/render/Model.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace aiverse::terraweave {

inline std::unique_ptr<render::Model> makeArborModel(
        const TreeProfile& profile, uint64_t seed,
        ArborMeshSettings meshSettings = {}) {
    auto model = std::make_unique<render::Model>();
    model->path = "terraweave/arbor/" + std::to_string(profile.speciesId) + "/" +
                  std::to_string(seed);
    model->batching.autoLod = false; // explicit generated geometric LOD meshes
    render::Material bark;
    bark.name = "TerraWeave Bark";
    bark.baseColor = {0.31f, 0.18f, 0.11f, 1.0f};
    bark.roughness = 0.9f;
    render::Material foliage;
    foliage.name = "TerraWeave Foliage";
    foliage.baseColor = {0.21f, 0.43f, 0.16f, 1.0f};
    foliage.roughness = 0.85f;
    foliage.doubleSided = true;
    // Opaque fallback until a project assigns an alpha-masked leaf texture.
    // The crossed cards are visible (green rectangles), not fake alpha masks.
    foliage.alpha = render::AlphaMode::Opaque;
    model->materials.push_back(bark);
    model->materials.push_back(foliage);

    for (TreeLOD tier : {TreeLOD::High, TreeLOD::Medium, TreeLOD::Low}) {
        const ArborMesh geometry = buildArborMesh(generateTree(profile, seed, tier),
                                                  meshSettings);
        const uint32_t baseVertex = static_cast<uint32_t>(model->vertices.size());
        const uint32_t baseIndex = static_cast<uint32_t>(model->indices.size());
        uint32_t barkVertices = 0;
        bool leavesStarted = false;
        for (const ArborVertex& v : geometry.vertices) {
            if (v.material > 1 || (leavesStarted && v.material == 0))
                throw std::logic_error("TerraWeave Arbor vertex material ordering");
            if (v.material == 0) ++barkVertices;
            else leavesStarted = true;
            render::ModelVertex vertex;
            // Render coordinates are metres. Generator data is in centimetres.
            vertex.pos = {v.x * 0.01f, v.y * 0.01f, v.z * 0.01f};
            vertex.nrm = {v.nx, v.ny, v.nz};
            vertex.uv = {v.u, v.v};
            model->vertices.push_back(vertex);
        }
        uint32_t barkIndexCount = 0;
        bool foundLeafIndex = false;
        for (uint32_t index : geometry.indices) {
            if (index >= geometry.vertices.size())
                throw std::logic_error("TerraWeave Arbor invalid mesh index");
            const uint8_t mat = geometry.vertices[index].material;
            if (mat == 1) foundLeafIndex = true;
            else if (foundLeafIndex)
                throw std::logic_error("TerraWeave Arbor mixed index materials");
            if (!foundLeafIndex) ++barkIndexCount;
            model->indices.push_back(baseVertex + index);
        }
        render::MeshDef mesh;
        mesh.name = tier == TreeLOD::High ? "Arbor LOD0" :
                    tier == TreeLOD::Medium ? "Arbor LOD1" : "Arbor LOD2";
        if (barkIndexCount) {
            render::Primitive primitive;
            primitive.material = 0;
            primitive.firstVertex = baseVertex;
            primitive.vertexCount = barkVertices;
            primitive.firstIndex = baseIndex;
            primitive.indexCount = barkIndexCount;
            mesh.primitives.push_back(primitive);
        }
        const uint32_t leafCount = static_cast<uint32_t>(geometry.indices.size()) -
                                   barkIndexCount;
        if (leafCount) {
            render::Primitive primitive;
            primitive.material = 1;
            primitive.firstVertex = baseVertex + barkVertices;
            primitive.vertexCount = static_cast<uint32_t>(geometry.vertices.size()) -
                                    barkVertices;
            primitive.firstIndex = baseIndex + barkIndexCount;
            primitive.indexCount = leafCount;
            mesh.primitives.push_back(primitive);
        }
        model->meshes.push_back(std::move(mesh));
    }
    // Renderer3D uses screen-size LOD selection with its existing fade policy.
    // Future billboard atlas should be added as LOD3, not a naked empty mesh.
    model->meshes[0].lods = {{1, 128.0f}, {2, 32.0f}};
    render::Node node;
    node.name = "TerraWeave Arbor";
    node.mesh = 0;
    model->nodes.push_back(std::move(node));
    model->finalize();
    return model;
}

} // namespace aiverse::terraweave
