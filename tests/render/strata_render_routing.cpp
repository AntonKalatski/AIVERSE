// Strata Stage 9: no-double-draw routing preflight on recording RHI.
// Tests command packing only. Never invokes render pass/Renderer3D draws.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataRenderRouting.h"
#include "engine/render/GlobalGeometryPool.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <utility>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::render::strata;

class RecordingDevice final : public gfx::Device {
public:
    gfx::DeviceInfo meta{gfx::Backend::Metal, true, false, "Strata route mock"};
    std::map<uint32_t, std::vector<uint8_t>> buffers;
    std::string last;
    uint32_t id = 1;
    uint32_t rasterDraws = 0, indirectDraws = 0;
    const gfx::DeviceInfo& info() const override { return meta; }
    const std::string& lastError() const override { return last; }
    bool supportsMultiDrawIndirect() const override { return true; }
    bool supportsCompute() const override { return true; }
    gfx::BufferHandle createBuffer(const gfx::BufferDesc& desc) override {
        if (desc.size == 0 || desc.size > 8*1024*1024) return {};
        auto handle = gfx::BufferHandle{id++};
        buffers[handle.id].assign(desc.size, 0);
        if (desc.data)
            std::memcpy(buffers.at(handle.id).data(), desc.data, desc.size);
        return handle;
    }
    void updateBuffer(gfx::BufferHandle handle, const void* data, size_t bytes,
                      size_t offset = 0) override {
        auto& b = buffers.at(handle.id);
        assert(data && offset <= b.size() && bytes <= b.size()-offset);
        std::memcpy(b.data()+offset, data, bytes);
    }
    bool updateSharedBufferChecked(gfx::BufferHandle handle,const void* data,
                                   size_t bytes,size_t offset=0) override {
        if (!buffers.count(handle.id) || !data || !bytes ||
            offset > buffers.at(handle.id).size() ||
            bytes > buffers.at(handle.id).size() - offset) return false;
        updateBuffer(handle,data,bytes,offset);
        return true;
    }
    void destroyBuffer(gfx::BufferHandle handle) override {
        buffers.erase(handle.id);
    }
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override {return {};}
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override {}
    void destroyTexture(gfx::TextureHandle) override {}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override {return {};}
    void destroyShader(gfx::ShaderHandle) override {}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override {return {};}
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
    void draw(uint32_t,uint32_t=0) override {++rasterDraws;}
    void drawIndexed(uint32_t,uint32_t=0,int32_t=0,uint32_t=1) override {
        ++rasterDraws;
    }
    bool drawIndexedIndirect(gfx::BufferHandle,size_t,uint32_t,
                             uint32_t=sizeof(gfx::DrawIndexedIndirectCommand)) override {
        ++indirectDraws;
        return true;
    }
};

static ModelAsset buildSourceAsset() {
    ModelAsset source;
    ModelPrimitiveAsset p;
    p.mesh = 0;
    p.primitive = 0;
    p.material = 0;
    auto& a = p.geometry;
    a.indices = {0,1,2, 0,1,2, 1,3,2};
    a.nodes.resize(3);
    a.bounds.resize(3);
    a.roots={0};
    for(uint32_t i=0;i<3;++i) {
        a.nodes[i].firstIndex=i*3;
        a.nodes[i].indexCount=3;
        a.nodes[i].materialGroup=0;
        a.nodes[i].resident=true;
    }
    a.nodes[0].firstChild=1;
    a.nodes[0].childCount=2;
    a.nodes[0].geometricError=.2f;
    a.bounds[0]={{-.5f,-.5f,.2f},{.5f,.5f,.8f}};
    a.bounds[1]={{-.5f,-.5f,.2f},{0.f,.5f,.8f}};
    a.bounds[2]={{0.f,-.5f,.2f},{.5f,.5f,.8f}};
    source.primitives.push_back(std::move(p));
    return source;
}

