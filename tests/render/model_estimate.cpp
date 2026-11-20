// Stage 102 regression (execution deferred): approximate owned GPU allocations.
#include "engine/render/ModelResidentEstimate.h"
#include <cassert>
#include <cstdint>
using namespace aiverse;
using namespace aiverse::render;
int main() {
    Model m;
    MeshDef mesh;
    Primitive part;part.vertexCount=100;part.indexCount=300;
    mesh.primitives.push_back(part);
    m.meshes.push_back(mesh);
    m.vb={1};m.ib={2};
    Image owned;owned.tex={3};owned.width=32;owned.height=16;
    Image borrowed;borrowed.tex={4};borrowed.width=128;
    borrowed.height=128;borrowed.borrowedTexture=true;
    m.images.push_back(owned);
    m.images.push_back(borrowed);
    m.baked.tex={5};m.baked.joints=4;m.baked.rows=2;
    const auto estimate=estimateModelResident(m);
    assert(estimate.dedicatedGeometryBytes==
        100*sizeof(ModelVertex)+300*sizeof(uint32_t));
    assert(estimate.ownedImageBytes==32*16*4);
    assert(estimate.borrowedImages==1);
    assert(estimate.poseTextureBytes==4*3*2*16);
    assert(estimate.knownBytes()==
        estimate.dedicatedGeometryBytes+
        estimate.ownedImageBytes+estimate.poseTextureBytes);
}
