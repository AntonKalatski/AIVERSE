// Convert authoritative fixed-point hierarchy affine to renderer matrix.
// Conversion occurs in the presentation layer only; never feed float results
// back into deterministic Simulation.
#pragma once
#include "Math3D.h"
#include "../sim/SceneHierarchy.h"
namespace aiverse::render {
inline mat4 hierarchyMatrix(const AffineTransform& source,Vec3 renderOrigin={}) {
    mat4 out=mat4::identity();
    for(int row=0;row<3;++row) {
        for(int col=0;col<3;++col)
            out.at(row,col)=float(source.at(size_t(row),size_t(col)).toDouble());
        const Fixed offset=row==0?renderOrigin.x:row==1?renderOrigin.y:renderOrigin.z;
        out.at(row,3)=float((source.at(size_t(row),3)-offset).toDouble());
    }
    return out;
}
inline mat4 hierarchyMatrix(const WorldTransform& world,Vec3 renderOrigin={}) {
    return hierarchyMatrix(world.affine,renderOrigin);
}
} // namespace aiverse::render
