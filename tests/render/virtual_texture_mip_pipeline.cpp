// Final-gate CPU regression: physical mip generation, atomic stream publication,
// indexed virtual mip maps and partial table rebuild against full reference.
// No native GPU is required; hardware pixel tests remain independent.
#include "engine/gfx/VirtualTextureStreamer.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse::gfx;
namespace {
void check(bool condition,const char* description) {
    if(!condition) throw std::runtime_error(description);
}

struct MockDevice final : Device {
    DeviceInfo deviceInfo;
    std::string error = "simulated upload rejection";
    std::map<uint32_t,TextureDesc> resources;
    struct Record {uint32_t texture;int mip,layer;};
    std::vector<Record> submitted;
    uint32_t nextId=1, atlasId=0, tableId=0;
    bool failNextAtlas=false;
    const DeviceInfo& info() const override {return deviceInfo;}
    const std::string& lastError() const override {return error;}
    bool supportsTextureDimension(TextureDimension d)const override {
        return d==TextureDimension::Texture2DArray;
    }
    bool supports(PixelFormat p)const override {
        return p==PixelFormat::RGBA8 || p==PixelFormat::RGBA8_sRGB;
    }
    BufferHandle createBuffer(const BufferDesc&)override{return {};}
    void updateBuffer(BufferHandle,const void*,size_t,size_t)override{}
    void destroyBuffer(BufferHandle)override{}
    TextureHandle createTexture(const TextureDesc& d)override {
        if(validateTextureDesc(d)!=TextureDescError::None ||
           d.dimension!=TextureDimension::Texture2DArray)return {};
        const uint32_t id=nextId++;
        auto desc=d;desc.data=nullptr;desc.label=nullptr;
        resources.emplace(id,desc);
        // Fixture uses 8x8 atlas pages and a 4x4 page table.
        if(d.width==8) atlasId=id;
        else if(d.width==4) tableId=id;
        else return {};
        return {id};
    }
    void updateTexture(TextureHandle,int,int,int,int,const void*,int)override{}
    bool updateTextureRegion(TextureHandle texture,const TextureUploadDesc& d)override {
        const auto found=resources.find(texture.id);
        if(found==resources.end() || !d.data || d.width<1 || d.height<1 ||
           d.mipLevel<0 || d.mipLevel>=found->second.mipLevels ||
           d.arrayLayer<0 || d.arrayLayer>=found->second.arrayLayers ||
           d.x<0 || d.y<0 ||
           d.x+d.width>std::max(1,found->second.width>>d.mipLevel) ||
           d.y+d.height>std::max(1,found->second.height>>d.mipLevel) ||
           d.rowPitchBytes<d.width*4 || d.slicePitchBytes<d.rowPitchBytes*d.height)
            return false;
        if(texture.id==atlasId && failNextAtlas) {
            failNextAtlas=false;
            return false;
        }
        submitted.push_back({texture.id,d.mipLevel,d.arrayLayer});
        return true;
    }
    void destroyTexture(TextureHandle t)override{resources.erase(t.id);}
    ShaderHandle createShader(const ShaderDesc&)override{return {};}
    void destroyShader(ShaderHandle)override{}
    PipelineHandle createPipeline(const PipelineDesc&)override{return {};}
    void destroyPipeline(PipelineHandle)override{}
    void beginFrame(int,int)override{}
    void endFrame()override{}
    void beginPass(const PassDesc&)override{}
    void endPass()override{}
    void setViewport(const Rect&)override{}
    void setScissor(const Rect&)override{}
    void setPipeline(PipelineHandle)override{}
    void setVertexBuffer(BufferHandle,size_t)override{}
    void setIndexBuffer(BufferHandle,size_t)override{}
    void setTexture(int,TextureHandle)override{}
    void setUniforms(int,const void*,size_t)override{}
    void draw(uint32_t,uint32_t)override{}
    void drawIndexed(uint32_t,uint32_t,int32_t,uint32_t)override{}
};

void physicalMipKernel() {
    std::vector<uint8_t> pixels(4u*4u*4u,0);
    for(int y=0;y<4;++y)
        for(int x=0;x<4;++x) {
            auto* p=pixels.data()+(size_t(y)*4u+size_t(x))*4u;
            p[0]=x%2==0 ? 255u : 0u;
            p[3]=255;
        }
    std::vector<VirtualTextureMipLevel> linear,srgb;
    check(buildVirtualTexturePhysicalMips(pixels.data(),pixels.size(),
          4,3,false,linear),"generate linear RGBA8 mip chain");
    check(buildVirtualTexturePhysicalMips(pixels.data(),pixels.size(),
          4,3,true,srgb),"generate sRGB mip chain");
    check(linear.size()==3 && linear[1].width==2 &&
          linear[2].width==1 &&
          linear[1].rgba[0]==128 &&
          srgb[1].rgba[0]>=187 && srgb[1].rgba[0]<=189,
          "sRGB half-intensity must be averaged in linear light");
    check(virtualTexturePhysicalMipChainBytes(4,3)==84,
          "physical mip byte budget includes every level");
    pixels[0]=255;pixels[3]=0;
    for(int i=1;i<4;++i) {
        auto* p=pixels.data()+size_t(i)*4u;
        p[0]=0;p[1]=255;p[2]=0;p[3]=255;
    }
    check(buildVirtualTexturePhysicalMips(pixels.data(),pixels.size(),
          4,2,false,linear) &&
          linear[1].rgba[0]==0 &&
          linear[1].rgba[1]==255 &&
          linear[1].rgba[3]>=190 && linear[1].rgba[3]<=192,
          "transparent red does not cause black/red alpha-fringe");
    check(!buildVirtualTexturePhysicalMips(pixels.data(),2,4,3,false,linear) &&
          linear.empty(),"truncated source fails and clears output");
}

void indexedTableReference() {
    VirtualTexturePageCache cache;
    VirtualTexturePageTable indexed;
    check(cache.initialize(8,8,4,2,10) &&
          indexed.initialize(8,8,4,2,true) &&
          indexed.selectableMipLevels()==4 &&
          indexed.tableArrayLayers()==8,
          "index table has one slice per logical virtual mip");
    for(int layer=0;layer<2;++layer) {
        VirtualPageKey key{3,0,0,layer};
        const auto slot=cache.reserve(key,true);
        check(slot.valid() && cache.complete(key,slot,true),
              "roots resident for both virtual layers");
    }
    check(indexed.rebuild(cache)!=0,"first table rebuild");
    check(indexed.entryForMip(7,7,1,0)[2]==3 &&
          indexed.entryForMip(7,7,1,3)[3]==255,
          "all virtual mips fall back to same-layer root");
    check(indexed.rebuild(cache)==0,"identical residency no-op");
    auto compareFull=[&] {
        VirtualTexturePageTable full;
        check(full.initialize(8,8,4,2,true),"reference table initialize");
        full.rebuild(cache);
        for(int layer=0;layer<2;++layer)
          for(int mip=0;mip<4;++mip)
            for(int y=0;y<8;++y)
              for(int x=0;x<8;++x)
                check(indexed.entryForMip(x,y,layer,mip)==
                      full.entryForMip(x,y,layer,mip),
                      "incremental and full table bytes must agree");
    };
    compareFull();
    for(int iteration=0;iteration<620;++iteration) {
        const int x=iteration%8, y=(iteration/8)%8;
        const int layer=(iteration/64)%2;
        VirtualPageKey key{0,x,y,layer};
        auto slot=cache.reserve(key);
        if(slot.valid() && slot.needsUpload)
            check(cache.complete(key,slot,true),"detail publication");
        if(iteration%47==0) cache.touch(key);
        if(iteration%25==0) {
            indexed.rebuild(cache);
            compareFull();
        }
    }
    indexed.rebuild(cache);
    compareFull(); // journal history overflow must safely rebuild in full

    // Two independent caches can have equal revisions and identical geometry
    // yet different physical mappings. Rebinding must force a full rescan.
    VirtualTexturePageCache first,second;
    VirtualTexturePageTable rebound;
    check(first.initialize(8,8,4,2,10) &&
          second.initialize(8,8,4,2,10) &&
          rebound.initialize(8,8,4,2,true),
          "cache identity fixture initialized");
    const auto onlyFirst=first.reserve({0,0,0,0});
    const auto onlySecond=second.reserve({0,7,7,1});
    check(onlyFirst.valid() && onlySecond.valid() &&
          first.complete({0,0,0,0},onlyFirst,true) &&
          second.complete({0,7,7,1},onlySecond,true) &&
          first.mappingRevision()==second.mappingRevision(),
          "two caches have same revision but different ready pages");
    check(rebound.rebuild(first)>0 &&
          rebound.entryForMip(0,0,0,0)[3]==255,
          "initial binding publishes first cache mapping");
    check(rebound.rebuild(second)>0 &&
          rebound.entryForMip(0,0,0,0)[3]==0 &&
          rebound.entryForMip(7,7,1,0)[3]==255,
          "switching cache identity cannot retain stale page table entries");
}

VirtualTextureStreamConfig streamConfig() {
    VirtualTextureStreamConfig c;
    c.tilesX=c.tilesY=4;
    c.mipLevels=3;c.virtualLayers=1;
    c.physicalPages=3;c.physicalPageSize=8;
    c.physicalMipLevels=4;c.indexedMipTable=true;
    c.pageTableBytesPerFlush=1024;
    return c;
}
TextureStreamConfig budget() {
    TextureStreamConfig c;
    c.gpuBudgetBytes=4096;
    c.stagingBudgetBytes=1024;
    c.uploadBytesPerFrame=256;
    c.uploadsPerFrame=2;
    return c;
}
void streamingAndCancellation() {
    MockDevice gpu;
    VirtualTextureStreamer streamer(gpu,budget());
    auto cfg=streamConfig();
    check(streamer.initialize(cfg) &&
          streamer.indexedMipPageTableTexture().valid() &&
          !streamer.pageTableTexture().valid() &&
          streamer.indexedMipTableLevels()==3,
          "indexed table is initialized before public handle access");
    check(streamer.estimatedGpuAllocatedBytes()==
          3*virtualTexturePhysicalMipChainBytes(8,4)+4*4*3*4,
          "atlas + page-table VRAM estimates include all mips");
    std::vector<uint8_t> page(8u*8u*4u,151);
    const VirtualPageKey root{2,0,0,0};
    const auto ticket=streamer.stage(root,page.data(),page.size(),true);
    check(ticket.valid() && !streamer.resolveExact(root).valid(),
          "page not resident after staging");
    int callbacks=0;
    auto complete=[&](VirtualPageKey key,bool ok) {
        ++callbacks;
        check(key==root && ok,"whole-root-mip batch completed successfully");
    };
    check(streamer.drain(complete)==1 &&
          !streamer.resolveExact(root).valid() && callbacks==0,
          "first physical mip upload cannot publish page");
    check(streamer.drain(complete)==2 &&
          !streamer.resolveExact(root).valid() && callbacks==0,
          "intermediate mip uploads cannot publish page");
    check(streamer.drain(complete)==1 &&
          streamer.resolveExact(root).valid() && callbacks==1,
          "fourth physical mip publishes page exactly once");
    for(int mip=0;mip<3;++mip)
        check(streamer.encodedPageTableForMip(3,3,0,mip)[2]==2 &&
              streamer.encodedPageTableForMip(3,3,0,mip)[3]==255,
              "all requested mips resolve resident coarse root");
    const VirtualPageKey fine{0,0,0,0};
    const auto staged=streamer.stage(fine,page.data(),page.size());
    check(staged.valid() && streamer.drain()==1 &&
          !streamer.resolveExact(fine).valid(),
          "partial fine chain remains hidden");
    check(streamer.cancel(staged) &&
          !streamer.resolveExact(fine).valid() &&
          streamer.resolveExact(root).valid(),
          "cancellation retires partial chain without evicting pinned root");
    const auto retried=streamer.stage(fine,page.data(),page.size());
    check(retried.valid(),"cancelled fine page can be restaged");
    gpu.failNextAtlas=true;
    bool observedFailure=false;
    check(streamer.drain([&](VirtualPageKey k,bool ok){
        observedFailure = k==fine && !ok;
    })==1 && observedFailure && !streamer.ready(),
       "failed mip upload quarantines atlas and never publishes fine page");
    streamer.shutdown();
    check(gpu.resources.empty(),"GPU atlas and table released once");
}

// Repeated partial uploads must never publish stale slices after cancellation
// or LRU reuse. The pinned root remains the stable same-layer fallback.
void repeatedCancellationAndLruReuse() {
    MockDevice gpu;
    VirtualTextureStreamer streamer(gpu,budget());
    auto cfg=streamConfig();
    cfg.physicalPages=2;
    check(streamer.initialize(cfg),"reuse fixture initialized");
    std::vector<uint8_t> page(8u*8u*4u,37);
    const VirtualPageKey root{2,0,0,0};
    const auto rootTicket=streamer.stage(root,page.data(),page.size(),true);
    check(rootTicket.valid(),"pinned root staged");
    for(int n=0;n<8 && !streamer.resolveExact(root).valid();++n)
        streamer.drain();
    check(streamer.resolveExact(root).valid(),"pinned root ready");
    VirtualPageKey previous{0,0,0,0};
    bool hasPrevious=false;
    for(int iteration=0;iteration<96;++iteration) {
        VirtualPageKey key{0,iteration%4,(iteration/4)%4,0};
        page[0]=uint8_t(iteration);
        auto ticket=streamer.stage(key,page.data(),page.size());
        check(ticket.valid(),"detail page staged into reusable slice");
        if(iteration%3==0) {
            check(streamer.drain()==1,"first detail mip submitted");
            check(!streamer.resolveExact(key).valid(),
                  "partially transferred mip chain not visible");
            check(streamer.cancel(ticket),"partially transferred batch cancelled");
            check(!streamer.resolveExact(key).valid(),
                  "cancelled detail cannot become resident");
            check(streamer.encodedPageTableForMip(key.x,key.y,0,0)[2]==2,
                  "cancelled detail resolves only coarse root");
            hasPrevious=false;
        } else {
            for(int n=0;n<8 && !streamer.resolveExact(key).valid();++n)
                streamer.drain();
            check(streamer.resolveExact(key).valid(),"full mip chain published");
            if(hasPrevious && !(previous==key)) {
                check(!streamer.resolveExact(previous).valid(),
                      "previous LRU slice was evicted before reuse");
                check(streamer.encodedPageTableForMip(previous.x,previous.y,0,0)[2]==2,
                      "evicted detail resolves pinned coarse root");
            }
            previous=key;
            hasPrevious=true;
        }
        check(streamer.resolveExact(root).valid(),
              "pinned root survives cancellation and LRU reuse");
        check(streamer.ready(),"streamer remains usable after repeated reuse");
    }
    streamer.shutdown();
    check(gpu.resources.empty(),"stress fixture GPU handles released");
}

void impossibleBudgets() {
    MockDevice gpu;
    auto limits=budget();
    auto cfg=streamConfig();
    limits.stagingBudgetBytes=320; // full physical mip chain is 340 bytes
    VirtualTextureStreamer streamer(gpu,limits);
    check(!streamer.initialize(cfg) && gpu.resources.empty(),
          "impossible whole-mip staging budget rejected before allocation");
    limits=budget();limits.gpuBudgetBytes=600; // atlas=1020, table=192
    VirtualTextureStreamer smallGpu(gpu,limits);
    check(!smallGpu.initialize(cfg) && gpu.resources.empty(),
          "GPU atlas+table combined memory budget rejected");
    cfg.pageTableBytesPerFlush=8; // one 4x4 RGBA table row requires 16B
    VirtualTextureStreamer smallRows(gpu,budget());
    check(!smallRows.initialize(cfg) && gpu.resources.empty(),
          "undersized table row budget rejected");
}
}
int main() {
    try {
        physicalMipKernel();
        indexedTableReference();
        streamingAndCancellation();
        repeatedCancellationAndLruReuse();
        impossibleBudgets();
    } catch(const std::exception& e) {
        std::fprintf(stderr,"VirtualTextureMipPipeline FAIL: %s\n",e.what());
        return 1;
    }
    std::puts("VirtualTextureMipPipeline: mip chains, indexed LRU and atomic publication PASS");
    return 0;
}
