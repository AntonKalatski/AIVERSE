// NEREID wave model shared by every water surface: the plane water in the lit shader (Shaders.h) and the
// sea of cube-sphere planets (RtsPlanetShaders.h, sampled triplanar on the sphere). GLSL and MSL twins.
#pragma once

#define AIVERSE_GLSL_NEREID_WAVES \
    "float nereidNoise(vec2 p) {\n" \
    "    vec2 i=floor(p),f=fract(p); f=f*f*(3.0-2.0*f);\n" \
    "    float a=fract(sin(dot(i,vec2(127.1,311.7)))*43758.5453);\n" \
    "    float b=fract(sin(dot(i+vec2(1,0),vec2(127.1,311.7)))*43758.5453);\n" \
    "    float c=fract(sin(dot(i+vec2(0,1),vec2(127.1,311.7)))*43758.5453);\n" \
    "    float d=fract(sin(dot(i+vec2(1,1),vec2(127.1,311.7)))*43758.5453);\n" \
    "    return mix(mix(a,b,f.x),mix(c,d,f.x),f.y);\n" \
    "}\n" \
    "vec2 nereidWaveGradient(vec2 p,float intrinsicTime,bool tagged) {\n" \
    "    float phaseWarp=0.65*sin(p.x*0.37+sin(p.y*0.29))+0.4*sin(p.y*0.53-p.x*0.19);\n" \
    "    const vec4 wv[6] = vec4[6](vec4(0.951,0.309,0.61,0.060),vec4(-0.454,0.891,0.97,0.045),vec4(0.588,-0.809,1.63,0.036),vec4(-0.978,-0.208,2.71,0.026),vec4(0.139,0.990,4.37,0.018),vec4(0.743,0.669,6.91,0.012));\n" \
    "    vec2 g=vec2(0.0);\n" \
    "    for(int i=0;i<6;++i) {\n" \
    "        float ph=dot(p,wv[i].xy)*wv[i].z+intrinsicTime*sqrt(wv[i].z)*1.3+(tagged?phaseWarp:0.0);\n" \
    "        g+=wv[i].xy*(wv[i].z*wv[i].w*cos(ph));\n" \
    "    }\n" \
    "    vec2 q=p*.9+vec2(intrinsicTime*.11,-intrinsicTime*.07);\n" \
    "    if(tagged)q+=vec2(sin(p.y*.37+p.x*.17),cos(p.x*.43-p.y*.23))*1.7;\n" \
    "    vec2 qi=floor(q),qf=fract(q),qu=qf*qf*(3.0-2.0*qf);\n" \
    "    float h00=fract(sin(dot(qi,vec2(127.1,311.7)))*43758.5),h10=fract(sin(dot(qi+vec2(1,0),vec2(127.1,311.7)))*43758.5);\n" \
    "    float h01=fract(sin(dot(qi+vec2(0,1),vec2(127.1,311.7)))*43758.5),h11=fract(sin(dot(qi+vec2(1,1),vec2(127.1,311.7)))*43758.5);\n" \
    "    vec2 dn=vec2(mix(h10-h00,h11-h01,qu.y),mix(h01-h00,h11-h10,qu.x))*6.0*qf*(1.0-qf);\n" \
    "    return g+dn*.09;\n" \
    "}\n" \
    "float nereidMacroPatch(vec2 p,float intrinsicTime) {\n" \
    "    vec2 m=p*.021+vec2(intrinsicTime*.004,0.0);\n" \
    "    return .5+.5*sin(m.x*2.3+sin(m.y*1.7)*2.1)*sin(m.y*1.9+sin(m.x*1.3)*1.7);\n" \
    "}\n" \
    "// NEREID waves on a sphere: the plane wave model sampled triplanar in body-local coordinates\n" \
    "// (q: point relative to the body centre, up: unit surface direction, fade: distance fade of the detail).\n" \
    "vec3 nereidSphereNormal(vec3 q, vec3 up, float t, float fade) {\n" \
    "    vec3 w = pow(abs(up), vec3(4.0)); w /= (w.x + w.y + w.z);\n" \
    "    vec2 gx = nereidWaveGradient(q.zy, t, true), gy = nereidWaveGradient(q.xz, t, true), gz = nereidWaveGradient(q.xy, t, true);\n" \
    "    vec3 g = vec3(0.0, gx.y, gx.x) * w.x + vec3(gy.x, 0.0, gy.y) * w.y + vec3(gz.x, gz.y, 0.0) * w.z;\n" \
    "    g -= up * dot(g, up);\n" \
    "    return normalize(up - g * fade);\n" \
    "}\n" \
    "float nereidSpherePatch(vec3 q, vec3 up, float t) {\n" \
    "    vec3 w = pow(abs(up), vec3(4.0)); w /= (w.x + w.y + w.z);\n" \
    "    return nereidMacroPatch(q.zy, t) * w.x + nereidMacroPatch(q.xz, t) * w.y + nereidMacroPatch(q.xy, t) * w.z;\n" \
    "}\n" \
    "float nereidSphereFoam(vec3 q, vec3 up, float t) {\n" \
    "    vec3 w = abs(up); w /= (w.x + w.y + w.z);\n" \
    "    vec2 drift = vec2(t * .21, -t * .17);\n" \
    "    return nereidNoise(q.zy * 2.3 + drift) * w.x + nereidNoise(q.xz * 2.3 + drift) * w.y + nereidNoise(q.xy * 2.3 + drift) * w.z;\n" \
    "}\n"

