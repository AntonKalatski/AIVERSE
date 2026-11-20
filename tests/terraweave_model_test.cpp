#include "engine/render/TerraWeaveGrassRender.h"
#include "engine/render/TerraWeaveModelAdapter.h"
#include "engine/render/TerraWeaveForestAdapter.h"
#include <cassert>
#include <cmath>
#include <cstdint>

using namespace aiverse::terraweave;

int main() {
    // Opt-in conifer foliage keeps structural endpoints identical across LODs.
    TreeProfile conifer;conifer.shape=CrownShape::Conifer;conifer.primaryBranches=120;
    ArborMeshSettings sprays;sprays.naturalConifer=true;sprays.doubleSidedLeaves=false;
    const auto natural=makeArborModel(conifer,909,sprays);
    assert(natural->meshes.size()==3);
    auto triangles=[](const auto& mesh){size_t n=0;for(const auto& p:mesh.primitives)n+=p.indexCount/3;return n;};
    assert(triangles(natural->meshes[0])>triangles(natural->meshes[1]));
    assert(triangles(natural->meshes[1])>triangles(natural->meshes[2]));
    assert(triangles(natural->meshes[0])<20000);
    for(const auto& v:natural->vertices){assert(std::isfinite(v.pos.x)&&std::isfinite(v.nrm.y)&&std::isfinite(v.uv.x));}
    const auto repeat=makeArborModel(conifer,909,sprays);
    assert(repeat->indices==natural->indices);
    for(size_t i=0;i<natural->vertices.size();++i)assert(natural->vertices[i].pos.x==repeat->vertices[i].pos.x);
    // Far forest geometry has an explicit bounded budget, not just LOD labels.
    conifer.primaryBranches=75;conifer.whorlSize=5;
    sprays.compactConifer=true;
    const auto compact=makeArborModel(conifer,909,sprays);
    assert(triangles(compact->meshes[0])<6000);
    assert(triangles(compact->meshes[1])<2500);
    assert(triangles(compact->meshes[2])<1000);
    assert(compact->meshes[0].lods[0].maxPixels==320.f);
    assert(compact->meshes[0].lods[1].maxPixels==112.f);
    for(const auto& v:compact->vertices)assert(std::abs(aiverse::render::length(v.nrm)-1.f)<.01f);
    const auto whorled=generateTree(conifer,909);
    assert(whorled.branches==generateTree(conifer,909).branches);
    assert(whorled.branches!=generateTree(conifer,910).branches);
    for(size_t i=1;i<whorled.branches.size();i+=5) {
        int lo=whorled.branches[i].y0,hi=lo;
        for(size_t j=i;j<std::min(i+5,whorled.branches.size());++j){lo=std::min(lo,whorled.branches[j].y0);hi=std::max(hi,whorled.branches[j].y0);assert(whorled.branches[j].parent==0);}
        assert(hi-lo<100);
    }
    bool invalidWhorl=false;conifer.whorlSize=1;
    try{(void)generateTree(conifer,909);}catch(const std::invalid_argument&){invalidWhorl=true;}
    assert(invalidWhorl);
    // Bounded grass, exact terrain contact and a fully excluded patch.
    GrassPatch patch;patch.key=7;patch.seed=99;patch.bounds={-400,-400,400,400};patch.maximumBlades=2048;
    GrassRenderQuality quality;quality.maxBladesPerPatch=2048;quality.bladeSegments=3;quality.crossedBlades=false;
    const auto grass=makeGrassPatchModel(patch,[](auto,auto){return 123;},quality);
    assert(grass && grass->indices.size()/3==2048*5);
    for(size_t i=0;i<grass->vertices.size();i+=7)assert(std::fabs(grass->vertices[i].pos.y-1.23f)<.0001f);
    assert(!makeGrassPatchModel(patch,[](auto,auto){return 123;},quality,[](auto,auto){return false;}));
    const auto masked=makeGrassPatchModel(patch,[](auto,auto){return 123;},quality,[](auto x,auto){return x>=0;});
    assert(masked && masked->indices.size()<grass->indices.size());
    quality.bladesPerTuft=16;quality.tuftRadiusCm=15;
    const auto tufted=makeGrassPatchModel(patch,[](auto,auto){return 123;},quality);assert(tufted&&tufted->indices.size()==grass->indices.size());
    for(size_t start=0;start<2048;start+=16){const auto centre=(tufted->vertices[start*7].pos+tufted->vertices[start*7+1].pos)*.5f;for(size_t i=start;i<start+16;++i){const auto blade=(tufted->vertices[i*7].pos+tufted->vertices[i*7+1].pos)*.5f;assert(std::abs(blade.x-centre.x)<=.301f&&std::abs(blade.z-centre.z)<=.301f);}}
    const auto grassLods=makeGrassPatchLodModel(patch,[](auto,auto){return 123;},quality);
    assert(grassLods&&grassLods->meshes.size()==3&&grassLods->meshes[0].lods.size()==2);
    assert(triangles(grassLods->meshes[0])>triangles(grassLods->meshes[1]));
    assert(triangles(grassLods->meshes[1])>triangles(grassLods->meshes[2]));
    assert(grassLods->batching.independentShadowLod&&!grassLods->materials[0].castShadow);
    quality.bladeSegments=5;bool badGrass=false;
    try{(void)makeGrassPatchModel(patch,[](auto,auto){return 0;},quality);}catch(const std::invalid_argument&){badGrass=true;}
    assert(badGrass);

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
    assert(model->batching.independentShadowLod);
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
    assert(shared && shared->nodes.size() == oak.primaryBranches + 1);
    assert(shared->meshes.size() == shared->nodes.size() * 3);
    const auto* billboard = forest.billboardPrototype(oak.speciesId);
    assert(billboard && billboard != shared);
    assert(billboard->nodes.size() == 1 && billboard->meshes.size() == 1);
    // The current impostor has separate bark and foliage cards so winter
    // can hide leaves without removing the opaque trunk. They still share
    // ONE atlas texture and ONE mesh/node per species.
    assert(billboard->indices.size() == 12 && billboard->vertices.size() == 8);
    assert(billboard->materials.size() == 2);
    assert(billboard->meshes[0].primitives.size() == 2);
    for (size_t layer = 0; layer < 2; ++layer) {
        const auto& p = billboard->meshes[0].primitives[layer];
        assert(p.material >= 0 && static_cast<size_t>(p.material) == layer);
        assert(p.firstVertex == layer * 4 && p.vertexCount == 4);
        assert(p.firstIndex == layer * 6 && p.indexCount == 6);
        assert(billboard->materials[layer].alpha ==
               aiverse::render::AlphaMode::Mask);
        assert(!billboard->materials[layer].castShadow);
        for (size_t k = p.firstIndex; k < p.firstIndex + p.indexCount; ++k) {
            assert(billboard->indices[k] >= p.firstVertex);
            assert(billboard->indices[k] < p.firstVertex + p.vertexCount);
        }
    }
    assert(billboard->images.size() == 1);
    assert(forest.billboardAzimuthCount(oak.speciesId) == 8);
    // The left eight tiles hold bark azimuths; the right eight hold foliage.
    assert(billboard->images[0].width == 128 * 8 * 2);
    assert(billboard->images[0].height == 256);
    assert(billboard->images[0].rgba.size() == 128u * 8u * 2u * 256u * 4u);
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
    assert(draws[0].model == shared && draws[1].model == billboard);
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
    assert(draws[0].model == shared && draws[1].model == billboard);


    // A full-tree impostor has real transparent pixels and cutout foliage.
    const auto& image = billboard->images[0].rgba;
    uint32_t transparent = 0, opaque = 0, brown = 0, green = 0;
    for (size_t k = 0; k < image.size(); k += 4) {
        if (image[k + 3] == 0) ++transparent;
        else {
            assert(image[k + 3] == 255);
            ++opaque;
            if (image[k + 1] > image[k]) ++green;
            else ++brown;
        }
    }
    assert(transparent > 0 && opaque > 0 && green > 0 && brown > 0);
    assert(image[3] == 0);
    const auto billboardRepeat = makeArborImpostorAtlas(oak, 17);
    assert(billboardRepeat.model->images[0].rgba == billboard->images[0].rgba);
    assert(billboardRepeat.model->indices == billboard->indices);
    assert(billboardRepeat.viewMaterials.size() == 8);
    for (size_t i = 0; i < billboardRepeat.viewMaterials.size(); ++i) {
        const auto& layers = billboardRepeat.viewMaterials[i];
        assert(layers.size() == 2);
        for (size_t layer = 0; layer < 2; ++layer) {
            assert(layers[layer].baseTex == 0);
            assert(layers[layer].uvScale.x > 0.0f);
            assert(layers[layer].uvScale.x < 1.0f / 16.0f);
        }
        // The two azimuth pages occupy disjoint halves of one texture.
        assert(layers[0].uvOffset.x < 0.5f);
        assert(layers[1].uvOffset.x > 0.5f);
        if (i > 0)
            for (size_t layer = 0; layer < 2; ++layer)
                assert(layers[layer].uvOffset.x >
                       billboardRepeat.viewMaterials[i - 1][layer].uvOffset.x);
    }
    assert(selectArborImpostorAzimuth(0, 0, 0, 0, 1000, 8) == 0);
    assert(selectArborImpostorAzimuth(0, 0, 0, 1000, 0, 8) == 2);
    assert(selectArborImpostorAzimuth(90000, 0, 0, 1000, 0, 8) == 0);
    bool badBillboardSettings = false;
    try { (void)makeArborBillboardModel(oak, 17, {16, 256, 2}); }
    catch (const std::invalid_argument&) { badBillboardSettings = true; }
    assert(badBillboardSettings);

    // A far tree rotates its +Z-facing card towards the real camera, not
    // towards the tree's authored yaw or the world origin.
    std::vector<aiverse::render::ModelDraw> eastFacing;
    forest.forEachDraw(
        std::vector<FloraBatch>{FloraBatch{1001, TreeLOD::Impostor, {first}}},
        origin, [&](const auto& draw) { eastFacing.push_back(draw); },
        ForestCameraCm{first.xCm + 10000, first.zCm});
    assert(eastFacing.size() == 1);
    assert(eastFacing.front().model == billboard);
    assert(std::fabs(eastFacing.front().world.at(0, 2) - 2.0f) < 0.0001f);
    assert(eastFacing.front().persistentId == stableId);
    assert(eastFacing.front().materials != nullptr);
    const auto frame0 = makeArborImpostorAtlas(oak, 17).viewMaterials[0][0];
    assert(std::fabs(eastFacing.front().materials->at(0).uvOffset.x -
                     frame0.uvOffset.x) < 0.0001f);
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
