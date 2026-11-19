#pragma once
// Stage 26 — rigid object motion overlay for PRISMA.
// ModelVertex position occupies location 0, stride 60.
// Velocity RG is signed current-minus-previous normalized screen UV; B=1 valid.
// Opaque rigid primitives only. Occlusion comes from sampled scene depth,
// not from writing to the original depth attachment.
namespace aiverse::render::motion_shaders {
inline const char* kVS = R"(
layout(location=0) in vec3 a_pos;
layout(std140) uniform U0 {
    mat4 currentMvp;
    mat4 previousMvp;
    vec4 params;
};
out vec4 v_prevClip;
void main() {
    vec4 local = vec4(a_pos,1.0);
    gl_Position = currentMvp * local;
    v_prevClip = previousMvp * local;
}
)";
inline const char* kFS = R"(
layout(std140) uniform U0 {
    mat4 currentMvp;
    mat4 previousMvp;
    vec4 params;
};
uniform sampler2D tex0;
in vec4 v_prevClip;
out vec4 o_color;
void main() {
    vec2 s = gl_FragCoord.xy / params.xy;
    vec2 uv = params.z > 0.5 ? vec2(s.x,1.0-s.y) : s;
    float sceneZ = texture(tex0, uv).r;
    if (gl_FragCoord.z > sceneZ + 0.0015) discard;
    if (v_prevClip.w <= 0.00001) discard;
    vec2 prior = v_prevClip.xy / v_prevClip.w * 0.5 + 0.5;
    o_color = vec4(s-prior,1.0,1.0);
}
)";
inline const char* kMSL = R"(
#include <metal_stdlib>
using namespace metal;
struct U0 { float4x4 currentMvp; float4x4 previousMvp; float4 params; };
struct VIn { float3 pos [[attribute(0)]]; };
struct VOut {
    float4 position [[position]];
    float4 previous;
};
vertex VOut vs_main(VIn in [[stage_in]], constant U0& u [[buffer(1)]]) {
    VOut o;
    const float4 local=float4(in.pos,1.0);
    o.position=u.currentMvp*local;
    o.previous=u.previousMvp*local;
    return o;
}
fragment float4 fs_main(
    VOut in [[stage_in]],
    constant U0& u [[buffer(1)]],
    depth2d<float> tex0 [[texture(0)]],
    sampler tex0_s [[sampler(0)]]) {
    float2 s=in.position.xy/u.params.xy;
    // Metal raster pixels and render target textures both use top-left UV.\n    float2 uv=s;
    const float sceneZ=tex0.sample(tex0_s,uv).r;
    if (in.position.z > sceneZ+0.0015) discard_fragment();
    if (in.previous.w <= 0.00001) discard_fragment();
    const float2 prior=in.previous.xy/in.previous.w*0.5+0.5;
    // Metal raster position has top-left origin. Convert to NDC y-up.
    const float2 current=float2(s.x,1.0-s.y);
    return float4(current-prior,1.0,1.0);
}
)";

