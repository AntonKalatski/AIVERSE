// Vulkan compute backend for AIVERSE's genre-independent Device API.
// Storage images: set=0 bindings 0..3, sampled textures: bindings 4..11,
// storage buffers: 12..15, optional std140 uniform block: binding 16.
// GLSL compute source is Vulkan 450 body (compileComputeProgram supplies
// #version and 64x1x1 local size if it is not declared by the caller).
#include "VkDevice.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace aiverse::gfx::vk {

ComputeHandle VulkanDevice::createCompute(const ComputeDesc& desc) {
    if (!supportsCompute() || !desc.glsl) {
        error_="Vulkan: compute requires a compute-capable queue and GLSL source";
        return {};
    }
    std::vector<uint32_t> spirv;
    if (!compileComputeProgram(desc.glsl,spirv,error_)) return {};
    VCompute compute;
    compute.shader=createModule(spirv);
    if (!compute.shader) {
        error_="Vulkan: compute shader module allocation failed";
        return {};
    }
    auto cleanup=[&]() {
        if (compute.pipeline) vkDestroyPipeline(device_,compute.pipeline,nullptr);
        if (compute.layout) vkDestroyPipelineLayout(device_,compute.layout,nullptr);
        if (compute.descriptorLayout)
            vkDestroyDescriptorSetLayout(device_,compute.descriptorLayout,nullptr);
        if (compute.shader) vkDestroyShaderModule(device_,compute.shader,nullptr);
    };
    std::array<VkDescriptorSetLayoutBinding,17> bindings{};
    for (uint32_t i=0;i<4;++i)
        bindings[i]={i,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    for (uint32_t i=0;i<8;++i)
        bindings[4u+i]={4u+i,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,
                        VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    for (uint32_t i=0;i<4;++i)
        bindings[12u+i]={12u+i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,
                         VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    bindings[16]={16u,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    auto li=vkStruct<VkDescriptorSetLayoutCreateInfo>(
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    li.bindingCount=uint32_t(bindings.size());
    li.pBindings=bindings.data();
    if (vkCreateDescriptorSetLayout(device_,&li,nullptr,&compute.descriptorLayout)!=VK_SUCCESS) {
        error_="Vulkan: compute resource binding layout creation failed";
        cleanup();return {};
    }
    auto pl=vkStruct<VkPipelineLayoutCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pl.setLayoutCount=1;pl.pSetLayouts=&compute.descriptorLayout;
    if (vkCreatePipelineLayout(device_,&pl,nullptr,&compute.layout)!=VK_SUCCESS) {
        error_="Vulkan: compute pipeline layout creation failed";
        cleanup();return {};
    }
    auto ci=vkStruct<VkComputePipelineCreateInfo>(
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    ci.layout=compute.layout;
    ci.stage=vkStruct<VkPipelineShaderStageCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module=compute.shader;
    ci.stage.pName="main";
    if (vkCreateComputePipelines(device_,pipelineCache_,1,&ci,nullptr,
                                 &compute.pipeline)!=VK_SUCCESS) {
        error_="Vulkan: compute pipeline creation failed (check shader bindings)";
        cleanup();return {};
    }
    error_.clear();
    return {computes_.add(compute)};
}

void VulkanDevice::destroyCompute(ComputeHandle handle) {
    VCompute* c=computes_.get(handle.id);
    if (!c) return;
    if (activeComputeId_==handle.id) endCompute();
    const VCompute disposed=*c;
    computes_.remove(handle.id);
    deferDestroy([this,disposed]() {
        if (disposed.pipeline) vkDestroyPipeline(device_,disposed.pipeline,nullptr);
        if (disposed.layout) vkDestroyPipelineLayout(device_,disposed.layout,nullptr);
        if (disposed.descriptorLayout)
            vkDestroyDescriptorSetLayout(device_,disposed.descriptorLayout,nullptr);
        if (disposed.shader) vkDestroyShaderModule(device_,disposed.shader,nullptr);
    });
}

bool VulkanDevice::beginCompute(ComputeHandle handle) {
    if (!supportsCompute() || !recording_ || inPass_ || activeComputeId_ ||
        !computes_.get(handle.id)) {
        error_="Vulkan: compute must begin within a graphics frame outside render passes";
        return false;
    }
    activeComputeId_=handle.id;
    computeStorage_.fill({});
    computeSampled_.fill({});
    computeBuffers_.fill({});
    computeUniforms_.clear();
    error_.clear();
    return true;
}

void VulkanDevice::setComputeBuffer(int slot,BufferHandle buffer) {
    if (!activeComputeId_ || slot<0 || slot>=int(computeBuffers_.size()) ||
        !buffers_.get(buffer.id) || buffers_.get(buffer.id)->type!=BufferType::Storage) {
        error_="Vulkan: compute buffers require active pipeline and a Storage buffer in slot 0..3";
        return;
    }
    computeBuffers_[size_t(slot)]=buffer;
}

void VulkanDevice::setComputeTexture(int slot,TextureHandle image) {
    if (!activeComputeId_ || slot<0 || slot>=int(computeSampled_.size()) ||
        !textures_.get(image.id)) {
        error_="Vulkan: compute sampled texture requires active pipeline and slot 0..7";
        return;
    }
    computeSampled_[size_t(slot)]=image;
}

bool VulkanDevice::bindComputeStorageTexture(int slot,TextureHandle image) {
    const VTexture* t=textures_.get(image.id);
    if (!activeComputeId_ || slot<0 || slot>=int(computeStorage_.size()) ||
        !t || !t->image || !t->storage || t->samples!=1 ||
        !supportsStorageTexture(t->format,t->dimension)) {
        error_="Vulkan: compute write requires a storage image and writable slot 0..3";
        return false;
    }
    computeStorage_[size_t(slot)]=image;
    return true;
}

void VulkanDevice::setComputeUniforms(const void* data,size_t size) {
    if (!activeComputeId_ || (size && !data) || size>4096) {
        error_="Vulkan: compute uniform bytes require active pipeline and size <=4096";
        return;
    }
    if (size) {
        const auto* ptr=static_cast<const uint8_t*>(data);
        computeUniforms_.assign(ptr,ptr+size);
    } else computeUniforms_.clear();
}

void VulkanDevice::dispatch(uint32_t x,uint32_t y) {
    if (!activeComputeId_ || !recording_ || inPass_ || !x || !y) return;
    const VCompute* compute=computes_.get(activeComputeId_);
    if (!compute) {
        error_="Vulkan: compute resource was destroyed";return;
    }
    if ((uint64_t(x)+63u)/64u>gpuProps_.limits.maxComputeWorkGroupCount[0] ||
        y>gpuProps_.limits.maxComputeWorkGroupCount[1]) {
        error_="Vulkan: compute dispatch exceeds device workgroup count limit";
        return;
    }
    for (auto image : computeStorage_)
        if (image.valid())
            for (auto sample : computeSampled_)
                if (sample==image) {
                    error_="Vulkan: a compute dispatch cannot read and write the same image";
                    return;
                }

    std::array<VkDescriptorPoolSize,4> poolSizes{{
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,4},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,8},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}
    }};
    auto poolInfo=vkStruct<VkDescriptorPoolCreateInfo>(
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    poolInfo.maxSets=1;
    poolInfo.poolSizeCount=uint32_t(poolSizes.size());
    poolInfo.pPoolSizes=poolSizes.data();
    VkDescriptorPool pool=VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device_,&poolInfo,nullptr,&pool)!=VK_SUCCESS) {
        error_="Vulkan: compute descriptor pool allocation failed";return;
    }
    auto ai=vkStruct<VkDescriptorSetAllocateInfo>(
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    ai.descriptorPool=pool;ai.descriptorSetCount=1;
    ai.pSetLayouts=&compute->descriptorLayout;
    VkDescriptorSet set=VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device_,&ai,&set)!=VK_SUCCESS) {
        error_="Vulkan: compute descriptor set allocation failed";
        vkDestroyDescriptorPool(device_,pool,nullptr);return;
    }

    // Immutable addresses; no vector reallocation can invalidate the
    // VkWriteDescriptorSet pImageInfo/pBufferInfo pointers.
    std::array<VkDescriptorImageInfo,12> images{};
    std::array<VkDescriptorBufferInfo,5> buffers{};
    std::array<VkWriteDescriptorSet,17> writes{};
    size_t writeCount=0;
    auto pushImage=[&](uint32_t binding,VkDescriptorType type,
                       VkDescriptorImageInfo* image) {
        auto& w=writes[writeCount++];
        w=vkStruct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        w.dstSet=set;w.dstBinding=binding;w.descriptorType=type;
        w.descriptorCount=1;w.pImageInfo=image;
    };
    auto pushBuffer=[&](uint32_t binding,VkDescriptorType type,
                        VkDescriptorBufferInfo* buffer) {
        auto& w=writes[writeCount++];
        w=vkStruct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        w.dstSet=set;w.dstBinding=binding;w.descriptorType=type;
        w.descriptorCount=1;w.pBufferInfo=buffer;
    };

    for(size_t slot=0;slot<computeStorage_.size();++slot) {
        const auto handle=computeStorage_[slot];
        if (!handle.valid()) continue;
        const VTexture* t=textures_.get(handle.id);
        if(!t || !t->storage || !t->view) {
            error_="Vulkan: storage image was deleted before compute submission";
            vkDestroyDescriptorPool(device_,pool,nullptr);return;
        }
        images[slot]={VK_NULL_HANDLE,t->view,VK_IMAGE_LAYOUT_GENERAL};
        pushImage(uint32_t(slot),VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,&images[slot]);
    }
    for(size_t slot=0;slot<computeSampled_.size();++slot) {
        const auto handle=computeSampled_[slot];
        if (!handle.valid())continue;
        const VTexture* t=textures_.get(handle.id);
        if(!t || !t->view || !t->sampler) {
            error_="Vulkan: sampled image was deleted before compute submission";
            vkDestroyDescriptorPool(device_,pool,nullptr);return;
        }
        images[4u+slot]={t->sampler,t->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        pushImage(uint32_t(4u+slot),VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                  &images[4u+slot]);
    }
    for(size_t slot=0;slot<computeBuffers_.size();++slot) {
        const auto h=computeBuffers_[slot];
        if(!h.valid())continue;
        VBuffer* b=buffers_.get(h.id);
        if (!b || b->type!=BufferType::Storage || !b->size) {
            error_="Vulkan: storage buffer was deleted before compute submission";
            vkDestroyDescriptorPool(device_,pool,nullptr);return;
        }
        VkBuffer native=resolveBuffer(h.id,serial_);
        if(!native) {
            error_="Vulkan: compute storage buffer version missing";
            vkDestroyDescriptorPool(device_,pool,nullptr);return;
        }
        buffers[slot]={native,0,VkDeviceSize(b->size)};
        pushBuffer(uint32_t(12u+slot),VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                   &buffers[slot]);
    }

    VkBuffer uniformBuffer=VK_NULL_HANDLE;
    Allocation uniformAlloc{};
    if(!computeUniforms_.empty()) {
        const size_t bytes=computeUniforms_.size();
        if (!createBufferObject(bytes,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,true,
                                uniformBuffer,uniformAlloc) || !uniformAlloc.mapped) {
            error_="Vulkan: compute uniform staging allocation failed";
            if(uniformBuffer)vkDestroyBuffer(device_,uniformBuffer,nullptr);
            memory_.release(uniformAlloc);
            vkDestroyDescriptorPool(device_,pool,nullptr);return;
        }
        std::memcpy(uniformAlloc.mapped,computeUniforms_.data(),bytes);
        buffers[4]={uniformBuffer,0,VkDeviceSize(bytes)};
        pushBuffer(16u,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,&buffers[4]);
    }
    vkUpdateDescriptorSets(device_,uint32_t(writeCount),writes.data(),0,nullptr);
    VkCommandBuffer command=slots_[size_t(slot_)].main;
    for(const auto handle:computeStorage_) {
        if(!handle.valid())continue;
        const VTexture* t=textures_.get(handle.id);
        barrierImage(command,t->image,false,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_IMAGE_LAYOUT_GENERAL,
                     VK_REMAINING_MIP_LEVELS,0,uint32_t(t->layers));
    }
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,compute->pipeline);
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,
                            compute->layout,0,1,&set,0,nullptr);
    vkCmdDispatch(command,(x+63u)/64u,y,1);
    for(const auto handle:computeStorage_) {
        if(!handle.valid())continue;
        const VTexture* t=textures_.get(handle.id);
        barrierImage(command,t->image,false,VK_IMAGE_LAYOUT_GENERAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_REMAINING_MIP_LEVELS,0,uint32_t(t->layers));
    }
    deferDestroy([this,pool,uniformBuffer,uniformAlloc]() mutable {
        vkDestroyDescriptorPool(device_,pool,nullptr);
        if (uniformBuffer) {
            vkDestroyBuffer(device_,uniformBuffer,nullptr);
            memory_.release(uniformAlloc);
        }
    });
    error_.clear();
}

void VulkanDevice::endCompute() {
    activeComputeId_=0;
    computeStorage_.fill({});
    computeSampled_.fill({});
    computeBuffers_.fill({});
    computeUniforms_.clear();
}

} // namespace aiverse::gfx::vk
