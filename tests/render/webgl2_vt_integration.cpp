// Opt-in Emscripten/WebGL2 acceptance using the real AIVERSE Device and VT streamer.
// The browser test separately validates actual WebGL2 framebuffer pixels.
#include "engine/platform/Platform.h"
#include "engine/gfx/VirtualTextureStreamer.h"
#include "engine/gfx/VirtualTextureSampling.h"
#include <algorithm>
#include <string>
#include <emscripten.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

namespace {
using namespace aiverse;
using namespace aiverse::gfx;
std::unique_ptr<VirtualTextureStreamer> stream;
unsigned generations=0, stages=0, completed=0, failures=0, shutdowns=0;
unsigned gpuReads=0;
unsigned outstandingReads=0;
bool started=false;
ShaderHandle samplerShader;
PipelineHandle samplerPipeline;
TextureHandle samplerTarget;
bool samplerQueued=false;
bool samplerVerified=false;
std::string samplerFragment;
VirtualPageKey root{2,0,0,0}, detail{0,0,0,0}, replacement{0,3,3,0};
std::vector<uint8_t> pageColor(uint8_t r,uint8_t g,uint8_t b) {
    std::vector<uint8_t> rgba(4u*4u*4u);
    for(size_t p=0;p<rgba.size();p+=4) {
        rgba[p]=r;rgba[p+1]=g;rgba[p+2]=b;rgba[p+3]=255;
    }
    return rgba;
}
void check(bool ok,const char* description) {
    if(ok)return;
    ++failures;
    std::fprintf(stderr,"FAIL: WebGL VT integration %s\n",description);
}
// This is a real GLSL draw through gfx::Device, not a JavaScript-produced
// texture. Every pixel column probes a different virtual page/LOD case.
void setupShader(Device& device) {
    static constexpr const char* vertex=R"GLSL(
void main() {
    vec2 p=vec2(gl_VertexID==1 ? 3.0 : -1.0,
                gl_VertexID==2 ? 3.0 : -1.0);
    gl_Position=vec4(p,0.0,1.0);
}
)GLSL";
    samplerFragment=std::string(kVirtualTextureGLSL)+R"GLSL(
uniform highp sampler2DArray tex0;
uniform highp sampler2DArray tex1;
layout(location=0) out vec4 outColor;
void main() {
    int column=int(gl_FragCoord.x);
    vec2 uv=column==0 ? vec2(0.125) : vec2(0.875);
    float lod=column==2 ? 0.5 : 0.0;
    int layer=column==3 ? 1 : 0;
    outColor=aiverseSampleVirtualTrilinear(
        tex0,tex1,uv,layer,0.0,vec4(9.0/255.0,12.0/255.0,18.0/255.0,1.0),
        vec2(0.0),vec2(0.0),lod,3,0,0);
}
)GLSL";
    ShaderDesc shader;
    shader.glslVertex=vertex;
    shader.glslFragment=samplerFragment.c_str();
    shader.textureSlots=2;
    shader.uniformSlots=0;
    samplerShader=device.createShader(shader);
    check(samplerShader.valid(),"create production canonical GLSL VT shader");
    if(!samplerShader.valid())return;
    PipelineDesc pipeline;
    pipeline.shader=samplerShader;
    pipeline.attrCount=0;
    pipeline.depthTest=false;
    pipeline.depthWrite=false;
    pipeline.cull=CullMode::None;
    samplerPipeline=device.createPipeline(pipeline);
    check(samplerPipeline.valid(),"create VT sampling graphics pipeline");
    TextureDesc target;
    target.width=target.height=4;
    target.format=PixelFormat::RGBA8;
    target.renderTarget=true;
    target.filter=Filter::Nearest;
    samplerTarget=device.createTexture(target);
    check(samplerTarget.valid(),"allocate offscreen sampler oracle target");
}
void queueShaderOracle(Device& device) {
    if(!samplerPipeline.valid() || !samplerTarget.valid())return;
    PassDesc pass;
    pass.colorTarget=samplerTarget;
    pass.clearDepthEnabled=false;
    device.beginPass(pass);
    device.setPipeline(samplerPipeline);
    device.setTexture(0,stream->physicalAtlas());
    device.setTexture(1,stream->indexedMipPageTableTexture());
    device.draw(3);
    device.endPass();
    // CPU oracle comes from immutable authored colors, not from the GPU
    // indexed table, atlas pixels or calculated shader page coordinates.
    const std::array<std::array<int,4>,4> expected{{
        {{28,75,98,255}},    // evicted detail: pinned root fallback
        {{11,205,47,255}},   // current fine detail
        {{20,140,73,255}},   // 0.5 LOD: fine + coarse (allow UNORM rounding)
        {{9,12,18,255}}     // invalid logical layer
    }};
    ++outstandingReads;
    samplerQueued=device.readTextureAsync(samplerTarget,{0,0},
        [expected](TextureReadbackResult value) {
            bool good=value.success() && value.width==4 && value.height==4 &&
                      value.pixels.size()==64;
            if(good) for(int y=0;y<4;++y)
                for(int x=0;x<4;++x)
                    for(int c=0;c<4;++c) {
                        const int delta=std::abs(int(value.pixels[size_t(y*4+x)*4+c])-
                                                 expected[size_t(x)][size_t(c)]);
                        if(delta>1)good=false;
                    }
            check(good,"canonical WebGL2 GLSL coarse/fine/trilinear/fallback GPU pixel oracle");
            samplerVerified=good;
            --outstandingReads;
        });
    if(!samplerQueued) {
        --outstandingReads;
        check(false,"queue canonical WebGL2 GLSL render-target readback");
    }
}

