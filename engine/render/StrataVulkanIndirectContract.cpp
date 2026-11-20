#include "StrataVulkanIndirectContract.h"

#include <cmath>
#include <limits>

namespace aiverse::render::strata {
namespace {
bool fail(std::string* error,const char* message) {
    if(error)*error=message;
    return false;
}
}
bool prepareVulkanIndirectContract(
        gfx::Device& device,const Model& source,const ModelAsset& staged,
        const GpuMetadataScene::IndexBinding& binding,
        const GpuPassConfig& view,bool hasShadowOrReflection,
        VulkanIndirectContract& out,GpuPassConfig& guardedView,
        std::string* error) {
    if(error)error->clear();
    if(device.info().backend!=gfx::Backend::Vulkan ||
       !device.supportsCompute() || !device.supportsMultiDrawIndirect() ||
       !device.gpuHealthy() || !device.frameSerial() ||
       hasShadowOrReflection || !binding.valid() ||
       staged.primitives.size()!=1 || !staged.skipped.empty() ||
       !source.geometryPoolPage || !source.vb.valid() ||
       source.vb!=source.ib || source.vb!=binding.buffer ||
       !source.skins.empty() || !source.clips.empty() ||
       !source.variants.empty() || source.morphTex.valid() ||
       source.geometry || source.baked.tex.valid() ||
       source.nodes.size()!=1 || source.order.size()!=1 ||
       source.order.front()!=0 || source.bindWorld.size()!=1 ||
       source.meshes.size()!=1 || source.materials.size()!=1 ||
       source.nodes.front().parent!=-1)
        return fail(error,"Vulkan indirect requires one immutable pooled rigid source and one main view");

    const auto& entry=staged.primitives.front();
    if(entry.mesh>=source.meshes.size() || entry.primitive!=0 ||
       entry.material>=source.materials.size() ||
       source.nodes.front().mesh!=int(entry.mesh) ||
       source.nodes.front().skin>=0 ||
       source.nodes.front().runtimeMutable ||
       !source.nodes.front().children.empty())
        return fail(error,"Vulkan indirect source has unsupported node/primitive graph");
    const auto& mesh=source.meshes[entry.mesh];
    if(mesh.deformable || !mesh.lods.empty() ||
       mesh.primitives.size()!=1 || !mesh.materialBatches.empty() ||
       mesh.shadowBatch.indexCount!=0)
        return fail(error,"Vulkan indirect rejects additional LOD/mesh/material batch paths");
    const auto& p=mesh.primitives.front();
    const auto& material=source.materials[entry.material];
    if(p.material!=int(entry.material) || !p.indexCount ||
       p.indexCount%3!=0 || p.morphTargets || p.shadowBatched ||
       !p.jointPalette.empty() || !p.variantMaterial.empty() ||
       material.alpha!=AlphaMode::Opaque || material.transparent ||
       material.shaderGraph || source.gpuBaseVertex >
           uint32_t(std::numeric_limits<int32_t>::max()) ||
       binding.baseVertex!=int32_t(source.gpuBaseVertex))
        return fail(error,"Vulkan indirect rejected nonopaque/deformed or mismatched GPU base vertex");
    const uint64_t originalOffset=uint64_t(source.gpuFirstIndex)+p.firstIndex;
    const uint64_t originalEnd=uint64_t(p.firstIndex)+p.indexCount;
    if(originalOffset>UINT32_MAX ||
       originalEnd*sizeof(uint32_t)>source.gpuPoolIndexBytes ||
       !binding.indexCount || !entry.geometry.logicalIndexCount() ||
       binding.indexCount<entry.geometry.logicalIndexCount())
        return fail(error,"Vulkan indirect original/Strata GPU index range mismatched");
    if(view.maxOutputClusters==0 || view.maxOutputClusters>2048 ||
       view.width<=0 || view.height<=0 ||
       !std::isfinite(view.errorThresholdPixels) ||
       view.errorThresholdPixels<=0)
        return fail(error,"Vulkan indirect requested invalid view or command capacity");

    GpuPassConfig safe=view;
    safe.baseVertex=int32_t(source.gpuBaseVertex);
    safe.gpuFallbackEnabled=true;
    safe.gpuFallbackFirstIndex=uint32_t(originalOffset);
    safe.gpuFallbackIndexCount=p.indexCount;
    safe.gpuFallbackBaseVertex=int32_t(source.gpuBaseVertex);

    VulkanIndirectContract contract;
    contract.source=&source;
    contract.vertex=source.vb;
    contract.index=source.ib;
    contract.geometryRevision=source.geometryRevision;
    contract.materialRevision=source.materialRevision;
    contract.expectedMaterial=entry.material;
    contract.originalFirstIndex=uint32_t(originalOffset);
    contract.originalIndexCount=p.indexCount;
    contract.pooledBaseVertex=int32_t(source.gpuBaseVertex);
    contract.expectedFrameSerial=device.frameSerial();
    out=contract;
    guardedView=safe;
    return true;
}

bool validateVulkanIndirectSubmission(
        const gfx::Device& device,const Model& source,
        const VulkanIndirectContract& prepared,
        const GpuSelectionSubmission& submission,
        std::string* error) {
    if(error)error->clear();
    if(device.info().backend!=gfx::Backend::Vulkan ||
       !device.supportsCompute() || !device.gpuHealthy() ||
       !device.frameSerial() || !submission.gpuIndirectPrepared() ||
       submission.frameSerial!=device.frameSerial() ||
       submission.frameSerial!=prepared.expectedFrameSerial ||
       submission.submittedInstances!=1 ||
       submission.capacity>2048 || !prepared.source ||
       prepared.source!=&source || source.geometryRevision!=prepared.geometryRevision ||
       source.materialRevision!=prepared.materialRevision ||
       source.vb!=prepared.vertex || source.ib!=prepared.index ||
       source.gpuBaseVertex!=uint32_t(prepared.pooledBaseVertex))
        return fail(error,"Vulkan GPU indirect packet stale, incomplete or not single-instance safe");
    return true;
}
} // namespace aiverse::render::strata
