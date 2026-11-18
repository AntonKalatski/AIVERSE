#include "engine/gfx/VirtualTextureFileStreamer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace aiverse::gfx;
namespace {
void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
struct TestFiles {
    std::filesystem::path root;
    TestFiles() {
        root = std::filesystem::temp_directory_path() /
            ("aiverse_vt_file_streamer_" + std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
    }
    ~TestFiles() {
        std::error_code ec;
        std::filesystem::remove_all(root,ec);
    }
    void page(VirtualPageKey key, uint8_t value) {
        const auto path = root / VirtualTextureDiskQueue::relativePath(key);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path,std::ios::binary);
        require(bool(out), "create disk-backed page fixture");
        std::vector<char> bytes(64,char(value));
        out.write(bytes.data(),std::streamsize(bytes.size()));
        require(bool(out), "write disk-backed page fixture");
    }
};
class MockGpu final : public Device {
public:
    struct Resource { TextureDesc desc; std::vector<uint8_t> bytes; };
    DeviceInfo deviceInfo{};
    std::string error_ = "mock GPU upload failed";
    std::map<uint32_t,Resource> live;
    uint32_t next = 1, atlasId = 0, tableId = 0;
    bool failNextAtlas = false;
    int atlasUpdates = 0, tableUpdates = 0, destroyed = 0;
    std::vector<uint8_t> atlasValues;

