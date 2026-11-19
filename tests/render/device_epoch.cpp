// Stage 108 deferred: explicit device generation prevents false validity
// when a GPU Device address is reused after host-controlled shutdown.
#include "engine/render/RenderResourceOwnerGroup.h"
#include "engine/render/RenderDeviceLifetime.h"
#include <cassert>
#include <stdexcept>
#include <string>
#include <thread>
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
using namespace aiverse;
class StubDevice final: public gfx::Device {
public:
    gfx::DeviceInfo d{};
    std::string error;
    const gfx::DeviceInfo& info() const override{return d;}
    const std::string& lastError() const override{return error;}
    bool waitForResourceIdle() override{return true;}
    gfx::BufferHandle createBuffer(const gfx::BufferDesc&) override{return {};}
    void updateBuffer(gfx::BufferHandle,const void*,size_t,size_t) override{}
    void destroyBuffer(gfx::BufferHandle) override{}
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override{return {};}
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override{}
    void destroyTexture(gfx::TextureHandle) override{}
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
    void setIndexBuffer(gfx::BufferHandle,size_t) override{}
    void setTexture(int,gfx::TextureHandle) override{}
    void setUniforms(int,const void*,size_t) override{}
    void draw(uint32_t,uint32_t) override{}
    void drawIndexed(uint32_t,uint32_t,int32_t,uint32_t) override{}
};
// CPU-only regressions for Stage 129-131 unique-owned Model cache leases.
// These do not assert GPU completion: StubDevice has no in-flight work.
static void verifyReadLeaseContracts() {
    StubDevice dev;
    render::ModelResourceCache cache;
    cache.bindResourceDevice(dev);
    auto model=std::make_unique<render::Model>();
    model->path="models/hero.glb";
    const render::Model* resident=cache.insertIfAbsent(
        "models/hero.glb#geometry",std::move(model));
    assert(resident);
    assert(!cache.acquireModelRead("missing.glb"));
    assert(!cache.acquireModelRead(static_cast<const render::Model*>(nullptr)));
    render::Model foreign;
    assert(!cache.acquireModelRead(&foreign));

    auto narrow=cache.acquireModelRead(resident);
    auto byKey=cache.acquireModelRead("models/hero.glb#geometry");
    assert(narrow && narrow.get()==resident && byKey.get()==resident);
    auto moved=std::move(narrow);
    assert(!narrow && moved.get()==resident);
    auto frame=cache.acquireFrameReadScope();
    assert(frame.valid() && cache.activeModelReaders()==3);
    const auto first=cache.modelReadSafetyStats();
    assert(first.peakReaders>=3);

    // A foreign thread MUST be rejected before it even reads models_.
    bool threadDenied=false;
    std::thread foreignLookup([&]{
        try {auto candidate=cache.acquireModelRead("models/hero.glb#geometry");(void)candidate;}
        catch(const std::logic_error&){threadDenied=true;}
    });
    foreignLookup.join();
    assert(threadDenied);

    unsigned callbacks=0;
    auto detach=[&](const auto&){++callbacks;};
    auto release=[&](const render::Model*){++callbacks;};
    auto denied=[&](auto&& fn){
        bool thrown=false;
        try{fn();}
        catch(const std::logic_error&){thrown=true;}
        assert(thrown);
    };
    denied([&]{cache.replace(dev,"models/hero.glb#geometry",
        std::make_unique<render::Model>(),release);});
    denied([&]{cache.invalidateSources(dev,
        std::vector<std::string>{"models/hero.glb"},detach);});
    denied([&]{cache.trimUnused(dev,{},release);});
    denied([&]{cache.shutdown(dev);});
    assert(callbacks==0);
    assert(cache.find("models/hero.glb#geometry")==resident);
    assert(cache.activeModelReaders()==3);
    assert(cache.modelReadSafetyStats().blockedMutations==4);
    assert(cache.modelReadSafetyStats().peakReaders>=3);

    frame.reset();
    moved.reset();
    byKey.reset();
    assert(cache.activeModelReaders()==0);
    cache.shutdown(dev); // no GPU buffers; empty/idle StubDevice
    assert(cache.find("models/hero.glb#geometry")==nullptr);
}
static void verifyReadOwnerRebind() {
    // The same CPU cache can be rebound on a DIFFERENT owner thread after
    // successful shutdown. No real GPU backend is accessed concurrently.
    StubDevice dev;
    render::ModelResourceCache cache;
    cache.bindResourceDevice(dev);
    {auto frame=cache.acquireFrameReadScope();assert(frame);}
    cache.shutdown(dev);
    std::thread newOwner([&]{
        cache.bindResourceDevice(dev);
        auto frame=cache.acquireFrameReadScope();
        assert(frame.valid());
        frame.reset();
        cache.shutdown(dev);
    });
    newOwner.join();
    assert(cache.activeModelReaders()==0);
}

int main() {
    verifyReadLeaseContracts();
    verifyReadOwnerRebind();
    StubDevice device;
    render::ModelResourceCache cache;
    render::ModelResourceCache hosted; // embedded GameClient shares editor epoch
    render::RenderDeviceLifetime first(device);
    const auto prior=first.lease();
    cache.bindResourceDevice(device,prior);
    hosted.bindResourceDevice(device,prior);
    assert(cache.resourceDeviceGeneration()==prior.generation());
    assert(hosted.resourceDeviceGeneration()==prior.generation());
    assert(prior.validFor(device));
    {
        render::RenderResourceOwnerGroup group;
        auto lease=group.registerScoped(prior,cache);
        auto hostedLease=group.registerScoped(prior,hosted);
        assert(group.snapshot(device).uniqueOwners==2);
        hostedLease.reset();
        lease.reset();
    }
    // Successful explicit GPU idle is required BEFORE retiring the
    // host's generation token or recreating the GPU device at this address.
    // Hosted GameClient destroys its resources first, then the owning
    // editor cache, before the ONE shared GPU generation is retired.
    hosted.shutdown(device);
    cache.shutdown(device);
    first.retire();
    assert(!prior.validFor(device));
    render::RenderDeviceLifetime next(device); // same pointer, new epoch
    const auto current=next.lease();
    assert(current.validFor(device));
    assert(current.generation()!=prior.generation());
    cache.bindResourceDevice(device,current);
    hosted.bindResourceDevice(device,current);
    assert(cache.resourceDeviceGeneration()==current.generation());
    assert(hosted.resourceDeviceGeneration()==current.generation());
    bool rejected=false;
    try {cache.bindResourceDevice(device,prior);}
    catch(const std::logic_error&) {rejected=true;}
    assert(rejected);
    hosted.shutdown(device);
    cache.shutdown(device);
}
