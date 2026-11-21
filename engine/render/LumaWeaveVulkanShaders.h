// Vulkan (GLSL 450, SPIR-V) versions of the LumaWeave screen kernels and the
// HDR compositor. Same algorithms as the Metal sources (LumaWeaveFullscreen /
// Pbr / Denoise / HdrComposite); the C++ owner is LumaWeaveScreenMetal, which
// binds these on the Vulkan TextureResources compute layout:
//   storage images 0..3, sampled images 4..11, buffers 12..15, uniforms 16.
// Buffers: 12 lights, 13 clusters + candidate indices (one buffer: the
// candidates follow the dims.w cells as packed uints), 14 BVH triangles,
// 15 BVH nodes. Depth reconstruction honours DeviceInfo::clipDepthZeroToOne
// through control.w (1 = zero-to-one NDC depth, 0 = GL -1..1).
#pragma once

namespace aiverse::render::luma_vk {

#define AIVERSE_GLSL_LUMA_VK_SCREEN_COMMON R"GLSL(
struct LumaScreenParams {
    mat4 invVP;
    vec4 eye;          // xyz world, w = texture-Y inverted relative to NDC
    vec4 resolution;   // output W/H, scene W/H
    vec4 origin;
    vec4 cellSize;
    uvec4 dims;        // nx, ny, nz, cell count
    uvec4 limits;      // lights, candidate refs, pixels, samples
    uvec4 control;     // BVH nodes, temporal seed, output width, zero-to-one depth
};
float depthAt(sampler2D depth,vec2 projectedUv,bool flipY) {
    vec2 readUv=flipY?vec2(projectedUv.x,1.0-projectedUv.y):projectedUv;
    return texture(depth,clamp(readUv,vec2(0.0),vec2(1.0))).r;
}
float ndcDepth(float depth,LumaScreenParams p) {
    return p.control.w!=0u?depth:depth*2.0-1.0;
}
vec3 lumaWorld(vec2 uv,float depth,LumaScreenParams p) {
    vec4 h=p.invVP*vec4(uv*2.0-1.0,ndcDepth(depth,p),1.0);
    return h.xyz/max(abs(h.w),1e-8)*(h.w<0.0?-1.0:1.0);
}
vec3 lumaNormal(vec2 uv,float center,sampler2D depth,LumaScreenParams p) {
    vec2 dx=vec2(1.0/max(p.resolution.z,1.0),0.0);
    vec2 dy=vec2(0.0,1.0/max(p.resolution.w,1.0));
    vec3 origin=lumaWorld(uv,center,p);
    vec3 tangent=lumaWorld(clamp(uv+dx,0.0,1.0),depthAt(depth,uv+dx,p.eye.w>0.5),p)-origin;
    vec3 bitangent=lumaWorld(clamp(uv+dy,0.0,1.0),depthAt(depth,uv+dy,p.eye.w>0.5),p)-origin;
    vec3 n=cross(tangent,bitangent);
    float length2=dot(n,n);
    if(length2<1e-10)return vec3(0.0,1.0,0.0);
    n*=inversesqrt(length2);
    if(dot(n,p.eye.xyz-origin)<0.0)n=-n;
    return n;
}
)GLSL"

