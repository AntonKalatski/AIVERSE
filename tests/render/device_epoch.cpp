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
int main() {
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
