#pragma once
namespace aiverse::render::localfog {
inline const char* helpers=R"(
// Periodic integer lattice hash: CPU/GPU share the same cosmetic noise contract.
float fogHash(vec3 p){
    uint x=uint(int(p.x))&255u,y=uint(int(p.y))&255u,z=uint(int(p.z))&255u;
    uint h=x*1597334677u+y*3812015801u+z*2798796415u;
    h=(h^(h>>16u))*2246822519u;h=h^(h>>13u);
    return float(h&65535u)/65535.0;
}
float fogNoise(vec3 p){
    vec3 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
    return mix(mix(mix(fogHash(i),fogHash(i+vec3(1,0,0)),f.x),mix(fogHash(i+vec3(0,1,0)),fogHash(i+vec3(1,1,0)),f.x),f.y),
        mix(mix(fogHash(i+vec3(0,0,1)),fogHash(i+vec3(1,0,1)),f.x),mix(fogHash(i+vec3(0,1,1)),fogHash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
float fogStructure(vec3 p,vec4 scale,vec4 shape,vec4 offset){
    float weight=exp(-max(p.y-shape.z,0.0)*shape.w);
    if(scale.w<=0.0)return weight;
    vec3 q=p/scale.xyz+offset.xyz;
    float n=.57*fogNoise(q)+.28*fogNoise(q*2.0+vec3(17,31,7))+.15*fogNoise(q*4.0+vec3(43,11,29));
    float clouds=smoothstep(1.0-shape.x,1.0-shape.x+shape.y,n);
    return weight*mix(1.0,clouds,scale.w);
}
vec3 fogLocal(vec3 p,vec4 c,vec4 x,vec4 y,vec4 z){p-=c.xyz;return vec3(dot(p,x.xyz),dot(p,y.xyz),dot(p,z.xyz));}
float fogWeight(vec3 p,vec4 c,vec4 x,vec4 y,vec4 z,float feather){
    vec3 q=fogLocal(p,c,x,y,z);float sdf=length(q)-x.w;
    if(c.w>.5){q=abs(q)-vec3(x.w,y.w,z.w);sdf=length(max(q,vec3(0)))+min(max(q.x,max(q.y,q.z)),0.0);}
    return feather>0.0?1.0-smoothstep(-feather,0.0,sdf):(sdf<=0.0?1.0:0.0);
}
vec2 fogHit(vec3 origin,vec3 dir,vec4 c,vec4 x,vec4 y,vec4 z,float cap){
    vec3 o=fogLocal(origin,c,x,y,z),d=vec3(dot(dir,x.xyz),dot(dir,y.xyz),dot(dir,z.xyz));
    if(c.w<.5){float b=dot(o,d),h=b*b-dot(o,o)+x.w*x.w;if(h<0.0)return vec2(1,-1);h=sqrt(h);return vec2(max(0.0,-b-h),min(cap,-b+h));}
    vec3 ext=vec3(x.w,y.w,z.w);float lo=0.0,hi=cap;
    for(int a=0;a<3;++a){if(abs(d[a])<.000001){if(abs(o[a])>ext[a])return vec2(1,-1);}
        else {float t0=(-ext[a]-o[a])/d[a],t1=(ext[a]-o[a])/d[a];lo=max(lo,min(t0,t1));hi=min(hi,max(t0,t1));}}
    return vec2(lo,hi);
}
)";
inline const char* body=R"(
vec2 uv=UV(s);vec4 background=COLOR(uv);float depth=DEPTH(uv);
vec4 nearH=invVP*vec4(s*2.0-1.0,1.0-px.w,1),farH=invVP*vec4(s*2.0-1.0,min(depth,.99999)*px.w+1.0-px.w,1);
vec3 origin=nearH.xyz/nearH.w,endpoint=farH.xyz/farH.w,dir=normalize(endpoint-origin);
float reach=depth<.99999?min(length(endpoint-origin),params.x):params.x;
// Sort and merge hit intervals so distant small volumes are not skipped in empty space.
vec2 hits[8];int count=0;float sampleOffset=.5;
for(int i=0;i<8;++i){if(material[i].w<=0.0)continue;
    if(noiseScale[i].w>0.0||noiseShape[i].w>0.0)sampleOffset=.2+.6*fogHash(vec3(floor(s/px.xy),0));
    vec2 hit=fogHit(origin,dir,centers[i],axesX[i],axesY[i],axesZ[i],reach);
    if(hit.y<=hit.x)continue;
    int at=count;for(int j=0;j<8;++j){if(j>=count)break;if(hit.x<hits[j].x){at=j;break;}}
    for(int j=7;j>0;--j){if(j>at&&j<=count)hits[j]=hits[j-1];}hits[at]=hit;++count;
}
vec2 merged[8];int ranges=0;
for(int i=0;i<8;++i){if(i>=count)break;
    if(ranges>0){if(hits[i].x<=merged[ranges-1].y){merged[ranges-1].y=max(merged[ranges-1].y,hits[i].y);continue;}}
    merged[ranges]=hits[i];++ranges;
}
float trans=1.0;vec3 fog=vec3(0);
float g=sun.w,mu=dot(dir,sun.xyz),phase=(1.0-g*g)/pow(max(1.0+g*g-2.0*g*mu,.001),1.5);
for(int range=0;range<8;++range){if(range>=ranges||trans<.001)break;
    float ds=(merged[range].y-merged[range].x)/params.w;
    for(int step=0;step<96;++step){if(float(step)>=params.w||trans<.001)break;
        vec3 p=origin+dir*(merged[range].x+(float(step)+sampleOffset)*ds);
        float density=0.0,reveal=0.0;vec3 tint=vec3(0);
        for(int i=0;i<8;++i){if(material[i].w<=0.0)continue;
            float den=material[i].w*fogWeight(p,centers[i],axesX[i],axesY[i],axesZ[i],edge[i].x);
            if(den>0.0)den*=fogStructure(p,noiseScale[i],noiseShape[i],noiseOffset[i]);
            density+=den;tint+=material[i].rgb*den;}
        if(density<=0.0)continue;
        tint/=density;
        for(int i=8;i<16;++i){if(material[i].w<=0.0)continue;
            reveal=max(reveal,material[i].w*fogWeight(p,centers[i],axesX[i],axesY[i],axesZ[i],edge[i].x));}
        density*=1.0-reveal;
        float visibility=1.0;
        if(params.z>.5){vec4 lp=lightVP*vec4(p,1);vec3 q=lp.xyz/lp.w;vec2 suv=UV(q.xy*.5+.5);float sz=q.z/px.w+(px.w-1.0)/px.w;
            if(min(suv.x,suv.y)>0.0&&max(suv.x,suv.y)<1.0&&sz>0.0&&sz<1.0)visibility=SHADOW(suv,sz);}
        float tr=exp(-density*ds);
        fog+=trans*(1.0-tr)*tint*(vec3(params.y)+sunColor.rgb*(visibility*phase));trans*=tr;
    }
}
RETURN(vec4(background.rgb*trans+fog,background.a));
)";
}
