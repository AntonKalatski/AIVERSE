// Baked lighting (lightmaps + light probes) GPU acceptance on the real backend.
// AIVERSE_BAKED_LIGHTING_TEST=1. A flat static slab is drawn with the sun off:
//   phase 0  baked quality Off             (reference, sky ambient only)
//   phase 1  at its baked placement        LIGHTMAP variant: a 2x1 page, red | blue
//   phase 2  moved off the baked placement PROBES variant: green probe grid
// Phase 1 must show one half red-dominant and the other blue-dominant (the page
// is sampled through uv2 across the slab); phase 2 must be green-dominant and
// greener than phase 0. Each phase frame is written as <AIVERSE_HDR_CAPTURE>.baked<phase>.ppm.
#pragma once
#include "engine/render/BakedLighting.h"
#include "engine/render/LightmapUv.h"
#include <algorithm>
#include <cstring>
namespace aiverse::test {
struct BakedLightingState {
    int phase=0;
    std::unique_ptr<Model> slab;
    BakedLighting baked;
    int node=-1;
    float3 half[3][2];   // mean linear-ish rgb of the left / right image half per phase
};
inline BakedLightingState& bakedLightingState(){static BakedLightingState s;return s;}
inline mat4 bakedSlabWorld(int phase){return phase==2?mat4::translate({0.5f,0,0}):mat4{};}

inline bool initBakedLighting(HdrScene& s,gfx::Device& d) {
    auto& st=bakedLightingState();
    MeshBuilder mb;
    mb.box({0,0,0},{4,0.2f,4});
    Material mt;
    mt.baseColor={1,1,1,1};
    st.slab=modelFromParts("baked_slab",{{"slab",&mb,mt,mat4{}}});
    st.slab->path="models/baked_slab.glb";
    for(auto& v:st.slab->vertices)setLightmapUv(v,{std::clamp((v.pos.x+2.f)/4.f,0.f,1.f),0.5f});
    for(auto& mesh:st.slab->meshes)for(auto& p:mesh.primitives)p.lightmapUv=true;
    if(!uploadModel(d,*st.slab))return false;
    for(size_t i=0;i<st.slab->nodes.size();++i)if(st.slab->nodes[i].mesh>=0){st.node=int(i);break;}
    if(st.node<0)return false;
    bake::LightmapData data;
    data.mode=bake::BakedMode::Indirect;
    data.pages.push_back({});
    auto& page=data.pages[0];
    page.width=2;page.height=1;page.rgbm.resize(8);
    bake::encodeRgbm({1.2f,0.02f,0.02f},data.rgbmRange,page.rgbm.data());
    bake::encodeRgbm({0.02f,0.02f,1.2f},data.rgbmRange,page.rgbm.data()+4);
    bake::LightmapRegion region;   // whole page: uv2 * (1,1) + (0,0)
    data.regions[bake::lightmapRegionKey(st.slab->path,st.node,
        bakedSlabWorld(1)*st.slab->bindWorld[size_t(st.node)])]=region;
    data.probes.origin={-4,-2,-4};data.probes.cell={8,4,8};
    data.probes.nx=data.probes.ny=data.probes.nz=2;
    data.probes.coeffs.assign(8*12,0.f);
    for(int p=0;p<8;++p)data.probes.coeffs[size_t(p)*12+1]=1.2f;   // ambient green
    if(!st.baked.upload(d,std::move(data)))return false;
    s.renderer.setBakedLighting(&st.baked);
    s.renderer.lighting.sunIntensity=0;
    s.renderer.lighting.fogDensity=0;
    s.renderer.lighting.skyColor={.25f,.25f,.25f};
    s.renderer.lighting.groundColor={.25f,.25f,.25f};
    s.renderer.setShadowQuality(ShadowQuality::Off);
    return true;
}

inline void drawBakedLighting(HdrScene& s,gfx::Device& d,int w,int h) {
    auto& st=bakedLightingState();
    constexpr int kFrames=6;
    if(s.frame==0 && st.phase==0 && !st.slab && !initBakedLighting(s,d)) {
        std::fprintf(stderr,"Baked lighting: setup FAIL (%s)\n",d.lastError().c_str());
        s.failed=true;return;
    }
    if(s.frame==0) {
        s.renderer.setBakedQuality(st.phase==0?BakedQuality::Off:BakedQuality::High);
        s.post.configure(Json::parse(R"({"hdr":"on","effects":[]})"),Json());
        s.camera.target={st.phase==2?0.5f:0.f,0,0};s.camera.distance=5.5f;
        s.camera.pitch=1.45f;s.camera.yaw=0;
    }
    if(s.ownsFrame)d.beginFrame(w,h);
    auto& r=s.renderer;
    r.beginScene(s.camera,w,h);
    ModelDraw md;md.model=st.slab.get();md.world=bakedSlabWorld(st.phase);md.flags=kDrawStatic;
    r.drawModel(md);
    gfx::PassDesc pass=s.output;pass.clearColor[0]=pass.clearColor[1]=pass.clearColor[2]=0;
    r.render(pass,s.pipeline());
    r.resolvePrisma();
    if(s.frame==kFrames) {
        auto receive=[&s](int cw,int ch,std::vector<uint8_t> px) {
            auto& st=bakedLightingState();
            if(cw<8||ch<8||px.size()!=size_t(cw*ch*4)){s.failed=true;return;}
            for(int side=0;side<2;++side) {
                double sum[3]={};size_t n=0;
                const int x0=side?cw*5/8:cw/8,x1=side?cw*7/8:cw*3/8;
                for(int y=ch*3/8;y<ch*5/8;++y)for(int x=x0;x<x1;++x){
                    const uint8_t* p=px.data()+(size_t(y)*size_t(cw)+size_t(x))*4;
                    for(int c=0;c<3;++c)sum[c]+=p[c];
                    ++n;
                }
                st.half[st.phase][side]={float(sum[0]/double(n)),float(sum[1]/double(n)),float(sum[2]/double(n))};
            }
            {
                std::ofstream f(s.path+".baked"+std::to_string(st.phase)+".ppm",std::ios::binary);
                f<<"P6\n"<<cw<<" "<<ch<<"\n255\n";
                for(size_t i=0;i<px.size();i+=4)f.write(reinterpret_cast<const char*>(px.data()+i),3);
            }
            const auto& a=st.half[st.phase][0];const auto& b=st.half[st.phase][1];
            std::printf("Baked lighting phase %d: left %.0f %.0f %.0f right %.0f %.0f %.0f\n",
                        st.phase,a.x,a.y,a.z,b.x,b.y,b.z);
            if(++st.phase==3) {
                const auto& off=st.half[0];const auto& lm=st.half[1];const auto& pr=st.half[2];
                const auto redDom=[](float3 c){return c.x>c.z*1.5f+4.f;};
                const auto blueDom=[](float3 c){return c.z>c.x*1.5f+4.f;};
                const bool lightmap=(redDom(lm[0])&&blueDom(lm[1]))||(blueDom(lm[0])&&redDom(lm[1]));
                const auto green=[](float3 c){return c.y>c.x*1.5f+4.f && c.y>c.z*1.5f+4.f;};
                const bool probes=green(pr[0])&&green(pr[1])&&pr[0].y>off[0].y+8.f&&pr[1].y>off[1].y+8.f;
                const bool ok=lightmap&&probes;
                std::printf("Baked lighting: lightmap %s, probes %s: %s\n",lightmap?"ok":"wrong",probes?"ok":"wrong",ok?"PASS":"FAIL");
                s.failed|=!ok;s.captured=true;
            }
            s.frame=-1;
        };
        if(s.readOutput)s.readOutput(receive);else d.captureNextFrame(receive);
    }
    d.endPass();if(s.ownsFrame)d.endFrame();++s.frame;
}
inline void shutdownBakedLighting(HdrScene& s,gfx::Device& d) {
    auto& st=bakedLightingState();
    if(!st.slab)return;
    s.renderer.setBakedLighting(nullptr);
    if(!d.waitForResourceIdle()){s.failed=true;return;}
    st.baked.release();
    releaseModel(d,*st.slab);
    st.slab.reset();
}
}
