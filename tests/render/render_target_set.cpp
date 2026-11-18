#include "engine/gfx/RenderTargetSet.h"

#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>

using namespace aiverse::gfx;
namespace {
void check(bool yes, const char* why) {
    if (!yes) throw std::runtime_error(why);
}
class FakeDevice final : public Device {
public:
    DeviceInfo deviceInfo;
    std::string error = "fake device rejected texture";
    int created = 0, destroyed = 0, began = 0, ended = 0, rejectAfter = -1;
    std::map<uint32_t,TextureDesc> images;
    RenderTargetPassDesc lastPass;

    const DeviceInfo& info() const override { return deviceInfo; }
    const std::string& lastError() const override { return error; }
    BufferHandle createBuffer(const BufferDesc&) override { return {}; }
    void updateBuffer(BufferHandle,const void*,size_t,size_t) override {}
    void destroyBuffer(BufferHandle) override {}
    TextureHandle createTexture(const TextureDesc& d) override {
        if (validateTextureDesc(d) != TextureDescError::None ||
            (rejectAfter >= 0 && created >= rejectAfter)) return {};
        const uint32_t id = uint32_t(++created);
        images.emplace(id,d);
        return {id};
    }
    void updateTexture(TextureHandle,int,int,int,int,const void*,int) override {}
    void destroyTexture(TextureHandle h) override {
        if (images.erase(h.id)) ++destroyed;
    }
    bool supportsRenderTargetPasses() const override { return true; }
    bool beginRenderTargets(const RenderTargetPassDesc& desc) override {
        if (!validateRenderTargetPassDesc(desc)) return false;
        lastPass = desc;
        ++began;
        return true;
    }
    ShaderHandle createShader(const ShaderDesc&) override { return {}; }
    void destroyShader(ShaderHandle) override {}
    PipelineHandle createPipeline(const PipelineDesc&) override { return {}; }
    void destroyPipeline(PipelineHandle) override {}
    void beginFrame(int,int) override {}
    void endFrame() override {}
    void beginPass(const PassDesc&) override {}
    void endPass() override { ++ended; }
    void setViewport(const Rect&) override {}
    void setScissor(const Rect&) override {}
    void setPipeline(PipelineHandle) override {}
    void setVertexBuffer(BufferHandle,size_t) override {}
    void setIndexBuffer(BufferHandle,size_t) override {}
    void setTexture(int,TextureHandle) override {}
    void setUniforms(int,const void*,size_t) override {}
    void draw(uint32_t,uint32_t) override {}
    void drawIndexed(uint32_t,uint32_t,int32_t,uint32_t) override {}
};
void normal() {
    FakeDevice gpu;
    RenderTargetSet targets;
    RenderTargetSet::Config cfg;
    cfg.width = 256; cfg.height = 128;
    cfg.colorCount = 2; cfg.depth = true;
    cfg.dimension = TextureDimension::Texture2DArray;
    cfg.layers = 3; cfg.mipLevels = 3;
    check(targets.init(gpu, cfg), "init two color and depth attachments");
    check(gpu.created == 3 && gpu.images.size() == 3, "three allocations");
    check(targets.color(0).valid() && targets.color(1).valid(), "colors");
    check(targets.depth().valid(), "depth");
    check(targets.sampledColor(0) == targets.color(0), "unresolved texture");
    check(targets.begin(1, 2, true, false, {1,0,0,1}), "selected mip/layer begin");
    check(!targets.begin(1, 2), "nested begin refused");
    check(gpu.lastPass.colorCount == 2 &&
          gpu.lastPass.colors[0].mipLevel == 1 &&
          gpu.lastPass.colors[1].arrayLayer == 2 &&
          gpu.lastPass.colors[0].clearColor[0] == 1 &&
          !gpu.lastPass.depth.clear, "record selected pass");
    targets.end();
    check(gpu.ended == 1 && !targets.active(), "end closes pass");
    check(!targets.begin(3, 2) && !targets.begin(0,3), "invalid subresources refused");
    targets.shutdown();
    check(gpu.destroyed == 3 && gpu.images.empty(), "cleanup");
}
void msaa() {
    FakeDevice gpu;
    RenderTargetSet targets;
    RenderTargetSet::Config cfg;
    cfg.width = cfg.height = 64; cfg.colorCount = 2;
    cfg.sampleCount = 4; cfg.depth = true;
    check(targets.init(gpu, cfg), "msaa create");
    check(gpu.created == 5, "two color + two resolve + depth");
    check(targets.color(0) != targets.sampledColor(0), "separate resolve handle");
    check(targets.begin(), "begin multisampled resolve");
    check(gpu.lastPass.colors[0].resolveTexture == targets.sampledColor(0),
          "first color resolve assigned");
    check(gpu.lastPass.colors[1].resolveTexture == targets.sampledColor(1),
          "second color resolve assigned");
    targets.shutdown();
    check(gpu.ended == 1 && gpu.destroyed == 5, "active shutdown ends pass and releases all");
}
void rollback() {
    FakeDevice gpu;
    gpu.rejectAfter = 1;
    RenderTargetSet targets;
    RenderTargetSet::Config cfg;
    cfg.width = cfg.height = 64;
    cfg.colorCount = 2;
    check(!targets.init(gpu,cfg), "failed second allocation");
    check(gpu.images.empty() && gpu.destroyed == 1, "rollback first texture");
    check(!targets.begin(), "cannot begin after failed init");
    cfg = {};
    check(!targets.init(gpu,cfg), "invalid config rejected");
}
}
int main() {
    try {
        normal();
        msaa();
        rollback();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "RenderTargetSet contract FAILED: %s\n", e.what());
        return 1;
    }
    std::puts("RenderTargetSet ownership/rollback: OK");
    return 0;
}
