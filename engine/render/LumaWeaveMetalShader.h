// Explicit Metal direct-light compute prototype: fixed stochastic shadow rays
// per supplied receiver. This is NOT auto-inserted into Renderer3D or PostFx.
// Geometry intersections reuse the exact Hybrid Dynamic GI Metal BVH tracer.
#pragma once
#include "GiGpuBvhMetalShader.h"

#define AIVERSE_MSL_LUMAWEAVE_DIRECT R"MSL(
#include <metal_stdlib>
using namespace metal;
)MSL" AIVERSE_MSL_GI_BVH_RAYQUERY R"MSL(
struct LumaLight {
    float4 positionRadius, colorIntensity, directionCone, settings;
    uint4 identity; // key lo, key hi, kind, indirect-GI flag
};
struct LumaCell {uint first,count,total,flags;};
struct LumaReceiver {float4 worldPosition, normal;};
struct LumaParams {
    float4 origin, cellSize;
    uint4 dims;    // number of cells x,y,z,total
    uint4 limits;  // sources, fast references, receiver count, samples per receiver
    uint4 control; // BVH node count, RNG frame seed, texture width, reserved
};
static float3 directUnshadowed(LumaLight l,float3 receiver,float3 normal) {
    float3 toLight=l.positionRadius.xyz-receiver;
    float d2=dot(toLight,toLight);
    float radius=l.positionRadius.w;
    if(d2<=.0001f || d2>=radius*radius || l.colorIntensity.w<=0.0f)
        return float3(0.0);
    float d=sqrt(d2);
    float3 L=toLight/d;
    float ndl=max(0.0f,dot(normal,L));
    if(ndl==0.0f)return float3(0.0);
    float angular=1.0f;
    if(l.identity.z==1u) {
        float cone=dot(l.directionCone.xyz,-L);
        float outer=l.directionCone.w;
        if(cone<=outer)return float3(0.0);
        float inner=l.settings.x;
        if(inner>outer)angular=clamp((cone-outer)/(inner-outer),0.0f,1.0f);
    }
    float attenuation=1.0f-d2/(radius*radius);
    attenuation=attenuation*attenuation/(1.0f+d2);
    return l.colorIntensity.xyz*(l.colorIntensity.w*ndl*angular*attenuation);
}
static float importance(float3 rgb,float bias) {
    return max(0.0f,dot(rgb,float3(.2126f,.7152f,.0722f)))*bias;
}
static uint nextRandom(thread uint& seed) {
    // Minimal reproducible shader RNG, not bit-identical to CPU SplitMix64.
    seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
    return seed;
}
kernel void cs_main(
    uint i [[thread_position_in_grid]],
    device const LumaLight* lights [[buffer(0)]],
    device const LumaReceiver* receivers [[buffer(1)]],
    device const LumaCell* cells [[buffer(2)]],
    device const uint* candidateIds [[buffer(3)]],
    device const Tri* triangles [[buffer(4)]],
    device const Node* nodes [[buffer(5)]],
    constant LumaParams& p [[buffer(8)]],
    texture2d<float,access::write> output [[texture(0)]]) {
    if(i>=p.limits.z)return;
    LumaReceiver receiver=receivers[i];
    uint2 uv=uint2(i%p.control.z,i/p.control.z);
    if(receiver.worldPosition.w<0.5f || p.limits.x==0u) {
        output.write(float4(0.0),uv);return;
    }
    float3 pos=receiver.worldPosition.xyz;
    float3 normal=receiver.normal.xyz;
    float n2=dot(normal,normal);
    if(n2<=1e-8f) {output.write(float4(0.0),uv);return;}
    normal*=rsqrt(n2);

    // World-aligned cluster candidates. Any truncated or out-of-grid cell
    // switches to ALL light sources to preserve correct direct energy.
    float3 grid=(pos-p.origin.xyz)/p.cellSize.xyz;
    int3 cell=int3(floor(grid));
    bool full=any(cell<int3(0)) || any(cell>=int3(p.dims.xyz));
    LumaCell entry{0,0,0,0};
    if(!full) {
        uint id=uint(cell.x)+p.dims.x*(uint(cell.y)+p.dims.y*uint(cell.z));
        entry=cells[id];
        full=(entry.flags&1u)!=0u;
    }
    uint count=full?p.limits.x:entry.count;
    float weightSum=0.0f;
    for(uint j=0;j<count;++j) {
        uint id=full?j:candidateIds[entry.first+j];
        if(id>=p.limits.x)continue;
        LumaLight l=lights[id];
        weightSum+=importance(directUnshadowed(l,pos,normal),l.settings.y);
    }
    if(!(weightSum>0.0f) || !isfinite(weightSum)) {
        output.write(float4(0.0),uv);return;
    }

    float3 total=float3(0.0f);
    uint rng=(i+1u)*747796405u ^ (p.control.y+1u)*2891336453u;
    if(rng==0u)rng=1u;
    for(uint sample=0;sample<p.limits.w;++sample) {
        float threshold=float(nextRandom(rng))*(1.0f/4294967296.0f)*weightSum;
        float cumulative=0.0f;
        float3 contribution=float3(0.0);
        float selectedWeight=0.0f;
        LumaLight selected=lights[0];
        for(uint j=0;j<count;++j) {
            uint id=full?j:candidateIds[entry.first+j];
            if(id>=p.limits.x)continue;
            LumaLight l=lights[id];
            float3 rgb=directUnshadowed(l,pos,normal);
            float w=importance(rgb,l.settings.y);
            if(w<=0.0f)continue;
            cumulative+=w;
            if(threshold<cumulative || j==count-1u) {
                contribution=rgb;
                selectedWeight=w;
                selected=l;
                break;
            }
        }
        if(selectedWeight<=0.0f)continue;
        bool occluded=false;
        if(selected.settings.z>0.5f && p.control.x>0u) {
            float3 origin=pos+normal*.02f;
            float3 ray=selected.positionRadius.xyz-origin;
            float d=length(ray);
            if(d>.05f) {
                float limit=d-.03f;
                occluded=traceBvh(origin,ray/d,limit,triangles,nodes,p.control.x).distance<limit;
            }
        }
        if(!occluded)total+=contribution*(weightSum/selectedWeight);
    }
    output.write(float4(total/max(float(p.limits.w),1.0f),1.0f),uv);
}
)MSL"
