// Stage 112 deferred native test: rollback survives pending/failed GPU fence.
#include "engine/render/ModelImageUploadTransaction.h"
#include "engine/render/ModelResourceCache.h"
#include "engine/render/ModelStagingTransaction.h"
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
    try {owner.publishStagedModels(device,duplicate);}
    catch(const std::logic_error&){rejectedDuplicate=true;}
    assert(rejectedDuplicate && duplicate.size()==1);
    duplicate.clear();
    device.idle=true;
    owner.shutdown(device);

    // Stage 114: two speculative Models upload the SAME missing GPU source.
    // Preparing a shared-cache plan must not change global GPU residency;
    // committing adopts one already-uploaded image and returns the other
    // dedicated handle for a real GPU-fenced retirement batch.
    render::Model left,right;
    auto leftImage=makeImage("atlas.png");
    leftImage.textureKey="textures/new-atlas.png";
    leftImage.textureCache=shared;
    auto rightImage=makeImage("atlas.png");
    rightImage.textureKey="textures/new-atlas.png";
    rightImage.textureCache=shared;
    assert(render::uploadStandaloneImage(device,leftImage));
    assert(render::uploadStandaloneImage(device,rightImage));
    assert(leftImage.tex!=rightImage.tex);
    left.images.push_back(std::move(leftImage));
    right.images.push_back(std::move(rightImage));
    assert(shared->gpuCount()==0);
    auto planned=shared->prepareGpuPromotion({&left,&right});
    assert(planned.newSharedTextures()==1 && planned.bindings()==2);
    assert(shared->gpuCount()==0);
    assert(!left.images[0].borrowedTexture && !right.images[0].borrowedTexture);
    auto duplicates=planned.commit();
    assert(shared->gpuCount()==1 && duplicates.size()==1);
    assert(left.images[0].borrowedTexture && right.images[0].borrowedTexture);
    assert(left.images[0].tex==right.images[0].tex);
    assert(left.images[0].textureLease==right.images[0].textureLease);
    const auto duplicateId=duplicates.front().tex.id;
    const int destroyedBeforePromotion=device.destroyed;
    device.complete=false;device.idle=false;
    pool.retire(device,std::move(duplicates));
    assert(pool.pendingImages()==1);
    assert(!pool.collect(device));
    assert(device.live.contains(duplicateId));
    device.complete=true;
    assert(pool.collect(device));
    assert(!device.live.contains(duplicateId));
    assert(device.destroyed==destroyedBeforePromotion+1);
    render::releaseModel(device,left);
    render::releaseModel(device,right);
    assert(shared->gpuCount()==1); // lease owner still resident
    shared->trim(device);
    assert(shared->gpuCount()==0 && device.live.empty());

    // Aborted promotion plan never published cache entries or changed
    // Model ownership; the staged Model's normal rollback path still owns
    // and retires its dedicated GPU texture.
    render::Model aborted;
    auto abortedImage=makeImage("reject.png");
    abortedImage.textureKey="textures/reject.png";
    abortedImage.textureCache=shared;
    assert(render::uploadStandaloneImage(device,abortedImage));
    aborted.images.push_back(std::move(abortedImage));
    {
        auto uncommitted=shared->prepareGpuPromotion({&aborted});
        assert(uncommitted.newSharedTextures()==1);
    }
    assert(shared->gpuCount()==0);
    assert(!aborted.images[0].borrowedTexture);
    device.idle=true;
    render::releaseModel(device,aborted);
    assert(device.live.empty());

    // Stage 114: material images are still outside Model::images when
    // the shared-promotion plan is prepared. One plan must support their
    // FUTURE append indices and deduplicate the same source across Models.
    render::Model materialA,materialB;
    device.complete=false;device.idle=false;
    {
        render::ModelImageUploadTransaction tx(device,pool,epoch.lease());
        auto a=makeImage("mat.png");
        a.textureKey="textures/material.png";a.textureCache=shared;
        auto b=makeImage("mat.png");
        b.textureKey="textures/material.png";b.textureCache=shared;
        assert(tx.stage(materialA,std::move(a))==0);
        assert(tx.stage(materialB,std::move(b))==0);
        const auto refs=tx.pendingImageReferences();
        assert(refs.size()==2);
        std::vector<render::ModelTextureCache::PendingImage> proposed;
        for(const auto& ref:refs)
            proposed.push_back({ref.model,ref.appendIndex,ref.staged});
        auto promotion=shared->prepareGpuPromotion({},proposed);
        assert(promotion.newSharedTextures()==1 && promotion.bindings()==2);
        assert(shared->gpuCount()==0);
        assert(materialA.images.empty() && materialB.images.empty());
        assert(tx.commit()==2);
        auto duplicates=promotion.commit();
        assert(duplicates.size()==1);
        assert(shared->gpuCount()==1);
        assert(materialA.images[0].borrowedTexture);
        assert(materialB.images[0].borrowedTexture);
        assert(materialA.images[0].tex==materialB.images[0].tex);
        pool.retire(device,std::move(duplicates));
    }
    assert(!pool.collect(device));
    device.complete=true;
    assert(pool.collect(device));
    render::releaseModel(device,materialA);
    render::releaseModel(device,materialB);
    shared->trim(device);
    assert(shared->gpuCount()==0 && device.live.empty());

    // A scratch Model can receive new authored material images in the
    // SAME transaction. Its Model::images vector may relocate when the
    // material append reserves space; promotion bindings must address
    // Model+index rather than dangling Image* pointers.
    render::Model mixed;
    auto original=makeImage("original.png");
    original.textureKey="textures/original.png";
    original.textureCache=shared;
    assert(render::uploadStandaloneImage(device,original));
    mixed.images.push_back(std::move(original));
    mixed.images.shrink_to_fit();
    {
        render::ModelImageUploadTransaction tx(device,pool,epoch.lease());
        auto extra=makeImage("override.png");
        extra.textureKey="textures/override.png";
        extra.textureCache=shared;
        assert(tx.stage(mixed,std::move(extra))==1);
        std::vector<render::ModelTextureCache::PendingImage> proposed;
        for(const auto& ref:tx.pendingImageReferences())
            proposed.push_back({ref.model,ref.appendIndex,ref.staged});
        auto plan=shared->prepareGpuPromotion({&mixed},proposed);
        assert(plan.newSharedTextures()==2 && plan.bindings()==2);
        assert(tx.commit()==1);
        auto redundant=plan.commit();
        assert(redundant.empty());
        assert(mixed.images.size()==2);
        assert(mixed.images[0].borrowedTexture);
        assert(mixed.images[1].borrowedTexture);
        assert(shared->gpuCount()==2);
    }
    device.idle=true;
    render::releaseModel(device,mixed);
    shared->trim(device);
    assert(shared->gpuCount()==0 && device.live.empty());

    // Generic native Model staging: a successful import publishes the
    // unchanged Model* and a shared GPU texture only after commit().
    render::ModelResourceCache native;
    render::ModelImageRollbackPool nativeRollback;
    native.bindResourceDevice(device,epoch.lease());
    auto nativeModel=std::make_unique<render::Model>();
    nativeModel->path="models/native.glb";
    auto nativeImage=makeImage("native.png");
    nativeImage.textureKey="textures/native.png";
    nativeImage.textureCache=native.textures();
    nativeModel->images.push_back(std::move(nativeImage));
    render::Model* nativePointer=nativeModel.get();
    {
        render::ModelStagingTransaction tx(
            device,native,nativeRollback,epoch.lease());
        assert(tx.stage(nativeModel->path,std::move(nativeModel))==nativePointer);
        assert(!native.find("models/native.glb"));
        assert(native.textures()->gpuCount()==0);
        assert(tx.commit()==1);
    }
    assert(native.find("models/native.glb")==nativePointer);
    assert(native.textures()->gpuCount()==1);
    assert(nativePointer->images[0].borrowedTexture);

    // Aborted native import never becomes a cache tombstone and holds
    // partially uploaded images until the Model's own completion fence.
    device.complete=false;device.idle=false;
    {
        render::ModelStagingTransaction tx(
            device,native,nativeRollback,epoch.lease());
        auto invalid=std::make_unique<render::Model>();
        invalid->path="models/cancelled.glb";
        auto pic=makeImage("cancelled.png");
        pic.textureKey="textures/cancelled.png";
        pic.textureCache=native.textures();
        invalid->images.push_back(std::move(pic));
        assert(tx.stage("models/cancelled.glb",std::move(invalid)));
        assert(!native.contains("models/cancelled.glb"));
    }
    assert(!native.contains("models/cancelled.glb"));
    assert(native.pendingRetirements()==1);
    assert(native.textures()->gpuCount()==1); // no aborted GPU-cache key
    assert(!native.collectRetired(device));
    device.complete=true;
    assert(native.collectRetired(device));
    assert(native.pendingRetirements()==0);
    device.idle=true;
    native.shutdown(device);
    assert(device.live.empty());

    epoch.retire();
    bool stale=false;
    try {
        render::ModelImageUploadTransaction stage(device,pool,epoch.lease());
    }catch(const std::logic_error&) {stale=true;}
    assert(stale);
}
