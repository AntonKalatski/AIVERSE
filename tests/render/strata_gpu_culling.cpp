// Strata GPU compute dispatch plumbing: recording-RHI only, NOT GPU parity.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataGpuCulling.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::render::strata;

class RecordingComputeDevice final : public gfx::Device {
public:
    gfx::DeviceInfo deviceInfo{gfx::Backend::Metal, true, false, "Strata test"};
    std::map<uint32_t, std::vector<uint8_t>> buffers;
    gfx::BufferHandle bound[4]{};
    GpuSelectionUniforms lastUniforms{};
    std::string shaderText, error;
    uint32_t next = 1, computeCalls = 0, dispatchCount = 0;
    uint32_t drawCalls = 0;
    bool supported = true, active = false;
    uint64_t completed = 100;
    const gfx::DeviceInfo& info() const override { return deviceInfo; }
    const std::string& lastError() const override { return error; }
    bool supportsCompute() const override { return supported; }
    uint64_t completedFrameSerial() const override { return completed; }
    gfx::BufferHandle createBuffer(const gfx::BufferDesc& d) override {
        if (!d.size || d.size > 4 * 1024 * 1024) return {};
        const auto id = next++;
        auto& dest = buffers[id];
        dest.assign(d.size, uint8_t(0));
        if (d.data) std::memcpy(dest.data(), d.data, d.size);
        return {id};
    }
    void updateBuffer(gfx::BufferHandle h, const void* data, size_t size,
                      size_t offset = 0) override {
        auto& b = buffers.at(h.id);
        assert(data && offset <= b.size() && size <= b.size() - offset);
        std::memcpy(b.data() + offset, data, size);
    }
    void destroyBuffer(gfx::BufferHandle h) override { buffers.erase(h.id); }
    gfx::ComputeHandle createCompute(const gfx::ComputeDesc& d) override {
        if (!d.msl || !std::strstr(d.msl, "kernel void cs_main")) return {};
        shaderText = d.msl;
        ++computeCalls;
        return {next++};
    }
    void destroyCompute(gfx::ComputeHandle) override {}
    bool beginCompute(gfx::ComputeHandle h) override {
        if (!supported || !h.valid() || active) return false;
        active = true; return true;
    }
    void setComputeBuffer(int index, gfx::BufferHandle h) override {
        assert(active && index >= 0 && index < 4 && h.valid());
        bound[index] = h;
    }
    void setComputeUniforms(const void* data, size_t size) override {
        assert(active && size == sizeof(GpuSelectionUniforms));
        std::memcpy(&lastUniforms, data, size);
    }
    void dispatch(uint32_t threads, uint32_t = 1) override {
        assert(active && threads > 0);
        dispatchCount += threads;
    }
    void endCompute() override { assert(active); active = false; }
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
    void draw(uint32_t,uint32_t=0) override { ++drawCalls; }
    void drawIndexed(uint32_t,uint32_t=0,int32_t=0,uint32_t=1) override {
        ++drawCalls;
    }
};

int main() {
    RecordingComputeDevice dev;
    GpuClusterCulling compute;
    auto one = makeGpuSceneInstance(mat4::translate({2, 3, 4}), 3, 101);
    assert(one.primitiveIndex == 3 && one.instanceToken == 101);
    assert(one.enabled == 1 && one.objectToWorld[12] == 2 &&
           one.objectToWorld[13] == 3 && one.objectToWorld[14] == 4);
    auto two = makeGpuSceneInstance(mat4::identity(), 0, 102, false);
    assert(two.enabled == 0);
    GpuPassConfig pass;
    pass.viewProjection = mat4::scale({.5f, .25f, 1.f});
    pass.width = 1920; pass.height = 1080;
    pass.errorThresholdPixels = 1.5f;
    pass.maxOutputClusters = 64;
    const ViewConfig oracle = cpuReferenceView(pass, one);
    const mat4 expected = pass.viewProjection *
                          mat4::translate({2, 3, 4});
    for (int i=0;i<16;++i)
        assert(std::fabs(oracle.objectToClip.m[i] - expected.m[i]) < 1e-5f);
    assert(oracle.maxSelectedNodes == 64 && oracle.width == 1920);

    GpuSelectionSubmission result;
    // Unsupported backend/capability must decline without creating resources.
    dev.deviceInfo.backend = gfx::Backend::GLES3;
    assert(!compute.dispatch(dev, {99}, {one}, pass, 101, result));
    assert(!result.valid() && dev.buffers.empty());
    dev.deviceInfo.backend = gfx::Backend::Metal;
    dev.supported = false;
    assert(!compute.dispatch(dev, {99}, {one}, pass, 101, result));
    dev.supported = true;

    assert(compute.dispatch(dev, {99}, {one,two}, pass, 101, result));
    assert(result.valid() && result.capacity == 64);
    assert(result.frameSerial == 101 && result.submission == 0);
    assert(result.submittedInstances == 2);
    assert(compute.computeCalls == 1 && compute.dispatchCount == 2);
    assert(dev.shaderText.find("projectError") != std::string::npos);
    assert(dev.lastUniforms.width == 1920 &&
           dev.lastUniforms.height == 1080);
    assert(dev.lastUniforms.instanceCount == 2);
    assert(dev.lastUniforms.outputCapacity == 64);
    assert(dev.lastUniforms.thresholdPixels == 1.5f);
    assert(dev.lastUniforms.viewProjection[0] == .5f);
    assert(dev.buffers.count(result.ranges.id));
    assert(dev.buffers.at(result.ranges.id).size() >= 64*sizeof(GpuSelectedCluster));
    assert(dev.buffers.count(result.counters.id));
    auto nextResult = result;
    assert(compute.dispatch(dev, {99}, {one}, pass, 101, nextResult));
    assert(nextResult.valid() && nextResult.submission == 1);
    assert(nextResult.ranges != result.ranges); // two views same GPU frame
    assert(nextResult.counters != result.counters);
    assert(dev.drawCalls == 0); // no raster or indirect commands

    GpuPassConfig invalid = pass;
    invalid.width = 0;
    assert(!compute.dispatch(dev, {99}, {one}, invalid, 101, nextResult));
    assert(nextResult.submission == 1); // transactional failure
    assert(compute.computeCalls == 1);
    // Frame 104 would reuse the same triple-buffer slot while frame 101
    // remains GPU-in-flight. Reject until completion is reported.
    assert(!compute.dispatch(dev, {99}, {one}, pass, 104, nextResult));
    dev.completed = 101;
    assert(compute.dispatch(dev, {99}, {one}, pass, 104, nextResult));
    assert(nextResult.frameSerial == 104 && nextResult.submission == 0);
    assert(dev.drawCalls == 0);
    compute.shutdown(dev);
    assert(dev.buffers.empty());
    return 0;
}
