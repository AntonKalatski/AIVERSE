#pragma once
// Rotated (yaw-only) solid boxes for SpatialWorld: the 12 triangles of the box surface. Casts and blocked()
// against them use the exact triangle SAT, so a wall turned by any angle collides as authored; the
// bounding boxes registered alongside are only the broad phase. Fixed math only. FPS maps: colliders with "yaw".
#include "SpatialWorld.h"
#include "engine/core/FixedMath.h"
#include <array>
namespace aiverse::world {
inline std::array<Triangle,12> orientedBoxTriangles(Vec3 center,Vec3 half,Fixed yawTurns){
    auto c=fxCosTurns(yawTurns),s=fxSinTurns(yawTurns);
    auto corner=[&](int i){Fixed x=(i&1)?half.x:-half.x,y=(i&2)?half.y:-half.y,z=(i&4)?half.z:-half.z;return center+Vec3{x*c+z*s,y,z*c-x*s};};
    std::array<Vec3,8> p;for(int i=0;i<8;++i)p[size_t(i)]=corner(i);
    // Faces as corner quads (two triangles each): -X, +X, -Y, +Y, -Z, +Z.
    static constexpr int faces[6][4]={{0,2,6,4},{1,5,7,3},{0,4,5,1},{2,3,7,6},{0,1,3,2},{4,6,7,5}};
    std::array<Triangle,12> out;
    for(int f=0;f<6;++f){const auto* q=faces[f];out[size_t(f*2)]={p[size_t(q[0])],p[size_t(q[1])],p[size_t(q[2])]};out[size_t(f*2+1)]={p[size_t(q[0])],p[size_t(q[2])],p[size_t(q[3])]};}
    return out;
}
}
