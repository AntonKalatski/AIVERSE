#include "engine/render/TerraWeaveModelAdapter.h"
#include "engine/render/TerraWeaveForestAdapter.h"
#include <cassert>
#include <cmath>
#include <cstdint>

using namespace aiverse::terraweave;

int main() {
    TreeProfile oak;
    oak.speciesId = 1001;
    oak.primaryBranches = 24;
    const auto model = makeArborModel(oak, 0x123456789ULL);
    assert(model);
    assert(model->nodes.size() == 1 && model->nodes[0].mesh == 0);
    assert(model->materials.size() == 2);
    assert(model->meshes.size() == 3);
    assert(model->meshes[0].lods.size() == 2);
    assert(model->meshes[0].lods[0].mesh == 1);
    assert(model->meshes[0].lods[1].mesh == 2);
    assert(!model->batching.autoLod);
    assert(model->boundsMax.y > 5.0f && model->boundsMax.y < 30.0f);
    assert(model->boundsMin.y >= -1.0f);
    assert(model->materials[1].doubleSided);
    assert(model->materials[1].alpha == aiverse::render::AlphaMode::Opaque);
    assert(model->vertices.size() > 100);
    assert(model->indices.size() > 100);
    assert(model->order.size() == 1);
    for (const auto& mesh : model->meshes) {
        assert(mesh.primitives.size() == 2);
        for (const auto& primitive : mesh.primitives) {
            assert(primitive.indexCount > 0 && primitive.indexCount % 3 == 0);
            assert(primitive.firstIndex + primitive.indexCount <= model->indices.size());
            for (uint32_t k = primitive.firstIndex;
                 k < primitive.firstIndex + primitive.indexCount; ++k)
                assert(model->indices[k] < model->vertices.size());
        }
    }
    for (const auto& v : model->vertices) {
        assert(std::isfinite(v.pos.x));
        assert(std::isfinite(v.pos.y));
        assert(std::isfinite(v.pos.z));
    }
    const auto again = makeArborModel(oak, 0x123456789ULL);
    assert(model->vertices.size() == again->vertices.size());
    assert(model->indices == again->indices);

    // Hierarchical BranchLOD: each structural branch has an independent
    // 3-mesh chain, with its original parent and all local leaf clusters.
    TreeProfile hierarchical = oak;
    hierarchical.primaryBranches = 17;
    const auto skeleton = generateTree(hierarchical, 0x987654321ULL, TreeLOD::High);
    const auto branchModel = makeArborBranchLodModel(hierarchical, 0x987654321ULL);
    assert(branchModel && branchModel->nodes.size() == skeleton.branches.size());
    assert(branchModel->meshes.size() == skeleton.branches.size() * 3);
    assert(!branchModel->batching.autoLod);
    for (size_t i = 0; i < skeleton.branches.size(); ++i) {
        const auto& node = branchModel->nodes[i];
        assert(node.mesh == static_cast<int>(i * 3));
        assert(node.parent == skeleton.branches[i].parent);
        const auto& highBranch = branchModel->meshes[i * 3];
        const auto& midBranch = branchModel->meshes[i * 3 + 1];
        const auto& lowBranch = branchModel->meshes[i * 3 + 2];
        assert(highBranch.lods.size() == 2);
        assert(highBranch.lods[0].mesh == static_cast<int>(i * 3 + 1));
        assert(highBranch.lods[1].mesh == static_cast<int>(i * 3 + 2));
        const auto indicesFor = [](const aiverse::render::MeshDef& mesh) {
            uint32_t result = 0;
            for (const auto& prim : mesh.primitives) result += prim.indexCount;
            return result;
        };
        assert(indicesFor(highBranch) > indicesFor(midBranch));
        assert(indicesFor(midBranch) > indicesFor(lowBranch));
        for (const auto& tier : {highBranch, midBranch, lowBranch}) {
            for (const auto& prim : tier.primitives) {
                assert(prim.firstIndex + prim.indexCount <= branchModel->indices.size());
                assert(prim.firstVertex + prim.vertexCount <= branchModel->vertices.size());
            }
        }
    }
    for (const auto& leaf : skeleton.leaves) {
        assert(leaf.parentBranch > 0);
        assert(static_cast<size_t>(leaf.parentBranch) < skeleton.branches.size());
    }
    const auto branchRepeat = makeArborBranchLodModel(hierarchical, 0x987654321ULL);
    assert(branchRepeat->indices == branchModel->indices);
    assert(branchRepeat->vertices.size() == branchModel->vertices.size());
    BranchLodSettings invalidBranch;
    invalidBranch.minimalPixels = invalidBranch.reducedPixels;
    bool invalidBranchRejected = false;
    try { (void)makeArborBranchLodModel(oak, 17, invalidBranch); }
    catch (const std::invalid_argument&) { invalidBranchRejected = true; }
    assert(invalidBranchRejected);

    // Stage 2f: shared prototype, CPU draw submission, stable identity and
    // origin-relative centimetre -> metre transforms. No GPU is required.
    ArborForestAdapter forest;
    forest.addPrototype(oak, 17);
    assert(forest.prototypeCount() == 1);
    const auto* shared = forest.prototype(oak.speciesId);
    assert(shared && shared->meshes.size() == 3);
    bool duplicateRejected = false;
    try { forest.addPrototype(oak, 18); }
    catch (const std::invalid_argument&) { duplicateRejected = true; }
    assert(duplicateRejected);
    assert(forest.prototypeCount() == 1);

    const FoliageInstance first{11, 1001, 50100, -50000, 1300, 90000, 2000};
    const FoliageInstance second{12, 1001, 50200, -50100, 1600, 0, 800};
    const std::vector<FloraBatch> forestBatches{
        FloraBatch{1001, TreeLOD::High, {first}},
        FloraBatch{1001, TreeLOD::Impostor, {second}}
    };
    std::vector<aiverse::render::ModelDraw> draws;
    const ForestRenderOriginCm origin{49100, 300, -49000};
    forest.forEachDraw(forestBatches, origin, [&](const auto& draw) {
        draws.push_back(draw);
    });
    assert(draws.size() == 2);
    assert(draws[0].model == shared && draws[1].model == shared);
    assert(draws[0].persistentId && draws[0].persistentId != draws[1].persistentId);
    assert(draws[0].flags == aiverse::render::kDrawStatic);
    assert(std::fabs(draws[0].world.at(0, 3) - 10.0f) < 0.0001f);
    assert(std::fabs(draws[0].world.at(1, 3) - 10.0f) < 0.0001f);
    assert(std::fabs(draws[0].world.at(2, 3) + 10.0f) < 0.0001f);
    assert(std::fabs(draws[0].world.at(0, 2) - 2.0f) < 0.0001f);
    assert(std::fabs(draws[1].world.at(0, 3) - 11.0f) < 0.0001f);
    const auto stableId = draws[0].persistentId;
    draws.clear();
    forest.forEachDraw(forestBatches, origin, [&](const auto& draw) {
        draws.push_back(draw);
    });
    assert(draws[0].persistentId == stableId); // stable hash, not draw index
    assert(draws[0].model == shared);

    bool unknownRejected = false;
    try {
        forest.forEachDraw(
            std::vector<FloraBatch>{FloraBatch{999, TreeLOD::High, {first}}},
            origin, [](const auto&) {});
    } catch (const std::invalid_argument&) { unknownRejected = true; }
    assert(unknownRejected);
    bool extentRejected = false;
    try {
        forest.forEachDraw(forestBatches, ForestRenderOriginCm{INT64_MAX, 0, 0},
                           [](const auto&) {});
    } catch (const std::out_of_range&) { extentRejected = true; }
    assert(extentRejected);
}