    const DeviceInfo& info() const override { return deviceInfo; }
    const std::string& lastError() const override { return error_; }
    bool supports(PixelFormat f) const override { return f == PixelFormat::RGBA8; }
    bool supportsTextureDimension(TextureDimension d) const override {
        return d == TextureDimension::Texture2DArray;
    }
    BufferHandle createBuffer(const BufferDesc&) override { return {}; }
    void updateBuffer(BufferHandle,const void*,size_t,size_t) override {}
    void destroyBuffer(BufferHandle) override {}
    TextureHandle createTexture(const TextureDesc& desc) override {
        if (validateTextureDesc(desc) != TextureDescError::None) return {};
        Resource r;
        r.desc = desc;
        r.desc.data = nullptr;
        const uint32_t id = next++;
        if (desc.data) {
            const size_t bytes = size_t(desc.width)*size_t(desc.height)*
                                 size_t(desc.arrayLayers)*4;
            const auto* pixels = static_cast<const uint8_t*>(desc.data);
            r.bytes.assign(pixels,pixels+bytes);
            tableId = id;
        } else atlasId = id;
        live.emplace(id,std::move(r));
        return {id};
    }
    void updateTexture(TextureHandle,int,int,int,int,const void*,int) override {}
    bool updateTextureRegion(TextureHandle handle, const TextureUploadDesc& u) override {
        auto it = live.find(handle.id);
        require(it != live.end() && u.data && u.width>0 && u.height>0 &&
                u.arrayLayer>=0 && u.mipLevel==0,
                "valid GPU page/table RHI subresource write");
        const auto* bytes = static_cast<const uint8_t*>(u.data);
        if (handle.id == atlasId) {
            ++atlasUpdates;
            if (failNextAtlas) { failNextAtlas = false; return false; }
            atlasValues.push_back(bytes[0]);
        } else if (handle.id == tableId) {
            ++tableUpdates;
            Resource& r = it->second;
            const size_t row = size_t(r.desc.width)*4;
            require(u.width == r.desc.width && u.rowPitchBytes >= int(row) &&
                    u.arrayLayer < r.desc.arrayLayers &&
                    u.y>=0 && u.y+u.height <= r.desc.height,
                    "bounded table dirty row upload");
            for (int y=0;y<u.height;++y) {
                auto* dst = r.bytes.data() +
                    (size_t(u.arrayLayer)*size_t(r.desc.height) +
                     size_t(u.y+y))*row;
                const auto* src = bytes+size_t(y)*size_t(u.rowPitchBytes);
                std::copy(src,src+row,dst);
            }
        }
        return true;
    }
    void destroyTexture(TextureHandle handle) override {
        if (live.erase(handle.id)) ++destroyed;
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
VirtualTextureStreamConfig gpuConfig() {
    VirtualTextureStreamConfig c;
    c.tilesX = c.tilesY = 4;
    c.mipLevels = 3;
    c.virtualLayers = 1;
    c.physicalPages = 2;
    c.physicalPageSize = 4;
    c.pageTableBytesPerFlush = 64;
    return c;
}
VirtualDiskPageConfig diskConfig(const TestFiles& source) {
    VirtualDiskPageConfig c;
    c.assetRoot = source.root;
    c.tilesX = c.tilesY = 4;
    c.mipLevels = 3;
    c.virtualLayers = 1;
    c.pageSize = 4;
    c.maxReadyBytes = 128;
    c.maxOutstanding = 8;
    return c;
}
TextureStreamConfig gpuLimits() {
    TextureStreamConfig c;
    c.gpuBudgetBytes=1024;
    c.stagingBudgetBytes=256;
    c.uploadBytesPerFrame=64;
    c.uploadsPerFrame=1;
    return c;
}
template<class F> void pumpUntil(VirtualTextureFileStreamer& stream,F&& predicate) {
    const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(4);
    while (std::chrono::steady_clock::now()<deadline) {
        stream.pump(2,[&](VirtualDiskPageTicket ticket,VirtualPageKey key,
                          bool success,const std::string& error) {
            predicate(ticket,key,success,error);
        });
        if (predicate.done) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    throw std::runtime_error("disk->GPU streaming completion timeout");
}
struct Observe {
    bool done=false,success=false;
    VirtualDiskPageTicket ticket;
    VirtualPageKey key;
    std::string error;
    void operator()(VirtualDiskPageTicket t,VirtualPageKey k,bool ok,const std::string& why) {
        done=true;success=ok;ticket=t;key=k;error=why;
    }
};

void fileGpuPublishAndEviction() {
    TestFiles files;
    const VirtualPageKey coarse{2,0,0,0}, fine{0,3,3,0}, other{0,0,0,0};
    files.page(coarse,17);
    files.page(fine,38);
    files.page(other,69);
    MockGpu gpu;
    VirtualTextureFileStreamer stream(gpu,gpuLimits());
    require(stream.initialize(gpuConfig(),diskConfig(files),2),
            "disk-backed physical atlas initialization");
    require(stream.physicalAtlas().valid() && stream.pageTableTexture().valid() &&
            stream.stats().pages.capacity == 2,
            "physical atlas+GPU page table allocated");
    const auto a = stream.requestPage(coarse,true);
    require(a.valid() && stream.requestPage(coarse,true)==a,
            "coarse file request deduplicated");
    require(!stream.resolve(fine).valid(),
            "missing coarse fallback not visible before GPU upload");
    Observe event;
    pumpUntil(stream,event);
    require(event.success && event.ticket==a && event.key==coarse &&
            stream.resolve(fine).valid() && stream.encodedPageTable(3,3,0)[2]==2 &&
            gpu.atlasValues.size()==1 && gpu.atlasValues[0]==17,
            "disk page becomes resident only after GPU transfer/table publish");
    require(stream.stats().pages.pinned == 1,
            "coarsest fallback physical atlas page pinned");
    require(!stream.requestPage(coarse).valid(),
            "already resident exact page not reloaded");

    const auto b=stream.requestPage(fine);
    require(b.valid(), "request missing high-resolution tile despite coarse fallback");
    event={};
    pumpUntil(stream,event);
    require(event.success && event.ticket==b &&
            stream.resolve(fine).valid() && stream.encodedPageTable(3,3,0)[2]==0 &&
            gpu.atlasValues.size()==2 && gpu.atlasValues[1]==38,
            "fine page overrides coarse fallback when GPU upload completes");

    const auto c=stream.requestPage(other);
    require(c.valid(), "request next detail tile");
    event={};
    pumpUntil(stream,event);
    require(event.success && event.ticket==c &&
            stream.stats().pages.evictions==1 &&
            stream.encodedPageTable(3,3,0)[2]==2 &&
            stream.encodedPageTable(0,0,0)[2]==0 &&
            gpu.atlasValues.size()==3 && gpu.atlasValues[2]==69,
            "LRU reuses only unpinned page and restores coarse fallback");
    stream.shutdown();
    require(gpu.live.empty() && gpu.destroyed == 2,
            "shutdown releases atlas and page table");
}

void cancellationMissingAndGpuFailure() {
    TestFiles files;
    const VirtualPageKey valid{2,0,0,0}, invalidFile{0,1,1,0};
    files.page(valid,120);
    MockGpu gpu;
    VirtualTextureFileStreamer stream(gpu,gpuLimits());
    require(stream.initialize(gpuConfig(),diskConfig(files),2),
            "separate disk-backed atlas");
    auto canceled=stream.requestPage(valid);
    require(canceled.valid() && stream.cancel(canceled) &&
            !stream.cancel(canceled) && stream.stats().trackedRequests==0,
            "cancel disk ticket before GPU publication");
    auto missing=stream.requestPage(invalidFile);
    require(missing.valid(), "request a missing page");
    Observe event;
    pumpUntil(stream,event);
    require(!event.success && event.ticket==missing &&
            !event.error.empty() && stream.ready() &&
            !stream.resolve(invalidFile).valid(),
            "missing disk file reports failure but leaves atlas healthy");

    auto fail=stream.requestPage(valid);
    require(fail.valid(), "re-request after canceled disk read");
    gpu.failNextAtlas=true;
    event={};
    pumpUntil(stream,event);
    require(!event.success && event.ticket==fail &&
            !stream.ready() && !stream.physicalAtlas().valid() &&
            !stream.pageTableTexture().valid(),
            "failed native page upload quarantines virtual atlas");
    stream.shutdown();
    require(gpu.live.empty(), "failed facade cleanup releases GPU resources");
}

void rejectMismatchedConfig() {
    TestFiles files;
    MockGpu gpu;
    VirtualTextureFileStreamer stream(gpu,gpuLimits());
    auto disk=diskConfig(files);
    disk.pageSize=8;
    require(!stream.initialize(gpuConfig(),disk) && !stream.ready(),
            "incompatible disk and GPU page dimensions rejected before allocation");
    require(gpu.live.empty(), "invalid disk config never leaks GPU texture");
}
}
int main() {
    try {
        fileGpuPublishAndEviction();
        cancellationMissingAndGpuFailure();
        rejectMismatchedConfig();
    } catch (const std::exception& e) {
        std::fprintf(stderr,"VirtualTextureFileStreamer test FAILED: %s\n",e.what());
        return 1;
    }
    std::puts("Virtual texture disk->GPU table publication, LRU, cancellation and failure: OK");
    return 0;
}
