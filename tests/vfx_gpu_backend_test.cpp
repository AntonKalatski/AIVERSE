// SpectraFX CPU / GPU backend selection and CPU fallback headless regression.
// GPU kernels remain gated by Metal/RHI and require a real-device test separately.
#include "engine/fx/SpectraFX.h"
#include <iostream>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <string>
#include <vector>
using namespace aiverse;

static void check(bool ok,const char* why) {
    if(!ok) throw std::runtime_error(why);
}
// RHI recording mock exercises command order without impersonating a real GPU.
// It does NOT compile or execute Metal shaders; a Metal device test remains open.
class RecordingMetalDevice final:public gfx::Device {
public:
    gfx::DeviceInfo description{gfx::Backend::Metal,false,false,"Recording Metal"};
    std::string error;
    bool availableCompute=true,frame=false,pass=false,computing=false,badOrder=false;
    uint32_t next=1,lastMode=99,lastDrawCount=0;
    size_t uploadBytes=0;
    std::vector<uint32_t> stages;

    const gfx::DeviceInfo& info() const override{return description;}
    gfx::BufferHandle createBuffer(const gfx::BufferDesc&)override{return {next++};}
    void updateBuffer(gfx::BufferHandle,const void*,size_t count,size_t=0)override{
        if(pass||computing)badOrder=true;
        uploadBytes+=count;
    }
    void destroyBuffer(gfx::BufferHandle)override{}
    gfx::TextureHandle createTexture(const gfx::TextureDesc&)override{return {next++};}
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int)override{}
    void destroyTexture(gfx::TextureHandle)override{}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&)override{return {next++};}
    void destroyShader(gfx::ShaderHandle)override{}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&)override{return {next++};}
    void destroyPipeline(gfx::PipelineHandle)override{}
    const std::string& lastError() const override{return error;}
    bool supportsCompute()const override{return availableCompute;}
    gfx::ComputeHandle createCompute(const gfx::ComputeDesc&)override{return {next++};}
    void destroyCompute(gfx::ComputeHandle)override{}
    bool beginCompute(gfx::ComputeHandle)override{
        if(!frame||pass||computing){badOrder=true;return false;}
        computing=true;return true;
    }
    void setComputeBuffer(int,gfx::BufferHandle)override{
        if(!computing)badOrder=true;
    }
    void setComputeUniforms(const void* data,size_t size)override{
        if(!computing||size<64){badOrder=true;return;}
        std::memcpy(&lastMode,static_cast<const uint8_t*>(data)+48,sizeof(uint32_t));
    }
    void dispatch(uint32_t n,uint32_t=1)override{
        if(!computing||!n)badOrder=true;
        stages.push_back(lastMode);
    }
    void endCompute()override{if(!computing)badOrder=true;computing=false;}
    void beginFrame(int width,int height)override{frame=true;fbW_=width;fbH_=height;}
    void endFrame()override{if(pass||computing)badOrder=true;frame=false;}
    void beginPass(const gfx::PassDesc&)override{
        if(!frame||computing||pass)badOrder=true;
        pass=true;
    }
    void endPass()override{if(!pass)badOrder=true;pass=false;}
    void setViewport(const gfx::Rect&)override{}
    void setScissor(const gfx::Rect&)override{}
    void setPipeline(gfx::PipelineHandle)override{if(!pass)badOrder=true;}
    void setVertexBuffer(gfx::BufferHandle,size_t=0)override{if(!pass)badOrder=true;}
    void setIndexBuffer(gfx::BufferHandle,size_t=0)override{if(!pass)badOrder=true;}
    void setTexture(int,gfx::TextureHandle)override{if(!pass)badOrder=true;}
    void setUniforms(int,const void*,size_t)override{if(!pass)badOrder=true;}
    void draw(uint32_t,uint32_t=0)override{if(!pass)badOrder=true;}
    void drawIndexed(uint32_t count,uint32_t=0,int32_t=0,uint32_t=1)override{
        if(!pass)badOrder=true;
        lastDrawCount=count;
    }
};

