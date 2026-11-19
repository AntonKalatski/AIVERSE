// LumaWeave low-resolution screen-space direct-light compute.
// Requires a real HDR scene depth texture from a PostProcess::scenePass.
// Reconstructs geometric normals from depth; material normals/alpha require a
// future explicit G-buffer producer. Output RGB = linear DIRECT incident
// radiance, A = raw device depth for later edge-aware upsampling.
// This does not replace or mix into any legacy render target by itself.
#pragma once
#include "GiGpuBvhMetalShader.h"
#include "LumaWeaveMetalCommon.h"

#define AIVERSE_MSL_LUMAWEAVE_FULLSCREEN R"MSL(
#include <metal_stdlib>
using namespace metal;
)MSL" AIVERSE_MSL_GI_BVH_RAYQUERY AIVERSE_MSL_LUMAWEAVE_COMMON R"MSL(
struct LumaScreenParams {
    float4x4 invVP;
    float4 eye;                 // xyz world, w=texture-Y inverted relative to NDC
    float4 resolution;          // output W/H, scene W/H
    float4 origin,cellSize;
    uint4 dims;                 // nx,ny,nz,count
    uint4 limits;               // lightCount,csr refs,receiverCount,sampleCount
    uint4 control;              // BVH nodes, temporal seed, output width, reserved
};
constexpr sampler lumaDepthSampler(coord::normalized,filter::nearest,
                                    address::clamp_to_edge);
static float depthAt(depth2d<float> depth,float2 projectedUv,bool flipY) {
    float2 readUv=flipY?float2(projectedUv.x,1.0f-projectedUv.y):projectedUv;
    return depth.sample(lumaDepthSampler,clamp(readUv,float2(0),float2(1)));
}
static float3 lumaWorld(float2 uv,float depth,constant LumaScreenParams& p) {
    float4 h=p.invVP*float4(uv*2.0f-1.0f,depth,1.0f);
    return h.xyz/max(abs(h.w),1e-8f)*(h.w<0?-1.0f:1.0f);
}
static float3 lumaNormal(float2 uv,float center,depth2d<float> depth,
                         constant LumaScreenParams& p) {
    float2 dx=float2(1.0f/max(p.resolution.z,1.0f),0.0f);
    float2 dy=float2(0.0f,1.0f/max(p.resolution.w,1.0f));
    float3 origin=lumaWorld(uv,center,p);
    float3 tangent=lumaWorld(clamp(uv+dx,0.0f,1.0f),
              depthAt(depth,uv+dx,p.eye.w>0.5f),p)-origin;
    float3 bitangent=lumaWorld(clamp(uv+dy,0.0f,1.0f),
              depthAt(depth,uv+dy,p.eye.w>0.5f),p)-origin;
    float3 n=cross(tangent,bitangent);
    float length2=dot(n,n);
    if(length2<1e-10f)return float3(0,1,0);
    n*=rsqrt(length2);
    if(dot(n,p.eye.xyz-origin)<0.0f)n=-n;
    return n;
}
kernel void cs_main(
    uint i [[thread_position_in_grid]],
    device const LumaLight* lights [[buffer(0)]],
    device const LumaCell* cells [[buffer(1)]],
    device const uint* candidates [[buffer(2)]],
    device const Tri* triangles [[buffer(3)]],
    device const Node* nodes [[buffer(4)]],
    constant LumaScreenParams& p [[buffer(8)]],
    texture2d<float,access::write> output [[texture(0)]],
    depth2d<float> depth [[texture(1)]]) {
    if(i>=p.limits.z)return;
    uint2 pixel=uint2(i%p.control.z,i/p.control.z);
    float2 uv=(float2(pixel)+.5f)/p.resolution.xy;
    float d=depthAt(depth,uv,p.eye.w>.5f);
    if(d>=.99999f) {
        output.write(float4(0.0f),pixel);
        return;
    }
    if(p.limits.x==0u) {
        output.write(float4(0,0,0,d),pixel);
        return;
    }
    float3 world=lumaWorld(uv,d,p);
    float3 normal=lumaNormal(uv,d,depth,p);

    float3 grid=(world-p.origin.xyz)/p.cellSize.xyz;
    int3 cell=int3(floor(grid));
    bool full=any(cell<int3(0))||any(cell>=int3(p.dims.xyz));
    LumaCell entry{0,0,0,0};
    if(!full) {
        uint id=uint(cell.x)+p.dims.x*(uint(cell.y)+p.dims.y*uint(cell.z));
        entry=cells[id];
        full=(entry.flags&1u)!=0u;
    }
    uint count=full?p.limits.x:entry.count;
    float totalWeight=0;
    for(uint j=0;j<count;++j) {
        uint id=full?j:candidates[entry.first+j];
        if(id>=p.limits.x)continue;
        LumaLight l=lights[id];
        totalWeight+=importance(directUnshadowed(l,world,normal),l.settings.y);
    }
    if(!(totalWeight>0.0f)||!isfinite(totalWeight)) {
        output.write(float4(0,0,0,d),pixel);
        return;
    }
    uint rng=(i+1u)*747796405u ^ (p.control.y+1u)*2891336453u;
    if(rng==0u)rng=1u;
    float3 direct=float3(0);
    for(uint sample=0;sample<p.limits.w;++sample) {
        const float target=float(nextRandom(rng))*(1.0f/4294967296.0f)*totalWeight;
        float weight=0, cumulative=0;
        float3 contribution=float3(0);
        LumaLight selected=lights[0];
        for(uint j=0;j<count;++j) {
            uint id=full?j:candidates[entry.first+j];
            if(id>=p.limits.x)continue;
            LumaLight l=lights[id];
            float3 c=directUnshadowed(l,world,normal);
            float w=importance(c,l.settings.y);
            if(w<=0)continue;
            cumulative+=w;
            if(target<cumulative || j==count-1u) {
                weight=w;selected=l;contribution=c;break;
            }
        }
        if(weight<=0)continue;
        bool blocked=false;
        if(selected.settings.z>.5f && p.control.x!=0u) {
            float3 start=world+normal*.02f;
            float3 ray=selected.positionRadius.xyz-start;
            float distance=length(ray);
            if(distance>.05f) {
                const float limit=distance-.03f;
                blocked=traceBvh(start,ray/distance,limit,triangles,nodes,
                                 p.control.x).distance<limit;
            }
        }
        if(!blocked)direct+=contribution*(totalWeight/weight);
    }
    direct/=max(1.0f,float(p.limits.w));
    output.write(float4(min(direct,float3(65504.0f)),d),pixel);
}
)MSL"