// Batched rigid velocity program: one indexed instance per independent
// ModelDraw rigid node. 16 copies of (current,previous) MVP fit in the
// portable 4096-byte U0 limit. The fragment mask/depth policy is identical
// to the single-draw version; instances never share transform history.
inline const char* kInstancedVS = R"(
layout(location=0) in vec3 a_pos;
layout(std140) uniform U0 {
    mat4 currentMvp[16];
    mat4 previousMvp[16];
    vec4 params;
};
out vec4 v_prevClip;
void main() {
    int index = gl_InstanceID;
    vec4 local = vec4(a_pos,1.0);
    gl_Position = currentMvp[index] * local;
    v_prevClip = previousMvp[index] * local;
}
)";
inline const char* kInstancedFS = R"(
layout(std140) uniform U0 {
    mat4 currentMvp[16];
    mat4 previousMvp[16];
    vec4 params;
};
uniform sampler2D tex0;
in vec4 v_prevClip;
out vec4 o_color;
void main() {
    vec2 s = gl_FragCoord.xy / params.xy;
    float sceneZ = texture(tex0,s).r;
    if (gl_FragCoord.z > sceneZ + 0.0015 || v_prevClip.w <= 0.00001)
        discard;
    vec2 prior = v_prevClip.xy / v_prevClip.w * 0.5 + 0.5;
    o_color = vec4(s-prior,1.0,1.0);
}
)";
inline const char* kInstancedMSL = R"(
#include <metal_stdlib>
using namespace metal;
struct U0 {
    float4x4 currentMvp[16];
    float4x4 previousMvp[16];
    float4 params;
};
struct VIn { float3 pos [[attribute(0)]]; };
struct VOut {
    float4 position [[position]];
    float4 previous;
};
vertex VOut vs_main(VIn in [[stage_in]],uint iid [[instance_id]],
                    constant U0& u [[buffer(1)]]) {
    VOut o;
    const float4 local = float4(in.pos,1.0);
    o.position = u.currentMvp[iid]*local;
    o.previous = u.previousMvp[iid]*local;
    return o;
}
fragment float4 fs_main(VOut in [[stage_in]],
    constant U0& u [[buffer(1)]],
    depth2d<float> tex0 [[texture(0)]],
    sampler tex0_s [[sampler(0)]]) {
    float2 s = in.position.xy / u.params.xy;
    float sceneZ = tex0.sample(tex0_s,s);
    if (in.position.z > sceneZ+0.0015 || in.previous.w <= 0.00001)
        discard_fragment();
    float2 prior = in.previous.xy / in.previous.w * 0.5 + 0.5;
    return float4(float2(s.x,1.0-s.y)-prior,1.0,1.0);
}
)";

