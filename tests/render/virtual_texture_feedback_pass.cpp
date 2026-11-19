#include "engine/gfx/VirtualTextureFeedbackPass.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace aiverse::gfx;
namespace {
void require(bool ok,const char* message) {
    if (!ok) throw std::runtime_error(message);
}
class FakeDevice final : public Device {
public:
    DeviceInfo deviceInfo{};
    std::string error="mock backend refused feedback";
    std::map<uint32_t,TextureDesc> live;
    uint32_t next=1;
    int created=0,destroyed=0,started=0,ended=0;
    bool allowRender=true,allowReadback=true,active=false,rejectCreate=false;
    RenderTargetPassDesc lastPass;
    Rect lastViewport{},lastScissor{};
    TextureReadFn onReadback;

    const DeviceInfo& info() const override { return deviceInfo; }
    const std::string& lastError() const override { return error; }
    bool supports(PixelFormat f) const override { return f==PixelFormat::RGBA8; }
    bool supportsTextureDimension(TextureDimension d) const override {
        return d == TextureDimension::Texture2D;
    }
    bool supportsRenderTargetPasses() const override { return allowRender; }
    TextureHandle createTexture(const TextureDesc& desc) override {
        if (rejectCreate || validateTextureDesc(desc)!=TextureDescError::None)
            return {};
        const TextureHandle h{next++};
        live.emplace(h.id,desc);
        ++created;
        return h;
    }
    void destroyTexture(TextureHandle h) override {
        if (live.erase(h.id)) ++destroyed;
    }
    bool beginRenderTargets(const RenderTargetPassDesc& p) override {
        if (!allowRender || active || !validateRenderTargetPassDesc(p) ||
            p.colorCount!=1 || p.depth.texture.valid()) return false;
        auto it=live.find(p.colors[0].texture.id);
        if (it==live.end() || !it->second.renderTarget ||
            it->second.format!=PixelFormat::RGBA8 ||
            it->second.sampleCount!=1) return false;
        lastPass=p;active=true;++started;return true;
    }
    bool readTextureAsync(TextureHandle h,TextureReadbackDesc sub,
                          TextureReadFn callback) override {
        if (active || !allowReadback || !h.valid() ||
            !live.count(h.id) || sub.mipLevel || sub.arrayLayer ||
            !callback || onReadback) return false;
        onReadback=std::move(callback);
        return true;
    }
    void completeReadback(TextureReadbackResult result) {
        auto callback=std::move(onReadback);
        require(bool(callback),"mock pending callback exists");
        callback(std::move(result));
    }
    BufferHandle createBuffer(const BufferDesc&) override {return {};}
    void updateBuffer(BufferHandle,const void*,size_t,size_t) override {}
    void destroyBuffer(BufferHandle) override {}
    void updateTexture(TextureHandle,int,int,int,int,const void*,int) override {}
    ShaderHandle createShader(const ShaderDesc&) override {return {};}
    void destroyShader(ShaderHandle) override {}
    PipelineHandle createPipeline(const PipelineDesc&) override {return {};}
    void destroyPipeline(PipelineHandle) override {}
    void beginFrame(int,int) override {}
    void endFrame() override {}
    void beginPass(const PassDesc&) override {}
    void endPass() override {if(active){active=false;++ended;}}
    void setViewport(const Rect& r) override {lastViewport=r;}
    void setScissor(const Rect& r) override {lastScissor=r;}
    void setPipeline(PipelineHandle) override {}
    void setVertexBuffer(BufferHandle,size_t) override {}
    void setIndexBuffer(BufferHandle,size_t) override {}
    void setTexture(int,TextureHandle) override {}
    void setUniforms(int,const void*,size_t) override {}
    void draw(uint32_t,uint32_t) override {}
    void drawIndexed(uint32_t,uint32_t,int32_t,uint32_t) override {}
};
VirtualTextureFeedbackConfig feedbackCfg() {
    VirtualTextureFeedbackConfig cfg;
    cfg.tilesX=cfg.tilesY=4;
    cfg.mipLevels=3;cfg.virtualLayers=1;
    cfg.maxReadbackPixels=64;
    cfg.maxUniquePages=16;
    cfg.maxVisibleRegions=16;
    return cfg;
}
VirtualTexturePrefetchConfig prefetchCfg() {
    VirtualTexturePrefetchConfig cfg;
    cfg.tilesX=cfg.tilesY=4;
    cfg.mipLevels=3;cfg.virtualLayers=1;
    cfg.maxCandidates=64;
    cfg.maxRequestsPerFrame=4;
    return cfg;
}
void creationAndCapture() {
    FakeDevice gpu;
    VirtualTextureFeedbackPass pass;
    require(pass.initialize(gpu,4,4,feedbackCfg(),prefetchCfg()),
            "feedback pass allocates bounded RGBA8 color target");
    const auto h=pass.texture();
    require(h.valid() && gpu.created==1 && gpu.live.size()==1 &&
            gpu.live.at(h.id).format==PixelFormat::RGBA8 &&
            !gpu.live.at(h.id).mipmaps &&
            gpu.live.at(h.id).sampleCount==1,
            "one offscreen linear RGBA8, no MSAA or automatic mips");
    require(pass.begin() && pass.active() &&
            gpu.lastPass.colorCount==1 &&
            gpu.lastPass.colors[0].texture==h &&
            gpu.lastPass.colors[0].clear &&
            gpu.lastPass.colors[0].clearColor[0]==0 &&
            gpu.lastPass.colors[0].clearColor[3]==0,
            "one opaque feedback attachment cleared to invalid ID zero");
    require(gpu.lastViewport.w==4 && gpu.lastViewport.h==4 &&
            gpu.lastScissor.w==4 && gpu.lastScissor.h==4,
            "viewport and scissor restricted to feedback texture");
    require(!pass.begin(),"nested feedback pass rejected");
    require(pass.endAndCapture() && !pass.active() &&
            pass.pending() && gpu.ended==1 && bool(gpu.onReadback),
            "end render pass before asynchronous GPU feedback capture");
    require(!pass.begin() && !pass.endAndCapture(),
            "feedback target cannot be overwritten while GPU read pending");
    TextureReadbackResult read;
    read.format=PixelFormat::RGBA8;
    read.width=read.height=1;
    const uint32_t packed=encodeVirtualTextureFeedback({0,0,0,0});
    for(int i=0;i<4;++i)read.pixels.push_back(uint8_t(packed>>(i*8)));
    gpu.completeReadback(std::move(read));
    require(!pass.pending() && pass.readyToSubmit(),
            "readback completion is decoded on owner thread");
    pass.shutdown();
    require(gpu.live.empty() && gpu.destroyed==1 &&
            !pass.texture().valid() && !pass.ready(),
            "feedback attachment released at shutdown");
}
void rendererCallbackLifecycle() {
    FakeDevice gpu;
    VirtualTextureFeedbackPass pass;
    require(pass.initialize(gpu,2,2,feedbackCfg(),prefetchCfg()),
            "renderer callback pass initialized");
    int calls=0;
    require(!pass.renderAndCapture([&](Device& device) {
                require(&device==&gpu && gpu.active,
                        "renderer callback receives active target device");
                ++calls;
                return false;
            }) && calls==1 && !gpu.active && gpu.ended==1 &&
            !pass.pending() && !gpu.onReadback,
            "failed renderer callback ends pass without capturing partial IDs");
    bool threw=false;
    try {
        pass.renderAndCapture([&](Device& device) -> bool {
            require(&device==&gpu && gpu.active,
                    "throwing renderer callback receives active pass");
            ++calls;
            throw std::runtime_error("synthetic feedback draw failure");
        });
    } catch (const std::runtime_error&) {
        threw=true;
    }
    require(threw && !gpu.active && gpu.ended==2 &&
            !pass.pending() && !gpu.onReadback,
            "throwing renderer callback unwinds active pass without readback");
    require(pass.renderAndCapture([&](Device& device) {
                require(&device==&gpu && gpu.active,
                        "successful callback runs inside feedback pass");
                ++calls;
                return true;
            }) && calls==3 && !gpu.active && pass.pending() &&
            gpu.ended==3 && bool(gpu.onReadback),
            "successful renderer callback ends and schedules async readback");
    TextureReadbackResult read;
    read.format=PixelFormat::RGBA8;
    read.width=read.height=1;
    const uint32_t id=encodeVirtualTextureFeedback({0,0,0,0});
    for(int i=0;i<4;++i)read.pixels.push_back(uint8_t(id>>(8*i)));
    gpu.completeReadback(std::move(read));
    require(pass.readyToSubmit() && !pass.renderAndCapture(
                [&](Device&) { ++calls;return true; }) && calls==3,
            "unconsumed feedback cannot be overwritten by next draw");
    pass.shutdown();
    require(gpu.live.empty(),"renderer callback target released");
}

void rejectionAndWeakLifetime() {
    FakeDevice gpu;
    VirtualTextureFeedbackPass pass;
    auto fb = feedbackCfg();
    auto pf = prefetchCfg();
    require(!pass.initialize(gpu,32,32,fb,pf) &&
            gpu.created==0 && !pass.ready(),
            "feedback pixel count cannot exceed decoder budget");
    gpu.allowRender=false;
    require(!pass.initialize(gpu,2,2,fb,pf) &&
            gpu.created==0,"unsupported render-target backend refused");
    gpu.allowRender=true; gpu.rejectCreate=true;
    require(!pass.initialize(gpu,2,2,fb,pf) &&
            gpu.live.empty(),"GPU allocation refusal rolls back");
    gpu.rejectCreate=false;
    pf.virtualLayers=2;
    require(!pass.initialize(gpu,2,2,fb,pf) &&
            gpu.live.empty() && gpu.created==gpu.destroyed,
            "incompatible feedback and prefetch geometry rolls back");
    pf=prefetchCfg();
    require(pass.initialize(gpu,2,2,fb,pf) && pass.begin(),
            "recovery after failed initialization");
    gpu.allowReadback=false;
    require(!pass.endAndCapture() && !pass.active() &&
            !pass.pending() && !pass.lastError().empty(),
            "unsupported async readback fails after safely ending pass");
    gpu.allowReadback=true;
    require(pass.begin() && pass.endAndCapture(),
            "subsequent frame can retry GPU readback");
    auto callback=std::move(gpu.onReadback);
    require(bool(callback),"mock GPU callback retained before target destroy");
    pass.shutdown();
    require(gpu.live.empty() && gpu.created==gpu.destroyed &&
            gpu.destroyed>=2,
            "shutdown releases target and earlier failed-init allocation independently of callback");
    TextureReadbackResult late;
    late.format=PixelFormat::RGBA8;
    late.width=late.height=1;
    late.pixels.assign(4,0);
    callback(std::move(late)); // weak state expired; must not dereference owner
    require(!pass.ready() && !pass.pending() &&
            !pass.readyToSubmit(),"stale GPU callback after teardown is ignored");
}
}
int main() {
    try { creationAndCapture();rendererCallbackLifecycle();rejectionAndWeakLifetime(); }
    catch(const std::exception& e) {
        std::fprintf(stderr,"Virtual feedback pass contract FAILED: %s\n",e.what());
        return 1;
    }
    std::puts("Virtual feedback pass ownership and async capture: OK");
    return 0;
}
