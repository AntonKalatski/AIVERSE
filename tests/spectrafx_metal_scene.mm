// Stage 104: real Renderer3D + SpectraFxSceneHost on production Metal RHI.
// Compares four independently rendered offscreen scenes for two cameras,
// verifies scene-depth occlusion and live reset, without FPS/RTS game code.
// Production GfxMetal/Renderer3D/ParticleSystem are linked via aiverse_runtime.
// No AppKit window is created; PlatformMac's regular main calls aiverseMain.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "engine/gfx/Gfx.h"
#include "engine/render/Renderer3D.h"
#include "engine/fx/SpectraFxSceneHost.h"

namespace {
constexpr int kSide=128;
int failures=0;
void expect(bool ok,const char* message) {
    if(!ok){std::fprintf(stderr,"FAIL [spectrafx-metal-scene] %s\n",message);++failures;}
}
struct Target {
    aiverse::gfx::TextureHandle color, depth;
};
Target createTarget(aiverse::gfx::Device& device) {
    aiverse::gfx::TextureDesc d{};
    d.width=d.height=kSide;d.renderTarget=true;
    d.format=aiverse::gfx::PixelFormat::RGBA8;
    d.label="SpectraFX Renderer3D scene acceptance color";
    const auto color=device.createTexture(d);
    d.format=aiverse::gfx::PixelFormat::Depth;
    d.label="SpectraFX Renderer3D scene acceptance depth";
    return {color,device.createTexture(d)};
}
void destroyTarget(aiverse::gfx::Device& d,Target& t) {
    if(t.color.valid())d.destroyTexture(t.color);
    if(t.depth.valid())d.destroyTexture(t.depth);
    t={};
}
aiverse::fx::SpectraFxView viewOf(const aiverse::render::RTSCamera& camera,
                                 const aiverse::gfx::Device& device) {
    const auto vp=camera.proj(1.0f,device.info().clipDepthZeroToOne)*camera.view();
    return aiverse::fx::SpectraFxView::fromCamera(camera,vp,kSide,kSide);
}
aiverse::gfx::PassDesc pass(Target t) {
    aiverse::gfx::PassDesc p{};
    p.colorTarget=t.color;p.depthTarget=t.depth;
    p.clearColor[0]=p.clearColor[1]=p.clearColor[2]=0.01f;
    p.clearColor[3]=1.0f;
    return p;
}
struct Diff {
    int changed=0;
    double centroid=0;
    int maxDifference=0;
};
Diff differences(const std::vector<uint8_t>& base,
                 const std::vector<uint8_t>& particles) {
    Diff result;
    if(base.size()!=size_t(kSide*kSide*4) || particles.size()!=base.size())
        return result;
    double weightedX=0, weight=0;
    for(int y=8;y<kSide-8;++y)for(int x=8;x<kSide-8;++x) {
        const size_t i=(size_t(y)*kSide+size_t(x))*4u;
        const int delta=int(particles[i])-int(base[i]);
        if(delta>20) {
            ++result.changed;
            weightedX+=double(x)*double(delta);
            weight+=delta;
        }
        result.maxDifference=std::max(result.maxDifference,delta);
    }
    if(weight>0)result.centroid=weightedX/weight;
    return result;
}
int redDifference(const std::vector<uint8_t>& a,
                  const std::vector<uint8_t>& b,int x,int y) {
    if(a.size()!=size_t(kSide*kSide*4)||a.size()!=b.size())return -999;
    const size_t i=(size_t(y)*kSide+size_t(x))*4u;
    return int(b[i])-int(a[i]);
}
int maxDelta(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b) {
    if(a.size()!=b.size()||a.empty())return 999;
    int peak=0;
    for(size_t i=0;i<a.size();i+=4u)
        peak=std::max(peak,std::abs(int(a[i])-int(b[i])));
    return peak;
}
}

