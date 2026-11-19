// Experimental GPU metadata contract, pure CPU checks (no graphics device).
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataGpuScene.h"
#include "engine/render/GlobalGeometryPool.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

using namespace aiverse::render::strata;

template <class T> T read(const std::vector<uint8_t>& v, size_t offset) {
    assert(offset <= v.size() && sizeof(T) <= v.size() - offset);
    T item;
    std::memcpy(&item, v.data() + offset, sizeof(T));
    return item;
}
static ModelPrimitiveAsset primitive(uint32_t mesh, uint32_t material) {
    ModelPrimitiveAsset p;
    p.mesh = mesh;
    p.primitive = 0;
    p.material = material;
    auto& a = p.geometry;
    a.indices = {0, 1, 2, 0, 1, 2, 2, 3, 0};
    a.roots = {0};
    a.nodes.resize(3);
    a.bounds.resize(3);
    for (uint32_t i = 0; i < 3; ++i) {
        auto& node = a.nodes[i];
        node.materialGroup = material;
        node.resident = true;
        node.indexCount = 3;
        node.firstIndex = i * 3;
        a.bounds[i] = {{float(i), 0, -1}, {float(i + 1), 1, 1}};
    }
    a.nodes[0].firstChild = 1;
    a.nodes[0].childCount = 2;
    a.nodes[0].geometricError = 0.5f;
    a.nodes[2].resident = false;
    return p;
}
// A recording RHI validates the shared-pool gate and GPU metadata lifetime
// without Metal hardware. This is NOT a raster/pixel parity test.
class RecordingDevice final : public aiverse::gfx::Device {
public:
    aiverse::gfx::DeviceInfo deviceInfo{aiverse::gfx::Backend::Metal, true, false, "Strata mock"};
    std::map<uint32_t, std::vector<uint8_t>> buffers;
    std::string error;
    uint32_t next = 1;
    const aiverse::gfx::DeviceInfo& info() const override { return deviceInfo; }
    aiverse::gfx::BufferHandle createBuffer(const aiverse::gfx::BufferDesc& d) override {
        if (!d.size || d.size > 8 * 1024 * 1024) return {};
        auto id = next++;
        auto& bytes = buffers[id];
        bytes.assign(d.size, uint8_t(0));
        if (d.data) std::memcpy(bytes.data(), d.data, d.size);
        return {id};
    }
    void updateBuffer(aiverse::gfx::BufferHandle h, const void* data, size_t len,
                      size_t offset = 0) override {
        auto& bytes = buffers.at(h.id);
        assert(offset <= bytes.size() && len <= bytes.size() - offset);
        std::memcpy(bytes.data() + offset, data, len);
    }
    void destroyBuffer(aiverse::gfx::BufferHandle h) override { buffers.erase(h.id); }
    aiverse::gfx::TextureHandle createTexture(const aiverse::gfx::TextureDesc&) override { return {}; }
    void updateTexture(aiverse::gfx::TextureHandle,int,int,int,int,const void*,int) override {}
    void destroyTexture(aiverse::gfx::TextureHandle) override {}
    aiverse::gfx::ShaderHandle createShader(const aiverse::gfx::ShaderDesc&) override { return {}; }
    void destroyShader(aiverse::gfx::ShaderHandle) override {}
    aiverse::gfx::PipelineHandle createPipeline(const aiverse::gfx::PipelineDesc&) override { return {}; }
    void destroyPipeline(aiverse::gfx::PipelineHandle) override {}
    const std::string& lastError() const override { return error; }
    bool supportsCompute() const override { return true; }
    bool supportsMultiDrawIndirect() const override { return true; }
    void beginFrame(int,int) override {}
    void endFrame() override {}
    void beginPass(const aiverse::gfx::PassDesc&) override {}
    void endPass() override {}
    void setViewport(const aiverse::gfx::Rect&) override {}
    void setScissor(const aiverse::gfx::Rect&) override {}
    void setPipeline(aiverse::gfx::PipelineHandle) override {}
    void setVertexBuffer(aiverse::gfx::BufferHandle,size_t = 0) override {}
    void setIndexBuffer(aiverse::gfx::BufferHandle,size_t = 0) override {}
    void setTexture(int,aiverse::gfx::TextureHandle) override {}
    void setUniforms(int,const void*,size_t) override {}
    void draw(uint32_t,uint32_t = 0) override {}
    void drawIndexed(uint32_t,uint32_t = 0,int32_t = 0,uint32_t = 1) override {}
};
int main() {
    ModelAsset asset;
    asset.primitives.push_back(primitive(2, 7));
    asset.primitives.push_back(primitive(5, 9));
    std::vector<uint8_t> packed;
    std::string error;
    assert(packGpuMetadata(asset, packed, &error));
    assert(error.empty() && !packed.empty());
    assert(packed.size() % 16 == 0);
    const GpuMetadataHeader header = read<GpuMetadataHeader>(packed, 0);
    assert(header.version == 1 && header.primitiveCount == 2);
    assert(header.nodeCount == 6 && header.rootCount == 2);
    assert(header.primitivesByteOffset == sizeof(GpuMetadataHeader));
    assert(header.nodesByteOffset == 32 + 2 * sizeof(GpuPrimitive));
    assert(header.rootsByteOffset == header.nodesByteOffset + 6 * sizeof(GpuNode));
    const GpuPrimitive first = read<GpuPrimitive>(packed, header.primitivesByteOffset);
    const GpuPrimitive second = read<GpuPrimitive>(packed,
        header.primitivesByteOffset + sizeof(GpuPrimitive));
    assert(first.mesh == 2 && first.materialGroup == 7);
    assert(first.nodeBase == 0 && first.nodeCount == 3);
    assert(second.mesh == 5 && second.materialGroup == 9);
    assert(second.nodeBase == 3 && second.nodeCount == 3);
    assert(first.rootBase == 0 && second.rootBase == 1);
    const uint32_t firstRoot = read<uint32_t>(packed, header.rootsByteOffset);
    const uint32_t secondRoot = read<uint32_t>(packed, header.rootsByteOffset + 4);
    assert(firstRoot == 0 && secondRoot == 3);
    const GpuNode parent = read<GpuNode>(packed, header.nodesByteOffset);
    const GpuNode child = read<GpuNode>(packed, header.nodesByteOffset + 5 * sizeof(GpuNode));
    assert(parent.firstChild == 1 && parent.childCount == 2);
    assert(child.firstChild == invalidNode && child.resident == 0);
    assert(child.sourceFirstIndex == 6 && child.sourceIndexCount == 3);
    assert(child.materialGroup == 9);
    assert(child.boundsMin[0] == 2.f && child.boundsMax[0] == 3.f);

    // Repeated primitive IDs and malformed local geometry must preserve the
    // previous payload. No GPU resource allocation occurs on pack failure.
    auto old = packed;
    asset.primitives.push_back(asset.primitives.front());
    assert(!packGpuMetadata(asset, packed, &error));
    assert(packed == old && !error.empty());
    asset.primitives.pop_back();
    asset.primitives[1].geometry.nodes[0].firstChild = 0;
    assert(!packGpuMetadata(asset, packed, &error));
    assert(packed == old);
    asset.primitives[1].geometry.nodes[0].firstChild = 1;
    asset.primitives[1].geometry.nodes[2].firstIndex = 0xfffffffeu;
    assert(!packGpuMetadata(asset, packed, &error));
    assert(packed == old);
    asset.primitives[1].geometry.nodes[2].firstIndex = 6;
    asset.primitives[0].geometry.bounds[2].max[0] =
        std::numeric_limits<float>::infinity();
    assert(!packGpuMetadata(asset, packed, &error));
    assert(packed == old);
    asset.primitives[0].geometry.bounds[2].max[0] = 3.f;
    assert(packGpuMetadata(asset, packed, &error));

    RecordingDevice dev;
    GpuMetadataScene staging;
    aiverse::render::Model sourceModel;
    sourceModel.vertices.resize(4);
    sourceModel.indices = {0, 1, 2, 0, 1, 2, 2, 3, 0};

    // No source Model::geometryPoolPage: never invent a second GPU geometry
    // pool or upload Strata metadata for a dedicated source VB/IB.
    assert(!staging.stage(dev, "shared", sourceModel, asset));
    assert(staging.stats().declined == 1 && dev.buffers.empty());
    // Upload through the existing engine pool, not a Strata allocator.
    assert(aiverse::render::uploadModelToGeometryPool(dev, sourceModel));
    assert(sourceModel.geometryPoolPage && sourceModel.vb == sourceModel.ib);
    assert(dev.buffers.size() == 1);
    assert(staging.stage(dev, "shared", sourceModel, asset));
    assert(staging.stats().sources == 1);
    assert(staging.stats().bytes == packed.size());
    assert(dev.buffers.size() == 2); // shared geometry + separate metadata only
    const auto h = staging.find("shared", sourceModel);
    assert(h.valid());
    assert(dev.buffers[h.id] == packed);
    assert(staging.stage(dev, "shared", sourceModel, asset));
    assert(staging.stats().sources == 1); // per-source deduplication
    sourceModel.markGeometryChanged();
    assert(!staging.find("shared", sourceModel).valid());
    assert(!staging.stage(dev, "shared", sourceModel, asset));
    assert(staging.stats().failed == 1);
    assert(dev.buffers.size() == 2);
    staging.shutdown(dev);
    assert(staging.stats().sources == 0);
    assert(dev.buffers.size() == 1);
    aiverse::render::releaseModelGeometryPool(dev, sourceModel);
    assert(dev.buffers.empty());
    return 0;
}
