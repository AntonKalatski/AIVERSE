#include "engine/render/TerraWeaveHorizonModel.h"
#include "engine/render/TerraWeaveHorizonScene.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <stdexcept>
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
        assert(dx*dx+dz*dz>=int64_t(settings.closeForestEndCm)*
                                 settings.closeForestEndCm);
    }
    uint32_t coverage=0;
    for(size_t i=3;i<bake.atlasRgba.size();i+=4)
        coverage+=bake.atlasRgba[i]>0;
    assert(coverage>0);
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

    bool rejected=false;
    auto bad=settings;
    bad.atlasTilePixels=128;
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
