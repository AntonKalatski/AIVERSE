// Actual Metal receiver -> direct compute -> HDR composite acceptance.
#pragma once
namespace aiverse::test {
inline void drawLumaScene(HdrScene& s,gfx::Device& d,int w,int h) {
    if(s.ownsFrame)d.beginFrame(w,h);
    const int test=s.probeIndex;
    if(!s.luma) {
        s.luma=std::make_unique<LumaWeavePostProcess>(s.post,s.renderer.lights);
        if(!s.luma->initialize(d) || !s.luma->publishGeometry({},1)) {
            std::fprintf(stderr,"Luma initialize FAIL: %s\n",d.lastError().c_str());s.failed=true;return;
        }
    }
    s.luma->pollGpu();
    if(s.gridProbe!=test) {
        if(s.luma->screen().busy()) {if(s.ownsFrame)d.endFrame();return;}
        std::vector<LumaLight> lights;
        if(test!=2) for(uint64_t i=1;i<=128;++i) {
            LumaLight light;light.key=i;light.position={0,0,1};light.radius=2;
            light.intensity=4.0f/128;light.color={1,.5f,.25f};lights.push_back(light);
        }
        s.lumaSources.publish(std::move(lights));
        LumaClusterConfig config;config.origin={-4,-4,-4};config.nx=config.ny=config.nz=1;
        s.lumaClusters.rebuild(s.lumaSources,config);
        if(!s.luma->publishSources(s.lumaSources,s.lumaClusters)) {s.failed=true;return;}
        GiGpuBvh geometry;
        if(test==6) {
            GiChunkedGeometryBuilder builder;
            builder.replace(1,{{{-8,-8,.5f},{8,-8,.5f},{8,8,.5f},0},
                               {{-8,-8,.5f},{8,8,.5f},{-8,8,.5f},0}},{{1,1,1}});
            geometry=GiGpuBvh::build(builder.publish());
        }
        if(!s.luma->publishGeometry(geometry,uint64_t(test)+2)){s.failed=true;return;}
        s.gridProbe=test;s.frame=0;
    }
    auto& r=s.renderer;
    r.lighting.sunIntensity=0;r.lighting.skyColor={};r.lighting.groundColor={};
    r.lighting.tonemap=false;r.setTraceEnabled(false);r.setModelReflectionsEnabled(false);
    r.lights.setQuality(test==4?LightQuality::High:LightQuality::Off);
    auto& m=s.probeBg->materials[0];m.shading=ShadingModel::Pbr;
    m.baseColor={1,1,1,1};m.metallic=(test==3||test==8)?1:0;m.roughness=1;
    // Case 9: PRISMA motion blur produces object velocity for a moving plane;
    // Luma's temporal history must take it and keep the same lighting.
    // Settings reset motion history, so case 9 sets them once.
    if(test!=9) {PrismaSettings settings;settings.hdr=HdrMode::On;s.post.setSettings(settings);}
    else if(s.frame==0)s.post.setSettings(parsePrisma(Json::parse(
        R"({"hdr":"on","effects":[{"type":"motionBlur","intensity":0.0001,"maxPixels":0.01,"samples":2}]})"),Json()));
    s.post.setQuality(PrismaQuality::High);
    s.luma->setEnabled(test!=1);
    s.luma->setQuality((test==5||test==8)?LumaWeaveQuality::High:LumaWeaveQuality::Balanced);
    r.beginScene(s.camera,w,h);
    if(test==4)r.lights.add({0,0,-1},2,{1,1,1},1); // behind surface: no direct contribution
    ModelDraw draw;draw.model=s.probeBg.get();
    if(test==9){draw.persistentId=7;draw.world=mat4::translate({.02f*float(s.frame),0,0});}
    r.drawModel(draw);
    r.render(s.output,s.luma.get());r.resolvePrisma();
    if(s.frame==12) {
        const auto stats=s.luma->stats();
        bool stateOk=(test==1 || test==4)?!stats.composited:stats.composited;
        if(test==4)stateOk &= stats.legacyConflict;
        if(test==5||test==8)stateOk &= stats.materialShaded;
        if(test==9)stateOk &= stats.objectVelocity;
        // At N=L=V, radius=2, distance=1: attenuation=.28125.
        // Balanced Lambert reflectance = incident/pi. GGX roughness=1:
        // diffuse=(1-F0)/pi, specular=F0/(4*pi), F0=.04.
        const float energy=((test==0||test==7||test==9)?1.0f:test==5?.97f:test==8?.25f:0.0f)*4*.28125f/3.14159265358979323846f;
        auto encode=[](float v){return v<=.0031308f?12.92f*v:1.055f*std::pow(v,1/2.4f)-.055f;};
        const float3 expected={encode(energy),encode(energy*.5f),encode(energy*.25f)};
        const int flags=int(stats.composited)|int(stats.objectVelocity)<<1|
                        int(stats.submission.encoded)<<2|int(stats.reusedPrevious)<<3|int(stats.gpuBusy)<<4|
                        int(r.objectMotionStats().submitted)<<5|int(r.objectMotionStats().velocitySeedOnly)<<6|
                        int(r.objectMotionStats().rigidDraws>0)<<7|int(r.objectMotionStats().rejected>0)<<8|
                        int(s.post.wantsObjectMotionVectors())<<9|int(s.post.objectMotionSceneDepth().valid())<<10;
        const std::string lastError=d.lastError();
        auto receive=[&s,test,expected,stateOk,flags,lastError](int cw,int ch,std::vector<uint8_t> px) {
            if(cw<4||ch<4||px.size()!=size_t(cw*ch*4)){s.failed=true;return;}
            // AIVERSE_HDR_CAPTURE set: keep each case frame as <path>.luma<case>.ppm.
            if(std::getenv("AIVERSE_HDR_CAPTURE")) {
                std::ofstream f(s.path+".luma"+std::to_string(test)+".ppm",std::ios::binary);
                f<<"P6\n"<<cw<<" "<<ch<<"\n255\n";
                for(size_t i=0;i<px.size();i+=4)f.write(reinterpret_cast<const char*>(px.data()+i),3);
            }
            float actual[3]={};const float target[]={expected.x,expected.y,expected.z};
            for(int y=ch/2-2;y<ch/2+2;++y)for(int x=cw/2-2;x<cw/2+2;++x)
                for(int c=0;c<3;++c)actual[c]+=px[size_t(y*cw+x)*4+c]/(16*255.0f);
            bool ok=stateOk;for(int c=0;c<3;++c)ok &= std::abs(actual[c]-target[c])<5/255.0f;
            std::printf("Luma GPU %d: RGB %.5f %.5f %.5f expected %.5f %.5f %.5f state=%d flags=%d %s\n",
                test,actual[0],actual[1],actual[2],target[0],target[1],target[2],stateOk,flags,ok?"PASS":"FAIL");
            if(!ok)std::printf("Luma GPU %d device error: %s\n",test,lastError.c_str());
            s.failed |= !ok;++s.probeIndex;s.frame=0;if(s.probeIndex==10)s.captured=true;
        };
        if(s.readOutput)s.readOutput(receive);else d.captureNextFrame(receive);
    }
    d.endPass();if(s.ownsFrame)d.endFrame();++s.frame;
}
}