// Sources, candidate lookup and the shared BVH shadow query (same intersection
// as GiGpuBvhMetalShader.h / the GI Vulkan kernel).
#define AIVERSE_GLSL_LUMA_VK_LIGHTS R"GLSL(
struct LumaLight {
    vec4 positionRadius, colorIntensity, directionCone, settings;
    uvec4 identity; // key lo, key hi, kind, indirect-GI flag
};
struct Tri { vec4 a, b, c, albedo; };
struct Node { vec4 lo, hi; uvec4 meta; };
layout(set=0,binding=12,std430) readonly buffer Lights { LumaLight lights[]; };
layout(set=0,binding=13,std430) readonly buffer Cells { uvec4 cellData[]; };
layout(set=0,binding=14,std430) readonly buffer Triangles { Tri triangles[]; };
layout(set=0,binding=15,std430) readonly buffer Nodes { Node nodes[]; };
layout(set=0,binding=16,std140) uniform Params { LumaScreenParams p; };
uint candidate(uint k) {
    uint index=p.dims.x*p.dims.y*p.dims.z+k/4u;
    return cellData[index][k%4u];
}
vec3 directUnshadowed(LumaLight l,vec3 receiver,vec3 normal) {
    vec3 toLight=l.positionRadius.xyz-receiver;
    float d2=dot(toLight,toLight);
    float radius=l.positionRadius.w;
    if(d2<=0.0001 || d2>=radius*radius || l.colorIntensity.w<=0.0)return vec3(0.0);
    float d=sqrt(d2);
    vec3 L=toLight/d;
    float ndl=max(0.0,dot(normal,L));
    if(ndl==0.0)return vec3(0.0);
    float angular=1.0;
    if(l.identity.z==1u) {
        float cone=dot(l.directionCone.xyz,-L);
        float outer=l.directionCone.w;
        if(cone<=outer)return vec3(0.0);
        float inner=l.settings.x;
        if(inner>outer)angular=clamp((cone-outer)/(inner-outer),0.0,1.0);
    }
    float attenuation=1.0-d2/(radius*radius);
    attenuation=attenuation*attenuation/(1.0+d2);
    return l.colorIntensity.xyz*(l.colorIntensity.w*ndl*angular*attenuation);
}
float importance(vec3 rgb,float bias) {
    return max(0.0,dot(rgb,vec3(0.2126,0.7152,0.0722)))*bias;
}
uint nextRandom(inout uint seed) {
    seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
    return seed;
}
bool hitsBox(vec3 o,vec3 d,float radius,Node node) {
    float entry=0.0,exit=radius;
    for(int axis=0;axis<3;++axis) {
        float q=o[axis],v=d[axis],mn=node.lo[axis],mx=node.hi[axis];
        if(abs(v)<1e-10) {
            if(q<mn || q>mx)return false;
            continue;
        }
        float t0=(mn-q)/v,t1=(mx-q)/v;
        if(t0>t1){float t=t0;t0=t1;t1=t;}
        entry=max(entry,t0);exit=min(exit,t1);
        if(entry>exit)return false;
    }
    return true;
}
float traceDistance(vec3 origin,vec3 direction,float limit,uint nodeCount) {
    float best=limit;
    if(nodeCount==0u)return best;
    uint stack[64];
    uint top=1u;stack[0]=0u;
    while(top!=0u) {
        uint nodeIndex=stack[--top];
        if(nodeIndex>=nodeCount)continue;
        Node node=nodes[nodeIndex];
        if(!hitsBox(origin,direction,best,node))continue;
        uint count=node.meta.w;
        if(count!=0u) {
            for(uint j=node.meta.z;j<node.meta.z+count;++j) {
                Tri t=triangles[j];
                vec3 e1=t.b.xyz-t.a.xyz,e2=t.c.xyz-t.a.xyz;
                vec3 crossD=cross(direction,e2);
                float det=dot(e1,crossD);
                if(abs(det)<1e-10)continue;
                float inv=1.0/det;
                vec3 s=origin-t.a.xyz;
                float u=dot(s,crossD)*inv;
                if(u<0.0||u>1.0)continue;
                vec3 q=cross(s,e1);
                float v=dot(direction,q)*inv;
                if(v<0.0||u+v>1.0)continue;
                float hit=dot(e2,q)*inv;
                if(hit>0.001 && hit<best)best=hit;
            }
        } else {
            uint children=node.meta.y==0xffffffffu?1u:2u;
            if(top+children<=64u) {
                if(children==2u)stack[top++]=node.meta.y;
                stack[top++]=node.meta.x;
            }
        }
    }
    return best;
}
// Cluster lookup: count of candidates and whether all lights apply.
void cellFor(vec3 world,out uint first,out uint count,out bool full) {
    vec3 grid=(world-p.origin.xyz)/p.cellSize.xyz;
    ivec3 cell=ivec3(floor(grid));
    full=any(lessThan(cell,ivec3(0)))||any(greaterThanEqual(cell,ivec3(p.dims.xyz)));
    first=0u;count=0u;
    if(!full) {
        uint id=uint(cell.x)+p.dims.x*(uint(cell.y)+p.dims.y*uint(cell.z));
        uvec4 entry=cellData[id];
        first=entry.x;count=entry.y;
        full=(entry.w&1u)!=0u;
    }
    if(full)count=p.limits.x;
}
)GLSL"

