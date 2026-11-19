#include "engine/gfx/VirtualTextureFileStreamer.h"
#include "engine/gfx/VirtualTexturePrefetchAdapter.h"
#include "engine/gfx/VirtualTextureFeedbackRuntime.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
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
    bool rejectFeedback = false;
    TextureReadFn feedbackCompletion;
    bool readTextureAsync(TextureHandle handle, TextureReadbackDesc,
                          TextureReadFn done) override {
        if (rejectFeedback || !handle.valid() || !done || feedbackCompletion)
            return false;
        feedbackCompletion=std::move(done);
        return true;
    }
    void finishFeedback(TextureReadbackResult result) {
        auto callback=std::move(feedbackCompletion);
        // A moved-from std::function is valid but may hold an unspecified
        // target. Explicitly release the mock's one-outstanding-readback
        // slot before delivering the callback, as an actual device does.
        feedbackCompletion = {};
        require(bool(callback),"synthetic pending feedback callback exists");
        callback(std::move(result));
    }

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


void automaticPrefetchAndPinPromotion() {
    TestFiles files;
    const VirtualPageKey coarse{2,0,0,0}, middle{1,1,1,0}, fine{0,3,3,0};
    files.page(coarse,13);
    files.page(middle,37);
    files.page(fine,91);
    MockGpu gpu;
    VirtualTextureFileStreamer stream(gpu,gpuLimits());
    auto atlas=gpuConfig();
    atlas.physicalPages=3; // no forced LRU eviction of visible mips
    require(stream.initialize(atlas,diskConfig(files),3),"prefetch streaming init");
    VirtualTexturePrefetchPlanner planner;
    VirtualTexturePrefetchConfig cfg;
    cfg.tilesX=cfg.tilesY=4;
    cfg.mipLevels=3; cfg.virtualLayers=1;
    cfg.maxCandidates=16;cfg.maxRequestsPerFrame=2;
    require(planner.initialize(cfg),"prefetch planner init");
    const std::vector<VirtualTextureVisibleRegion> visible{
        {0.75,0.75,1.,1.,0,0,9}
    };
    auto first=prefetchVisibleVirtualPages(stream,planner,visible);
    require(first.submittedNewRequests==2 &&
            first.tickets.size()==2 && first.refusedRequests==0,
            "coarse then intermediate page requests fit the first frame");
    require(stream.pendingPage(coarse) && stream.pendingPage(middle) &&
            !stream.pendingPage(fine),
            "request budget stops before the finest tile");
    auto next=prefetchVisibleVirtualPages(stream,planner,visible);
    require(next.submittedNewRequests==1 && stream.pendingPage(fine) &&
            next.planning.alreadyResident==2,
            "pending pages are deduplicated; next frame requests only detail");
    auto none=prefetchVisibleVirtualPages(stream,planner,visible);
    require(none.submittedNewRequests==0 && none.planning.alreadyResident==3,
            "no reread while disk or GPU writes are still pending");
    struct Watch {
        int count=0;
        bool done=false;
        void operator()(VirtualDiskPageTicket,VirtualPageKey,
                        bool ok,const std::string& error) {
            require(ok && error.empty(),"visibility page upload success");
            if(++count==3)done=true;
        }
    } watch;
    pumpUntil(stream,watch);
    require(stream.exactResidentPage(coarse) &&
            stream.exactResidentPage(middle) &&
            stream.exactResidentPage(fine) &&
            stream.stats().pages.pinned==1,
            "all visible mip levels resident, only coarse fallback pinned");
    auto resident=prefetchVisibleVirtualPages(stream,planner,visible);
    require(resident.submittedNewRequests==0 &&
            resident.planning.alreadyResident==3,
            "actual resident pages do not trigger more disk I/O");
    stream.shutdown();
    require(gpu.live.empty(),"prefetch completes and releases GPU allocations");

    // A coarse request created by another presentation subsystem may already
    // be pending, but a new view must still promote it to pinned fallback.
    MockGpu secondGpu;
    VirtualTextureFileStreamer second(secondGpu,gpuLimits());
    require(second.initialize(gpuConfig(),diskConfig(files),2),
            "separate pending pin promotion fixture");
    const auto weak=second.requestPage(coarse,false);
    require(weak.valid() && second.pendingPage(coarse),"un-pinned request pending");
    auto promotion=prefetchVisibleVirtualPages(second,planner,visible);
    require(promotion.planning.alreadyResident >= 1,
            "pending coarse page filtered and promoted");
    Observe promoted;
    pumpUntil(second,promoted);
    require(promoted.success && promoted.key==coarse &&
            second.stats().pages.pinned==1,
            "coarsest page promoted before GPU residency publication");
    second.shutdown();
}


