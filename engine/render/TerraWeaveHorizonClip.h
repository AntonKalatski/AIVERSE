// CPU-only exact 2D subtraction of resident near-ground rectangles from the
// two original triangles of a Horizon terrain quad. Polygons are clipped in
// double-precision world centimetres and triangulated with their original
// winding, preserving the source distant terrain plane via barycentric sampling.
// The total extra triangle count is strictly bounded; on overflow the caller
// uses the conservative whole-quad fallback. No backend/ECS dependencies.
#pragma once

#include "engine/world/TerraWeaveHorizon.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>
#include <utility>

namespace aiverse::terraweave {

struct HorizonClipPoint { double x=0,z=0; };
struct HorizonClipTriangle {
    uint32_t quadIndex=0;
    uint8_t sourceHalf=0; // original terrain quad triangle 0 or 1
    std::array<HorizonClipPoint,3> corners{};
};
struct HorizonTerrainClipPlan {
    std::vector<uint8_t> touchedQuads; // N*N: original triangles replaced
    std::vector<HorizonClipTriangle> triangles; // bounded by maxNearClipTriangles
};

namespace horizon_clip_detail {
using Polygon=std::vector<HorizonClipPoint>;

inline double area2(const Polygon& p) {
    if(p.size()<3)return 0.0;
    // Subtract the first point before cross-products. At large absolute
    // world coordinates, direct x*z shoelaces lose small polygon precision.
    double area=0;
    const auto base=p.front();
    for(size_t i=1;i+1<p.size();++i){
        const auto& a=p[i],&b=p[i+1];
        area+=(a.x-base.x)*(b.z-base.z)-
              (a.z-base.z)*(b.x-base.x);
    }
    return area;
}

inline Polygon halfPlane(const Polygon& poly,int axis,double edge,bool greater) {
    Polygon out;
    if(poly.empty())return out;
    out.reserve(poly.size()+2);
    const auto coordinate=[axis](HorizonClipPoint p) {
        return axis==0 ? p.x : p.z;
    };
    const auto inside=[greater,edge](double p) {
        return greater ? p>=edge : p<=edge;
    };
    auto prev=poly.back();
    bool wasInside=inside(coordinate(prev));
    for(const auto& current:poly) {
        const bool isInside=inside(coordinate(current));
        if(isInside!=wasInside){
            const double prevCoord=coordinate(prev);
            const double currCoord=coordinate(current);
            const double delta=currCoord-prevCoord;
            const double t=delta==0 ? 0.0 :
                std::clamp((edge-prevCoord)/delta,0.0,1.0);
            HorizonClipPoint p{prev.x+(current.x-prev.x)*t,
                               prev.z+(current.z-prev.z)*t};
            if(axis==0)p.x=edge;else p.z=edge;
            out.push_back(p);
        }
        if(isInside)out.push_back(current);
        prev=current;
        wasInside=isInside;
    }
    // Shared corner intersections may appear twice if a triangle edge is
    // collinear with a tile boundary; suppress zero-size segments.
    Polygon clean;
    clean.reserve(out.size());
    for(const auto& p:out)
        if(clean.empty() || std::abs(clean.back().x-p.x)>1e-7 ||
                            std::abs(clean.back().z-p.z)>1e-7)
            clean.push_back(p);
    if(clean.size()>1 && std::abs(clean.front().x-clean.back().x)<1e-7 &&
                         std::abs(clean.front().z-clean.back().z)<1e-7)
        clean.pop_back();
    return clean;
}
inline bool overlaps(HorizonClipPoint a,HorizonClipPoint b,const RectCm& rect) {
    return a.x<double(rect.maxX) && b.x>double(rect.minX) &&
           a.z<double(rect.maxZ) && b.z>double(rect.minZ);
}
inline std::pair<HorizonClipPoint,HorizonClipPoint> bounds(const Polygon& p) {
    HorizonClipPoint lo{1e30,1e30},hi{-1e30,-1e30};
    for(const auto& q:p) {
        lo.x=std::min(lo.x,q.x);lo.z=std::min(lo.z,q.z);
        hi.x=std::max(hi.x,q.x);hi.z=std::max(hi.z,q.z);
    }
    return {lo,hi};
}
inline void appendIfArea(std::vector<Polygon>& into,Polygon p) {
    if(p.size()>=3 && std::abs(area2(p))>1e-5)
        into.push_back(std::move(p));
}
inline std::vector<Polygon> subtract(const Polygon& polygon,const RectCm& rect) {
    const auto [min,max]=bounds(polygon);
    if(!overlaps(min,max,rect))return {polygon};
    std::vector<Polygon> out;
    // Partition the outside into mutually disjoint regions: left, right,
    // middle-top and middle-bottom. No triangle may be double counted.
    appendIfArea(out,halfPlane(polygon,0,double(rect.minX),false));
    appendIfArea(out,halfPlane(polygon,0,double(rect.maxX),true));
    auto middle=halfPlane(polygon,0,double(rect.minX),true);
    middle=halfPlane(middle,0,double(rect.maxX),false);
    appendIfArea(out,halfPlane(middle,1,double(rect.minZ),false));
    appendIfArea(out,halfPlane(middle,1,double(rect.maxZ),true));
    return out;
}
} // namespace horizon_clip_detail

// A plan is optional: nullopt explicitly signals too much work / invalid
// near rectangles and requests the preceding conservative quad algorithm.
// This API is reusable by native games and unrelated terrain generators.
inline std::optional<HorizonTerrainClipPlan> planHorizonNearGroundClipping(
        const HorizonBake& bake) {
    HorizonTerrainClipPlan plan;
    if(!bake.maxNearClipTriangles || bake.nearClipRects.empty() ||
       bake.terrainVertices.empty())return plan;
    const uint32_t n=bake.terrainResolution;
    if(!n || n>96 || bake.terrainVertices.size()!=size_t(n+1)*(n+1) ||
       bake.nearClipRects.size()>1024 ||
       (!bake.nearOwnedQuads.empty() &&
         bake.nearOwnedQuads.size()!=size_t(n)*n))
        return std::nullopt;
    plan.touchedQuads.assign(size_t(n)*n,0);
    // Immutable near coverage is a set of non-overlapping half-open XZ
    // rectangles. Each partial coarse quad is split along its two original
    // source triangles to retain exactly their slope and winding.
    for(uint32_t z=0;z<n;++z)
        for(uint32_t x=0;x<n;++x){
            const uint32_t quad=z*n+x;
            if(!bake.nearOwnedQuads.empty()&&bake.nearOwnedQuads[quad])
                continue; // already entirely removed by existing mask
            const size_t a=size_t(z)*(n+1)+x;
            const auto& tl=bake.terrainVertices[a];
            const auto& tr=bake.terrainVertices[a+1];
            const auto& bl=bake.terrainVertices[a+n+1];
            const auto& br=bake.terrainVertices[a+n+2];
            const RectCm cell{tl.xCm,tl.zCm,br.xCm,br.zCm};
            std::vector<const RectCm*> clips;
            for(const auto& rect:bake.nearClipRects)
                if(cell.minX<rect.maxX && cell.maxX>rect.minX &&
                   cell.minZ<rect.maxZ && cell.maxZ>rect.minZ)
                    clips.push_back(&rect);
            if(clips.empty())continue;
            plan.touchedQuads[quad]=1;
            const std::array<std::array<HorizonClipPoint,3>,2> halves{{
                {{{double(tl.xCm),double(tl.zCm)},
                  {double(bl.xCm),double(bl.zCm)},
                  {double(tr.xCm),double(tr.zCm)}}},
                {{{double(tr.xCm),double(tr.zCm)},
                  {double(bl.xCm),double(bl.zCm)},
                  {double(br.xCm),double(br.zCm)}}}
            }};
            for(uint8_t half=0;half<2;++half) {
                std::vector<horizon_clip_detail::Polygon> polygons;
                polygons.emplace_back(halves[half].begin(),
                                      halves[half].end());
                for(const RectCm* rect:clips){
                    std::vector<horizon_clip_detail::Polygon> remaining;
                    for(const auto& poly:polygons){
                        auto outside=horizon_clip_detail::subtract(poly,*rect);
                        for(auto& part:outside)
                            remaining.push_back(std::move(part));
                        if(remaining.size()>bake.maxNearClipTriangles)
                            return std::nullopt;
                    }
                    polygons=std::move(remaining);
                    if(polygons.empty())break;
                }
                for(const auto& polygon:polygons){
                    for(size_t i=1;i+1<polygon.size();++i){
                        // A half-plane can leave collinear vertices exactly
                        // on a near tile edge. Do not upload degenerate
                        // triangles or count them against geometry budgets.
                        const double ax=polygon[i].x-polygon[0].x;
                        const double az=polygon[i].z-polygon[0].z;
                        const double bx=polygon[i+1].x-polygon[0].x;
                        const double bz=polygon[i+1].z-polygon[0].z;
                        if(std::abs(ax*bz-az*bx)<1e-5)continue;
                        if(plan.triangles.size()>=bake.maxNearClipTriangles)
                            return std::nullopt;
                        plan.triangles.push_back({
                            quad,half,{{polygon[0],polygon[i],polygon[i+1]}}
                        });
                    }
                }
            }
        }
    return plan;
}

} // namespace aiverse::terraweave
