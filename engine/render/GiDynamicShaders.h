// Optional dynamic GI L1 probe overlay. Only indirect radiance; no direct or sky term.
// The 384-texel RGBA32F/RGBA16F rows match GiDynamicProbeTexture (128 probes per row, 3 texels/probe).
// Four-probe tetrahedral interpolation, edge fade, zero outside the volume.
// Shader variants opt in with DYNAMIC_GI; this header adds no default shader cost.
#pragma once

#define AIVERSE_GLSL_DYNAMIC_GI_AT(TEX) \
    "\n" \
    "#if defined(DYNAMIC_GI)\n" \
    "layout(std140) uniform U6 { vec4 u_dynamicGiOrigin; vec4 u_dynamicGiSize; vec4 u_dynamicGiRing; };\n" \
    "uniform highp sampler2D " \
    TEX \
    ";\n" \
    "#if defined(DYNAMIC_GI_VALIDITY)\n" \
    "uniform highp sampler2D tex14;\n" \
    "#endif\n" \
    "vec4 dynamicGiTexel(int index) {\n" \
    "    return texelFetch(" \
    TEX \
    ", ivec2(index % 384, index / 384), 0);\n" \
    "}\n" \
    "vec3 dynamicGiAt(vec3 p, vec3 n) {\n" \
    "    vec3 coord=(p-u_dynamicGiOrigin.xyz)*u_dynamicGiOrigin.w-vec3(0.5);\n" \
    "    vec3 size=u_dynamicGiSize.xyz;\n" \
    "    if(any(lessThan(coord,vec3(-0.5))) || any(greaterThan(coord,size-vec3(0.5)))) return vec3(0.0);\n" \
    "    float edge=min(min(coord.x+0.5,size.x-0.5-coord.x),\n" \
    "                   min(min(coord.y+0.5,size.y-0.5-coord.y),min(coord.z+0.5,size.z-0.5-coord.z)));\n" \
    "    float fade=clamp(edge*2.0,0.0,1.0);\n" \
    "    vec3 fcoord=clamp(coord,vec3(0.0),size-1.0);\n" \
    "    ivec3 base=ivec3(floor(fcoord));\n" \
    "    vec3 f=fract(fcoord);\n" \
    "    int first=f.x>=f.y?(f.x>=f.z?0:2):(f.y>=f.z?1:2);\n" \
    "    int last=f.x<=f.y?(f.x<=f.z?0:2):(f.y<=f.z?1:2);\n" \
    "    if(first==last)last=(first+2)%3;\n" \
    "    int middle=3-first-last;\n" \
    "    vec4 normal=vec4(1.0,n);\n" \
    "    vec3 result=vec3(0.0);\n" \
    "    for(int i=0;i<4;++i){\n" \
    "        ivec3 corner=ivec3(0);\n" \
    "        if(i>=1)corner[first]=1;\n" \
    "        if(i>=2)corner[middle]=1;\n" \
    "        if(i>=3)corner[last]=1;\n" \
    "        float w=i==0?1.0-f[first]:i==1?f[first]-f[middle]:i==2?f[middle]-f[last]:f[last];\n" \
    "        if(w<=0.0)continue;\n" \
    "        ivec3 c=clamp(base+corner,ivec3(0),ivec3(size)-1);\n" \
    "        // Convert logical grid coordinates into stable toroidal atlas slots.\n" \
    "        ivec3 physical=(c+ivec3(u_dynamicGiRing.xyz))%ivec3(size);\n" \
    "        int probe=physical.x+int(size.x)*(physical.y+int(size.y)*physical.z), offset=probe*3;\n" \
    "#if defined(DYNAMIC_GI_VALIDITY)\n" \
    "        if(texelFetch(tex14,ivec2(probe%128,probe/128),0).r<0.5)continue;\n" \
    "#endif\n" \
    "        result+=w*max(vec3(dot(dynamicGiTexel(offset),normal),\n" \
    "                           dot(dynamicGiTexel(offset+1),normal),\n" \
    "                           dot(dynamicGiTexel(offset+2),normal)),vec3(0.0));\n" \
    "    }\n" \
    "    return result*fade;\n" \
    "}\n" \
    "#endif\n"