void asynchronousGpuFeedbackRuntime() {
    TestFiles files;
    const VirtualPageKey coarse{2,0,0,0}, middle{1,1,1,0}, fine{0,3,3,0};
    files.page(coarse,13);
    files.page(middle,37);
    files.page(fine,91);
    MockGpu gpu;
    VirtualTextureFileStreamer stream(gpu,gpuLimits());
    auto atlas=gpuConfig();
    atlas.physicalPages=3;
    require(stream.initialize(atlas,diskConfig(files),3),
            "feedback atlas and disk queue initialize");
    VirtualTextureFeedbackConfig feedback;
    feedback.tilesX=feedback.tilesY=4;
    feedback.mipLevels=3; feedback.virtualLayers=1;
    feedback.maxReadbackPixels=16;
    feedback.maxUniquePages=4;feedback.maxVisibleRegions=4;
    VirtualTexturePrefetchConfig prefetch;
    prefetch.tilesX=prefetch.tilesY=4;
    prefetch.mipLevels=3;prefetch.virtualLayers=1;
    prefetch.maxCandidates=16;prefetch.maxRequestsPerFrame=2;
    VirtualTextureFeedbackRuntime runtime;
    require(runtime.initialize(gpu,feedback,prefetch),
            "matching feedback and prefetch geometry initialized");
    const auto target=stream.pageTableTexture(); // mock accepts any live handle
    require(runtime.capture(target) && runtime.pending() &&
            !runtime.capture(target),
            "feedback capture accepted once until next-frame completion");
    TextureReadbackResult readback;
    readback.format=PixelFormat::RGBA8;
    readback.width=2;readback.height=1;
    readback.pixels.resize(8);
    const uint32_t id=encodeVirtualTextureFeedback(fine);
    for(size_t pixel=0;pixel<2;++pixel)
        for(int channel=0;channel<4;++channel)
            readback.pixels[pixel*4+size_t(channel)]=
                uint8_t((id>>(channel*8))&255u);
    gpu.finishFeedback(readback); // simulates owner-thread beginFrame delivery
    require(!runtime.pending() && runtime.readyToSubmit() &&
            runtime.lastFeedbackStats().uniquePages==1,
            "deferred feedback decoded exactly once");
    require(!gpu.feedbackCompletion,
            "mock readback completion releases the outstanding callback slot");
    const auto submitted=runtime.submit(stream);
    require(submitted.submittedNewRequests==2 &&
            stream.pendingPage(coarse) && stream.pendingPage(middle) &&
            !stream.pendingPage(fine),
            "feedback drives only budgeted coarse and middle disk requests");
    require(!runtime.readyToSubmit(),
            "feedback readback consumed once, while missing pages can continue");
    const auto nextFrame=runtime.submit(stream);
    require(nextFrame.submittedNewRequests==1 &&
            stream.pendingPage(fine),
            "deferred GPU feedback requests fine mip on following frame");
    const auto completed=runtime.submit(stream);
    require(completed.submittedNewRequests==0,
            "no duplicate disk requests after all visible mips are pending");
    require(runtime.capture(target),"capture next frame after consuming result");
    const auto pending=std::move(gpu.feedbackCompletion);
    require(bool(pending),"second GPU callback remains in flight");
    runtime.shutdown(); // user closes the preview/streamer before completion
    pending(std::move(readback)); // weak state must not publish/use a dangling this
    require(!runtime.pending() && !runtime.readyToSubmit(),
            "stale GPU callback after owner destruction is harmless");

    // A one-frame retry budget must never dispatch detail pages in later frames.
    require(runtime.initialize(gpu,feedback,prefetch,1),
            "single-frame deferred budget accepted");
    require(runtime.capture(target),"capture with single-frame budget");
    // The earlier shutdown fixture moved its payload into the stale callback;
    // create a fresh readback rather than reusing a moved-from pixel vector.
    TextureReadbackResult boundedReadback;
    boundedReadback.format=PixelFormat::RGBA8;
    boundedReadback.width=2;
    boundedReadback.height=1;
    boundedReadback.pixels.resize(8);
    for (size_t pixel=0;pixel<2;++pixel)
        for (int channel=0;channel<4;++channel)
            boundedReadback.pixels[pixel*4+size_t(channel)]=
                uint8_t((id>>(channel*8))&255u);
    gpu.finishFeedback(std::move(boundedReadback));
    const auto bounded=runtime.submit(stream);
    require(bounded.submittedNewRequests==0,
            "existing pending pages remain deduplicated");
    const auto expired=runtime.submit(stream);
    require(expired.submittedNewRequests==0,
            "expired visibility does not submit additional pages");
    runtime.shutdown();
    require(!runtime.initialize(gpu,feedback,prefetch,0) &&
            !runtime.lastError().empty(),
            "zero deferred-frame budget rejected");
    require(!runtime.initialize(gpu,feedback,prefetch,1025) &&
            !runtime.lastError().empty(),
            "unbounded deferred-frame budget rejected");

    require(runtime.initialize(gpu,feedback,prefetch),"recreate runtime");
    gpu.rejectFeedback=true;
    require(!runtime.capture(target) && !runtime.pending() &&
            !runtime.lastError().empty(),
            "unsupported device readback refuses capture without inflight state");
    gpu.rejectFeedback=false;
    require(runtime.capture(target),"GPU feedback ready after earlier refusal");
    TextureReadbackResult invalid;
    invalid.format=PixelFormat::RGBA8_sRGB;
    invalid.width=invalid.height=1;
    invalid.pixels.assign(4,0);
    gpu.finishFeedback(std::move(invalid));
    require(!runtime.pending() && !runtime.readyToSubmit() &&
            !runtime.lastError().empty(),
            "sRGB feedback would corrupt integer IDs and is rejected");
    runtime.shutdown();
    prefetch.virtualLayers=2;
    require(!runtime.initialize(gpu,feedback,prefetch) &&
            !runtime.readyToSubmit(),"mismatched geometry fails closed");
    stream.shutdown();
    require(gpu.live.empty(),"feedback test releases owned GPU objects");
}

