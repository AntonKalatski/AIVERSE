#include "engine/gfx/TextureStreaming.h"

#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace aiverse::gfx;
namespace {
void require(bool value, const char* why) {
    if (!value) throw std::runtime_error(why);
}

class MockGpu final : public Device {
public:
    DeviceInfo info_{};
    std::string error_ = "mock device update failed";
    uint32_t nextGpu_ = 1;
    std::vector<uint32_t> destroyed;
    std::vector<std::vector<uint8_t>> writes;
    bool failNext = false;
    bool dimensions = true;

    const DeviceInfo& info() const override { return info_; }
    const std::string& lastError() const override { return error_; }
    BufferHandle createBuffer(const BufferDesc&) override { return {}; }
    void updateBuffer(BufferHandle, const void*, size_t, size_t) override {}
    void destroyBuffer(BufferHandle) override {}
    bool supportsTextureDimension(TextureDimension) const override { return dimensions; }
    bool supports(PixelFormat f) const override {
        return f == PixelFormat::RGBA8 || f == PixelFormat::R8 || f == PixelFormat::RG8;
    }
    TextureHandle createTexture(const TextureDesc& d) override {
        if (validateTextureDesc(d) != TextureDescError::None) return {};
        return {nextGpu_++};
    }
    void updateTexture(TextureHandle, int, int, int, int, const void*, int) override {}
    bool updateTextureRegion(TextureHandle h, const TextureUploadDesc& u) override {
        require(h.valid() && u.data && u.rowPitchBytes > 0, "mock got invalid upload");
        if (failNext) { failNext = false; return false; }
        const size_t bytes = size_t(u.slicePitchBytes) * size_t(u.depth - 1) +
                             size_t(u.rowPitchBytes) * size_t(u.height);
        const auto* p = static_cast<const uint8_t*>(u.data);
        writes.emplace_back(p, p + bytes);
        return true;
    }
    void destroyTexture(TextureHandle h) override { destroyed.push_back(h.id); }
    ShaderHandle createShader(const ShaderDesc&) override { return {}; }
    void destroyShader(ShaderHandle) override {}
    PipelineHandle createPipeline(const PipelineDesc&) override { return {}; }
    void destroyPipeline(PipelineHandle) override {}
    void beginFrame(int, int) override {}
    void endFrame() override {}
    void beginPass(const PassDesc&) override {}
    void endPass() override {}
    void setViewport(const Rect&) override {}
    void setScissor(const Rect&) override {}
    void setPipeline(PipelineHandle) override {}
    void setVertexBuffer(BufferHandle, size_t) override {}
    void setIndexBuffer(BufferHandle, size_t) override {}
    void setTexture(int, TextureHandle) override {}
    void setUniforms(int, const void*, size_t) override {}
    void draw(uint32_t, uint32_t) override {}
    void drawIndexed(uint32_t, uint32_t, int32_t, uint32_t) override {}
};

void frameBudgetsAndCopies() {
    MockGpu gpu;
    TextureStreamConfig limits;
    limits.gpuBudgetBytes = 64;
    limits.stagingBudgetBytes = 32;
    limits.uploadBytesPerFrame = 16;
    limits.uploadsPerFrame = 1;
    TextureStreamQueue queue(gpu, limits);
    TextureDesc desc;
    desc.width = desc.height = 2;
    const auto first = queue.createTexture(desc);
    const auto second = queue.createTexture(desc);
    require(first.valid() && second.valid(), "two 2D stream resources");
    require(queue.stats().gpuAllocatedBytes == 32 &&
            queue.stats().liveResources == 2, "GPU budget accounting");
    uint8_t source[16]{};
    source[0] = 42;
    TextureUploadDesc up;
    up.width = up.height = 2;
    const auto t0 = queue.enqueue(first, up, source, sizeof source);
    source[0] = 77;
    const auto t1 = queue.enqueue(second, up, source, sizeof source);
    source[0] = 99;
    require(t0.valid() && t1.valid(), "two queued uploads");
    require(queue.stats().stagedBytes == 32 && queue.stats().pendingUploads == 2,
            "staging memory budget");
    require(!queue.enqueue(first, up, source, sizeof source).valid(),
            "staging overcommit rejected");
    std::vector<uint64_t> completionTickets;
    require(queue.drain([&](TextureUploadTicket ticket, TextureStreamId resource, bool ok) {
                require(ok && resource == first, "first completion resource/result");
                completionTickets.push_back(ticket.value);
            }) == 1 && gpu.writes.size() == 1 &&
            gpu.writes[0][0] == 42 && completionTickets.size() == 1 &&
            completionTickets[0] == t0.value,
            "first frame transfers copied CPU bytes and notifies caller");
    require(queue.stats().stagedBytes == 16, "one staging allocation retired");
    require(queue.drain([&](TextureUploadTicket ticket, TextureStreamId resource, bool ok) {
                require(ok && resource == second && ticket == t1,
                        "second completion resource/result");
                completionTickets.push_back(ticket.value);
                // Queue mutation in a completion is safe: notifications run
                // after the frame loop, not during internal iteration.
            }) == 1 && gpu.writes[1][0] == 77 &&
            completionTickets.size() == 2,
            "second frame respects upload count and source ownership");
    require(queue.stats().completedUploads == 2, "two successful transfers");
    require(queue.texture(first).valid(), "stream handle still maps to GPU");
    const auto t2 = queue.enqueue(first, up, source, sizeof source);
    require(t2.valid() && queue.cancel(t2), "cancel pending ticket");
    require(!queue.cancel(t2) && queue.stats().cancelledUploads == 1,
            "ticket invalidated exactly once");
    require(queue.destroy(first) && !queue.texture(first).valid(),
            "destroy invalidates logical resource");
    require(!queue.enqueue(first, up, source, sizeof source).valid(),
            "stale logical resource does not accept uploads");
    const auto third = queue.createTexture(desc);
    require(third.valid() && third.value != first.value,
            "logical resource ID is never recycled");
    queue.shutdown();
    require(gpu.destroyed.size() == 3 &&
            queue.stats().liveResources == 0 &&
            queue.stats().gpuAllocatedBytes == 0, "shutdown retires every texture");
}

void pitchedVolumeAndFailure() {
    MockGpu gpu;
    TextureStreamConfig limits;
    limits.gpuBudgetBytes = 256;
    limits.stagingBudgetBytes = 128;
    limits.uploadBytesPerFrame = 128;
    limits.uploadsPerFrame = 4;
    TextureStreamQueue queue(gpu, limits);
    TextureDesc d;
    d.width = d.height = d.depth = 2;
    d.dimension = TextureDimension::Texture3D;
    const auto volume = queue.createTexture(d);
    require(volume.valid() && queue.stats().gpuAllocatedBytes == 32,
            "volume estimates all three axes");
    TextureUploadDesc u;
    u.width = u.height = u.depth = 2;
    u.rowPitchBytes = 12;
    u.slicePitchBytes = 24;
    uint8_t source[48]{};
    source[0] = 1;
    source[24] = 2;
    require(queue.enqueue(volume, u, source, sizeof source).valid(),
            "pitched 3D volume uploads queued");
    require(queue.drain() == 1 && gpu.writes.size() == 1 &&
            gpu.writes[0].size() == 48 &&
            gpu.writes[0][0] == 1 && gpu.writes[0][24] == 2,
            "strided volume source retained and submitted");
    u.rowPitchBytes = 7;
    require(!queue.enqueue(volume, u, source, sizeof source).valid(),
            "row pitch smaller than width*4 rejected");
    u.rowPitchBytes = 12;
    u.depth = 3;
    require(!queue.enqueue(volume, u, source, sizeof source).valid(),
            "volume z overflow rejected");
    u.depth = 2;
    require(!queue.enqueue(volume, u, source, 20).valid(),
            "truncated payload rejected");
    gpu.failNext = true;
    require(queue.enqueue(volume, u, source, sizeof source).valid(),
            "failed write fixture queued");
    require(queue.enqueue(volume, u, source, sizeof source).valid(),
            "second queued job to be cancelled after failure");
    bool failedCompletion = false;
    require(queue.drain([&](TextureUploadTicket, TextureStreamId id, bool ok) {
                failedCompletion = id == volume && !ok;
                require(queue.failed(volume), "callback observes failed resource status");
            }) == 1 && queue.failed(volume) && failedCompletion,
            "failed GPU upload quarantines resource and reports failure");
    require(queue.stats().failedUploads == 1 &&
            queue.stats().pendingUploads == 0 &&
            queue.stats().cancelledUploads == 1, "later uploads cancelled on failure");
    require(!queue.enqueue(volume, u, source, sizeof source).valid(),
            "failed resource refuses new work");
    require(queue.destroy(volume), "quarantined texture is releasable");
}

void tightBudgetsAndUnsupported() {
    MockGpu gpu;
    TextureStreamConfig tiny;
    tiny.gpuBudgetBytes = 16;
    tiny.stagingBudgetBytes = 8;
    tiny.uploadBytesPerFrame = 8;
    TextureStreamQueue queue(gpu, tiny);
    TextureDesc d;
    d.width = d.height = 2;
    require(queue.createTexture(d).valid(), "one small texture fits");
    require(!queue.createTexture(d).valid(), "GPU memory cap rejects second");
    TextureUploadDesc u;
    u.width = u.height = 2;
    uint8_t px[16]{};
    require(!queue.enqueue({1}, u, px, sizeof px).valid(),
            "oversized frame upload rejected instead of starving forever");
    d.format = PixelFormat::BC7;
    require(!queue.createTexture(d).valid(), "compressed partial streaming rejected");
    d.format = PixelFormat::RGBA8;
    d.renderTarget = true;
    require(!queue.createTexture(d).valid(), "render target stream ownership rejected");
    gpu.dimensions = false;
    d.renderTarget = false;
    require(!queue.createTexture(d).valid(), "unsupported dimension rejected");
}
} // namespace

int main() {
    try {
        frameBudgetsAndCopies();
        pitchedVolumeAndFailure();
        tightBudgetsAndUnsupported();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Texture streaming contract FAILED: %s\n", e.what());
        return 1;
    }
    std::puts("Texture streaming ownership/staging/budgets/cancel: OK");
    return 0;
}