#define AIVERSE_GLSL_DYNAMIC_GI AIVERSE_GLSL_DYNAMIC_GI_AT("tex13")

#define AIVERSE_MSL_DYNAMIC_GI \
    "\n" \
    "#if defined(DYNAMIC_GI)\n" \
    "#ifndef DYNAMIC_GI_TEXTURE\n" \
    "#define DYNAMIC_GI_TEXTURE tex13\n" \
    "#define DYNAMIC_GI_SLOT 13\n" \
    "#define DYNAMIC_GI_BUFFER 7\n" \
    "#define DYNAMIC_GI_UNIFORM U6\n" \
    "#endif\n" \
    "struct GiDynamicU { float4 origin; float4 size; float4 ring; };\n" \
    "static float4 dynamicGiTexel(texture2d<float> tex13,int index) {\n" \
    "    return tex13.read(uint2(uint(index%384),uint(index/384)));\n" \
    "}\n" \
    "static float3 dynamicGiAt(texture2d<float> tex13,constant GiDynamicU& u,\n" \
    "#if defined(DYNAMIC_GI_VALIDITY)\n" \
    "                           texture2d<float> tex14,\n" \
    "#endif\n" \
    "                           float3 p,float3 n) {\n" \
    "    float3 coord=(p-u.origin.xyz)*u.origin.w-float3(0.5);\n" \
    "    float3 size=u.size.xyz;\n" \
    "    if(any(coord<float3(-0.5)) || any(coord>size-float3(0.5)))return float3(0.0);\n" \
    "    float edge=min(min(coord.x+0.5,size.x-0.5-coord.x),\n" \
    "                   min(min(coord.y+0.5,size.y-0.5-coord.y),min(coord.z+0.5,size.z-0.5-coord.z)));\n" \
    "    float fade=clamp(edge*2.0,0.0,1.0);\n" \
    "    float3 fcoord=clamp(coord,float3(0.0),size-1.0);\n" \
    "    int3 base=int3(floor(fcoord));\n" \
    "    float3 f=fract(fcoord);\n" \
    "    int first=f.x>=f.y?(f.x>=f.z?0:2):(f.y>=f.z?1:2);\n" \
    "    int last=f.x<=f.y?(f.x<=f.z?0:2):(f.y<=f.z?1:2);\n" \
    "    if(first==last)last=(first+2)%3;\n" \
    "    int middle=3-first-last;\n" \
    "    float4 normal=float4(1.0,n);\n" \
    "    float3 result=float3(0.0);\n" \
    "    for(int i=0;i<4;++i){\n" \
    "        int3 corner=int3(0);\n" \
    "        if(i>=1)corner[first]=1;\n" \
    "        if(i>=2)corner[middle]=1;\n" \
    "        if(i>=3)corner[last]=1;\n" \
    "        float w=i==0?1.0-f[first]:i==1?f[first]-f[middle]:i==2?f[middle]-f[last]:f[last];\n" \
    "        if(w<=0.0)continue;\n" \
    "        int3 c=clamp(base+corner,int3(0),int3(size)-1);\n" \
    "        int3 physical=(c+int3(u.ring.xyz))%int3(size);\n" \
    "        int probe=physical.x+int(size.x)*(physical.y+int(size.y)*physical.z),offset=probe*3;\n" \
    "#if defined(DYNAMIC_GI_VALIDITY)\n" \
    "        if(tex14.read(uint2(uint(probe%128),uint(probe/128))).r<0.5f)continue;\n" \
    "#endif\n" \
    "        result+=w*max(float3(dot(dynamicGiTexel(tex13,offset),normal),\n" \
    "                             dot(dynamicGiTexel(tex13,offset+1),normal),\n" \
    "                             dot(dynamicGiTexel(tex13,offset+2),normal)),float3(0.0));\n" \
    "    }\n" \
    "    return result*fade;\n" \
    "}\n" \
    "#endif\n"
