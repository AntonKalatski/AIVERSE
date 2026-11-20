#include "apps/studio-native/StudioModelPicking.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::studio;

static Model oneTriangle() {
    Model model;
    model.nodes.resize(1);
    model.nodes[0].mesh=0;
    model.bindWorld.push_back(mat4::identity());
    model.meshes.resize(1);
    Primitive primitive;
    primitive.firstIndex=0;
    primitive.indexCount=3;
    model.meshes[0].primitives.push_back(primitive);
    model.vertices.resize(3);
    model.vertices[0].pos={0,0,0};
    model.vertices[1].pos={2,0,0};
    model.vertices[2].pos={0,2,0};
    model.indices={0,1,2};
    return model;
}
int main() {
    const float3 origin{.25f,.25f,5}, direction{0,0,-1};
    auto model=oneTriangle();
    const auto first=studioPickModelSurface(model,mat4::identity(),origin,direction);
    assert(first);
    assert(std::abs(first.distance-5)<1e-4f);
    assert(first.node==0 && first.mesh==0 && first.primitive==0 && first.triangle==0);
    // Within a triangle's AABB but outside actual face.
    assert(!studioPickModelSurface(model,mat4::identity(),{1.8f,1.8f,5},direction));
    // Double sided triangle winding still hits.
    assert(studioPickModelSurface(model,mat4::identity(),{.25f,.25f,-5},{0,0,1}));
    // Both world and node transforms must be included.
    model.bindWorld[0]=mat4::translate({1,0,0});
    const auto shifted=studioPickModelSurface(model,mat4::translate({0,0,-3}),
                                             {1.25f,.25f,5},direction);
    assert(shifted && std::abs(shifted.distance-8)<1e-4f);
    assert(!studioPickModelSurface(model,mat4::identity(),origin,direction));
    // Missing CPU geometry must NOT fall back to a false-positive AABB.
    model.vertices.clear();
    assert(!studioPickModelSurface(model,mat4::identity(),{1.25f,.25f,5},direction));
    // Non-rigid/skinned or morphed meshes are not safe for bind-pose picking.
    model=oneTriangle();
    model.nodes[0].skin=0;
    assert(!studioPickModelSurface(model,mat4::identity(),origin,direction));
    model.nodes[0].skin=-1;
    model.meshes[0].primitives[0].morphTargets=1;
    assert(!studioPickModelSurface(model,mat4::identity(),origin,direction));
    // Corrupt indices must be ignored without out-of-bounds access.
    model=oneTriangle();
    model.indices[2]=UINT32_MAX;
    assert(!studioPickModelSurface(model,mat4::identity(),origin,direction));
    model=oneTriangle();
    assert(!studioPickModelSurface(model,mat4::identity(),origin,{0,0,0}));
    assert(!studioPickModelSurface(model,mat4::identity(),origin,direction,0));
    const float inf=std::numeric_limits<float>::infinity();
    assert(!studioPickModelSurface(model,mat4::identity(),{inf,0,0},direction));
    // The closer model wins, even when instance ordering is reversed.
    model=oneTriangle();
    auto front=oneTriangle(), back=oneTriangle();
    const auto nearest=studioPickModelInstances({
        {"back",&back,mat4::translate({0,0,-5})},
        {"front",&front,mat4::translate({0,0,-1})}
    },origin,direction);
    assert(nearest.id=="front");
    assert(std::abs(nearest.surface.distance-6)<1e-4f);
    assert(studioPickModelInstances({
        {"hidden",nullptr,mat4::identity()}
    },origin,direction).id.empty());
    std::cout<<"PASS studio CPU model triangle picking\n";
}