// Screen direct (diffuse incident) or, with LUMAWEAVE_PBR, material-reflected GGX.
#define AIVERSE_GLSL_LUMA_VK_DIRECT_BODY R"GLSL(
layout(set=0,binding=0,rgba16f) writeonly uniform image2D outputImage;
layout(set=0,binding=4) uniform sampler2D sceneDepth;
#if defined(LUMAWEAVE_PBR)
layout(set=0,binding=5) uniform sampler2D surfaceMaterial;
layout(set=0,binding=6) uniform sampler2D surfaceNormal;
layout(set=0,binding=7) uniform sampler2D receiverDepth;
vec3 lumaPbrReflected(LumaLight l,vec3 q,vec3 n,vec3 v,vec3 base,float metallic,float rough) {
    vec3 cosineRadiance=directUnshadowed(l,q,n);
    if(all(lessThanEqual(cosineRadiance,vec3(0.0))))return vec3(0.0);
    vec3 toLight=l.positionRadius.xyz-q;
    float dist2=dot(toLight,toLight);
    if(dist2<=0.0001)return vec3(0.0);
    vec3 L=toLight*inversesqrt(dist2);
    float ndl=max(dot(n,L),0.0),ndv=max(dot(n,v),0.0);
    if(ndl<=1e-5 || ndv<=1e-5)return vec3(0.0);
    vec3 halfVector=v+L;
    float h2=dot(halfVector,halfVector);
    if(h2<=1e-8)return vec3(0.0);
    vec3 H=halfVector*inversesqrt(h2);
    float ndh=max(dot(n,H),0.0),vdh=max(dot(v,H),0.0);
    float alpha=max(0.045,rough);
    float a=alpha*alpha,a2=a*a;
    float d=ndh*ndh*(a2-1.0)+1.0;
    float D=a2/(3.141592653589793*d*d);
    float k=(alpha+1.0)*(alpha+1.0)*0.125;
    float G=(ndl/(ndl*(1.0-k)+k))*(ndv/(ndv*(1.0-k)+k));
    vec3 F0=mix(vec3(0.04),base,metallic);
    vec3 F=F0+(1.0-F0)*pow(1.0-vdh,5.0);
    vec3 diffuse=base*(1.0-metallic)*(1.0-F)*(1.0/3.141592653589793);
    vec3 specular=(D*G*F)/max(4.0*ndl*ndv,1e-5);
    return cosineRadiance*(diffuse+specular);
}
#endif
void main() {
    uint i=gl_GlobalInvocationID.x;
    if(i>=p.limits.z)return;
    ivec2 pixel=ivec2(int(i%p.control.z),int(i/p.control.z));
    vec2 uv=(vec2(pixel)+0.5)/p.resolution.xy;
    bool flipped=p.eye.w>0.5;
    float d=depthAt(sceneDepth,uv,flipped);
    if(d>=0.99999 || d<=0.0){imageStore(outputImage,pixel,vec4(0.0));return;}
    if(p.limits.x==0u){imageStore(outputImage,pixel,vec4(0,0,0,d));return;}
    vec3 world=lumaWorld(uv,d,p);
#if defined(LUMAWEAVE_PBR)
    float receiverZ=depthAt(receiverDepth,uv,flipped);
    if(receiverZ>=0.99999 || abs(receiverZ-d)>0.008){imageStore(outputImage,pixel,vec4(0,0,0,d));return;}
    vec2 readUv=flipped?vec2(uv.x,1.0-uv.y):uv;
    vec4 material=texture(surfaceMaterial,readUv);
    vec4 surface=texture(surfaceNormal,readUv);
    float n2=dot(surface.xyz,surface.xyz);
    if(n2<0.25 || any(isnan(surface)) || any(isinf(surface))){imageStore(outputImage,pixel,vec4(0,0,0,d));return;}
    vec3 normal=surface.xyz*inversesqrt(n2);
    vec3 base=clamp(material.rgb,vec3(0.0),vec3(1.0));
    float metallic=clamp(material.a,0.0,1.0);
    float rough=clamp(surface.w,0.045,1.0);
    vec3 V=p.eye.xyz-world;
    float v2=dot(V,V);
    if(v2<1e-8){imageStore(outputImage,pixel,vec4(0,0,0,d));return;}
    V*=inversesqrt(v2);
#define LUMA_RADIANCE(l) lumaPbrReflected(l,world,normal,V,base,metallic,rough)
#else
    vec3 normal=lumaNormal(uv,d,sceneDepth,p);
#define LUMA_RADIANCE(l) directUnshadowed(l,world,normal)
#endif
    uint first,count;bool full;
    cellFor(world,first,count,full);
    float totalWeight=0.0;
    for(uint j=0u;j<count;++j) {
        uint id=full?j:candidate(first+j);
        if(id>=p.limits.x)continue;
        LumaLight l=lights[id];
        totalWeight+=importance(LUMA_RADIANCE(l),l.settings.y);
    }
    if(!(totalWeight>0.0) || isinf(totalWeight)){imageStore(outputImage,pixel,vec4(0,0,0,d));return;}
    uint rng=(i+1u)*747796405u ^ (p.control.y+1u)*2891336453u;
    if(rng==0u)rng=1u;
    vec3 direct=vec3(0.0);
    for(uint s=0u;s<p.limits.w;++s) {
        float target=float(nextRandom(rng))*(1.0/4294967296.0)*totalWeight;
        float weight=0.0,cumulative=0.0;
        vec3 contribution=vec3(0.0);
        LumaLight selected=lights[0];
        for(uint j=0u;j<count;++j) {
            uint id=full?j:candidate(first+j);
            if(id>=p.limits.x)continue;
            LumaLight l=lights[id];
            vec3 c=LUMA_RADIANCE(l);
            float w=importance(c,l.settings.y);
            if(w<=0.0)continue;
            cumulative+=w;
            if(target<cumulative || j==count-1u){weight=w;selected=l;contribution=c;break;}
        }
        if(weight<=0.0)continue;
        bool blocked=false;
        if(selected.settings.z>0.5 && p.control.x!=0u) {
            vec3 start=world+normal*0.02;
            vec3 ray=selected.positionRadius.xyz-start;
            float distance=length(ray);
            if(distance>0.05) {
                float limit=distance-0.03;
                blocked=traceDistance(start,ray/distance,limit,p.control.x)<limit;
            }
        }
        if(!blocked)direct+=contribution*(totalWeight/weight);
    }
    direct/=max(1.0,float(p.limits.w));
    imageStore(outputImage,pixel,vec4(clamp(direct,vec3(0.0),vec3(65504.0)),d));
}
)GLSL"

