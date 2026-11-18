// CPU-only native facade regression; actual GPU backends are separately validated.
#include "engine/gfx/TextureResidentStreamer.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse::gfx;
namespace {
void require(bool ok, const char* why){if(!ok)throw std::runtime_error(why);}
class FakeDevice final : public Device {
public:
    DeviceInfo info_;
    std::string error_ = "synthetic GPU upload failure";
    std::map<uint32_t,TextureDesc> images;
    uint32_t next=1;
    int uploads=0,destroyed=0;
    bool failNext=false;
    std::vector<int> uploadedMip, uploadedLayer;
    std::vector<uint8_t> uploadedFirstByte;
    const DeviceInfo& info()const override{return info_;}
    const std::string& lastError()const override{return error_;}
    bool supportsTextureDimension(TextureDimension d)const override{
        return d==TextureDimension::Texture2D ||
               d==TextureDimension::Texture2DArray ||
               d==TextureDimension::Texture3D || d==TextureDimension::Cube;
    }
    bool supports(PixelFormat f)const override{return f==PixelFormat::RGBA8;}
    BufferHandle createBuffer(const BufferDesc&)override{return {};}
    void updateBuffer(BufferHandle,const void*,size_t,size_t)override{}
    void destroyBuffer(BufferHandle)override{}
    TextureHandle createTexture(const TextureDesc& d)override{
        if(validateTextureDesc(d)!=TextureDescError::None)return {};
        const uint32_t id=next++;
        images.emplace(id,d);
        return {id};
    }
    void updateTexture(TextureHandle,int,int,int,int,const void*,int)override{}
    bool updateTextureRegion(TextureHandle h,const TextureUploadDesc& u)override{
        if(!images.count(h.id)||!u.data)return false;
        ++uploads;
        uploadedMip.push_back(u.mipLevel);
        uploadedLayer.push_back(u.arrayLayer);
        uploadedFirstByte.push_back(static_cast<const uint8_t*>(u.data)[0]);
        if(failNext){failNext=false;return false;}
        return true;
    }
    void destroyTexture(TextureHandle h)override{if(images.erase(h.id))++destroyed;}
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

TextureStreamConfig limits(){
    TextureStreamConfig c;
    c.gpuBudgetBytes=4096;
    c.stagingBudgetBytes=512;
    c.uploadBytesPerFrame=80;
    c.uploadsPerFrame=2;
    return c;
}
TextureResidentStreamer::FillFn producer(uint8_t marker){
    return [marker](TextureMipRequest, int w,int h,int depth,std::vector<uint8_t>& out){
        out.assign(size_t(w)*size_t(h)*size_t(depth)*4u,marker);
        return true;
    };
}
void layeredResidencyAndBudgets(){
    FakeDevice gpu;
    TextureResidentStreamer stream(gpu,limits());
    TextureDesc desc;
    desc.width=4;desc.height=4;desc.mipLevels=3;
    desc.dimension=TextureDimension::Texture2DArray;desc.arrayLayers=2;
    const auto id=stream.create(desc);
    require(id.valid() && !stream.sampleable(id),"arrays start unsampleable");
    SamplerDesc base,clamp;
    require(!stream.residentSampler(id,base,clamp),"absent coarsest mip lacks sampler");
    require(stream.stageNext(id,producer(11)).valid() &&
            stream.stageNext(id,producer(22)).valid(),"two coarse faces staged");
    require(stream.queuedMipCount(id)==2 && !stream.sampleable(id),
            "queued faces do not expose GPU texture");
    std::vector<int> completed;
    require(stream.drain([&](TextureStreamId result,TextureMipRequest mip,bool success){
        require(result==id && success && mip.mip==2,"coarse completion identity");
        completed.push_back(mip.layer);
        if(mip.layer==0)require(!stream.sampleable(id),"one face is incomplete");
        else require(stream.sampleable(id),"both faces now resident");
    })==2,"coarse GPU updates drained");
    require(completed.size()==2 && completed[0]==0 && completed[1]==1,
            "callback order matches staged faces");
    require(stream.residentSampler(id,base,clamp) &&
            clamp.minLod==2.f && clamp.maxLod==2.f,
            "coarse layer-complete residency clamps sampler exactly");
    require(stream.stageNext(id,producer(31)).valid() &&
            stream.stageNext(id,producer(32)).valid(),"middle faces staged");
    require(stream.drain()==2 && stream.finestResidentMip(id)==1,
            "middle mip becomes contiguous");
    require(stream.stageNext(id,producer(41)).valid() &&
            stream.stageNext(id,producer(42)).valid(),"fine faces staged");
    require(stream.drain()==1 && stream.finestResidentMip(id)==1,
            "80-byte frame budget permits one 64-byte fine face");
    require(stream.drain()==1 && stream.fullyResident(id),
            "second frame completes the fine mip and all array faces");
    require(stream.residentSampler(id,base,clamp) &&
            clamp.minLod==0.f && clamp.maxLod==2.f,
            "full residency opens all mip levels");
    require(gpu.uploadedMip.size()==6 &&
            gpu.uploadedMip[0]==2 && gpu.uploadedMip[2]==1 &&
            gpu.uploadedMip[4]==0 && gpu.uploadedLayer[5]==1 &&
            gpu.uploadedFirstByte[5]==42,"actual staged data and ordering");
    require(stream.stats().gpuAllocatedBytes==168,"all authored mip allocations budgeted");
    require(stream.destroy(id) && !stream.sampledTexture(id).valid() &&
            gpu.images.empty(),"destroy frees backing GPU image");
}
void failureCancellationAndGeneration(){
    FakeDevice gpu;
    TextureResidentStreamer stream(gpu,limits());
    TextureDesc d;
    d.width=8;d.height=8;d.mipLevels=4;
    const auto old=stream.create(d);
    require(old.valid(),"old texture allocation");
    const auto malformed=stream.stageNext(old,[](TextureMipRequest,int,int,int,
                                                std::vector<uint8_t>& bytes){
        bytes.assign(1,33);return true;
    });
    require(!malformed.valid() && !stream.sampleable(old),
            "truncated producer data rejected and request unclaimed");
    const auto rejected=stream.stageNext(old,[](TextureMipRequest,int,int,int,
                                               std::vector<uint8_t>&){
        return false;
    });
    require(!rejected.valid(),"producer failure makes mip retryable");
    const auto ticket=stream.stageNext(old,producer(73));
    require(ticket.valid() && stream.cancel(ticket),
            "cancel stage job before transfer");
    require(!stream.cancel(ticket),"double cancellation rejected");
    const auto retry=stream.stageNext(old,producer(88));
    require(retry.valid() && stream.drain()==1 &&
            gpu.uploadedFirstByte.back()==88 && stream.sampleable(old),
            "cancelled coarse mip is re-queued and deep copied");
    require(stream.destroy(old),"destroy old GPU resource");
    const auto fresh=stream.create(d);
    require(fresh.valid() && fresh.value!=old.value &&
            !stream.sampleable(fresh),"logical resource ID never reused");
    require(stream.stageNext(fresh,producer(99)).valid(),"fresh coarse request");
    gpu.failNext=true;
    bool sawFailure=false;
    require(stream.drain([&](TextureStreamId id,TextureMipRequest req,bool ok){
        sawFailure=!ok && id==fresh && req.mip==3 &&
                   !stream.sampleable(fresh);
    })==1 && sawFailure && stream.failed(fresh),"GPU upload failure quarantines resource");
    require(!stream.stageNext(fresh,producer(19)).valid() &&
            !stream.sampledTexture(fresh).valid(),"quarantined resource cannot stage or sample");
    require(stream.destroy(fresh) && gpu.images.empty(),"failed resource cleanup");
    require(gpu.destroyed==2,"each physical allocation destroyed exactly once");
}
void volumeMip(){
    FakeDevice gpu;
    TextureResidentStreamer stream(gpu,limits());
    TextureDesc d;
    d.width=4;d.height=4;d.depth=4;d.dimension=TextureDimension::Texture3D;
    d.mipLevels=3;
    auto id=stream.create(d);
    require(id.valid(),"volume allocated");
    auto provider=[](TextureMipRequest,int w,int h,int depth,std::vector<uint8_t>& bytes){
        bytes.assign(size_t(w)*size_t(h)*size_t(depth)*4,55);
        return true;
    };
    require(stream.stageNext(id,provider).valid() &&
            stream.drain()==1 && stream.sampleable(id),
            "volume coarsest mip is one complete 3D upload");
    require(stream.finestResidentMip(id)==2,"volume sampler starts at coarse mip");
    stream.shutdown();
    require(gpu.images.empty(),"shutdown reclaims GPU resources");
}
}
int main(){
    try{layeredResidencyAndBudgets();failureCancellationAndGeneration();volumeMip();}
    catch(const std::exception& e){
        std::fprintf(stderr,"TextureResidentStreamer fixture FAILED: %s\n",e.what());
        return 1;
    }
    std::puts("TextureResidentStreamer contracts: OK");return 0;
}
