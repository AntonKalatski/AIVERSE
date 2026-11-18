#include "engine/gfx/VirtualTextureFeedback.h"
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse::gfx;
namespace {
void require(bool yes,const char* what){if(!yes)throw std::runtime_error(what);}
void write(TextureReadbackResult& result,size_t pixel,uint32_t word){
    for(int j=0;j<4;++j)
        result.pixels[pixel*4u+size_t(j)]=uint8_t((word>>(j*8))&255u);
}
void roundtrip() {
    const VirtualPageKey a{0,7,6,1},b{1,1,1,0},c{3,0,0,0};
    for (const auto& key:{a,b,c,VirtualPageKey{3,0,0,1}}) {
        const uint32_t word=encodeVirtualTextureFeedback(key);
        require((word&0x80000000u)!=0 &&
                decodeVirtualTextureFeedbackId(word)==key,
                "packed page ID roundtrip preserves mip layer and coords");
    }
    require(encodeVirtualTextureFeedback({-1,0,0,0})==0 &&
            encodeVirtualTextureFeedback({0,2048,0,0})==0 &&
            encodeVirtualTextureFeedback({31,0,0,0})==0 &&
            encodeVirtualTextureFeedback({0,0,0,16})==0 &&
            !decodeVirtualTextureFeedbackId(0).valid(),
            "invalid page key or clear value cannot encode as a valid page");
}
void frequencyAndPlanner() {
    VirtualTextureFeedbackConfig cfg;
    cfg.tilesX=cfg.tilesY=8;
    cfg.mipLevels=4;
    cfg.virtualLayers=2;
    cfg.maxReadbackPixels=16;
    cfg.maxUniquePages=4;
    cfg.maxVisibleRegions=2;
    VirtualTextureFeedbackDecoder decoder;
    require(decoder.initialize(cfg),"valid feedback decoder configuration");
    const VirtualPageKey a{0,7,6,1},b{1,1,1,0},c{3,0,0,0};
    TextureReadbackResult pixels;
    pixels.format=PixelFormat::RGBA8;
    pixels.width=4;pixels.height=2;
    pixels.pixels.assign(32,0);
    const uint32_t wordA=encodeVirtualTextureFeedback(a);
    const uint32_t wordB=encodeVirtualTextureFeedback(b);
    const uint32_t wordC=encodeVirtualTextureFeedback(c);
    for(size_t i=0;i<4;++i)write(pixels,i,wordA);
    write(pixels,4,wordB);write(pixels,5,wordB);
    write(pixels,6,wordC);
    std::vector<VirtualTextureVisibleRegion> regions;
    std::string err;
    require(decoder.decode(pixels,regions,err)&&err.empty(),
            "bounded linear RGBA8 feedback decode");
    const auto stats=decoder.stats();
    require(stats.sampledPixels==8 && stats.invalidIds==1 &&
            stats.uniquePages==3 && stats.outputRegions==2 &&
            stats.omittedRegions==1,
            "frequency tally includes cleared fragments and region budget");
    require(regions.size()==2 && regions[0].priority==4 &&
            regions[0].layer==1 && regions[0].desiredMip==0 &&
            regions[0].u0==7.0/8.0 && regions[0].v0==6.0/8.0 &&
            regions[1].priority==2 && regions[1].desiredMip==1,
            "most-visible pages prioritized with exact mip UV rectangles");
    VirtualTexturePrefetchPlanner planner;
    VirtualTexturePrefetchConfig pref;
    pref.tilesX=8;pref.tilesY=8;pref.mipLevels=4;pref.virtualLayers=2;
    pref.maxCandidates=128;pref.maxRequestsPerFrame=32;
    require(planner.initialize(pref),"feedback-to-prefetch planner initialized");
    auto requests=planner.plan(regions);
    require(!requests.empty() && requests[0].key.mip==3 &&
            requests[0].pin,"GPU feedback output requests pinned coarse pages first");
    bool sawFine=false,sawMiddle=false;
    for(const auto& request:requests){
        if(request.key==a)sawFine=true;
        if(request.key==b)sawMiddle=true;
    }
    require(sawFine && sawMiddle,"feedback retains both requested detail pages");
    pixels.format=PixelFormat::RGBA8_sRGB;
    require(!decoder.decode(pixels,regions,err) &&
            !err.empty() && regions.empty(),
            "sRGB feedback target rejected to preserve integer IDs");
    pixels.format=PixelFormat::RGBA8;
    pixels.pixels.pop_back();
    require(!decoder.decode(pixels,regions,err) &&
            regions.empty(),"truncated callback readback rejected");
}
void invalidAndLimits() {
    VirtualTextureFeedbackDecoder decoder;
    std::vector<VirtualTextureVisibleRegion> regions;
    std::string error;
    TextureReadbackResult image;
    image.format=PixelFormat::RGBA8;
    image.width=image.height=1;
    image.pixels.resize(4);
    require(!decoder.decode(image,regions,error),"uninitialized decoder refuses input");
    VirtualTextureFeedbackConfig cfg;
    cfg.tilesX=cfg.tilesY=4;cfg.mipLevels=3;cfg.virtualLayers=1;
    cfg.maxReadbackPixels=8;cfg.maxUniquePages=1;cfg.maxVisibleRegions=1;
    require(decoder.initialize(cfg),"valid constrained decoder");
    image.width=3;image.height=1;image.pixels.assign(12,0);
    write(image,0,encodeVirtualTextureFeedback({0,0,0,0}));
    write(image,1,encodeVirtualTextureFeedback({0,1,0,0}));
    write(image,2,encodeVirtualTextureFeedback({0,7,0,0}));
    require(decoder.decode(image,regions,error) &&
            decoder.stats().uniquePages==1 &&
            decoder.stats().omittedUniquePages==1 &&
            decoder.stats().invalidIds==1,
            "unique page budget and out-of-range coordinate guard");
    image.width=9;image.height=1;image.pixels.assign(36,0);
    require(!decoder.decode(image,regions,error),"huge readback budget refused");
    cfg.virtualLayers=17;
    require(!decoder.initialize(cfg) && !decoder.ready(),
            "exceeding 4-bit virtual layer encoding resets decoder");
}
}
int main() {
    try{roundtrip();frequencyAndPlanner();invalidAndLimits();}
    catch(const std::exception& e){
        std::fprintf(stderr,"Virtual texture GPU feedback FAILED: %s\n",e.what());
        return 1;
    }
    std::puts("Virtual texture RGBA8 feedback codec and visibility: OK");
    return 0;
}
