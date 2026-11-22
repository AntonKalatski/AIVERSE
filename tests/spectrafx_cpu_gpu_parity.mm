// SpectraFX CPU/GPU linkage acceptance on the production Metal RHI.
//
// One authored emitter asset, two simulators:
//   GPU  = Metal compute kernel (kSpectraFxComputeMetal), positions stay on
//          the GPU and the billboard renderer samples them directly;
//   CPU  = the CPU twin (spectraFxEvolveParticle) of the same kernel, packed
//          into an RGBA32F texture and drawn by the SAME renderer.
// Checks:
//   1. kernel parity: identical seeds, 180 frames with gravity, damping and
//      ground bounce; GPU state read back (test only) vs CPU twin state;
//   2. emitter parity: the same fx/gpu asset JSON played by
//      SpectraFxHybridEmitter on the GPU and forced to the CPU twin; per-frame
//      live counts must match exactly, rendered images must match;
//   automatic selection: "auto" picks the GPU on Metal ("cpu" or a
//      compute-less device picks the CPU twin, see spectrafx_contract_test);
//   3. perf: particles per millisecond, CPU twin vs GPU (whole-frame GPU
//      time of a frame that only runs the evolution dispatch). Printed, not
//      asserted: other agents share the GPU.
// No FPS/RTS/editor code. SKIP (77) without a usable Metal device.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "engine/gfx/Gfx.h"
#include "engine/fx/ParticleSystem.h"
#include "engine/fx/SpectraFxGpuEvolution.h"
#include "engine/fx/SpectraFxGpuBillboardRenderer.h"
#include "engine/fx/SpectraFxGpuEmitterAsset.h"
#include "engine/fx/SpectraFxHybridEmitter.h"

namespace {
constexpr int kSide=128;
int failures=0;
void expect(bool ok,const char* what) {
    if(!ok){std::fprintf(stderr,"FAIL [spectrafx-cpu-gpu] %s\n",what);++failures;}
    else std::printf("PASS [spectrafx-cpu-gpu] %s\n",what);
}
using aiverse::render::float3;
using aiverse::render::float4;
namespace fx=aiverse::fx;
namespace gfx=aiverse::gfx;

uint32_t hash(uint32_t x){x^=x>>16;x*=0x7feb352dU;x^=x>>15;x*=0x846ca68bU;x^=x>>16;return x;}
float unit(uint32_t i,uint32_t salt){return float(hash(i*977u+salt)&0xffffffu)/float(0xffffff);}

std::vector<fx::SpectraFxGpuSeed> makeSeeds(uint32_t n) {
    std::vector<fx::SpectraFxGpuSeed> seeds(n);
    for(uint32_t i=0;i<n;++i) {
        auto& s=seeds[i];
        s.position={unit(i,1)*4-2,unit(i,2)*2+0.5f,unit(i,3)*4-2};
        s.velocity={unit(i,4)*6-3,unit(i,5)*8,unit(i,6)*6-3};
        s.lifetime=0.5f+unit(i,7)*4.0f;
        s.age=0;
        if(i%17==0) s.lifetime=0; // dormant slots stay dormant on both paths
    }
    return seeds;
}
fx::SpectraFxGpuEnvironment environment() {
    fx::SpectraFxGpuEnvironment env;
    env.acceleration={0.3f,-9.81f,0};
    env.damping=0.985f;
    env.collideGround=true;
    env.groundHeight=0;
    env.restitution=0.45f;
    env.tangentRetention=0.8f;
    return env;
}
struct Target { gfx::TextureHandle color,depth; };
Target target(gfx::Device& d) {
    gfx::TextureDesc t{};t.width=t.height=kSide;t.renderTarget=true;
    t.format=gfx::PixelFormat::RGBA8;t.label="SpectraFX parity color";
    const auto c=d.createTexture(t);
    t.format=gfx::PixelFormat::Depth;t.label="SpectraFX parity depth";
    return {c,d.createTexture(t)};
}
gfx::PassDesc pass(Target t) {
    gfx::PassDesc p{};p.colorTarget=t.color;p.depthTarget=t.depth;
    p.clearColor[3]=1;return p;
}
double median(std::vector<double> v) {
    if(v.empty()) return 0;
    std::sort(v.begin(),v.end());
    return v[v.size()/2];
}
bool capture(const char* suffix,const std::vector<uint8_t>& rgba) {
    const char* prefix=std::getenv("AIVERSE_SPECTRAFX_PARITY_CAPTURE");
    if(!prefix||!*prefix) return true;
    const std::string path=std::string(prefix)+"-"+suffix+".ppm";
    std::FILE* f=std::fopen(path.c_str(),"wb");
    if(!f) return false;
    std::fprintf(f,"P6\n%d %d\n255\n",kSide,kSide);
    for(size_t i=0;i+3<rgba.size();i+=4) std::fwrite(rgba.data()+i,1,3,f);
    std::fclose(f);
    std::printf("[spectrafx-cpu-gpu] capture=%s\n",path.c_str());
    return true;
}
} // namespace

