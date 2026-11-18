#include "engine/gfx/VirtualTexturePageTable.h"

#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace aiverse::gfx;
namespace {
void require(bool good, const char* reason) {
    if (!good) throw std::runtime_error(reason);
}
struct Upload {
    int layer = -1, y = -1, height = 0;
    std::vector<uint8_t> bytes;
};
class MockGpu final : public Device {
public:
    DeviceInfo deviceInfo{};
    std::string error = "mock upload rejected";
    std::vector<Upload> uploaded;
    std::vector<uint8_t> initial;
    bool failNext = false;

    const DeviceInfo& info() const override { return deviceInfo; }
    const std::string& lastError() const override { return error; }
    bool supportsTextureDimension(TextureDimension) const override { return true; }
    BufferHandle createBuffer(const BufferDesc&) override { return {}; }
    void updateBuffer(BufferHandle, const void*, size_t, size_t) override {}
    void destroyBuffer(BufferHandle) override {}
    TextureHandle createTexture(const TextureDesc& d) override {
        require(validateTextureDesc(d) == TextureDescError::None &&
                d.dimension == TextureDimension::Texture2DArray &&
                d.data != nullptr && d.format == PixelFormat::RGBA8,
                "create 2DArray table with initial page bytes");
        const auto* p = static_cast<const uint8_t*>(d.data);
        initial.assign(p, p + size_t(d.width) * size_t(d.height) *
                           size_t(d.arrayLayers) * 4);
        return {17};
    }
    void updateTexture(TextureHandle, int, int, int, int, const void*, int) override {}
    void destroyTexture(TextureHandle) override {}
    bool updateTextureRegion(TextureHandle handle, const TextureUploadDesc& d) override {
        require(handle.id == 17 && d.width == 4 && d.mipLevel == 0 &&
                d.rowPitchBytes == 16 && d.slicePitchBytes == 16 * d.height,
                "page-table update format, row and slice pitches");
        if (failNext) { failNext = false; return false; }
        const auto* bytes = static_cast<const uint8_t*>(d.data);
        uploaded.push_back({d.arrayLayer,d.y,d.height,
                            std::vector<uint8_t>(bytes,bytes + d.slicePitchBytes)});
        return true;
    }
    ShaderHandle createShader(const ShaderDesc&) override { return {}; }
    void destroyShader(ShaderHandle) override {}
    PipelineHandle createPipeline(const PipelineDesc&) override { return {}; }
    void destroyPipeline(PipelineHandle) override {}
    void beginFrame(int,int) override {}
    void endFrame() override {}
    void beginPass(const PassDesc&) override {}
    void endPass() override {}
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

void pageTableAndDirtyRows() {
    VirtualTexturePageCache cache;
    VirtualTexturePageTable table;
    require(cache.initialize(4,4,3,2,4) && table.initialize(4,4,3,2),
            "initialize physical cache and CPU page table");
    require(table.pendingRows() == 8, "initial table marked dirty");
    MockGpu gpu;
    const TextureHandle gpuTable = table.createGpuTexture(gpu);
    require(gpuTable.valid() && gpu.initial.size() == 128 &&
            table.pendingRows() == 0 && gpu.initial[3] == 0,
            "allocated page table initialized invalid");
    const VirtualPageKey root0{2,0,0,0}, root1{2,0,0,1};
    const auto a0 = cache.reserve(root0,true), a1 = cache.reserve(root1,true);
    require(a0.valid() && a1.valid() && cache.complete(root0,a0,true) &&
            cache.complete(root1,a1,true), "two pinned coarse root pages resident");
    require(table.rebuild(cache) == 32 && table.pendingRows() == 8,
            "all two layers become mapped and dirty");
    auto pixel0 = table.entry(3,3,0), pixel1 = table.entry(3,3,1);
    require(pixel0[0] == uint8_t(a0.physicalSlice) &&
            pixel0[2] == 2 && pixel0[3] == 255 &&
            pixel1[0] == uint8_t(a1.physicalSlice) &&
            pixel1[2] == 2 && pixel1[3] == 255,
            "RGBA8 table stores physical page index and coarse mip");
    require(table.flush(gpu,gpuTable,15) == 0 &&
            table.pendingRows() == 8, "budget smaller than a row leaves dirty data");
    require(table.flush(gpu,gpuTable,16) == 16 &&
            table.pendingRows() == 7 &&
            gpu.uploaded.size() == 1 &&
            gpu.uploaded[0].layer == 0 &&
            gpu.uploaded[0].y == 0 &&
            gpu.uploaded[0].height == 1,
            "bounded first row GPU upload");
    gpu.failNext = true;
    require(table.flush(gpu,gpuTable,64) == 0 &&
            table.pendingRows() == 7,
            "GPU failure preserves dirty page-table rows");
    require(table.flush(gpu,gpuTable,64) == 64 &&
            table.pendingRows() == 3,
            "retry flush makes progress under four-row byte budget");
    require(table.flush(gpu,gpuTable,64) == 48 &&
            table.pendingRows() == 0,
            "remaining rows flushed next frame");
    require(table.rebuild(cache) == 0 && table.pendingRows() == 0,
            "unchanged resident data causes no uploads");

    const VirtualPageKey detailed{0,0,0,1};
    auto a2 = cache.reserve(detailed);
    require(a2.valid() && cache.complete(detailed,a2,true),
            "layer-one detailed physical page resident");
    require(table.rebuild(cache) == 1 && table.pendingRows() == 1,
            "single changed lookup dirties only one row");
    auto fine = table.entry(0,0,1);
    require(fine[0] == uint8_t(a2.physicalSlice) &&
            fine[2] == 0 && fine[3] == 255,
            "page table prefers the fine physical page over a coarse fallback");
    require(table.flush(gpu,gpuTable,16) == 16 &&
            table.pendingRows() == 0, "dirty row uploaded in one chunk");
    require(cache.pin(root0,false) && cache.evict(root0),
            "explicitly evict formerly pinned coarse root");
    require(table.rebuild(cache) == 16 && table.pendingRows() == 4,
            "all layer-zero tiles lose their page mapping");
    require(table.entry(3,3,0)[3] == 0 &&
            table.entry(3,3,1)[3] == 255,
            "invalid page table lookup never bleeds across virtual layers");
}

void invalidGeometry() {
    VirtualTexturePageTable table;
    require(!table.initialize(0,4,1,1) &&
            !table.initialize(4,4,0,1) &&
            !table.initialize(4,4,1,0),
            "invalid page-table dimensions rejected");
    require(table.initialize(2,2,2,1), "valid table");
    VirtualTexturePageCache wrong;
    require(wrong.initialize(4,4,2,1,2), "wrong-size cache");
    require(table.rebuild(wrong) == 0, "mismatched cache geometry cannot corrupt page table");
    require(table.entry(-1,0,0)[3] == 0 &&
            table.entry(0,0,4)[3] == 0,
            "out-of-range inspection is safely invalid");
}
}

int main() {
    try {
        pageTableAndDirtyRows();
        invalidGeometry();
    } catch (const std::exception& e) {
        std::fprintf(stderr,"VirtualTexturePageTable test FAILED: %s\n",e.what());
        return 1;
    }
    std::puts("Virtual page-table encoding and bounded GPU-row uploads: OK");
    return 0;
}
