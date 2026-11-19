// Vulkan compute RHI: descriptor-backed 64-thread dispatch, CPU-side encode
// validation, and deferred (non-stalling) status-buffer readback.
// Stage 16.1: off by default until users explicitly create a Compute pipeline.
// Compute metadata stays genre-independent. Shader and indirect draw
// decisions remain owned by STRATA/Renderer3D, not this RHI backend.
#include "VkDevice.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace aiverse::gfx::vk {
namespace {
constexpr uint32_t kThreadsPerGroup=64;
constexpr uint32_t kSetsPerPool=64;
constexpr size_t kPushBytes=128; // guaranteed minimum on Vulkan 1.0
constexpr size_t kMaxReadbackBytes=64u*1024u;
}

ComputeHandle VulkanDevice::createCompute(const ComputeDesc& description) {
    if (!supportsCompute() || !description.glsl ||
        !*description.glsl || !vkCreateComputePipelines) {
        error_="Vulkan compute: unsupported device or missing GLSL450 shader";
        return {};
    }
    std::vector<uint32_t> code;
    std::string reason;
    if (!compileComputeProgram(description.glsl,code,&reason)) {
        error_=reason;
        return {};
    }
    if (!computeSetLayout_) {
        const auto& limits=gpuProps_.limits;
        computeBindingCount_=std::min<uint32_t>(
            8,std::min(limits.maxPerStageDescriptorStorageBuffers,
                       limits.maxDescriptorSetStorageBuffers));
        if (computeBindingCount_<4) {
            error_="Vulkan compute: fewer than four storage buffer bindings";
            return {};
        }
        VkDescriptorSetLayoutBinding bindings[8]{};
        for (uint32_t i=0;i<computeBindingCount_;++i) {
            bindings[i].binding=i;
            bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount=1;
            bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        }
        auto layout=vkStruct<VkDescriptorSetLayoutCreateInfo>(
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        layout.bindingCount=computeBindingCount_;
        layout.pBindings=bindings;
        VkDescriptorSetLayout proposed=VK_NULL_HANDLE;
        if (vkCreateDescriptorSetLayout(device_,&layout,nullptr,&proposed)!=VK_SUCCESS) {
            error_="Vulkan compute: descriptor set layout allocation failed";
            return {};
        }
        VkPushConstantRange push{};
        push.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        push.offset=0;
        push.size=uint32_t(kPushBytes);
        auto pi=vkStruct<VkPipelineLayoutCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        pi.setLayoutCount=1;
        pi.pSetLayouts=&proposed;
        pi.pushConstantRangeCount=1;
        pi.pPushConstantRanges=&push;
        VkPipelineLayout computed=VK_NULL_HANDLE;
        if (vkCreatePipelineLayout(device_,&pi,nullptr,&computed)!=VK_SUCCESS) {
            vkDestroyDescriptorSetLayout(device_,proposed,nullptr);
            error_="Vulkan compute: pipeline layout allocation failed";
            return {};
        }
        computeSetLayout_=proposed;
        computePipelineLayout_=computed;
    }
    const VkShaderModule module=createModule(code);
    if (!module) {
        error_="Vulkan compute: shader module allocation failed";
        return {};
    }
    auto stage=vkStruct<VkPipelineShaderStageCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module=module;
    stage.pName="main";
    auto create=vkStruct<VkComputePipelineCreateInfo>(
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    create.stage=stage;
    create.layout=computePipelineLayout_;
    VkPipeline pipeline=VK_NULL_HANDLE;
    if (vkCreateComputePipelines(device_,pipelineCache_,1,&create,
                                 nullptr,&pipeline)!=VK_SUCCESS || !pipeline) {
        vkDestroyShaderModule(device_,module,nullptr);
        error_="Vulkan compute: pipeline creation failed";
        return {};
    }
    return {computes_.add({module,pipeline})};
}

void VulkanDevice::destroyCompute(ComputeHandle handle) {
    const auto* compute=computes_.get(handle.id);
    if (!compute)return;
    const VkPipeline pipeline=compute->pipeline;
    const VkShaderModule module=compute->module;
    deferDestroy([this,pipeline,module] {
        vkDestroyPipeline(device_,pipeline,nullptr);
        vkDestroyShaderModule(device_,module,nullptr);
    });
    computes_.remove(handle.id);
    if(computeHandle_.id==handle.id) {
        computing_=false;
        computeHandle_={};
        computeDispatchSucceeded_=false;
    }
}

bool VulkanDevice::beginCompute(ComputeHandle handle) {
    if (!supportsCompute() || !recording_ || inPass_ || computing_ ||
        !computes_.get(handle.id))return false;
    computing_=true;
    computeStateValid_=true;
    computeDispatchSucceeded_=false;
    computeUniformSize_=0;
    computeBuffers_.fill({});
    computeHandle_=handle;
    vkCmdBindPipeline(slots_[size_t(slot_)].main,
                      VK_PIPELINE_BIND_POINT_COMPUTE,
                      computes_.get(handle.id)->pipeline);
    return true;
}

void VulkanDevice::setComputeBuffer(int slot,BufferHandle handle) {
    if (!computing_ || slot<0 || uint32_t(slot)>=computeBindingCount_) {
        computeStateValid_=false;
        return;
    }
    const VBuffer* buffer=buffers_.get(handle.id);
    if (!buffer || (buffer->type!=BufferType::Storage &&
                    buffer->type!=BufferType::Indirect)) {
        computeStateValid_=false;
        return;
    }
    computeBuffers_[size_t(slot)]=handle;
}

void VulkanDevice::setComputeUniforms(const void* data,size_t size) {
    if(!computing_ || !data || !size || size>kPushBytes ||
       (size&3u)) {
        computeStateValid_=false;
        return;
    }
    std::memcpy(computeUniforms_.data(),data,size);
    computeUniformSize_=size;
}

bool VulkanDevice::dispatchChecked(uint32_t totalThreadsX,uint32_t groupsY) {
    computeDispatchSucceeded_=false;
    if(!computing_ || !computeStateValid_ || !healthy_ ||
       !recording_ || inPass_ || !totalThreadsX || !groupsY ||
       !computeUniformSize_ || !computePipelineLayout_)
        return false;
    const uint32_t groupsX=(totalThreadsX+kThreadsPerGroup-1)/
                           kThreadsPerGroup;
    if(!groupsX || groupsX>gpuProps_.limits.maxComputeWorkGroupCount[0] ||
       groupsY>gpuProps_.limits.maxComputeWorkGroupCount[1])
        return false;
    FrameSlot& slot=slots_[size_t(slot_)];
    auto& frame=computeFrames_[size_t(slot_)];
    if(frame.usedSets==kSetsPerPool){
        ++frame.nextPool;
        frame.usedSets=0;
    }
    if(frame.nextPool==frame.pools.size()){
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                 computeBindingCount_*kSetsPerPool};
        auto ci=vkStruct<VkDescriptorPoolCreateInfo>(
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        ci.maxSets=kSetsPerPool;
        ci.poolSizeCount=1;
        ci.pPoolSizes=&size;
        VkDescriptorPool newPool=VK_NULL_HANDLE;
        if(vkCreateDescriptorPool(device_,&ci,nullptr,&newPool)!=VK_SUCCESS)
            return false;
        frame.pools.push_back(newPool);
    }
    auto alloc=vkStruct<VkDescriptorSetAllocateInfo>(
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    alloc.descriptorPool=frame.pools[frame.nextPool];
    alloc.descriptorSetCount=1;
    alloc.pSetLayouts=&computeSetLayout_;
    VkDescriptorSet set=VK_NULL_HANDLE;
    if(vkAllocateDescriptorSets(device_,&alloc,&set)!=VK_SUCCESS)
        return false;
    ++frame.usedSets;

    VkDescriptorBufferInfo infos[8]{};
    VkWriteDescriptorSet writes[8]{};
    uint32_t count=0;
    // Unreferenced descriptors may remain unwritten; the GLSL shader MUST
    // only read slots actually bound by the caller. The STRATA kernel
    // requires all of metadata/instances/ranges/counters (slots 0..3).
    for(uint32_t binding=0;binding<computeBindingCount_;++binding){
        const auto handle=computeBuffers_[binding];
        if(!handle.valid())continue;
        const VBuffer* buffer=buffers_.get(handle.id);
        if(!buffer)return false;
        const VkBuffer vkBuffer=resolveBuffer(handle.id,serial_);
        if(!vkBuffer)return false;
        infos[count]={vkBuffer,0,VkDeviceSize(buffer->size)};
        writes[count]=vkStruct<VkWriteDescriptorSet>(
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        writes[count].dstSet=set;
        writes[count].dstBinding=binding;
        writes[count].descriptorCount=1;
        writes[count].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[count].pBufferInfo=&infos[count];
        ++count;
    }
    if(!count)return false;
    vkUpdateDescriptorSets(device_,count,writes,0,nullptr);
    VkCommandBuffer cb=slot.main;
    // Staging uploads of immutable metadata/instances in this frame must
    // happen-before compute reads; do not assume a transfer->vertex barrier
    // covers shader storage usage.
    auto before=vkStruct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    before.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT |
                         VK_ACCESS_SHADER_WRITE_BIT;
    before.dstAccessMask=VK_ACCESS_SHADER_READ_BIT |
                         VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT |
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,1,&before,0,nullptr,0,nullptr);
    vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,
        computePipelineLayout_,0,1,&set,0,nullptr);
    vkCmdPushConstants(cb,computePipelineLayout_,
        VK_SHADER_STAGE_COMPUTE_BIT,0,uint32_t(computeUniformSize_),
        computeUniforms_.data());
    vkCmdDispatch(cb,groupsX,groupsY,1);
    // GPU-written culling results / indexed indirect arguments must be
    // visible to subsequent transfer diagnostics or actual draw commands.
    auto after=vkStruct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    after.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
    after.dstAccessMask=VK_ACCESS_SHADER_READ_BIT |
                        VK_ACCESS_TRANSFER_READ_BIT |
                        VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
                        VK_ACCESS_INDEX_READ_BIT |
                        VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
        VK_PIPELINE_STAGE_TRANSFER_BIT |
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        0,1,&after,0,nullptr,0,nullptr);
    computeDispatchSucceeded_=true;
    return true;
}

void VulkanDevice::dispatch(uint32_t totalThreadsX,uint32_t groupsY) {
    (void)dispatchChecked(totalThreadsX,groupsY);
}

void VulkanDevice::endCompute() {
    computing_=false;
    computeHandle_={};
    computeBuffers_.fill({});
    computeUniformSize_=0;
    computeStateValid_=false;
}

bool VulkanDevice::readBufferAsync(BufferHandle handle,size_t size,
                                   BufferReadFn callback) {
    if(!healthy_ || !recording_ || inPass_ || computing_ ||
       !callback || !handle.valid() || !size || size>kMaxReadbackBytes ||
       computeReads_.size()>=64)
        return false;
    const auto* buffer=buffers_.get(handle.id);
    if(!buffer || size>buffer->size)return false;
    const VkBuffer source=resolveBuffer(handle.id,serial_);
    if(!source)return false;
    PendingComputeRead pending;
    if(!createBufferObject(size,VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           true,pending.buffer,pending.allocation))
        return false;
    pending.serial=serial_;
    pending.bytes=size;
    pending.callback=std::move(callback);
    VkCommandBuffer cb=slots_[size_t(slot_)].main;
    auto before=vkStruct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    before.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT |
                         VK_ACCESS_TRANSFER_WRITE_BIT;
    before.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,0,
                         1,&before,0,nullptr,0,nullptr);
    VkBufferCopy region{0,0,size};
    vkCmdCopyBuffer(cb,source,pending.buffer,1,&region);
    auto after=vkStruct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    after.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    after.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT,0,
                         1,&after,0,nullptr,0,nullptr);
    computeReads_.push_back(std::move(pending));
    return true;
}