int aiverseMain(int,char**) {
    @autoreleasepool {
        id<MTLDevice> metal=MTLCreateSystemDefaultDevice();
        if(!metal){std::puts("SKIP [spectrafx-cpu-gpu] no Metal device");return 77;}
        CAMetalLayer* layer=[CAMetalLayer layer];
        layer.device=metal;layer.frame=CGRectMake(0,0,kSide,kSide);
        layer.drawableSize=CGSizeMake(kSide,kSide);
        auto device=gfx::createMetalDevice((__bridge void*)layer);
        if(!device||!device->supportsCompute()){
            std::puts("SKIP [spectrafx-cpu-gpu] RHI compute unavailable");return 77;
        }
        std::printf("[spectrafx-cpu-gpu] device=%s\n",device->info().name.c_str());

        // ---------------------------------------------------------------
        // 1. Kernel parity: 4096 slots (the RHI's 64 KiB readback limit).
        // ---------------------------------------------------------------
        {
            constexpr uint32_t kCount=4096;
            constexpr int kFrames=180;
            const auto seeds=makeSeeds(kCount);
            fx::SpectraFxGpuEvolution gpu,cpu;
            expect(gpu.init(*device,seeds,false,{},false) && !gpu.cpuTwin(),
                   "GPU evolution initialized on Metal");
            expect(cpu.init(*device,seeds,false,{},true) && cpu.cpuTwin(),
                   "CPU twin initialized from the same seeds");
            const auto env=environment();
            bool stepped=true;
            for(int f=0;f<kFrames;++f) {
                device->beginFrame(kSide,kSide);
                stepped=gpu.step(1.0f/60,env)&&cpu.step(1.0f/60,env)&&stepped;
                device->endFrame();
            }
            expect(stepped,"180 frames stepped on both simulators");
            std::vector<uint8_t> pos,vel;
            bool gotP=false,gotV=false;
            device->beginFrame(kSide,kSide);
            const bool asked=device->readBufferAsync(gpu.positionBuffer(),kCount*16,
                    [&](std::vector<uint8_t> b){pos=std::move(b);gotP=true;}) &&
                device->readBufferAsync(gpu.velocityBuffer(),kCount*16,
                    [&](std::vector<uint8_t> b){vel=std::move(b);gotV=true;});
            device->endFrame();
            device->waitIdle();
            for(int i=0;i<3&&!(gotP&&gotV);++i){device->beginFrame(kSide,kSide);device->endFrame();device->waitIdle();}
            expect(asked&&gotP&&gotV&&pos.size()==kCount*16&&vel.size()==kCount*16,
                   "test-only GPU state readback delivered");
            if(gotP&&gotV&&pos.size()==kCount*16) {
                const auto& cp=cpu.twinPositionAge();
                const auto& cv=cpu.twinVelocityLife();
                std::vector<double> errors;errors.reserve(kCount);
                uint32_t aliveGpu=0,aliveCpu=0;
                double maxAge=0;
                for(uint32_t i=0;i<kCount;++i) {
                    float4 g{},gv{};
                    std::memcpy(&g,pos.data()+i*16,16);
                    std::memcpy(&gv,vel.data()+i*16,16);
                    const double dx=g.x-cp[i].x,dy=g.y-cp[i].y,dz=g.z-cp[i].z;
                    errors.push_back(std::sqrt(dx*dx+dy*dy+dz*dz));
                    maxAge=std::max(maxAge,double(std::fabs(g.w-cp[i].w)));
                    if(gv.w>0&&g.w<gv.w) ++aliveGpu;
                    if(cv[i].w>0&&cp[i].w<cv[i].w) ++aliveCpu;
                }
                std::sort(errors.begin(),errors.end());
                const double p50=errors[kCount/2],p99=errors[kCount*99/100],
                             worst=errors.back();
                std::printf("[spectrafx-cpu-gpu] kernel_parity particles=%u frames=%d "
                            "alive_gpu=%u alive_cpu=%u pos_err_m p50=%.2e p99=%.2e max=%.2e "
                            "age_err_max=%.2e\n",kCount,kFrames,aliveGpu,aliveCpu,
                            p50,p99,worst,maxAge);
                expect(aliveGpu==aliveCpu,"alive particle sets are identical");
                expect(maxAge<1e-4,"particle ages agree (|d|<1e-4 s)");
                expect(p99<1e-3,"99% of positions agree within 1 mm after 3 s");
                expect(worst<5e-2,"every position agrees within 5 cm (bounce rounding)");
            }
            gpu.shutdown();cpu.shutdown();
            device->waitIdle();
        }

        // ---------------------------------------------------------------
        // 2. Emitter parity: one asset, GPU vs forced CPU twin, same images.
        // ---------------------------------------------------------------
        {
            const std::string asset=R"JSON({
                "version":1,"preference":"auto","particles":2048,
                "appearance":{"additive":true,"radius":0.35,"color":[1,0.45,0.1]},
                "spawn":{"rate":300,"burst":40,"life":1.6,"speed":6,"spread":0.6,"seed":7},
                "environment":{"acceleration":[0,-9.81,0],"damping":0.99,
                    "groundCollision":{"enabled":true,"height":-4,"restitution":0.4,
                                       "tangentRetention":0.8}}
            })JSON";
            fx::SpectraFxHybridEmitter::Config config;
            std::string error;
            expect(fx::SpectraFxGpuEmitterAsset::readText(asset,config,&error),
                   "asset without cpuEffect is valid (CPU twin fallback)");
            if(!error.empty()) std::fprintf(stderr,"  asset: %s\n",error.c_str());
            fx::ParticleSystem ps;
            ps.init(device.get());
            fx::SpectraFxHybridEmitter onGpu,onCpu;
            auto cpuConfig=config;
            cpuConfig.backend.preference=fx::SpectraFxExecutionPreference::Cpu;
            expect(onGpu.begin(*device,ps,config,{0,-3,0},{0,1,0}) && onGpu.gpuActive(),
                   "auto preference selects the Metal GPU");
            expect(onCpu.begin(*device,ps,cpuConfig,{0,-3,0},{0,1,0}) &&
                   onCpu.cpuTwinActive() && !onCpu.gpuActive(),
                   "cpu preference runs the same asset on the CPU twin");
            std::printf("[spectrafx-cpu-gpu] status gpu='%s' cpu='%s'\n",
                        onGpu.status().c_str(),onCpu.status().c_str());
            Target ta=target(*device),tb=target(*device);
            const auto vp=aiverse::render::mat4::ortho(-8,8,-8,8,-10,10,
                                                     device->info().clipDepthZeroToOne);
            bool countsMatch=true;
            uint32_t peak=0;
            std::vector<uint8_t> imgA,imgB;
            int worstDelta=0;double worstMean=0;uint64_t litA=0,litB=0;
            for(int f=0;f<90;++f) {
                device->beginFrame(kSide,kSide);
                const bool a=onGpu.updateGpu(1.0f/60),b=onCpu.updateGpu(1.0f/60);
                if(!a||!b) countsMatch=false;
                if(onGpu.gpuActiveParticles()!=onCpu.gpuActiveParticles()) countsMatch=false;
                peak=std::max(peak,onGpu.gpuActiveParticles());
                device->beginPass(pass(ta));
                device->setViewport({0,0,kSide,kSide});
                onGpu.renderGpu(vp,{1,0,0},{0,1,0});
                device->endPass();
                device->beginPass(pass(tb));
                device->setViewport({0,0,kSide,kSide});
                onCpu.renderGpu(vp,{1,0,0},{0,1,0});
                device->endPass();
                device->endFrame();
                if(f%30==29) {
                    device->waitIdle();
                    if(device->readColorRGBA8(ta.color,imgA)&&device->readColorRGBA8(tb.color,imgB)&&
                       imgA.size()==imgB.size()) {
                        double sum=0;int peakDelta=0;uint64_t la=0,lb=0;
                        for(size_t i=0;i<imgA.size();i+=4)
                            for(int c=0;c<3;++c){
                                const int d=std::abs(int(imgA[i+c])-int(imgB[i+c]));
                                sum+=d;peakDelta=std::max(peakDelta,d);
                            }
                        for(size_t i=0;i<imgA.size();i+=4){la+=imgA[i]>60;lb+=imgB[i]>60;} // clear colour is ~26
                        const double mean=sum/double(imgA.size()/4*3);
                        worstDelta=std::max(worstDelta,peakDelta);
                        worstMean=std::max(worstMean,mean);
                        litA=la;litB=lb;
                        std::printf("[spectrafx-cpu-gpu] frame=%d live=%u lit_gpu=%llu lit_cpu=%llu "
                                    "max_delta=%d mean_delta=%.4f\n",f+1,onGpu.gpuActiveParticles(),
                                    (unsigned long long)la,(unsigned long long)lb,peakDelta,mean);
                        if(f==59){capture("gpu",imgA);capture("cpu",imgB);}
                    }
                }
            }
            expect(countsMatch && peak>100,
                   "per-frame live counts identical (shared deterministic spawn scheduler)");
            expect(litA>200 && litB>200 && litA<uint64_t(kSide*kSide)/2,"both paths draw visible particles");
            expect(worstMean<0.5 && worstDelta<=48,
                   "GPU and CPU twin images match (mean<0.5/255, max<=48/255)");
            device->waitIdle();
            onGpu.stop();onCpu.stop();
            for(int i=0;i<4;++i){device->beginFrame(kSide,kSide);device->endFrame();}
            device->waitIdle();
            ps.shutdown();
            device->destroyTexture(ta.color);device->destroyTexture(ta.depth);
            device->destroyTexture(tb.color);device->destroyTexture(tb.depth);
        }

        // ---------------------------------------------------------------
        // 3. Perf: particles per millisecond, CPU twin vs GPU.
        // ---------------------------------------------------------------
        {
            const int reps=[]{const char* r=std::getenv("AIVERSE_SPECTRAFX_PARITY_REPS");
                              return r?std::max(1,std::atoi(r)):3;}();
            for(uint32_t n:{4096u,16384u,65536u}) {
                auto seeds=makeSeeds(n);
                for(auto& s:seeds){s.lifetime=100;s.age=0;} // all live for 2 s
                for(int rep=0;rep<reps;++rep) {
                    fx::SpectraFxGpuEvolution gpu,cpu;
                    if(!gpu.init(*device,seeds,false,{},false)||!cpu.init(*device,seeds,false,{},true)) {
                        expect(false,"perf allocations");break;
                    }
                    const auto env=environment();
                    std::vector<double> cpuMs,gpuFrameMs,encodeMs;
                    const uint64_t tagBase=0x5350435000000000ull+uint64_t(n)*16+uint64_t(rep)*4096;
                    // GPU: frames with only the evolution dispatch, GPU-timed.
                    for(int f=0;f<120;++f) {
                        device->beginFrame(kSide,kSide);
                        for(const auto& t:device->takeGpuTimings())
                            if(t.valid&&t.tag>=tagBase&&t.tag<tagBase+120&&t.tag>=tagBase+10)
                                gpuFrameMs.push_back(t.milliseconds);
                        device->timeGpuFrame(tagBase+uint64_t(f));
                        const auto t0=std::chrono::steady_clock::now();
                        gpu.step(1.0f/120,env);
                        encodeMs.push_back(std::chrono::duration<double,std::milli>(
                            std::chrono::steady_clock::now()-t0).count());
                        device->endFrame();
                    }
                    device->waitIdle();
                    device->beginFrame(kSide,kSide);
                    for(const auto& t:device->takeGpuTimings())
                        if(t.valid&&t.tag>=tagBase+10&&t.tag<tagBase+120) gpuFrameMs.push_back(t.milliseconds);
                    device->endFrame();
                    // CPU twin: kernel + packed texture upload, wall clock.
                    for(int f=0;f<120;++f) {
                        device->beginFrame(kSide,kSide);
                        const auto t0=std::chrono::steady_clock::now();
                        cpu.step(1.0f/120,env);
                        const double ms=std::chrono::duration<double,std::milli>(
                            std::chrono::steady_clock::now()-t0).count();
                        if(f>=10) cpuMs.push_back(ms);
                        device->endFrame();
                    }
                    device->waitIdle();
                    const double g=median(gpuFrameMs),c=median(cpuMs);
                    std::printf("[spectrafx-cpu-gpu] perf particles=%u rep=%d "
                                "cpu_twin_ms=%.4f cpu_particles_per_ms=%.0f "
                                "gpu_frame_ms=%.4f gpu_particles_per_ms=%.0f "
                                "gpu_encode_ms=%.4f gpu_samples=%zu\n",
                                n,rep,c,c>0?n/c:0.0,g,g>0?n/g:0.0,median(encodeMs),
                                gpuFrameMs.size());
                    gpu.shutdown();cpu.shutdown();
                    device->waitIdle();
                }
            }
        }
        std::printf("[spectrafx-cpu-gpu] %s failures=%d\n",failures?"FAILED":"PASSED",failures);
        return failures?1:0;
    }
}
