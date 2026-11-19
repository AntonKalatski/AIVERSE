// Portable Strata raster contract regression: no GPU compute requirements.
// Recording RHI verifies GLES3 index-only fallback and Vulkan shared pool;
// the native Mac build will compile this target but execution is deferred.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataPortableRender.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::render::strata;

class Recorder final : public gfx::Device {
public:
    explicit Recorder(gfx::Backend backend, bool mdi)
        : info_{backend,true,false,"Portable Strata recorder"}, mdi_(mdi) {}
    gfx::DeviceInfo info_{};
    bool mdi_=false;
    uint32_t next_=1;
    uint32_t draws=0, allocs=0, releases=0;
    std::map<uint32_t,std::vector<uint8_t>> data_;
    std::string error_;
    const gfx::DeviceInfo& info() const override {return info_;}
    const std::string& lastError() const override {return error_;}
    bool supportsMultiDrawIndirect() const override {return mdi_;}
    gfx::BufferHandle createBuffer(const gfx::BufferDesc& d) override {
        if(!d.size || d.size>32u*1024u*1024u)return {};
        gfx::BufferHandle h{next_++};
        data_[h.id].resize(d.size);
        if(d.data)std::memcpy(data_[h.id].data(),d.data,d.size);
        ++allocs;
        return h;
    }
    void updateBuffer(gfx::BufferHandle h,const void* bytes,size_t length,
                      size_t offset=0) override {
        assert(data_.count(h.id)&&offset<=data_[h.id].size()&&
               length<=data_[h.id].size()-offset);
        std::memcpy(data_[h.id].data()+offset,bytes,length);
    }
    bool updateSharedBufferChecked(gfx::BufferHandle h,const void* bytes,
                                   size_t length,size_t offset=0) override {
        if(!data_.count(h.id)||!bytes||
           offset>data_[h.id].size()||
           length>data_[h.id].size()-offset)return false;
        updateBuffer(h,bytes,length,offset);return true;
    }
    void destroyBuffer(gfx::BufferHandle h) override {
        if(data_.erase(h.id))++releases;
    }
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override{return {};}
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override {}
    void destroyTexture(gfx::TextureHandle) override {}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override{return {};}
    void destroyShader(gfx::ShaderHandle) override {}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override{return {};}
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
    void draw(uint32_t,uint32_t=0) override {++draws;}
    void drawIndexed(uint32_t,uint32_t=0,int32_t=0,uint32_t=1) override{
        ++draws;
    }
};