inline constexpr const char* kDirect =
    AIVERSE_GLSL_LUMA_VK_SCREEN_COMMON AIVERSE_GLSL_LUMA_VK_LIGHTS AIVERSE_GLSL_LUMA_VK_DIRECT_BODY;
inline constexpr const char* kDirectPbr = "#define LUMAWEAVE_PBR 1\n"
    AIVERSE_GLSL_LUMA_VK_SCREEN_COMMON AIVERSE_GLSL_LUMA_VK_LIGHTS AIVERSE_GLSL_LUMA_VK_DIRECT_BODY;

// Spatial edge-aware 3x3 + temporal reprojected history (Metal LumaWeaveDenoise).
#define AIVERSE_GLSL_LUMA_VK_DENOISE_BODY R"GLSL(
layout(set=0,binding=16,std140) uniform Params {
    LumaScreenParams current;
    mat4 prevVP;
    vec4 history;   // previousValid, blend, normal dot, depth delta
    vec4 spatial;   // neighbour depth delta, RGB clamp, camera cut, object velocity bound
};
layout(set=0,binding=0,rgba16f) writeonly uniform image2D filtered;
layout(set=0,binding=1,rgba16f) writeonly uniform image2D normals;
layout(set=0,binding=4) uniform sampler2D direct;
layout(set=0,binding=5) uniform sampler2D oldFiltered;
layout(set=0,binding=6) uniform sampler2D oldNormals;
layout(set=0,binding=7) uniform sampler2D sceneDepth;
#if defined(LUMAWEAVE_PBR_NORMALS)
layout(set=0,binding=8) uniform sampler2D surfaceNormal;
#endif
layout(set=0,binding=9) uniform sampler2D objectVelocity;
void main() {
    uint i=gl_GlobalInvocationID.x;
    if(i>=current.limits.z)return;
    int w=int(current.resolution.x),h=int(current.resolution.y);
    ivec2 pixel=ivec2(int(i)%w,int(i)/w);
    vec4 center=texelFetch(direct,pixel,0);
    float d=center.a;
    if(d>=0.99999 || d<=0.0) {
        imageStore(filtered,pixel,vec4(0.0));imageStore(normals,pixel,vec4(0.0));return;
    }
    vec2 uv=(vec2(pixel)+0.5)/current.resolution.xy;
    vec3 world=lumaWorld(uv,d,current);
    vec3 normal=lumaNormal(uv,d,sceneDepth,current);
#if defined(LUMAWEAVE_PBR_NORMALS)
    vec2 surfaceUv=current.eye.w>0.5?vec2(uv.x,1.0-uv.y):uv;
    ivec2 size=textureSize(surfaceNormal,0);
    ivec2 surfaceCoord=ivec2(clamp(surfaceUv*vec2(size),vec2(0.0),vec2(size-1)));
    vec3 received=texelFetch(surfaceNormal,surfaceCoord,0).xyz;
    if(dot(received,received)>0.25)normal=normalize(received);
#endif
    float sigma=max(spatial.x,1e-6);
    vec3 color=max(center.rgb,vec3(0.0));
    float sum=1.0;
    for(int yy=-1;yy<=1;++yy)for(int xx=-1;xx<=1;++xx) {
        if(xx==0&&yy==0)continue;
        ivec2 c=pixel+ivec2(xx,yy);
        if(c.x<0||c.y<0||c.x>=w||c.y>=h)continue;
        vec4 tap=texelFetch(direct,c,0);
        if(tap.a<=0.0 || tap.a>=0.99999)continue;
        float depthDelta=abs(d-tap.a);
        if(depthDelta>sigma*4.0)continue;
        float weight=exp(-depthDelta*depthDelta/(2.0*sigma*sigma))*((xx==0||yy==0)?0.8:0.55);
        color+=max(tap.rgb,vec3(0.0))*weight;
        sum+=weight;
    }
    color/=sum;
    if(history.x>0.5) {
        vec4 clip=prevVP*vec4(world,1.0);
        // Moving objects: object velocity replaces camera reprojection (Metal kernel).
        bool moved=false;
        vec2 movedUv=vec2(0.0);
        if(spatial.w>0.5) {
            vec2 velocityUv=current.eye.w>0.5?vec2(uv.x,1.0-uv.y):uv;
            vec4 v=texture(objectVelocity,velocityUv);
            if(v.b>0.5){moved=true;movedUv=uv-v.rg;}
        }
        if(clip.w>1e-5 || moved) {
            vec3 ndc=clip.w>1e-5?clip.xyz/clip.w:vec3(0.0);
            // History stores device depth; GL clip depth maps -1..1 to 0..1.
            float oldDepth=current.control.w!=0u?ndc.z:ndc.z*0.5+0.5;
            vec2 oldUv=moved?movedUv:ndc.xy*0.5+0.5;
            vec2 readUv=current.eye.w>0.5?vec2(oldUv.x,1.0-oldUv.y):oldUv;
            if(all(greaterThanEqual(readUv,vec2(0.0))) && all(lessThan(readUv,vec2(1.0))) &&
               (moved || (oldDepth>=0.0 && oldDepth<=1.0))) {
                ivec2 oldPixel=ivec2(readUv*current.resolution.xy);
                vec4 old=texelFetch(oldFiltered,oldPixel,0);
                vec4 oldNormal=texelFetch(oldNormals,oldPixel,0);
                float delta=abs(oldNormal.w-oldDepth);
                float nd=dot(normal,oldNormal.xyz);
                if(old.w>0.0 && old.w<0.99999 && (moved || delta<=history.w) && nd>=history.z) {
                    vec3 clampWidth=vec3(spatial.y)*(1.0+color);
                    vec3 historic=clamp(old.rgb,max(color-clampWidth,vec3(0.0)),color+clampWidth);
                    color=mix(color,historic,clamp(history.y,0.0,0.95));
                }
            }
        }
    }
    imageStore(filtered,pixel,vec4(max(color,vec3(0.0)),d));
    imageStore(normals,pixel,vec4(normal,d));
}
)GLSL"

