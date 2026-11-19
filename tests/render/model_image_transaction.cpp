// Stage 112 deferred native test: rollback survives pending/failed GPU fence.
#include "engine/render/ModelImageUploadTransaction.h"
#include "engine/render/ModelResourceCache.h"
#include <cassert>
#include <set>
#include <string>
#include <stdexcept>
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
using namespace aiverse;
struct Device : gfx::Device {
    gfx::DeviceInfo inf{};
    std::string err;
    std::set<uint32_t> live;
    uint32_t serial=1;
    bool complete=false,failed=false,idle=false;
    int destroyed=0;
    const gfx::DeviceInfo& info() const override{return inf;}
    const std::string& lastError() const override{return err;}
    bool waitForResourceIdle() override{return idle;}
    uint64_t beginResourceRetirement() override{return 9;}
    bool resourceRetirementComplete(uint64_t) override{return complete;}
    gfx::ResourceRetirementStatus resourceRetirementStatus(uint64_t) override {
        return failed?gfx::ResourceRetirementStatus::Failed:
               complete?gfx::ResourceRetirementStatus::Complete:
                        gfx::ResourceRetirementStatus::Pending;
    }
    gfx::BufferHandle createBuffer(const gfx::BufferDesc&) override{return {};}
    void updateBuffer(gfx::BufferHandle,const void*,size_t,size_t) override{}
    void destroyBuffer(gfx::BufferHandle) override{}
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override {
        const uint32_t n=serial++;live.insert(n);return {n};
    }
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override{}
    void destroyTexture(gfx::TextureHandle h) override {
        assert(live.erase(h.id)==1);++destroyed;
    }
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override{return {};}
    void destroyShader(gfx::ShaderHandle) override{}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override{return {};}
    void destroyPipeline(gfx::PipelineHandle) override{}
    void beginFrame(int,int) override{}
    void endFrame() override{}
    void beginPass(const gfx::PassDesc&) override{}
    void endPass() override{}
    void setViewport(const gfx::Rect&) override{}
    void setScissor(const gfx::Rect&) override{}
    void setPipeline(gfx::PipelineHandle) override{}
    void setVertexBuffer(gfx::BufferHandle,size_t) override{}
    void setIndexBuffer(gfx::BufferHandle) override{}
    void setTexture(int,gfx::TextureHandle) override{}
    void setUniforms(int,const void*,size_t) override{}
    void draw(uint32_t,uint32_t) override{}
    void drawIndexed(uint32_t,uint32_t,int32_t,uint32_t) override{}
};
render::Image makeImage(const char* name) {
    render::Image img;img.name=name;img.width=img.height=1;
    img.rgba={255,128,64,255};return img;
}
int main(){
    Device device;
    render::Model first,second;
    render::ModelImageRollbackPool pool;
    render::RenderDeviceLifetime epoch(device);
    {
        render::ModelImageUploadTransaction stage(device,pool,epoch.lease());
        assert(stage.stage(first,makeImage("a.png"))==0);
        assert(stage.stage(first,makeImage("a.png"))==0);
        assert(stage.stage(second,makeImage("b.png"))==0);
        assert(first.images.empty()&&second.images.empty());
        assert(stage.pendingImages()==2);
    } // abort
    assert(first.images.empty()&&second.images.empty());
    assert(pool.pendingImages()==2&&device.destroyed==0);
    assert(!pool.collect(device)); // first fence
    assert(!pool.collect(device)); // pending
    device.failed=true;
    assert(!pool.collect(device)); // failed fence
    assert(!pool.drain(device)); // idle not yet proven
    assert(device.destroyed==0);
    device.idle=true;
    assert(pool.drain(device)); // full idle recovery
    assert(pool.pendingImages()==0&&device.destroyed==2);
    device.failed=false;device.idle=false;
    {
        render::ModelImageUploadTransaction stage(device,pool,epoch.lease());
        assert(stage.stage(first,makeImage("c.png"))==0);
        assert(stage.stage(second,makeImage("d.png"))==0);
        assert(stage.commit()==2);
    }
    assert(first.images.size()==1&&second.images.size()==1);
    assert(pool.pendingImages()==0&&device.destroyed==2);
    device.idle=true;
    render::releaseModel(device,first);
    render::releaseModel(device,second);
    assert(device.destroyed==4&&device.live.empty());
    // Stage 113: speculative Model import may reuse an ALREADY resident
    // shared texture but must never add a missing GPU key to that cache.
    auto shared=std::make_shared<render::ModelTextureCache>();
    auto resident=makeImage("shared.png");
    resident.textureKey="textures/shared.png";
    resident.textureCache=shared;
    shared->upload(device,resident);
    assert(resident.tex.valid() && shared->gpuCount()==1);
    // The importer keeps only its Model leases. Drop the temporary probe
    // lease so the cache can trim after the retired Model completes.
    resident.textureLease.reset();
    auto scratch=std::make_unique<render::Model>();
    auto hit=makeImage("shared.png");
    hit.textureKey="textures/shared.png";
    hit.textureCache=shared;
    scratch->images.push_back(std::move(hit));
    auto miss=makeImage("fresh.png");
    miss.textureKey="textures/fresh.png";
    miss.textureCache=shared;
    scratch->images.push_back(std::move(miss));
    assert(render::uploadModel(device,*scratch,false,false));
    assert(scratch->images[0].borrowedTexture);
    assert(scratch->images[0].tex==resident.tex);
    assert(!scratch->images[1].borrowedTexture);
    assert(scratch->images[1].tex.valid());
    assert(shared->gpuCount()==1); // failed import cannot pollute shared residency

    // An unpublished Model with borrowed/dedicated textures must be
    // released only after its OWN GPU completion marker, not at abort.
    render::ModelResourceCache owner;
    owner.bindResourceDevice(device,epoch.lease());
    device.idle=false;
    device.complete=false;
    const int before=device.destroyed;
    owner.retireUnpublished(device,scratch);
    assert(!scratch && owner.pendingRetirements()==1);
    assert(!owner.collectRetired(device)); // submit marker
    assert(device.destroyed==before);
    shared->trim(device); // lease held by retired Model, still resident
    assert(shared->gpuCount()==1);
    device.complete=true;
    assert(owner.collectRetired(device));
    assert(owner.pendingRetirements()==0);
    assert(device.destroyed==before+1); // dedicated miss destroyed, alias borrowed
    shared->trim(device);
    assert(shared->gpuCount()==0 && device.live.empty());

    // Commit moves scratch map nodes into the live cache without changing
    // the stable Model*; duplicate cache keys reject the whole transfer.
    render::ModelResourceCache::ModelMap stagedModels;
    auto stable=std::make_unique<render::Model>();
    auto* ptr=stable.get();
    stagedModels.emplace("models/stable.glb",std::move(stable));
    owner.publishStagedModels(device,stagedModels);
    assert(stagedModels.empty());
    assert(owner.find("models/stable.glb")==ptr);
    render::ModelResourceCache::ModelMap duplicate;
    duplicate.emplace("models/stable.glb",std::make_unique<render::Model>());
    bool rejectedDuplicate=false;
    try {owner.publishStagedModels(duplicate);}
    catch(const std::logic_error&){rejectedDuplicate=true;}
    assert(rejectedDuplicate && duplicate.size()==1);
    duplicate.clear();
    device.idle=true;
    owner.shutdown(device);

    epoch.retire();
    bool stale=false;
    try {
        render::ModelImageUploadTransaction stage(device,pool,epoch.lease());
    }catch(const std::logic_error&) {stale=true;}
    assert(stale);
}
