// Stage 15 budgeted index-only residency tests; native CTest execution
// deliberately deferred until the final shared AIVERSE build.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataIndexResidency.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::render::strata;

class IndexRecorder final : public gfx::Device {
public:
    explicit IndexRecorder(gfx::Backend backend)
        : info_{backend, true, false, "Strata residency recorder"} {}
    gfx::DeviceInfo info_{};
    std::string error_;
    uint32_t next_ = 1;
    bool healthy_ = true, failNext_ = false;
    uint32_t allocations_ = 0, destroyed_ = 0;
    std::map<uint32_t, std::vector<uint8_t>> buffers_;
    const gfx::DeviceInfo& info() const override { return info_; }
    const std::string& lastError() const override { return error_; }
    bool gpuHealthy() const override { return healthy_; }
    gfx::BufferHandle createBuffer(const gfx::BufferDesc& d) override {
        if (failNext_) { failNext_ = false; return {}; }
        if (d.type != gfx::BufferType::Index ||
            d.usage != gfx::Usage::Immutable || !d.size || !d.data)
            return {};
        gfx::BufferHandle h{next_++};
        std::vector<uint8_t> data(d.size);
        std::memcpy(data.data(), d.data, d.size);
        buffers_[h.id] = std::move(data);
        ++allocations_;
        return h;
    }
    void updateBuffer(gfx::BufferHandle, const void*, size_t, size_t = 0) override {
        assert(false && "Immutable STRATA indices must never be updated in place");
    }
    void destroyBuffer(gfx::BufferHandle h) override {
        if (buffers_.erase(h.id)) ++destroyed_;
    }
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override { return {}; }
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override {}
    void destroyTexture(gfx::TextureHandle) override {}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override { return {}; }
    void destroyShader(gfx::ShaderHandle) override {}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override { return {}; }
    void destroyPipeline(gfx::PipelineHandle) override {}
    void beginFrame(int,int) override {}
    void endFrame() override {}
    void beginPass(const gfx::PassDesc&) override {}
    void endPass() override {}
    void setViewport(const gfx::Rect&) override {}
    void setScissor(const gfx::Rect&) override {}
    void setPipeline(gfx::PipelineHandle) override {}
    void setVertexBuffer(gfx::BufferHandle,size_t=0) override {}
    void setIndexBuffer(gfx::BufferHandle,size_t=0) override {}
    void setTexture(int,gfx::TextureHandle) override {}
    void setUniforms(int,const void*,size_t) override {}
    void draw(uint32_t,uint32_t=0) override {}
    void drawIndexed(uint32_t,uint32_t=0,int32_t=0,uint32_t=1) override {}
};

static ModelAsset twoPrimitives() {
    ModelAsset a;
    for (uint32_t p = 0; p < 2; ++p) {
        ModelPrimitiveAsset entry{};
        entry.mesh = p;
        entry.material = p;
        auto& g = entry.geometry;
        g.roots = {0};
        g.nodes.resize(3);
        g.bounds.resize(3);
        g.nodes[0].firstChild = 1;
        g.nodes[0].childCount = 2;
        g.nodes[0].firstIndex = 0;
        g.nodes[0].indexCount = 6;
        g.nodes[0].geometricError = 1.0f;
        g.nodes[1].firstIndex = 6;
        g.nodes[1].indexCount = 3;
        g.nodes[2].firstIndex = 9;
        g.nodes[2].indexCount = 3;
        for (auto& n : g.nodes) {
            n.resident = true;
            n.materialGroup = p;
        }
        for (auto& b : g.bounds)
            b = {{-1.f,-1.f,-1.f},{1.f,1.f,1.f}};
        g.indices = {0,1,2,1,3,2,0,1,2,1,3,2};
        a.primitives.push_back(std::move(entry));
    }
    return a;
}
static void exercise(gfx::Backend backend) {
    IndexRecorder dev(backend);
    ModelAsset asset = twoPrimitives();
    IndexResidency cache;
    std::string error;

    // Two roots consume 48 bytes; one full sibling promotion costs 24.
    assert(!cache.initialize(dev,asset,47,&error));
    assert(!error.empty() && dev.allocations_ == 0);
    assert(cache.initialize(dev,asset,72,&error));
    assert(error.empty());
    assert(cache.valid());
    assert(cache.stats().pinnedRootBytes == 48);
    assert(cache.stats().activeBytes == 48);
    assert(cache.resident(0,0) && cache.resident(1,0));
    assert(!cache.resident(0,1) && !cache.resident(1,1));
    assert(dev.buffers_.size()==1);

    assert(cache.update({{0,1,1.0f},{1,1,10.f}},&error));
    assert(cache.resident(1,1) && cache.resident(1,2));
    assert(!cache.resident(0,1) && !cache.resident(0,2));
    assert(cache.stats().activeBytes==72);
    assert(cache.stats().deferredDemands>0);
    assert(dev.buffers_.size()==1);
    assert(cache.span(1,1).indexCount==3);
    const auto packed=dev.buffers_.at(cache.buffer().id);
    assert(packed.size()==72);
    const uint32_t creations=dev.allocations_;
    assert(cache.update({{1,1,10.f}},&error));
    assert(dev.allocations_==creations); // same residency, no repack

    // Deliberate allocation failure cannot partially publish or evict.
    const auto oldBuffer=cache.buffer();
    dev.failNext_=true;
    assert(!cache.update({{0,1,9.f}},&error));
    assert(cache.buffer()==oldBuffer);
    assert(cache.resident(1,1) && !cache.resident(0,1));
    assert(cache.stats().failedUploads==1);

    // Successful switch changes allocation (immutable generations), evicts
    // unused descendants, and keeps both pinned roots as fallbacks.
    assert(cache.update({{0,1,9.f}},&error));
    assert(cache.buffer()!=oldBuffer);
    assert(cache.resident(0,1) && cache.resident(0,2));
    assert(!cache.resident(1,1) && cache.resident(1,0));
    assert(cache.stats().evictions>=2);
    assert(dev.buffers_.size()==1);
    assert(cache.update({},&error));
    assert(cache.stats().activeBytes==48);
    assert(cache.resident(0,0) && cache.resident(1,0));
    assert(!cache.resident(0,1));

    // Bad demand and unhealthy device cannot change a live cache.
    const auto rootHandle=cache.buffer();
    assert(!cache.update({{4,0,1.f}},&error));
    assert(cache.buffer()==rootHandle);
    dev.healthy_=false;
    assert(!cache.update({{0,1,10.f}},&error));
    assert(!cache.valid());
    dev.healthy_=true;
    IndexRecorder foreign(backend);
    cache.shutdown(foreign);
    assert(cache.buffer()==rootHandle && foreign.destroyed_==0);
    cache.shutdown(dev);
    assert(dev.buffers_.empty());
}
int main() {
    exercise(gfx::Backend::Metal);
    exercise(gfx::Backend::Vulkan);
    exercise(gfx::Backend::GLES3);
    exercise(gfx::Backend::GL33);
    return 0;
}
