// CPU/mock-GPU virtual physical page streaming tests. No GPU driver used.
#include "engine/gfx/VirtualTextureStreamer.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse::gfx;
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
class Mock final : public Device {
public:
    DeviceInfo info_;
    std::string error_="synthetic page GPU failure";
    struct Allocation {TextureDesc desc;std::vector<uint8_t> data;};
    std::map<uint32_t,Allocation> live;
    uint32_t next=1, atlasId=0, tableId=0;
    bool failNextAtlas=false;
    int uploads=0,destroyed=0;
    std::vector<uint32_t> uploadTargets;
    const DeviceInfo& info()const override{return info_;}
    const std::string& lastError()const override{return error_;}
    bool supportsTextureDimension(TextureDimension d)const override{
        return d==TextureDimension::Texture2DArray ||
               d==TextureDimension::Texture2D;
    }
    bool supports(PixelFormat f)const override{
        return f==PixelFormat::RGBA8 || f==PixelFormat::RGBA8_sRGB;
    }
    BufferHandle createBuffer(const BufferDesc&)override{return {};}
    void updateBuffer(BufferHandle,const void*,size_t,size_t)override{}
    void destroyBuffer(BufferHandle)override{}
    TextureHandle createTexture(const TextureDesc& d)override{
        if(validateTextureDesc(d)!=TextureDescError::None)return {};
        const uint32_t id=next++;
        Allocation a;a.desc=d;a.desc.data=nullptr;
        if(d.data){
            const size_t n=size_t(d.width)*size_t(d.height)*size_t(d.arrayLayers)*4u;
            const uint8_t* p=static_cast<const uint8_t*>(d.data);
            a.data.assign(p,p+n);
            tableId=id;
        } else atlasId=id;
        live.emplace(id,std::move(a));
        return {id};
    }
    void updateTexture(TextureHandle,int,int,int,int,const void*,int)override{}
    bool updateTextureRegion(TextureHandle id,const TextureUploadDesc& u)override{
        auto found=live.find(id.id);
        if(found==live.end() || !u.data)return false;
        ++uploads;uploadTargets.push_back(id.id);
        if(id.id==atlasId && failNextAtlas){
            failNextAtlas=false;
            return false;
        }
        if(id.id==tableId){
            auto& a=found->second;
            const int w=a.desc.width,h=a.desc.height,layers=a.desc.arrayLayers;
            if(u.width!=w||u.x!=0||u.arrayLayer<0||u.arrayLayer>=layers||
               u.y<0||u.y+u.height>h)return false;
            const size_t stride=size_t(w)*4u;
            for(int y=0;y<u.height;++y){
                const uint8_t* pixels=static_cast<const uint8_t*>(u.data)+
                                       size_t(y)*size_t(u.rowPitchBytes);
                std::copy(pixels,pixels+stride,
                          a.data.begin()+(size_t(u.arrayLayer)*size_t(h)+size_t(u.y+y))*stride);
            }
        }
        return true;
    }
    void destroyTexture(TextureHandle id)override{if(live.erase(id.id))++destroyed;}
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
VirtualTextureStreamConfig config(){
    VirtualTextureStreamConfig c;
    c.tilesX=c.tilesY=4;c.mipLevels=3;c.virtualLayers=1;
    c.physicalPages=2;c.physicalPageSize=4;c.pageTableBytesPerFlush=64;
    return c;
}
TextureStreamConfig limits(){
    TextureStreamConfig c;
    c.gpuBudgetBytes=1024;c.stagingBudgetBytes=256;
    c.uploadBytesPerFrame=64;c.uploadsPerFrame=2;
    return c;
}
void lruAndPublication(){
    Mock gpu;
    VirtualTextureStreamer v(gpu,limits());
    require(v.initialize(config()),"physical atlas and page table initialize");
    require(v.pageStats().capacity==2 && v.pageStats().free==2 &&
            v.physicalAtlas().valid() && v.pageTableTexture().valid(),
            "two allocated physical slots and shader table");
    const VirtualPageKey coarse{2,0,0,0}, pinned{1,0,0,0}, fine{0,3,3,0};
    std::vector<uint8_t> bytes(64,17);
    auto t=v.stage(coarse,bytes.data(),bytes.size());
    require(t.valid() && !v.resolve(fine).valid(),
            "inflight page cannot appear in shader mapping");
    std::fill(bytes.begin(),bytes.end(),99); // streamer owns deep-copied staging
    bool observedReady=false;
    require(v.drain([&](VirtualPageKey k,bool ok){
        observedReady=ok && k==coarse && v.resolve(fine).valid();
    })==1 && observedReady,"coarse page publishes after GPU completion");
    auto entry=v.encodedPageTable(3,3,0);
    require(entry[3]==255 && entry[2]==2,
            "all virtual mip-zero children resolve to resident parent");
    auto p=v.stage(pinned,bytes.data(),bytes.size(),true);
    require(p.valid()&&v.drain()==1,"pinned intermediate page uploaded");
    require(v.pageStats().pinned==1 && v.pageStats().resident==2,
            "one slot pinned and both currently resident");
    const int before=gpu.uploads;
    auto next=v.stage(fine,bytes.data(),bytes.size());
    require(next.valid(),"LRU slot reallocated while other remains pinned");
    require(gpu.uploads>before && !v.resolve(fine).valid(),
            "obsolete table mapping invalidated BEFORE physical slot overwrite");
    entry=v.encodedPageTable(3,3,0);
    require(entry[3]==0,"pending new fine page is not exposed");
    require(v.pageStats().evictions==1,"LRU eviction counted");
    require(v.drain()==1&&v.resolve(fine).valid(),"fine page published");
    entry=v.encodedPageTable(3,3,0);
    require(entry[3]==255&&entry[2]==0,"new fine page overrides parent");
    require(!v.evict(pinned),"pinned page cannot be evicted");
    require(v.evict(fine),"unpinned ready page can be evicted");
    require(!v.resolve(fine).valid(),"evicted fine page no longer sampled");
    v.shutdown();
    require(gpu.live.empty()&&gpu.destroyed==2,
            "owned page atlas and page table released exactly once");
}
void cancelAndFailure(){
    Mock gpu;
    VirtualTextureStreamer v(gpu,limits());
    require(v.initialize(config()),"fresh virtual atlas");
    std::vector<uint8_t> pixels(64,7);
    const VirtualPageKey a{2,0,0,0};
    require(!v.stage(a,pixels.data(),8).valid(),"truncated physical page refused");
    const auto ticket=v.stage(a,pixels.data(),pixels.size());
    require(ticket.valid()&&v.cancel(ticket),"pending upload cancelled");
    require(!v.resolve(a).valid(),"cancelled page not resident");
    const auto again=v.stage(a,pixels.data(),pixels.size());
    require(again.valid(),"cancelled page can be reserved again");
    gpu.failNextAtlas=true;
    bool failedCb=false;
    require(v.drain([&](VirtualPageKey key,bool ok){
        failedCb=key==a&&!ok;
    })==1&&failedCb&&!v.ready(),
            "failed GPU upload quarantines all virtual mappings");
    require(!v.physicalAtlas().valid() && !v.pageTableTexture().valid(),
            "failed physical page never exposed to shader");
    require(!v.stage(a,pixels.data(),pixels.size()).valid(),
            "failed atlas refuses new writes until recreation");
    v.shutdown();
    require(gpu.live.empty(),"shutdown cleans failed resources");
}
}
int main(){
    try{lruAndPublication();cancelAndFailure();}
    catch(const std::exception& e){
        std::fprintf(stderr,"VirtualTextureStreamer fixture FAILED: %s\n",e.what());
        return 1;
    }
    std::puts("VirtualTextureStreamer cache, page table and budgets: OK");
    return 0;
}
