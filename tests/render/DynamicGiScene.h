// Real-device CPU transport / atlas / model receiver acceptance.
#pragma once
namespace aiverse::test {
inline void drawDynamicGiScene(HdrScene& s,gfx::Device& d,int w,int h) {
    const int test=s.probeIndex;
    if(!s.dynamicGi.ready()) {
        GiMainCpuConfig cfg;
        cfg.origin={-2,-2,-2};cfg.nx=cfg.ny=cfg.nz=4;
        cfg.samples=64;cfg.traceDistance=8;
        cfg.cpuBudgetMs=100;cfg.maxProbesPerFrame=128;
        if(!s.dynamicGi.initialize(d,cfg)){s.failed=true;return;}
        GiChunkedGeometryBuilder builder;
        builder.replace(1,{{{-8,-8,2},{8,-8,2},{8,8,2},0},
                           {{-8,-8,2},{8,8,2},{-8,8,2},0}},{{1,1,1}});
        if(!s.dynamicGi.publishGeometry(
            std::make_shared<GiChunkedGeometrySnapshot>(builder.publish()),1)) {
            s.failed=true;return;
        }
    }
    if(s.gridProbe!=test && (test==0 || test==4 || test==5)) {
        std::vector<GiMovingLight> sources;
        if(test!=4)sources.push_back({1,{0,0,0},{1,.5f,.25f},5,4});
        if(!s.dynamicGi.publishLights(std::move(sources))){s.failed=true;return;}
        // Small synthetic fixture only: production hosts retain bounded per-frame updates.
        for(int attempt=0;s.dynamicGi.reference().scheduler().pending() && attempt<64;++attempt)
            if(!s.dynamicGi.updateFrame({0,0,0}).canPresent){s.failed=true;return;}
        if(s.dynamicGi.reference().scheduler().pending()){s.failed=true;return;}
    }
    s.gridProbe=test;
    auto& r=s.renderer;
    r.lighting.sunIntensity=0;r.lighting.skyColor={};r.lighting.groundColor={};
    r.lighting.tonemap=false;r.lighting.fogDensity=0;
    r.setShadowQuality(ShadowQuality::Off);r.setTraceEnabled(false);
    r.setModelReflectionsEnabled(false);r.lights.setQuality(LightQuality::Off);
    r.setMaterialQuality(test==2?MaterialQuality::Low:MaterialQuality::High);
    auto& material=s.probeBg->materials[0];
    material.shading=ShadingModel::Pbr;material.baseColor={1,1,1,1};
    material.roughness=1;material.metallic=0;material.vertexColors=false;
    material.occlusionTex=test==3?1:-1;
    PrismaSettings settings;settings.hdr=HdrMode::On;
    s.post.setSettings(settings);s.post.setQuality(PrismaQuality::High);
    if(s.ownsFrame)d.beginFrame(w,h);
    r.beginScene(s.camera,w,h);
    // Case 1 intentionally does not reattach: beginScene must clear the previous view.
    if(test!=1 && !r.setDynamicGiOverlay(s.dynamicGi)){s.failed=true;return;}
    ModelDraw draw;draw.model=s.probeBg.get();r.drawModel(draw);
    r.render(s.output,s.pipeline());r.resolvePrisma();
    if(s.frame==5) {
        const auto& cfg=s.dynamicGi.config();
        auto expected=sampleDynamicGi(s.dynamicGi.reference(),cfg.origin,cfg.spacing,
                                      cfg.nx,cfg.ny,cfg.nz,{0,0,0},{0,0,1});
        if(test==1 || test==3 || test==4)expected={};
        if((test==0 || test==2 || test==5) && expected.x<.01f)s.failed=true;
        auto encode=[](float v){return v<=.0031308f?12.92f*v:1.055f*std::pow(v,1/2.4f)-.055f;};
        expected={encode(expected.x),encode(expected.y),encode(expected.z)};
        auto receive=[&s,test,expected](int cw,int ch,std::vector<uint8_t> px) {
            if(cw<4 || ch<4 || px.size()!=size_t(cw*ch*4)){s.failed=true;return;}
            float actual[3]={};const float target[3]={expected.x,expected.y,expected.z};
            for(int y=ch/2-2;y<ch/2+2;++y)for(int x=cw/2-2;x<cw/2+2;++x)
                for(int c=0;c<3;++c)actual[c]+=px[size_t(y*cw+x)*4+c]/(16*255.0f);
            bool ok=true;for(int c=0;c<3;++c)ok &= std::abs(actual[c]-target[c])<4/255.0f;
            std::printf("Dynamic GI GPU %d: RGB %.5f %.5f %.5f expected %.5f %.5f %.5f %s\n",
                test,actual[0],actual[1],actual[2],target[0],target[1],target[2],ok?"PASS":"FAIL");
            s.failed |= !ok;++s.probeIndex;s.frame=0;
            if(s.probeIndex==6)s.captured=true;
        };
        if(s.readOutput)s.readOutput(receive);else d.captureNextFrame(receive);
    }
    d.endPass();if(s.ownsFrame)d.endFrame();++s.frame;
}
}