inline constexpr const char* kDenoise =
    AIVERSE_GLSL_LUMA_VK_SCREEN_COMMON AIVERSE_GLSL_LUMA_VK_DENOISE_BODY;
inline constexpr const char* kDenoisePbr = "#define LUMAWEAVE_PBR_NORMALS 1\n"
    AIVERSE_GLSL_LUMA_VK_SCREEN_COMMON AIVERSE_GLSL_LUMA_VK_DENOISE_BODY;

// HDR additive compositor (GLSL ES 3.00 bodies for the GL / Vulkan RHI).
inline constexpr const char* kComposeVS = R"GLSL(
layout(location = 0) in vec2 a_pos;
out vec2 v_uv;
void main() { v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }
)GLSL";
inline constexpr const char* kComposeFS = R"GLSL(
layout(std140) uniform U0 { vec4 u_options; }; // flipY, depth tolerance, exposure, already reflected
uniform highp sampler2D tex0;  // low-resolution direct (alpha = device depth)
uniform highp sampler2D tex1;  // receiver material: baseColor, metallic
uniform highp sampler2D tex2;  // scene depth
uniform highp sampler2D tex3;  // receiver depth
in vec2 v_uv;
out vec4 o_color;
void main() {
    vec2 texUv = u_options.x > 0.5 ? vec2(v_uv.x, 1.0 - v_uv.y) : v_uv;
    float depth = texture(tex2, texUv).r;
    o_color = vec4(0.0);
    if (depth >= 0.99999) return;
    float receiverZ = texture(tex3, texUv).r;
    if (receiverZ >= 0.99999 || abs(receiverZ - depth) > max(u_options.y, 1e-5)) return;
    vec4 material = texture(tex1, texUv);
    vec3 diffuse = clamp(material.rgb, 0.0, 1.0) * (1.0 - clamp(material.a, 0.0, 1.0));
    if (u_options.w < 0.5 && all(lessThanEqual(diffuse, vec3(0.0)))) return;
    ivec2 size = textureSize(tex0, 0);
    vec2 uv = texUv * vec2(size) - 0.5;
    ivec2 base = ivec2(floor(uv));
    vec2 f = fract(uv);
    vec3 accumulated = vec3(0.0);
    float weightSum = 0.0;
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        ivec2 coord = clamp(base + ivec2(x, y), ivec2(0), size - 1);
        vec4 value = texelFetch(tex0, coord, 0);
        if (value.a <= 0.0 || value.a >= 0.99999) continue;
        float delta = abs(depth - value.a);
        if (delta > u_options.y) continue;
        float depthWeight = 1.0 - delta / max(u_options.y, 1e-6);
        float bilinear = (x == 0 ? 1.0 - f.x : f.x) * (y == 0 ? 1.0 - f.y : f.y);
        float w = bilinear * depthWeight;
        accumulated += max(value.rgb, vec3(0.0)) * w;
        weightSum += w;
    }
    if (weightSum <= 0.0) return;
    vec3 irradiance = accumulated / weightSum;
    vec3 reflected = u_options.w > 0.5 ? irradiance * max(u_options.z, 0.0)
                                        : irradiance * diffuse * (1.0 / 3.14159265358979323846) * max(u_options.z, 0.0);
    o_color = vec4(max(reflected, vec3(0.0)), 0.0);
}
)GLSL";

} // namespace aiverse::render::luma_vk
