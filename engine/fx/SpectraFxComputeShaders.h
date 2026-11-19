// SpectraFX GPU particle integration kernel.
// Canonical gameplay remains in deterministic ECS; this is presentation-only.
// The kernel advances a bounded SoA batch and exports positions as a GPU
// storage texture. It does NOT replace ParticleSystem rendering/emitters yet.
#pragma once
namespace aiverse::fx {

inline constexpr const char* kSpectraFxComputeMetal=R"GPU(
#include <metal_stdlib>
using namespace metal;
struct Parameters { uint count; float dt; float damping; float pad; float4 acceleration; float4 ground; };
kernel void cs_main(uint2 tid [[thread_position_in_grid]],
                    device float4* positionAge [[buffer(0)]],
                    device float4* velocityLife [[buffer(1)]],
                    constant Parameters& p [[buffer(8)]],
                    texture2d<float,access::write> positionTexture [[texture(0)]]) {
    const uint i=tid.x;
    if(i>=p.count || tid.y!=0u) return;
    float4 a=positionAge[i];
    const float4 v=velocityLife[i];
    if(v.w>0.0f && a.w<v.w) {
        float3 nextVelocity=(v.xyz+p.acceleration.xyz*p.dt)*p.damping;
        a.xyz+=nextVelocity*p.dt;
        if(p.ground.w>0.5f && a.y<p.ground.x) {
            a.y=p.ground.x;
            if(nextVelocity.y<0.0f) {
                nextVelocity.y=-nextVelocity.y*p.ground.y;
                nextVelocity.xz*=p.ground.z;
            }
        }
        a.w=min(a.w+p.dt,v.w);
        velocityLife[i]=float4(nextVelocity,v.w);
    }
    positionAge[i]=a;
    const float remaining=v.w>0.0f?clamp((v.w-a.w)/v.w,0.0f,1.0f):0.0f;
    positionTexture.write(float4(a.xyz,remaining),uint2(i%positionTexture.get_width(),i/positionTexture.get_width()));
}
)GPU";

inline constexpr const char* kSpectraFxComputeGLSL=R"GPU(
#version 430
layout(local_size_x=64,local_size_y=1) in;
layout(std430,binding=0) buffer A { vec4 positionAge[]; };
layout(std430,binding=1) buffer B { vec4 velocityLife[]; };
layout(std140,binding=8) uniform Parameters {
    uint count; float dt; float damping; float pad; vec4 acceleration; vec4 ground;
} p;
layout(rgba32f,binding=0) uniform writeonly image2D positionTexture;
void main() {
    const uint i=gl_GlobalInvocationID.x;
    if(i>=p.count || gl_GlobalInvocationID.y!=0u) return;
    vec4 a=positionAge[i];
    vec4 v=velocityLife[i];
    if(v.w>0.0 && a.w<v.w) {
        vec3 velocity=(v.xyz+p.acceleration.xyz*p.dt)*p.damping;
        a.xyz+=velocity*p.dt;
        if(p.ground.w>0.5 && a.y<p.ground.x) {
            a.y=p.ground.x;
            if(velocity.y<0.0) {
                velocity.y=-velocity.y*p.ground.y;
                velocity.xz*=p.ground.z;
            }
        }
        a.w=min(a.w+p.dt,v.w);
        velocityLife[i]=vec4(velocity,v.w);
    }
    positionAge[i]=a;
    const ivec2 size=imageSize(positionTexture);
    const float remaining=v.w>0.0?clamp((v.w-a.w)/v.w,0.0,1.0):0.0;
    imageStore(positionTexture,ivec2(int(i)%size.x,int(i)/size.x),vec4(a.xyz,remaining));
}
)GPU";
} // namespace aiverse::fx