static Model makeModel(){
    Model m;
    m.path="portable-rigid";
    m.batching.autoLod=false;
    m.vertices.resize(4);
    m.vertices[0].pos={-.5f,-.5f,.4f};
    m.vertices[1].pos={ .5f,-.5f,.4f};
    m.vertices[2].pos={-.5f, .5f,.4f};
    m.vertices[3].pos={ .5f, .5f,.4f};
    m.indices={0,1,2,1,3,2};
    m.meshes.resize(1);
    Primitive p;
    p.firstVertex=0;p.vertexCount=4;
    p.firstIndex=0;p.indexCount=6;p.material=0;
    m.meshes[0].primitives.push_back(p);
    m.materials.resize(1);
    Node n;n.mesh=0;
    m.nodes.push_back(n);
    m.finalize();
    return m;
}
static ModelAsset makeAsset(){
    ModelAsset a;
    ModelPrimitiveAsset p;
    p.mesh=0;p.primitive=0;p.material=0;
    auto& g=p.geometry;
    g.indices={0,1,2,1,3,2,0,1,2,1,3,2};
    g.roots={0};
    g.nodes.resize(3);
    g.bounds.resize(3);
    g.nodes[0].firstChild=1;
    g.nodes[0].childCount=2;
    g.nodes[0].firstIndex=0;g.nodes[0].indexCount=6;
    g.nodes[0].geometricError=.25f;
    g.nodes[1].firstIndex=6;g.nodes[1].indexCount=3;
    g.nodes[2].firstIndex=9;g.nodes[2].indexCount=3;
    for(uint32_t i=0;i<3;++i){
        g.nodes[i].resident=true;
        g.nodes[i].materialGroup=0;
        g.bounds[i]={{-.5f,-.5f,.2f},{.5f,.5f,.8f}};
    }
    a.primitives.push_back(std::move(p));
    return a;
}
static void testBackend(gfx::Backend backend,bool mdi){
    Recorder dev(backend,mdi);
    Model original=makeModel();
    const auto asset=makeAsset();
    if(mdi)assert(uploadModelToGeometryPool(dev,original));
    else {
        gfx::BufferDesc v{};v.type=gfx::BufferType::Vertex;
        v.size=original.vertices.size()*sizeof(ModelVertex);
        v.data=original.vertices.data();
        original.vb=dev.createBuffer(v);
        gfx::BufferDesc i{};i.type=gfx::BufferType::Index;
        i.size=original.indices.size()*sizeof(uint32_t);
        i.data=original.indices.data();
        original.ib=dev.createBuffer(i);
        assert(original.vb.valid()&&original.ib.valid());
    }
    const auto vb=original.vb;
    const auto ib=original.ib;
    const uint32_t initialAllocations=dev.allocs;
    PortableSource staged;
    std::string error;
    assert(staged.stage(dev,original,asset,&error));
    assert(error.empty());
    assert(staged.validFor(original));
    assert(staged.stats().pooledIndices == mdi);
    assert(staged.stats().dedicatedIndices == !mdi);
    assert(dev.allocs==initialAllocations+(!mdi?1u:0u));
    assert(staged.indexBuffer().valid());
    if(mdi)assert(staged.indexBuffer()==ib);
    else assert(staged.indexBuffer()!=ib);
    PortablePassView fine{mat4::identity(),800,600,true,.01f};
    PortablePassView coarse{mat4::identity(),800,600,true,10000.f};
    Model renderModel;
    assert(staged.prepareModel(mat4::identity(),{coarse,fine},false,
                               renderModel,&error));
    assert(renderModel.vb==vb);
    assert(renderModel.ib==staged.indexBuffer());
    assert(renderModel.gpuFirstIndex==staged.firstIndex());
    assert(renderModel.meshes.size()==1);
    assert(renderModel.meshes[0].primitives.size()==2);
    assert(renderModel.materials.size()==original.materials.size());
    assert(staged.prepareModel(mat4::identity(),{coarse},false,
                               renderModel,&error));
    assert(renderModel.meshes[0].primitives.size()==1);
    assert(renderModel.meshes[0].primitives[0].indexCount==6);
    assert(staged.prepareModel(mat4::identity(),{},true,
                               renderModel,&error));
    assert(renderModel.meshes[0].primitives.size()==2);
    // A material swap that disables the opaque source is a strict fallback:
    // rejected prepare does not clobber the last valid alias.
    original.materials[0].alpha=AlphaMode::Blend;
    assert(!staged.prepareModel(mat4::identity(),{fine},false,
                                renderModel,&error));
    assert(!error.empty());
    assert(renderModel.meshes[0].primitives.size()==2);
    original.materials[0].alpha=AlphaMode::Opaque;
    original.markMaterialChanged();
    assert(!staged.validFor(original));
    assert(!staged.prepareModel(mat4::identity(),{fine},false,
                                renderModel,&error));
    // Renderer consumes the alias and never submits original alongside it.
    dev.drawIndexed(renderModel.meshes[0].primitives[0].indexCount);
    assert(dev.draws==1);
    renderModel=Model{}; // borrow released before source/page shutdown
    staged.shutdown(dev);
    if(mdi)releaseModelGeometryPool(dev,original);
    else {dev.destroyBuffer(vb);dev.destroyBuffer(ib);}
    assert(dev.data_.empty());
}
int main(){
    testBackend(gfx::Backend::GLES3,false);
    testBackend(gfx::Backend::Vulkan,true);
    testBackend(gfx::Backend::Metal,true);
    testBackend(gfx::Backend::GL33,false);
    return 0;
}