int main() {
    RecordingDevice device;
    Model model;
    model.materials.resize(1);
    model.vertices.resize(4);
    model.indices = {0,1,2,1,3,2};
    model.meshes.resize(1);
    Primitive original;
    original.indexCount=6;
    original.material=0;
    model.meshes[0].primitives.push_back(original);
    Node node;
    node.mesh=0;
    model.nodes.push_back(node);

    const auto asset=buildSourceAsset();
    RenderCandidate candidate;
    std::string error;
    GpuMetadataScene residency;
    ViewConfig view;
    view.width=800; view.height=600;
    view.errorThresholdPixels=1.f;
    assert(!prepareRenderCandidate(model,asset,residency,"static",0,0,
                                   RenderPass::MainOpaque,view,candidate,&error));
    assert(!error.empty());
    assert(uploadModelToGeometryPool(device,model));
    assert(residency.stage(device,"static",model,asset));

    assert(prepareRenderCandidate(model,asset,residency,"static",0,0,
                                  RenderPass::MainOpaque,view,candidate,&error));
    assert(candidate.source==&model);
    assert(candidate.mesh==0 && candidate.primitive==0 && candidate.material==0);
    assert(candidate.selectedClusters==2 && candidate.ranges.size()==2);
    GpuMetadataScene::IndexBinding indices;
    assert(residency.indexBinding("static",model,indices));
    assert(candidate.indexBuffer==indices.buffer &&
           candidate.vertexBuffer==model.vb);
    assert(candidate.ranges[0].firstIndex==indices.firstIndex+3);
    assert(candidate.ranges[1].firstIndex==indices.firstIndex+6);
    assert(candidate.ranges[0].baseVertex==indices.baseVertex);

    RenderRouteGate gate;
    assert(chooseExclusiveRenderRoute(gate)==RenderRoute::Legacy);
    std::vector<gfx::DrawIndexedIndirectCommand> commands;
    gfx::DrawIndexedIndirectCommand untouched;
    untouched.firstIndex=1234;
    commands.push_back(untouched);
    assert(!encodeRendererCommands(model,residency,candidate,7,gate,commands));
    assert(commands.size()==1 && commands.front().firstIndex==1234);
    gate.requested=true;
    gate.sourceAndMaterialSafe=true;
    gate.sourceResident=true;
    gate.rendererOwnsSubmission=true;
    // Both parity gates must be accepted explicitly before any Strata
    // source route can replace the original renderer path.
    assert(chooseExclusiveRenderRoute(gate)==RenderRoute::Legacy);
    gate.cpuGpuSelectionParityVerified=true;
    assert(chooseExclusiveRenderRoute(gate)==RenderRoute::Legacy);
    gate.rasterParityVerified=true;
    assert(chooseExclusiveRenderRoute(gate)==RenderRoute::Strata);
    assert(!encodeRendererCommands(model,residency,candidate,invalidNode,gate,commands));
    assert(commands.size()==1 && commands.front().firstIndex==1234);

    assert(encodeRendererCommands(model,residency,candidate,7,gate,commands));
    assert(commands.size()==2);
    for(size_t i=0;i<commands.size();++i) {
        assert(commands[i].firstIndex==candidate.ranges[i].firstIndex);
        assert(commands[i].indexCount==3);
        assert(commands[i].instanceCount==1);
        assert(commands[i].baseVertex==indices.baseVertex);
        assert(commands[i].firstInstance==7);
    }
    assert(device.rasterDraws==0 && device.indirectDraws==0);

    // A second pass with a large pixel threshold is a different, valid
    // *exclusive* candidate. It selects the parent, not its two children.
    view.errorThresholdPixels=5000.f;
    assert(prepareRenderCandidate(model,asset,residency,"static",0,0,
                                  RenderPass::SunShadow,view,candidate,&error));
    assert(candidate.ranges.size()==1);
    assert(candidate.ranges[0].firstIndex==indices.firstIndex);
    assert(encodeRendererCommands(model,residency,candidate,8,gate,commands));
    assert(commands.size()==1 && commands.front().firstInstance==8);
    // Completely offscreen is an exclusive empty route, not fallback to
    // drawing the source's full legacy primitive.
    view.objectToClip=mat4::translate({10,0,0});
    assert(prepareRenderCandidate(model,asset,residency,"static",0,0,
                                  RenderPass::MainOpaque,view,candidate,&error));
    assert(candidate.ranges.empty());
    assert(encodeRendererCommands(model,residency,candidate,8,gate,commands));
    assert(commands.empty());
    view.objectToClip=mat4::identity();

    // Updated materials/geometry invalidate previously prepared commands.
    assert(prepareRenderCandidate(model,asset,residency,"static",0,0,
                                  RenderPass::MainOpaque,view,candidate,&error));
    model.markMaterialChanged();
    assert(!encodeRendererCommands(model,residency,candidate,7,gate,commands));
    model.materials[0].alpha=AlphaMode::Blend;
    assert(!prepareRenderCandidate(model,asset,residency,"static",0,0,
                                   RenderPass::MainOpaque,view,candidate,&error));
    model.materials[0].alpha=AlphaMode::Opaque;
    model.materials[0].castShadow=false;
    assert(!prepareRenderCandidate(model,asset,residency,"static",0,0,
                                   RenderPass::SunShadow,view,candidate,&error));
    model.materials[0].castShadow=true;
    assert(prepareRenderCandidate(model,asset,residency,"static",0,0,
                                  RenderPass::MainOpaque,view,candidate,&error));
    model.markGeometryChanged();
    assert(!encodeRendererCommands(model,residency,candidate,7,gate,commands));
    assert(!prepareRenderCandidate(model,asset,residency,"static",0,0,
                                   RenderPass::MainOpaque,view,candidate,&error));

    residency.shutdown(device);
    releaseModelGeometryPool(device,model);
    assert(device.buffers.empty());
    assert(device.rasterDraws==0 && device.indirectDraws==0);
    return 0;
}
