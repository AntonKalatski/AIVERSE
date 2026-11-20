#include "apps/studio-native/StudioTerrainGeometry.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace aiverse;
using namespace aiverse::studio;
using namespace aiverse::render;

int main() {
    editor::MapData d;
    d.w=2; d.h=2;
    d.cells={'.','~','#','R'};
    d.corners={0,0,0,0,16,0,0,0,0}; // centre elevated by one unit
    MeshBuilder mesh;
    const auto stats=buildTerrainPreview(d,mesh);
    assert(stats.quads==4 && stats.step==1);
    assert(mesh.vertices().size()==16 && mesh.indices().size()==24);
    assert(std::abs(terrainPreviewHeight(d,1,1)-1.0f)<0.001f);
    assert(std::abs(terrainPreviewHeight(d,0.5f,0.5f)-0.25f)<0.001f);
    assert(terrainPreviewColor('.')!=terrainPreviewColor('~'));
    assert(terrainPreviewColor('~')!=terrainPreviewColor('#'));
    for (const auto& v:mesh.vertices()) assert(v.nrm.y>0.1f);
    d.bodies.emplace_back();
    MeshBuilder planet;
    assert(buildTerrainPreview(d,planet).quads==0);
    d.bodies.clear();
    d.corners.clear();
    d.levels={2,2,2,2};
    d.heights={8,8,8,8};
    assert(std::abs(terrainPreviewHeight(d,0,0,1.5f)-3.5f)<0.001f);
    editor::MapData huge;
    huge.w=1024; huge.h=768; // bounded downsampling; no large cell vector allocation
    MeshBuilder reduced;
    auto limit=buildTerrainPreview(huge,reduced);
    assert(limit.step==4 && limit.quads==size_t(256*192));
    std::cout<<"StudioTerrainGeometry PASS\n";
}
