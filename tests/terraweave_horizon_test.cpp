#include "engine/render/TerraWeaveHorizonModel.h"
#include "engine/render/TerraWeaveHorizonScene.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <set>
#include <vector>

using namespace aiverse::terraweave;

int main() {
    TerrainSettings terrain;
    terrain.seed=1537;
    terrain.baseHeightCm=450;
    terrain.reliefCm=0;
    BiomeSettings climate;
    climate.baseHumidity=900;
    climate.variation=0;
    HorizonSettings settings;
    settings.middleTileCm=8000;
    settings.middleTilesPerSide=12;
    settings.distantTileCm=32000;
    settings.distantTilesPerSide=12;
    settings.closeForestEndCm=8000;
    settings.middleForestEndCm=30000;
    settings.terrainBeginCm=25000;
    settings.terrainResolution=12;
    settings.atlasTilePixels=16;
    settings.canopyHeightCm=1000;

    const auto budget=estimateHorizonBudget(settings);
    assert(budget.atlasBytes<=16u*1024u*1024u);
    assert(budget.maxTriangles<=settings.terrainResolution*
           settings.terrainResolution*2+budget.maxForestClusters*4);
    const auto defaults=estimateHorizonBudget(HorizonSettings{});
    assert(defaults.atlasBytes<=8u*1024u*1024u);
    assert(defaults.maxTriangles<10000);
    assert(defaults.maxForestClusters==832);
    HorizonSettings dualDefault;
    dualDefault.orientationViews=2;
    const auto dualBudget=estimateHorizonBudget(dualDefault);
    assert(dualBudget.atlasBytes<=16u*1024u*1024u);
    assert(dualBudget.atlasBytes>defaults.atlasBytes);
    assert(dualBudget.maxTriangles==defaults.maxTriangles);

    const auto bake=bakeHorizon(terrain,climate,settings,0,0);
    assert(bake.atlasWidth==bake.atlasHeight);
    assert(bake.atlasRgba.size()==size_t(bake.atlasWidth)*bake.atlasHeight*4);
    assert(!bake.clusters.empty());
    assert(bake.terrainVertices.size()==13*13);
    for(const auto& cluster:bake.clusters) {
        assert(cluster.atlasX+cluster.atlasSize<=bake.atlasWidth);
        assert(cluster.atlasY+cluster.atlasSize<=bake.atlasHeight);
        assert(cluster.topCm>cluster.bottomCm);
        const int64_t dx=cluster.centerXcm, dz=cluster.centerZcm;
        const int64_t visibleStart=std::max<int32_t>(
            0,settings.closeForestEndCm-settings.nearBlendCm);
        assert(dx*dx+dz*dz>=visibleStart*visibleStart);
    }
    uint32_t coverage=0;
    for(size_t i=3;i<bake.atlasRgba.size();i+=4)
        coverage+=bake.atlasRgba[i]>0;
    assert(coverage>0);
    // Crowns must be shaped, not uniform solid columns/hedge stripes.
    std::set<uint32_t> leafColours;
    for(size_t i=0;i<bake.atlasRgba.size();i+=4) {
        if (bake.atlasRgba[i+3]>=128) {
            const uint32_t color=uint32_t(bake.atlasRgba[i]) |
                (uint32_t(bake.atlasRgba[i+1])<<8) |
                (uint32_t(bake.atlasRgba[i+2])<<16);
            leafColours.insert(color);
        }
    }
    assert(leafColours.size()>25);
    assert(horizon_detail::smoothCoverage(50,100,200)==0);
    assert(horizon_detail::smoothCoverage(200,100,200)==255);
    assert(horizon_detail::smoothCoverage(150,100,200)>110);
    assert(horizon_detail::smoothCoverage(150,100,200)<145);
    const auto repeat=bakeHorizon(terrain,climate,settings,0,0);
    assert(bake.atlasRgba==repeat.atlasRgba);
    assert(bake.clusters.size()==repeat.clusters.size());
    for(size_t i=0;i<bake.clusters.size();++i) {
        const auto&a=bake.clusters[i],&b=repeat.clusters[i];
        assert(a.centerXcm==b.centerXcm);
        assert(a.centerZcm==b.centerZcm);
        assert(a.atlasX==b.atlasX&&a.atlasY==b.atlasY);
        assert(a.bottomCm==b.bottomCm&&a.topCm==b.topCm);
    }

    auto model=makeHorizonModel(bake);
    assert(model);
    assert(model->nodes.size()==1);
    assert(model->meshes.size()==1);
    assert(model->materials.size()==2);
    assert(model->images.size()==1);
    assert(model->images[0].rgba==bake.atlasRgba);
    assert(model->materials[1].alpha==aiverse::render::AlphaMode::Mask);
    assert(model->meshes[0].primitives.size()==2);
    assert(model->vertices.size()<(13*13)+bake.clusters.size()*8+8);
    for(const auto& p:model->meshes[0].primitives) {
        assert(p.indexCount>0);
        assert(p.indexCount%3==0);
        assert(p.firstIndex+p.indexCount<=model->indices.size());
        for(uint32_t j=p.firstIndex;j<p.firstIndex+p.indexCount;++j)
            assert(model->indices[j]<model->vertices.size());
    }
    assert(model->meshes[0].primitives[1].indexCount ==
           bake.clusters.size()*12u); // two crossed cards per cluster
    assert(!model->batching.autoLod);

    // Quality tier: two distinct 90-degree atlas captures, same mesh
    // and triangle counts, still one model / one atlas / two materials.
    HorizonSettings dualSettings=settings;
    dualSettings.orientationViews=2;
    const auto sideBake=bakeHorizon(terrain,climate,dualSettings,0,0);
    assert(sideBake.clusters.size()==bake.clusters.size());
    assert(sideBake.atlasWidth>bake.atlasWidth);
    assert(sideBake.atlasRgba!=bake.atlasRgba);
    for (const auto& cluster:sideBake.clusters) {
        assert(cluster.atlasX!=cluster.atlasSideX ||
               cluster.atlasY!=cluster.atlasSideY);
        assert(cluster.atlasSideX+cluster.atlasSize<=sideBake.atlasWidth);
        assert(cluster.atlasSideY+cluster.atlasSize<=sideBake.atlasHeight);
    }
    const auto sideModel=makeHorizonModel(sideBake);
    assert(sideModel->meshes[0].primitives.size()==
           model->meshes[0].primitives.size());
    assert(sideModel->vertices.size()==model->vertices.size());
    assert(sideModel->indices==model->indices);
    assert(sideModel->images.size()==1);
    assert(sideModel->materials.size()==2);
    const auto& forestPart=sideModel->meshes[0].primitives.back();
    assert(forestPart.vertexCount>=8);
    const auto& nearUv=sideModel->vertices[forestPart.firstVertex].uv;
    const auto& sideUv=sideModel->vertices[forestPart.firstVertex+4].uv;
    assert(nearUv.x!=sideUv.x || nearUv.y!=sideUv.y);


    bool rejected=false;
    auto bad=settings;
    bad.atlasTilePixels=128;
    try {validateHorizonSettings(bad);}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
    rejected=false;
    bad=settings;
    bad.crownDetail=0;
    try {validateHorizonSettings(bad);}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
    rejected=false;
    bad=settings;
    bad.tierBlendCm=settings.middleForestEndCm;
    try {validateHorizonSettings(bad);}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
    rejected=false;
    bad=settings;
    bad.orientationViews=3;
    try {validateHorizonSettings(bad);}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
    rejected=false;
    try{(void)bakeHorizon(terrain,climate,settings,INT64_MAX,0);}
    catch(const std::out_of_range&){rejected=true;}
    assert(rejected);

    // Constructor should prepare a *bounded* async scheduler without requiring
    // an initialized graphics device. Actual GPU upload has a separate test.
    TerraWeaveHorizonScene scene(terrain,climate,settings);
    assert(!scene.ready());
    assert(scene.clusterCount()==0);
}