#define AIVERSE_MSL_NEREID_WAVES \
    "static float nereidNoise(float2 p) {\n" \
    "    float2 i=floor(p),f=fract(p); f=f*f*(3.0-2.0*f);\n" \
    "    float a=fract(sin(dot(i,float2(127.1,311.7)))*43758.5453);\n" \
    "    float b=fract(sin(dot(i+float2(1,0),float2(127.1,311.7)))*43758.5453);\n" \
    "    float c=fract(sin(dot(i+float2(0,1),float2(127.1,311.7)))*43758.5453);\n" \
    "    float d=fract(sin(dot(i+float2(1,1),float2(127.1,311.7)))*43758.5453);\n" \
    "    return mix(mix(a,b,f.x),mix(c,d,f.x),f.y);\n" \
    "}\n" \
    "float2 nereidWaveGradient(float2 p,float intrinsicTime,bool tagged) {\n" \
    "    float phaseWarp=0.65*sin(p.x*0.37+sin(p.y*0.29))+0.4*sin(p.y*0.53-p.x*0.19);\n" \
    "    const float4 wv[6] = {float4(0.951,0.309,0.61,0.060),float4(-0.454,0.891,0.97,0.045),float4(0.588,-0.809,1.63,0.036),float4(-0.978,-0.208,2.71,0.026),float4(0.139,0.990,4.37,0.018),float4(0.743,0.669,6.91,0.012)};\n" \
    "    float2 g=float2(0.0);\n" \
    "    for(int i=0;i<6;++i) {\n" \
    "        float ph=dot(p,wv[i].xy)*wv[i].z+intrinsicTime*sqrt(wv[i].z)*1.3+(tagged?phaseWarp:0.0);\n" \
    "        g+=wv[i].xy*(wv[i].z*wv[i].w*cos(ph));\n" \
    "    }\n" \
    "    float2 q=p*.9+float2(intrinsicTime*.11,-intrinsicTime*.07);\n" \
    "    if(tagged)q+=float2(sin(p.y*.37+p.x*.17),cos(p.x*.43-p.y*.23))*1.7;\n" \
    "    float2 qi=floor(q),qf=fract(q),qu=qf*qf*(3.0-2.0*qf);\n" \
    "    float h00=fract(sin(dot(qi,float2(127.1,311.7)))*43758.5),h10=fract(sin(dot(qi+float2(1,0),float2(127.1,311.7)))*43758.5);\n" \
    "    float h01=fract(sin(dot(qi+float2(0,1),float2(127.1,311.7)))*43758.5),h11=fract(sin(dot(qi+float2(1,1),float2(127.1,311.7)))*43758.5);\n" \
    "    float2 dn=float2(mix(h10-h00,h11-h01,qu.y),mix(h01-h00,h11-h10,qu.x))*6.0*qf*(1.0-qf);\n" \
    "    return g+dn*.09;\n" \
    "}\n" \
    "float nereidMacroPatch(float2 p,float intrinsicTime) {\n" \
    "    float2 m=p*.021+float2(intrinsicTime*.004,0.0);\n" \
    "    return .5+.5*sin(m.x*2.3+sin(m.y*1.7)*2.1)*sin(m.y*1.9+sin(m.x*1.3)*1.7);\n" \
    "}\n" \
    "// NEREID waves on a sphere: the plane wave model sampled triplanar in body-local coordinates\n" \
    "// (q: point relative to the body centre, up: unit surface direction, fade: distance fade of the detail).\n" \
    "float3 nereidSphereNormal(float3 q, float3 up, float t, float fade) {\n" \
    "    float3 w = pow(abs(up), float3(4.0)); w /= (w.x + w.y + w.z);\n" \
    "    float2 gx = nereidWaveGradient(q.zy, t, true), gy = nereidWaveGradient(q.xz, t, true), gz = nereidWaveGradient(q.xy, t, true);\n" \
    "    float3 g = float3(0.0, gx.y, gx.x) * w.x + float3(gy.x, 0.0, gy.y) * w.y + float3(gz.x, gz.y, 0.0) * w.z;\n" \
    "    g -= up * dot(g, up);\n" \
    "    return normalize(up - g * fade);\n" \
    "}\n" \
    "float nereidSpherePatch(float3 q, float3 up, float t) {\n" \
    "    float3 w = pow(abs(up), float3(4.0)); w /= (w.x + w.y + w.z);\n" \
    "    return nereidMacroPatch(q.zy, t) * w.x + nereidMacroPatch(q.xz, t) * w.y + nereidMacroPatch(q.xy, t) * w.z;\n" \
    "}\n" \
    "float nereidSphereFoam(float3 q, float3 up, float t) {\n" \
    "    float3 w = abs(up); w /= (w.x + w.y + w.z);\n" \
    "    float2 drift = float2(t * .21, -t * .17);\n" \
    "    return nereidNoise(q.zy * 2.3 + drift) * w.x + nereidNoise(q.xz * 2.3 + drift) * w.y + nereidNoise(q.xy * 2.3 + drift) * w.z;\n" \
    "}\n"