void init(Device& device) {
    ++generations;
    stages=0;
    gpuReads=0;
    outstandingReads=0;
    started=false;
    samplerQueued=false;samplerVerified=false;
    samplerShader={};samplerPipeline={};samplerTarget={};
    samplerFragment.clear();
    setupShader(device);
    TextureStreamConfig limits;
    limits.gpuBudgetBytes=4096;
    limits.stagingBudgetBytes=512;
    limits.uploadBytesPerFrame=256;
    limits.uploadsPerFrame=8;
    stream=std::make_unique<VirtualTextureStreamer>(device,limits);
    VirtualTextureStreamConfig cfg;
    cfg.tilesX=cfg.tilesY=4;
    cfg.mipLevels=3;cfg.virtualLayers=1;
    cfg.physicalPages=2;cfg.physicalPageSize=4;
    cfg.physicalMipLevels=2;cfg.indexedMipTable=true;
    cfg.pageTableBytesPerFlush=512;
    check(stream->initialize(cfg),"initialize real GPU atlas and indexed table");
    if(!stream->ready())return;
    auto pixels=pageColor(28,75,98);
    check(stream->stage(root,pixels.data(),pixels.size(),true).valid(),
          "stage pinned root into real GPU atlas");
    started=true;
}
void frame(Device& device,double) {
    if(!stream || !stream->ready() || !started)return;
    stream->drain();
    if(stages==0 && stream->resolveExact(root).valid()) {
        const auto encoded=stream->encodedPageTableForMip(3,3,0,0);
        check(encoded[2]==2 && encoded[3]==255,
              "root fallback appears in indexed GPU table");
        auto pixels=pageColor(201,31,62);
        check(stream->stage(detail,pixels.data(),pixels.size()).valid(),
              "stage fine detail into real GPU atlas");
        stages=1;
    } else if(stages==1 && stream->resolveExact(detail).valid()) {
        check(stream->encodedPageTableForMip(0,0,0,0)[2]==0,
              "fine detail visible in page table");
        auto pixels=pageColor(11,205,47);
        check(stream->stage(replacement,pixels.data(),pixels.size()).valid(),
              "stage replacement, reusing LRU detail slice");
        stages=2;
    } else if(stages==2 && stream->resolveExact(replacement).valid()) {
        check(!stream->resolveExact(detail).valid(),
              "replaced detail no longer resident");
        check(stream->resolveExact(root).valid(),
              "root remains pinned after reuse");
        check(stream->encodedPageTableForMip(0,0,0,0)[2]==2,
              "old detail falls back to root");
        check(stream->encodedPageTableForMip(3,3,0,0)[2]==0,
              "replacement indexed entry committed");
        check(stream->streamStats().stagedBytes==0,
              "all streaming staging bytes released");
        // Independent readback of the *production* physical resources.
        // Page table layout is [logicalLayer * mipLevels + requestedMip].
        // At mip selector zero, root fills every cell except replacement
        // key (3,3), which points to physical slice 1 and resident mip 0.
        const auto rootExpected=pageColor(28,75,98);
        const auto replacementExpected=pageColor(11,205,47);
        std::vector<uint8_t> tableExpected(4u*4u*4u,0);
        for(size_t p=0;p<tableExpected.size();p+=4) {
            tableExpected[p]=0;
            tableExpected[p+2]=2;
            tableExpected[p+3]=255;
        }
        const size_t replacementOffset=(3u*4u+3u)*4u;
        tableExpected[replacementOffset]=1;
        tableExpected[replacementOffset+2]=0;
        const auto queueRead=[&](TextureHandle handle,int slice,
                                 std::vector<uint8_t> expected,
                                 const char* description) {
            ++outstandingReads;
            const bool queued=device.readTextureAsync(handle,{0,slice},
                [expected=std::move(expected),description](TextureReadbackResult image) {
                    check(image.success() && image.width==4 && image.height==4 &&
                          image.pixels==expected,description);
                    ++gpuReads;
                    --outstandingReads;
                });
            if(!queued) {
                --outstandingReads;
                check(false,description);
            }
        };
        queueRead(stream->physicalAtlas(),0,rootExpected,
                  "production pinned root physical GPU bytes");
        queueRead(stream->physicalAtlas(),1,replacementExpected,
                  "production recycled detail physical GPU bytes");
        queueRead(stream->indexedMipPageTableTexture(),0,tableExpected,
                  "production indexed page table actual GPU bytes");
        stages=3;
    } else if(stages==3 && outstandingReads==0 && gpuReads==3 && failures==0) {
        queueShaderOracle(device);
        stages=4;
    } else if(stages==4 && outstandingReads==0 && samplerVerified && failures==0) {
        stages=5;
        ++completed;
        std::printf("PASS: WebGL2 production VT atlas and table GPU readback generation=%u\n",
                    generations);
    }
}
void shutdown() {
    ++shutdowns;
    if(stream) stream->shutdown();
    stream.reset();
    // All GPU objects belong to the current Device generation.
    // Browser context recovery calls shutdown before Device destruction.
    // Cleanup is handled by platform Device destructor for test-owned handles
    // when the lost context is already invalid.
    samplerShader={};samplerPipeline={};samplerTarget={};
    samplerFragment.clear();
}
}
extern "C" {
EMSCRIPTEN_KEEPALIVE unsigned aiverse_web_vt_generation() { return generations; }
EMSCRIPTEN_KEEPALIVE unsigned aiverse_web_vt_stage() { return stages; }
EMSCRIPTEN_KEEPALIVE unsigned aiverse_web_vt_gpu_reads() { return gpuReads; }
EMSCRIPTEN_KEEPALIVE unsigned aiverse_web_vt_shader_verified() { return samplerVerified ? 1u : 0u; }
EMSCRIPTEN_KEEPALIVE unsigned aiverse_web_vt_completed() { return completed; }
EMSCRIPTEN_KEEPALIVE unsigned aiverse_web_vt_shutdowns() { return shutdowns; }
EMSCRIPTEN_KEEPALIVE unsigned aiverse_web_vt_failures() { return failures; }
}
int aiverseMain(int,char**) {
    aiverse::platform::AppConfig cfg;
    cfg.width=16;cfg.height=16;
    aiverse::platform::AppCallbacks callbacks;
    callbacks.init=init;
    callbacks.frame=frame;
    callbacks.shutdown=shutdown;
    return aiverse::platform::run(cfg,std::move(callbacks));
}