// Current and previous GPU-deformed positions for palette skinned + morph
// vertices. Baked animation uses a different source (clip texture) and is
// intentionally excluded. Only the vertex position is deformed; the velocity
// overlay does not need normals/tangents or the original material pipeline.
inline const char* kDeformedVS = R"(
layout(location=0) in vec3 a_pos;
layout(location=5) in vec4 a_joints;
layout(location=6) in vec4 a_weights;
layout(std140) uniform U0 {
    mat4 currentMvp;
    mat4 previousMvp;
    vec4 params;
};
layout(std140) uniform U3 { vec4 currentJoints[240]; };
layout(std140) uniform U4 { vec4 previousJoints[240]; };
layout(std140) uniform U5 {
    vec4 currentMorph[2]; vec4 previousMorph[2];
    vec4 info; vec4 info2;
};
uniform highp sampler2D tex8;
out vec4 v_prevClip;
vec4 deformPoint(vec4 p, bool previous) {
    if (params.w > 0.5) {
        int vid = gl_VertexID - int(info.y);
        int count = int(info.z), targets = int(info.w);
        int base = int(info.x), textureWidth = max(int(info2.x),1);
        for (int k=0; k<8; ++k) {
            if (k >= targets) break;
            float weight = previous ? previousMorph[k/4][k%4] : currentMorph[k/4][k%4];
            if (weight == 0.0) continue;
            int index = base + (k * count + vid) * 2;
            p.xyz += weight * texelFetch(tex8,
                ivec2(index % textureWidth,index / textureWidth),0).xyz;
        }
    }
    if (params.z > 0.5) {
        vec4 weights = a_weights / max(dot(a_weights,vec4(1.0)),0.00001);
        vec4 r0=vec4(0.0),r1=vec4(0.0),r2=vec4(0.0);
        for(int k=0;k<4;++k) {
            if(weights[k]<=0.0)continue;
            int slot=int(a_joints[k]+0.5);
            r0 += (previous ? previousJoints[slot*3] : currentJoints[slot*3])*weights[k];
            r1 += (previous ? previousJoints[slot*3+1] : currentJoints[slot*3+1])*weights[k];
            r2 += (previous ? previousJoints[slot*3+2] : currentJoints[slot*3+2])*weights[k];
        }
        return vec4(dot(r0,p),dot(r1,p),dot(r2,p),1.0);
    }
    return p;
}
void main(){
    vec4 local=vec4(a_pos,1.0);
    gl_Position=currentMvp*deformPoint(local,false);
    v_prevClip=previousMvp*deformPoint(local,true);
}
)";
inline const char* kDeformedMSL = R"(
#include <metal_stdlib>
using namespace metal;
struct U0 {float4x4 currentMvp;float4x4 previousMvp;float4 params;};
struct Joints {float4 v[240];};
struct Morph {
    float4 currentWeights[2];float4 previousWeights[2];
    float4 info;float4 info2;
};
struct VIn {
    float3 pos [[attribute(0)]];
    uchar4 joints [[attribute(5)]];
    float4 weights [[attribute(6)]];
};
struct VOut {float4 position [[position]];float4 previous;};
float4 deformPoint(float4 p,VIn in,bool prev,
                  constant U0& u,constant Joints& current,
                  constant Joints& previous,constant Morph& mo,
                  texture2d<float> tex8,uint vid){
    if(u.params.w>0.5) {
        int lv=int(vid)-int(mo.info.y);
        int count=int(mo.info.z),targets=int(mo.info.w);
        int base=int(mo.info.x),width=max(int(mo.info2.x),1);
        for(int k=0;k<8;++k) {
            if(k>=targets)break;
            float weight=prev?mo.previousWeights[k/4][k%4]:mo.currentWeights[k/4][k%4];
            if(weight==0.0)continue;
            int index=base+(k*count+lv)*2;
            p.xyz+=weight*tex8.read(uint2(index%width,index/width)).xyz;
        }
    }
    if(u.params.z>0.5) {
        float4 weight=in.weights/max(dot(in.weights,float4(1.0)),0.00001);
        float4 r0=float4(0),r1=float4(0),r2=float4(0);
        for(int k=0;k<4;++k){
            if(weight[k]<=0.0)continue;
            int slot=int(in.joints[k]);
            r0+=(prev?previous.v[slot*3]:current.v[slot*3])*weight[k];
            r1+=(prev?previous.v[slot*3+1]:current.v[slot*3+1])*weight[k];
            r2+=(prev?previous.v[slot*3+2]:current.v[slot*3+2])*weight[k];
        }
        return float4(dot(r0,p),dot(r1,p),dot(r2,p),1.0);
    }
    return p;
}
vertex VOut vs_main(VIn in [[stage_in]],uint vid [[vertex_id]],
    constant U0& u [[buffer(1)]],
    constant Joints& current [[buffer(4)]],
    constant Joints& previous [[buffer(5)]],
    constant Morph& mo [[buffer(6)]],
    texture2d<float> tex8 [[texture(8)]]) {
    VOut o;
    const float4 local=float4(in.pos,1.0);
    o.position=u.currentMvp*deformPoint(local,in,false,u,current,previous,mo,tex8,vid);
    o.previous=u.previousMvp*deformPoint(local,in,true,u,current,previous,mo,tex8,vid);
    return o;
}
fragment float4 fs_main(VOut in [[stage_in]],constant U0& u [[buffer(1)]],
    depth2d<float> tex0 [[texture(0)]],sampler s0 [[sampler(0)]]) {
    float2 s=in.position.xy/u.params.xy;
    float z=tex0.sample(s0,s);
    if(in.position.z>z+0.0015||in.previous.w<=0.00001)discard_fragment();
    float2 p=in.previous.xy/in.previous.w*0.5+0.5;
    return float4(float2(s.x,1.0-s.y)-p,1.0,1.0);
}
)";
} // namespace aiverse::render::motion_shaders