int aiverseMain(int,char**) {
    @autoreleasepool {
        id<MTLDevice> metal=MTLCreateSystemDefaultDevice();
        if(!metal) {
            std::puts("SKIP [spectrafx-metal-scene] no Apple Metal device");
            return 77;
        }
        CAMetalLayer* layer=[CAMetalLayer layer];
        layer.device=metal;
        layer.frame=CGRectMake(0,0,kSide,kSide);
        layer.drawableSize=CGSizeMake(kSide,kSide);
        auto device=aiverse::gfx::createMetalDevice((__bridge void*)layer);
        if(!device || !device->supportsCompute()) {
            std::puts("SKIP [spectrafx-metal-scene] RHI compute unavailable");
            return 77;
        }
        std::printf("[spectrafx-metal-scene] device=%s\n",
                    device->info().name.c_str());

        aiverse::render::Renderer3D renderer;
        if(!renderer.init(*device)) {
            std::fprintf(stderr,"FAIL [spectrafx-metal-scene] Renderer3D init: %s\n",
                         device->lastError().c_str());
            return 1;
        }
        renderer.setShadowQuality(aiverse::render::ShadowQuality::Off);
        renderer.setTraceEnabled(false);
        renderer.setModelReflectionsEnabled(false);
        renderer.lights.setQuality(aiverse::render::LightQuality::Off);
        renderer.lighting.fogDensity=0;

        // In-memory, project-owned authoring assets; no fixed gameplay,
        // Editor or filesystem requirement in the SpectraFX engine module.
        const std::string gpuAsset=R"JSON({
            "version":1,"preference":"gpu","particles":64,
            "allowCpuFallback":false,"cpuEffect":"",
            "quality":"high",
            "appearance":{"additive":true,"radius":0.55,
                          "color":[1,0.15,0.05]},
            "spawn":{"burst":1,"rate":0,"life":10,"speed":0},
            "environment":{"acceleration":[0,0,0],"damping":1}
        })JSON";
        const std::string routes=R"JSON({
            "version":1,"sceneQuality":"high",
            "routes":[{"eventType":42,"asset":"scene_test",
                       "enabled":true,"ownPlayerOnly":true}]
        })JSON";
        aiverse::fx::SpectraFxSceneHost fx;
        const auto reader=[&](const std::string& name,std::string& content){
            if(name=="fx/gpu/routes.json"){content=routes;return true;}
            if(name=="fx/gpu/scene_test.gpu.json"){content=gpuAsset;return true;}
            return false;
        };
        expect(fx.init(*device,reader) && fx.enabled(),
               "production SpectraFxSceneHost initialized");
        if(failures) {
            std::fprintf(stderr,"[spectrafx-metal-scene] host: %s\n",
                         fx.lastError().c_str());
            renderer.shutdown();
            return 1;
        }
        fx.setQuality(aiverse::fx::FxQuality::High);

        aiverse::render::MeshBuilder geometry;
        const auto opaque=geometry.box({0,0,0},{0.65f,0.65f,0.65f})
                                  .upload(*device);
        expect(opaque.vb.valid()&&opaque.ib.valid(),
               "opaque occluder mesh uploaded through Renderer3D");
        Target baselineA=createTarget(*device),effectA=createTarget(*device);
        Target baselineB=createTarget(*device),effectB=createTarget(*device);
        expect(baselineA.color.valid()&&baselineA.depth.valid()&&
               effectA.color.valid()&&effectA.depth.valid()&&
               baselineB.color.valid()&&baselineB.depth.valid()&&
               effectB.color.valid()&&effectB.depth.valid(),
               "four independent RGBA8/depth render targets");
        if(failures) {
            fx.shutdown();renderer.shutdown();
            return 1;
        }

        aiverse::render::RTSCamera cameraA;
        cameraA.target={0,0,0};
        cameraA.distance=5;
        cameraA.pitch=0;
        cameraA.yaw=0;
        cameraA.fovY=0.75f;
        cameraA.zNear=0.1f;
        cameraA.zFar=100;
        auto cameraB=cameraA;
        cameraB.target={0.9f,0,0};
        const auto va=viewOf(cameraA,*device);
        const auto vb=viewOf(cameraB,*device);
        expect(va.valid()&&vb.valid(),"two genre-neutral camera view packets");

        // Simple opaque blue box is deliberately placed between eye
        // (z=5) and the particle center (z=0) to test hardware depth.
        const auto renderView=[&](const aiverse::render::RTSCamera& camera,
                                  const aiverse::fx::SpectraFxView& view,
                                  Target target,bool includeFx) {
            if(includeFx)
                expect(fx.prepareRenderView(view),
                       "prepare each camera without repeating simulation");
            renderer.beginScene(camera,kSide,kSide);
            renderer.drawMesh(opaque,aiverse::render::mat4::translate({0,0,1}),
                              {0.04f,0.08f,0.38f,1},
                              aiverse::render::kDrawUnlit);
            renderer.render(pass(target));
            if(includeFx)fx.render(renderer.lighting,view);
            device->endPass();
        };
        device->beginFrame(kSide,kSide);
        aiverse::fx::SpectraFxCue cue{};
        cue.id=42;cue.key=7;cue.action=aiverse::fx::SpectraFxCue::Action::Play;
        cue.position={0,0,0};cue.direction={0,1,0};
        expect(fx.enqueueCue(cue,true,fx.cueEpoch()),
               "enqueue generic presentation cue");
        fx.prepareGpu(0.016f,va);
        fx.prepareCpu(0.016f,va);
        expect(fx.playback()&&
               fx.playback()->backendSnapshot().dedicatedGpu==1,
               "GPU backend selected, no CPU fallback");
        // Rendering four viewports does not call either update stage again.
        renderView(cameraA,va,baselineA,false);
        renderView(cameraA,va,effectA,true);
        renderView(cameraB,vb,baselineB,false);
        renderView(cameraB,vb,effectB,true);
        device->endFrame();

        std::vector<uint8_t> clearA,withA,clearB,withB;
        expect(device->readColorRGBA8(baselineA.color,clearA) &&
               device->readColorRGBA8(effectA.color,withA) &&
               device->readColorRGBA8(baselineB.color,clearB) &&
               device->readColorRGBA8(effectB.color,withB),
               "completed Renderer3D color targets readable through real RHI");
        const auto deltaA=differences(clearA,withA);
        const auto deltaB=differences(clearB,withB);
        expect(deltaA.changed>40&&deltaA.maxDifference>30,
               "billboard contributes distinct pixels over Renderer3D scene A");
        expect(deltaB.changed>40&&deltaB.maxDifference>30,
               "billboard contributes distinct pixels over Renderer3D scene B");
        expect(deltaB.centroid+6<deltaA.centroid,
               "independent camera B projects particle to a different screen x");
        expect(std::abs(redDifference(clearA,withA,kSide/2,kSide/2))<=5,
               "opaque Renderer3D geometry occludes the particle center");
        std::printf("[spectrafx-metal-scene] A pixels=%d center=%.2f "
                    "B pixels=%d center=%.2f depth_center_delta=%d\n",
                    deltaA.changed,deltaA.centroid,deltaB.changed,
                    deltaB.centroid,
                    redDifference(clearA,withA,kSide/2,kSide/2));

        // Scene reset must detach the visual source immediately, reject
        // stale epoch cues, and keep buffers for safe Metal retirement.
        const auto oldEpoch=fx.cueEpoch();
        device->beginFrame(kSide,kSide);
        expect(fx.resetScene(),"live reset outside encoders succeeds");
        expect(!fx.enqueueCue(cue,true,oldEpoch),
               "old scene callback cannot re-enqueue an effect");
        expect(fx.playback()&&fx.playback()->attached()==0,
               "reset detached all GPU emitters");
        expect(fx.playback()&&fx.playback()->retiringGpuSlots()>=64,
               "GPU storage retained during in-flight retirement");
        fx.prepareGpu(0.016f,va);
        fx.prepareCpu(0.016f,va);
        renderView(cameraA,va,effectA,true);
        device->endFrame();
        expect(device->readColorRGBA8(effectA.color,withA),
               "reset frame color probe");
        expect(maxDelta(clearA,withA)<=4,
               "reset scene returns to baseline without ghost particles");

        for(int i=0;i<4;++i){
            device->beginFrame(kSide,kSide);
            fx.prepareGpu(0.016f,va);
            device->endFrame();
        }
        expect(fx.playback()&&fx.playback()->retiringGpuSlots()==0,
               "GPU resources retired after completed three-frame window");
        expect(device->gpuHealthy(),"Renderer3D + SpectraFX GPU healthy");
        // Ensure every submitted GPU buffer completes before owner teardown.
        std::vector<uint8_t> finalRead;
        expect(device->readColorRGBA8(effectA.color,finalRead),
               "all queued Metal work finished before teardown");
        fx.shutdown();
        renderer.shutdown();
        device->destroyBuffer(opaque.vb);
        device->destroyBuffer(opaque.ib);
        destroyTarget(*device,baselineA);
        destroyTarget(*device,effectA);
        destroyTarget(*device,baselineB);
        destroyTarget(*device,effectB);
        std::printf("[spectrafx-metal-scene] failures=%d\n",failures);
        if(failures)return 1;
        std::puts("PASS [spectrafx-metal-scene] production Renderer3D "
                  "depth, two camera RTs, SceneHost and reset retirement");
        return 0;
    }
}