void cameraFeedbackReplacesDeferredVisibility() {
    TestFiles files;
    const VirtualPageKey oldFine{0,3,3,0}, newFine{0,0,0,0};
    MockGpu gpu;
    VirtualTextureFileStreamer stream(gpu,gpuLimits());
    auto atlas=gpuConfig();
    atlas.physicalPages=3;
    require(stream.initialize(atlas,diskConfig(files),3),
            "camera replacement atlas initialized");
    VirtualTextureFeedbackConfig feedback;
    feedback.tilesX=feedback.tilesY=4;
    feedback.mipLevels=3;
    feedback.maxReadbackPixels=16;
    feedback.maxUniquePages=4;
    feedback.maxVisibleRegions=4;
    VirtualTexturePrefetchConfig prefetch;
    prefetch.tilesX=prefetch.tilesY=4;
    prefetch.mipLevels=3;
    prefetch.maxCandidates=16;
    prefetch.maxRequestsPerFrame=2;
    VirtualTextureFeedbackRuntime runtime;
    require(runtime.initialize(gpu,feedback,prefetch),
            "camera replacement runtime initialized");
    auto emit=[&](VirtualPageKey page) {
        TextureReadbackResult image;
        image.format=PixelFormat::RGBA8;
        image.width=image.height=1;
        const uint32_t id=encodeVirtualTextureFeedback(page);
        image.pixels.resize(4);
        for(int i=0;i<4;++i)
            image.pixels[size_t(i)]=uint8_t((id>>(i*8))&255u);
        require(runtime.capture(stream.pageTableTexture()),
                "camera feedback capture accepted");
        gpu.finishFeedback(std::move(image));
        require(runtime.readyToSubmit(),"camera feedback decoded");
    };
    emit(oldFine);
    const auto first=runtime.submit(stream);
    require(first.submittedNewRequests==2 && !stream.pendingPage(oldFine),
            "old view leaves fine detail deferred");
    emit(newFine);
    const auto replacement=runtime.submit(stream);
    require(replacement.submittedNewRequests>=1 &&
            !stream.pendingPage(oldFine),
            "new camera visibility replaces deferred old fine detail");
    runtime.shutdown();
    stream.shutdown();
    require(gpu.live.empty(),"camera replacement releases GPU resources");
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
        automaticPrefetchAndPinPromotion();
        asynchronousGpuFeedbackRuntime();
        cameraFeedbackReplacesDeferredVisibility();
        rejectMismatchedConfig();
    } catch (const std::exception& e) {
        std::fprintf(stderr,"VirtualTextureFileStreamer test FAILED: %s\n",e.what());
        return 1;
    }
    std::puts("Virtual texture disk->GPU table publication, LRU, cancellation and failure: OK");
    return 0;
}
