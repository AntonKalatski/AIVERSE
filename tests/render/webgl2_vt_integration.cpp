// Opt-in Emscripten/WebGL2 acceptance using the real AIVERSE Device and VT streamer.
// The browser test separately validates actual WebGL2 framebuffer pixels.
#include "engine/platform/Platform.h"
#include "engine/gfx/VirtualTextureStreamer.h"
#include <emscripten.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace {
using namespace aiverse;
using namespace aiverse::gfx;
std::unique_ptr<VirtualTextureStreamer> stream;
unsigned generations=0, stages=0, completed=0, failures=0, shutdowns=0;
bool started=false;
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
void init(Device& device) {
    ++generations;
    stages=0;
    started=false;
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
void frame(Device&,double) {
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
        stages=3;
        ++completed;
        std::printf("PASS: WebGL2 production VT streaming LRU generation=%u\n",generations);
    }
}
void shutdown() {
    ++shutdowns;
    if(stream) stream->shutdown();
    stream.reset();
}
}
extern "C" {
EMSCRIPTEN_KEEPALIVE unsigned aiverse_web_vt_generation() { return generations; }
EMSCRIPTEN_KEEPALIVE unsigned aiverse_web_vt_stage() { return stages; }
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
