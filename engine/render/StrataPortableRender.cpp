#include "StrataPortableRender.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <utility>

namespace aiverse::render::strata {
namespace {
bool fail(std::string* error,const char* reason) {
    if(error)*error=reason;
    return false;
}
constexpr size_t kMaxIndexBytes=16u*1024u*1024u;
constexpr size_t kMaxSelectedPrimitives=65536;

bool safePrimitive(const Model& source,uint32_t mesh,uint32_t primitive,
                   uint32_t material){
    if(mesh>=source.meshes.size()||material>=source.materials.size()||
       primitive>=source.meshes[mesh].primitives.size())
        return false;
    const auto& m=source.meshes[mesh];
    const auto& p=m.primitives[primitive];
    const auto& mat=source.materials[material];
    return !m.deformable && m.lods.empty() &&
           p.material==int(material) && !p.morphTargets &&
           !p.jointPalette.size() && p.variantMaterial.empty() &&
           p.indexCount>0 && p.indexCount%3==0 &&
           mat.alpha==AlphaMode::Opaque && !mat.transparent &&
           !mat.shaderGraph;
}
bool safeSource(const Model& source){
    if(!source.vb.valid()||!source.ib.valid()||source.geometry||
       !source.skins.empty()||!source.clips.empty()||
       !source.variants.empty()||source.morphTex.valid()||
       source.nodes.empty()||source.meshes.empty()||
       source.materials.empty()||source.bindWorld.size()!=source.nodes.size()||
       source.gpuBaseVertex>uint32_t(std::numeric_limits<int32_t>::max()))
        return false;
    for(const auto& node:source.nodes)
        if(node.skin>=0 || node.runtimeMutable) return false;
    return true;
}
} // namespace

bool PortableSource::validFor(const Model& source) const {
    return source_==&source && asset_ && indexBuffer_.valid() &&
           sourceGeometryRevision_==source.geometryRevision &&
           sourceMaterialRevision_==source.materialRevision &&
           source.vb.valid() && source.ib.valid() &&
           (pooledIndices_.valid()
                ? auxiliaryGeometryIndicesValidFor(pooledIndices_,source)
                : dedicated_);
}

bool PortableSource::stage(gfx::Device& dev,const Model& source,
                           const ModelAsset& asset,std::string* error){
    if(error)error->clear();
    if(source_ || indexBuffer_.valid() || pooledIndices_.valid())
        return fail(error,"portable Strata source already staged; shutdown first");
    if((dev.info().backend!=gfx::Backend::Metal &&
        dev.info().backend!=gfx::Backend::Vulkan &&
        dev.info().backend!=gfx::Backend::GLES3 &&
        dev.info().backend!=gfx::Backend::GL33) ||
       !dev.gpuHealthy() || !safeSource(source))
        return fail(error,"backend or source is not compatible with portable rigid Strata");
    if(asset.primitives.empty() || !asset.skipped.empty())
        return fail(error,"partial Strata staged model must use legacy renderer");

    // Whole-source opt-in only: no silent loss of primitives or materials.
    // RHI source CPU streams may have been cleared by uploadModel().
    std::set<std::pair<uint32_t,uint32_t>> covered;
    uint64_t sourceVertexCeiling=0, stagedIndices=0;
    for(size_t mesh=0;mesh<source.meshes.size();++mesh){
        const auto& m=source.meshes[mesh];
        for(const auto& p:m.primitives){
            const uint64_t end=uint64_t(p.firstVertex)+p.vertexCount;
            sourceVertexCeiling=std::max(sourceVertexCeiling,end);
        }
    }
    if(!sourceVertexCeiling || sourceVertexCeiling>uint32_t(-1))
        return fail(error,"source vertex bounds unavailable after upload");

    std::vector<uint32_t> combined;
    std::vector<uint32_t> offsets;
    offsets.reserve(asset.primitives.size());
    for(const auto& entry:asset.primitives){
        const auto key=std::pair{entry.mesh,entry.primitive};
        if(!covered.insert(key).second ||
           !safePrimitive(source,entry.mesh,entry.primitive,entry.material) ||
           !validate(entry.geometry.nodes,entry.geometry.roots) ||
           entry.geometry.bounds.size()!=entry.geometry.nodes.size())
            return fail(error,"invalid or duplicate staged rigid material primitive");
        if(entry.geometry.indices.empty() ||
           stagedIndices+entry.geometry.indices.size()>kMaxIndexBytes/sizeof(uint32_t))
            return fail(error,"portable Strata source index budget exceeded");
        for(uint32_t idx:entry.geometry.indices)
            if(idx>=sourceVertexCeiling)
                return fail(error,"Strata index exceeds original vertex allocation");
        for(const auto& n:entry.geometry.nodes)
            if(!n.resident || n.materialGroup!=entry.material ||
               n.firstIndex>entry.geometry.indices.size() ||
               n.indexCount>entry.geometry.indices.size()-n.firstIndex)
                return fail(error,"Strata primitive includes nonresident or foreign material clusters");
        offsets.push_back(uint32_t(stagedIndices));
        stagedIndices+=entry.geometry.indices.size();
        combined.insert(combined.end(),entry.geometry.indices.begin(),
                        entry.geometry.indices.end());
    }
    for(uint32_t mesh=0;mesh<source.meshes.size();++mesh)
        for(uint32_t primitive=0;
            primitive<source.meshes[mesh].primitives.size();++primitive)
            if(!covered.count({mesh,primitive}))
                return fail(error,"Strata stage does not cover every source primitive");
    if(combined.empty())return fail(error,"no portable Strata indices");

    GeometryPoolIndexSpan pooled;
    gfx::BufferHandle buffer;
    bool dedicated=false;
    uint32_t base=0;
    // Prefer the original source's pool, even on Vulkan without compute.
    // GLES3 normally has dedicated vertex/index buffers; allocate ONLY an
    // immutable auxiliary index stream, NEVER duplicate its vertex data.
    if(source.geometryPoolPage &&
       uploadAuxiliaryGeometryIndices(dev,source,combined.data(),
                                      combined.size(),pooled)){
        buffer=pooled.buffer;
        base=pooled.firstIndex;
    }else{
        gfx::BufferDesc desc{};
        desc.type=gfx::BufferType::Index;
        desc.usage=gfx::Usage::Immutable;
        desc.size=combined.size()*sizeof(uint32_t);
        desc.data=combined.data();
        desc.label="Strata portable GLES3/Vulkan indexed LOD sidecar";
        buffer=dev.createBuffer(desc);
        if(!buffer.valid())
            return fail(error,"portable Strata index-only GPU upload failed");
        dedicated=true;
    }

    source_=&source;
    asset_=&asset;
    indexBuffer_=buffer;
    firstIndex_=base;
    baseVertex_=source.gpuBaseVertex;
    indexCount_=uint32_t(stagedIndices);
    sourceGeometryRevision_=source.geometryRevision;
    sourceMaterialRevision_=source.materialRevision;
    primitiveOffsets_=std::move(offsets);
    pooledIndices_=std::move(pooled);
    dedicated_=dedicated;
    stats_={};
    stats_.pooledIndices=pooledIndices_.valid();
    stats_.dedicatedIndices=dedicated;
    return true;
}

bool PortableSource::prepareModel(
        const mat4& instanceWorld,
        const std::vector<PortablePassView>& passes,
        bool forceFinestForUnspecifiedPass,
        Model& output,std::string* error){
    if(error)error->clear();
    const auto reject=[&](const char* reason){
        ++stats_.fallbackPreparations;
        return fail(error,reason);
    };
    if(!source_ || !validFor(*source_) || !safeSource(*source_) ||
       (!forceFinestForUnspecifiedPass && passes.empty()) ||
       passes.size()>8)
        return reject("stale Strata source or unsupported pass configuration");
    for(const auto& p:passes)
        if(p.width<=0 || p.height<=0 ||
           p.width>32768 || p.height>32768 ||
           !std::isfinite(p.errorThresholdPixels) ||
           p.errorThresholdPixels<=0)
            return reject("invalid portable Strata pass");
    for(const float x:instanceWorld.m)
        if(!std::isfinite(x))return reject("invalid instance transform");

    // Source meshes may be referenced by multiple nodes. The alias carries
    // a single primitive layout; refine to the deepest frontier requested
    // by ANY node in ANY pass. Renderer3D then owns main/shadow/reflection
    // culling independently, preventing one pass from losing geometry.
    Model alias;
    alias.path=source_->path+"#strata-portable";
    alias.batching=source_->batching;
    alias.batching.autoLod=false;
    alias.geometryRevision=source_->geometryRevision;
    alias.materialRevision=source_->materialRevision;
    alias.nodes=source_->nodes;
    alias.order=source_->order;
    alias.bindWorld=source_->bindWorld;
    alias.meshes=source_->meshes;
    alias.materials=source_->materials;
    alias.images=source_->images;
    for(auto& img:alias.images)img.borrowedTexture=true;
    alias.boundsMin=source_->boundsMin;
    alias.boundsMax=source_->boundsMax;
    alias.vb=source_->vb;
    alias.ib=indexBuffer_;
    alias.gpuFirstIndex=firstIndex_;
    alias.gpuBaseVertex=baseVertex_;
    // A dedicated GLES3/Vulkan fallback IB is NOT a shared pool allocation,
    // even if the source VB happens to be pooled. Only an actual pooled
    // auxiliary span may inherit the page for Vulkan canonical draw indexing.
    alias.geometryPoolPage=pooledIndices_.valid()
        ? source_->geometryPoolPage : nullptr;
    for(auto& m:alias.meshes){
        m.primitives.clear();
        m.lods.clear();
        m.shadowBatch={};
        m.materialBatches.clear();
    }
    uint64_t selected=0;
    for(size_t i=0;i<asset_->primitives.size();++i){
        const auto& entry=asset_->primitives[i];
        if(!safePrimitive(*source_,entry.mesh,entry.primitive,entry.material))
            return reject("live material or source became incompatible");
        const auto& g=entry.geometry;
        std::vector<uint32_t> parent(g.nodes.size(),invalidNode);
        for(uint32_t n=0;n<g.nodes.size();++n)
            for(uint32_t ch=0;ch<g.nodes[n].childCount;++ch){
                const uint32_t id=g.nodes[n].firstChild+ch;
                if(id>=parent.size() || parent[id]!=invalidNode)
                    return reject("malformed cluster parent topology");
                parent[id]=n;
            }
        std::vector<uint8_t> refine(g.nodes.size(),0);
        if(forceFinestForUnspecifiedPass){
            // Unknown future reflection/capture views use the original
            // finest resident meshlets. Never cull by the main camera alone.
            for(uint32_t n=0;n<g.nodes.size();++n)
                if(g.nodes[n].childCount)refine[n]=1;
        }else{
            bool wasUsed=false;
            for(size_t n=0;n<source_->nodes.size();++n){
                if(source_->nodes[n].mesh!=int(entry.mesh))continue;
                wasUsed=true;
                for(const auto& pass:passes){
                    ViewConfig view{};
                    view.objectToClip=pass.viewProjection*instanceWorld*
                                      source_->bindWorld[n];
                    view.width=pass.width;
                    view.height=pass.height;
                    view.clipDepthZeroToOne=pass.clipDepthZeroToOne;
                    view.errorThresholdPixels=pass.errorThresholdPixels;
                    // The existing renderer independently culls the per-pass
                    // model primitives. Never select only main-camera geometry.
                    view.frustumCull=false;
                    view.maxSelectedNodes=uint32_t(kMaxSelectedPrimitives);
                    ViewPlan plan;
                    if(!planView(g,view,plan,nullptr) ||
                       plan.budgetExceeded)
                        return reject("portable Strata CPU LOD plan failed");
                    for(const auto& range:plan.ranges){
                        uint32_t ancestor=parent[range.node];
                        while(ancestor!=invalidNode){
                            refine[ancestor]=1;
                            ancestor=parent[ancestor];
                        }
                    }
                }
            }
            if(!wasUsed)return reject("staged material mesh is unused by model nodes");
        }
        std::vector<uint32_t> pending(g.roots.rbegin(),g.roots.rend());
        while(!pending.empty()){
            const uint32_t n=pending.back();pending.pop_back();
            const auto& node=g.nodes[n];
            if(refine[n] && node.childCount){
                bool childrenResident=true;
                for(uint32_t child=0;child<node.childCount;++child)
                    if(!g.nodes[node.firstChild+child].resident)
                        childrenResident=false;
                if(!childrenResident)
                    return reject("no resident children for requested Strata LOD");
                for(uint32_t child=node.childCount;child>0;--child)
                    pending.push_back(node.firstChild+child-1);
                continue;
            }
            if(!node.resident || node.materialGroup!=entry.material ||
               selected>=kMaxSelectedPrimitives ||
               uint64_t(primitiveOffsets_[i])+node.firstIndex+node.indexCount>
                   uint64_t(indexCount_))
                return reject("invalid portable Strata selected index frontier");
            Primitive prim=source_->meshes[entry.mesh].primitives[entry.primitive];
            prim.firstIndex=primitiveOffsets_[i]+node.firstIndex;
            prim.indexCount=node.indexCount;
            prim.shadowBatched=false;
            const auto& bounds=g.bounds[n];
            prim.boundsMin={bounds.min[0],bounds.min[1],bounds.min[2]};
            prim.boundsMax={bounds.max[0],bounds.max[1],bounds.max[2]};
            alias.meshes[entry.mesh].primitives.push_back(std::move(prim));
            ++selected;
        }
    }
    if(!selected)return reject("no resident selected Strata clusters");
    output=std::move(alias); // transaction: original output untouched on error
    ++stats_.drawPreparations;
    stats_.selectedClusters+=selected;
    stats_.forcedFinest=forceFinestForUnspecifiedPass;
    return true;
}

void PortableSource::shutdown(gfx::Device& dev){
    if(pooledIndices_.valid())
        releaseAuxiliaryGeometryIndices(dev,pooledIndices_);
    if(dedicated_&&indexBuffer_.valid())
        dev.destroyBuffer(indexBuffer_);
    source_=nullptr;asset_=nullptr;indexBuffer_={};
    pooledIndices_={};dedicated_=false;
    firstIndex_=indexCount_=baseVertex_=0;
    sourceGeometryRevision_=sourceMaterialRevision_=0;
    primitiveOffsets_.clear();
    stats_={};
}
} // namespace aiverse::render::strata
