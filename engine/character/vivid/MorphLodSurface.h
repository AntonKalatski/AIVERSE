// VIVID facial morph surface correspondence for independently decimated LODs.
// Pure CPU authoring, no model mutation or animation scheduler.
#pragma once
#include "SparseMorph.h"
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::vivid {
struct LodSurfaceSample {
    std::uint32_t triangle=0; // triangle offset / 3 in source index array
    render::float3 bary{1,0,0};
    float distanceSquared=0;
};
class MorphLodSurface {
    static render::float3 closest(render::float3 p,render::float3 a,
                                  render::float3 b,render::float3 c,
                                  render::float3& weights) {
        using namespace render;
        const auto ab=b-a,ac=c-a,ap=p-a;
        const float d1=dot(ab,ap),d2=dot(ac,ap);
        if(d1<=0&&d2<=0){weights={1,0,0};return a;}
        const auto bp=p-b;
        const float d3=dot(ab,bp),d4=dot(ac,bp);
        if(d3>=0&&d4<=d3){weights={0,1,0};return b;}
        const float vc=d1*d4-d3*d2;
        if(vc<=0&&d1>=0&&d3<=0) {
            const float t=d1/(d1-d3);
            weights={1-t,t,0};return a+ab*t;
        }
        const auto cp=p-c;
        const float d5=dot(ab,cp),d6=dot(ac,cp);
        if(d6>=0&&d5<=d6){weights={0,0,1};return c;}
        const float vb=d5*d2-d1*d6;
        if(vb<=0&&d2>=0&&d6<=0) {
            const float t=d2/(d2-d6);
            weights={1-t,0,t};return a+ac*t;
        }
        const float va=d3*d6-d5*d4;
        if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0) {
            const float t=(d4-d3)/((d4-d3)+(d5-d6));
            weights={0,1-t,t};return b+(c-b)*t;
        }
        const float denom=1.0f/(va+vb+vc);
        const float v=vb*denom,w=vc*denom;
        weights={1-v-w,v,w};
        return a+ab*v+ac*w;
    }
    static bool finite(render::float3 v) {
        return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
    }
public:
    // Bounded reference projection. maxDistance is measured in model-space
    // units. Samples that are too far from the original mesh fail closed.
    // Indices must reference the same source vertex stream as morph CSR.
    static std::vector<LodSurfaceSample> project(
        std::span<const render::float3> source,
        std::span<const std::uint32_t> sourceIndices,
        std::span<const render::float3> lod,
        float maxDistance=0.05f) {
        using namespace render;
        if(source.empty()||source.size()>65536||lod.empty()||
           lod.size()>32768||sourceIndices.empty()||
           sourceIndices.size()%3!=0 || sourceIndices.size()/3>131072||
           !std::isfinite(maxDistance)||maxDistance<=0||maxDistance>10)
            throw std::invalid_argument("VIVID LOD surface: invalid mesh/budget/tolerance");
        for(const auto& p:source)if(!finite(p))
            throw std::invalid_argument("VIVID LOD surface: nonfinite source");
        for(auto index:sourceIndices)if(index>=source.size())
            throw std::invalid_argument("VIVID LOD surface: invalid source index");
        // Reject degenerate source triangles, which have unstable barycentrics.
        for(std::size_t i=0;i<sourceIndices.size();i+=3) {
            const auto a=source[sourceIndices[i]],b=source[sourceIndices[i+1]],
                       c=source[sourceIndices[i+2]];
            if(length(cross(b-a,c-a))<1.e-8f)
                throw std::invalid_argument("VIVID LOD surface: degenerate source triangle");
        }
        std::vector<LodSurfaceSample> result;
        result.reserve(lod.size());
        const float allowed=maxDistance*maxDistance;
        for(const auto& point:lod) {
            if(!finite(point))
                throw std::invalid_argument("VIVID LOD surface: nonfinite LOD position");
            LodSurfaceSample best;
            best.distanceSquared=std::numeric_limits<float>::max();
            bool found=false;
            for(std::size_t i=0;i<sourceIndices.size();i+=3) {
                const auto a=source[sourceIndices[i]],b=source[sourceIndices[i+1]],
                           c=source[sourceIndices[i+2]];
                float3 bary{};
                const auto candidate=closest(point,a,b,c,bary);
                const auto delta=point-candidate;
                const float distance=dot(delta,delta);
                if(distance<best.distanceSquared) {
                    best.triangle=std::uint32_t(i/3);
                    best.bary=bary;
                    best.distanceSquared=distance;
                    found=true;
                }
            }
            if(!found||best.distanceSquared>allowed)
                throw std::invalid_argument("VIVID LOD surface: LOD vertex outside projection tolerance");
            result.push_back(best);
        }
        return result;
    }
    static SparseMorphLibrary transfer(
        const SparseMorphLibrary& source,
        std::span<const std::uint32_t> sourceIndices,
        std::span<const LodSurfaceSample> mapping) {
        if(mapping.empty()||mapping.size()>16'000'000||
           sourceIndices.size()%3!=0)
            throw std::invalid_argument("VIVID LOD surface: invalid transfer topology");
        for(auto id:sourceIndices)if(id>=source.vertices())
            throw std::invalid_argument("VIVID LOD surface: source vertex outside CSR");
        for(const auto& sample:mapping) {
            if(sample.triangle>=sourceIndices.size()/3 ||
               !finite(sample.bary) || !std::isfinite(sample.distanceSquared) ||
               sample.distanceSquared<0)
                throw std::invalid_argument("VIVID LOD surface: invalid sample");
            const float sum=sample.bary.x+sample.bary.y+sample.bary.z;
            if(std::fabs(sum-1)>1.e-3f ||
               sample.bary.x< -1.e-4f || sample.bary.y< -1.e-4f ||
               sample.bary.z< -1.e-4f)
                throw std::invalid_argument("VIVID LOD surface: non-barycentric weights");
        }
        std::vector<MorphTargetInput> authored;
        authored.reserve(source.targets());
        for(std::size_t t=0;t<source.targets();++t) {
            MorphTargetInput target;
            target.name=std::string(source.targetName(t));
            std::vector<const GpuMorphDelta*> lookup(source.vertices(),nullptr);
            const auto range=source.gpuRanges()[t];
            for(std::uint32_t i=0;i<range.count;++i) {
                const auto& d=source.gpuDeltas()[range.first+i];
                lookup[d.vertex]=&d;
            }
            for(std::size_t v=0;v<mapping.size();++v) {
                const auto& sample=mapping[v];
                const std::size_t first=std::size_t(sample.triangle)*3;
                const auto ids=sourceIndices.subspan(first,3);
                const float weights[]={sample.bary.x,sample.bary.y,sample.bary.z};
                MorphDelta delta;
                delta.vertex=std::uint32_t(v);
                for(int corner=0;corner<3;++corner) {
                    const auto* d=lookup[ids[std::size_t(corner)]];
                    if(!d)continue;
                    for(int axis=0;axis<3;++axis) {
                        const auto i=std::size_t(axis);
                        delta.position[i]+=d->position[axis]*weights[corner];
                        delta.normal[i]+=d->normal[axis]*weights[corner];
                        delta.tangent[i]+=d->tangent[axis]*weights[corner];
                    }
                }
                // The core compiler strips zero deltas and preserves names.
                target.deltas.push_back(delta);
            }
            authored.push_back(std::move(target));
        }
        return SparseMorphLibrary::compile(mapping.size(),std::move(authored));
    }
};
} // namespace aiverse::vivid