void VulkanDevice::deliverComputeReadbacks() {
    size_t kept=0;
    std::vector<PendingComputeRead> finished;
    for(size_t i=0;i<computeReads_.size();++i){
        if(computeReads_[i].serial<=completed_)
            finished.push_back(std::move(computeReads_[i]));
        else
            computeReads_[kept++]=std::move(computeReads_[i]);
    }
    computeReads_.resize(kept);
    for(auto& pending:finished){
        std::vector<uint8_t> bytes(pending.bytes);
        if(healthy_ && pending.allocation.mapped)
            std::memcpy(bytes.data(),pending.allocation.mapped,pending.bytes);
        else bytes.clear();
        vkDestroyBuffer(device_,pending.buffer,nullptr);
        memory_.release(pending.allocation);
        if(pending.callback)pending.callback(std::move(bytes));
    }
}

void VulkanDevice::resetComputeFrame(uint32_t frameSlot) {
    auto& frame=computeFrames_[frameSlot];
    for(auto pool:frame.pools)vkResetDescriptorPool(device_,pool,0);
    frame.nextPool=0;
    frame.usedSets=0;
}

void VulkanDevice::shutdownCompute() {
    for(auto& pending:computeReads_){
        if(pending.buffer)vkDestroyBuffer(device_,pending.buffer,nullptr);
        memory_.release(pending.allocation);
    }
    computeReads_.clear();
    computes_.each([this](VCompute& compute) {
        if(compute.pipeline)vkDestroyPipeline(device_,compute.pipeline,nullptr);
        if(compute.module)vkDestroyShaderModule(device_,compute.module,nullptr);
    });
    for(auto& frame:computeFrames_){
        for(auto pool:frame.pools)vkDestroyDescriptorPool(device_,pool,nullptr);
        frame.pools.clear();
    }
    if(computePipelineLayout_)
        vkDestroyPipelineLayout(device_,computePipelineLayout_,nullptr);
    if(computeSetLayout_)
        vkDestroyDescriptorSetLayout(device_,computeSetLayout_,nullptr);
    computePipelineLayout_=VK_NULL_HANDLE;
    computeSetLayout_=VK_NULL_HANDLE;
    computing_=false;
}
} // namespace aiverse::gfx::vk