int main() {
    using spectra::ComputePreference;
    using spectra::ComputeBackend;
    using spectra::chooseComputeBackend;
    check(chooseComputeBackend(gfx::Backend::Metal,true,ComputePreference::Automatic)==ComputeBackend::Metal,
          "available Metal compute was not selected");
    check(chooseComputeBackend(gfx::Backend::Metal,false,ComputePreference::PreferGpu)==ComputeBackend::Cpu,
          "Metal without compute did not fall back");
    check(chooseComputeBackend(gfx::Backend::GLES3,true,ComputePreference::Automatic)==ComputeBackend::Cpu,
          "GLES3 illegally selected Metal backend");
    check(chooseComputeBackend(gfx::Backend::Vulkan,true,ComputePreference::Automatic)==ComputeBackend::Cpu,
          "Vulkan GPU path is not implemented yet");
    check(chooseComputeBackend(gfx::Backend::GL33,true,ComputePreference::Automatic)==ComputeBackend::Cpu,
          "GL33 GPU path is not implemented yet");
    check(chooseComputeBackend(gfx::Backend::Metal,true,ComputePreference::CpuOnly)==ComputeBackend::Cpu,
          "CpuOnly policy ignored");

    spectra::ComputeEmitterDesc config;
    config.capacity=80;
    config.burst=5;
    config.lifetime=0.75f;
    config.speed=3;
    config.gravityY=-9.81f;
    config.backend=ComputePreference::Automatic;

    std::string reason;
    check(spectra::validComputeEmitter(config,&reason),"default config invalid");
    auto invalid=config;
    invalid.capacity=4097;
    check(!spectra::validComputeEmitter(invalid,&reason),"unsafe >4096 vertex/index capacity accepted");
    invalid=config; invalid.burst=0;
    check(!spectra::validComputeEmitter(invalid,&reason),"zero burst accepted");
    invalid=config; invalid.lifetime=-3;
    check(!spectra::validComputeEmitter(invalid,&reason),"negative lifetime accepted");
    invalid=config; invalid.speed=999999;
    check(!spectra::validComputeEmitter(invalid,&reason),"out-of-range speed accepted");

    spectra::ComputeParticles emitter;
    check(emitter.init(nullptr,config),"CPU fallback headless init failed");
    check(emitter.backend()==ComputeBackend::Cpu,"null GPU device should use CPU backend");
    check(emitter.burstAt({0,2,0},{1,0,0}),"CPU fallback failed to spawn particles");
    check(emitter.step(0.08f),"CPU fallback step failed");
    std::vector<fx::FxParticleInfo> particles;
    emitter.cpuFallback().snapshot(particles);
    check(particles.size()==5,"fallback lost expected burst size");
    check(particles.front().pos.x>0.001f,"fallback did not move particle");
    emitter.clear();
    emitter.cpuFallback().snapshot(particles);
    check(particles.empty(),"clear did not remove CPU fallback particles");
    check(emitter.burstAt({0,0,0}),"fallback spawn after clear failed");
    emitter.shutdown();
    check(!emitter.burstAt({0,0,0}),"spawn accepted after shutdown");

    // GPU host-path smoke via RHI recording, no real shader execution.
    RecordingMetalDevice mock;
    config.backend=ComputePreference::Automatic;
    config.capacity=32;
    config.burst=3;
    check(emitter.init(&mock,config),"recording Metal device GPU init failed");
    check(emitter.gpuActive(),"supported Metal mock did not select GPU");
    render::RTSCamera cam{{0,1,0},10.f,0.4f,0.6f};
    mock.beginFrame(800,600);
    emitter.setView(cam,cam.proj(800.f/600.f,false)*cam.view(),800,600);
    check(emitter.burstAt({0,2,0},{0,1,0}),"GPU queue rejected burst");
    check(emitter.step(1.f/60.f),"GPU queue compute step failed");
    check(mock.stages==std::vector<uint32_t>({0,1,2}),
          "GPU dispatch order must be spawn, integrate, expand");
    mock.beginPass(gfx::PassDesc{});
    emitter.draw();
    check(mock.lastDrawCount==config.capacity*6,
          "GPU billboard index count does not match pool capacity");
    mock.endPass();
    mock.endFrame();
    check(!mock.badOrder,"GPU API called during invalid frame/encoder state");
    check(emitter.stats().alive==config.burst,"GPU alive slot tracking lost particles");
    emitter.shutdown();

    // Explicit hard GPU requirement never silently reports a CPU implementation.
    config.backend=ComputePreference::RequireGpu;
    check(!emitter.init(nullptr,config),"RequireGpu should fail without a Device");
    check(!emitter.reason().empty(),"RequireGpu failure reason missing");
    std::cout<<"PASS SpectraFX compute policy, config validation, CPU fallback and lifecycle\n";
}
