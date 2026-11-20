// STRATA Stage 16 Vulkan GLSL/SPIR-V contract and optional real GPU parity.
// The test is authored in the feature branch; execution is deferred to the
// owner's final native driver-enabled build. No success is presumed here.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataGpuCulling.h"
#include "engine/render/StrataVulkanCullingShaders.h"
#include "engine/gfx/vulkan/VkShaderCompiler.h"
#include "engine/gfx/vulkan/Vulkan.h"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::render::strata;
using namespace aiverse::gfx;

static ModelAsset sample() {
    ModelAsset out;
    ModelPrimitiveAsset p{};
    p.mesh=0;p.primitive=0;p.material=2;
    p.geometry.indices={0,1,2};
    p.geometry.roots={0};
    ClusterNode node{};
    node.resident=true;
    node.materialGroup=2;
    node.firstIndex=0;
    node.indexCount=3;
    p.geometry.nodes.push_back(node);
    p.geometry.bounds.push_back({{-0.2f,-0.2f,0.2f},{0.2f,0.2f,0.8f}});
    out.primitives.push_back(std::move(p));
    return out;
}

int main() {
    std::string error;
    std::vector<uint32_t> spirv;
    assert(aiverse::gfx::vk::compileComputeProgram(
        kVulkanGpuCulling,spirv,&error));
    assert(error.empty() && spirv.size()>20);
    assert(spirv.front()==0x07230203u); // SPIR-V magic
    const auto saved=spirv;
    assert(!aiverse::gfx::vk::compileComputeProgram(
        "#version 450\nvoid main(){missing_function();}",spirv,&error));
    assert(!error.empty() && spirv==saved);

    auto device=createVulkanDevice(VulkanWindow{},&error);
    if(!device) {
        std::cout<<"[strata-vulkan] SKIP no device: "<<error<<"\n";
        return 0; // shader compilation has still been checked
    }
    if(!device->supportsCompute()) {
        std::cout<<"[strata-vulkan] SKIP compute unsupported\n";
        return 0;
    }
    const auto asset=sample();
    std::vector<uint8_t> metadata;
    assert(packGpuMetadata(asset,metadata,&error));
    GpuMetadataHeader header{};
    std::memcpy(&header,metadata.data(),sizeof(header));
    header.version=2;
    std::memcpy(metadata.data(),&header,sizeof(header));
    GpuPrimitive primitive{};
    std::memcpy(&primitive,metadata.data()+header.primitivesByteOffset,
                sizeof(primitive));
    primitive.gpuFirstIndex=400;
    std::memcpy(metadata.data()+header.primitivesByteOffset,
                &primitive,sizeof(primitive));
    gfx::BufferDesc desc{};
    desc.type=gfx::BufferType::Storage;
    desc.usage=gfx::Usage::Immutable;
    desc.data=metadata.data();
    desc.size=metadata.size();
    desc.label="strata Vulkan metadata validation fixture";
    const auto gpuMetadata=device->createBuffer(desc);
    assert(gpuMetadata.valid());

    GpuClusterCulling culling;
    auto instance=makeGpuSceneInstance(mat4::identity(),0,121);
    GpuPassConfig view;
    view.viewProjection=mat4::identity();
    view.width=640;view.height=480;
    view.maxOutputClusters=16;
    view.errorThresholdPixels=1.0f;
    view.baseVertex=7; // verify indexed Vulkan vertexOffset propagation
    bool callbackCalled=false;
    GpuSelectionReadback result;
    device->beginFrame(64,64);
    GpuSelectionSubmission submission;
    const uint64_t serial=device->frameSerial();
    assert(culling.dispatch(*device,gpuMetadata,{instance},
                            view,serial,submission));
    assert(submission.valid() && submission.gpuIndirectPrepared());
    assert(culling.requestReadback(*device,submission,
        [&](GpuSelectionReadback read){
            callbackCalled=true;
            result=std::move(read);
        }));
    bool commandsReady=false;
    std::vector<uint8_t> commands;
    assert(device->readBufferAsync(submission.indirect,
        sizeof(gfx::DrawIndexedIndirectCommand)*view.maxOutputClusters,
        [&](std::vector<uint8_t> bytes){
            commands=std::move(bytes);
            commandsReady=true;
        }));
    assert(!callbackCalled && !commandsReady);
    device->endFrame();
    // Frame slots are fence-guarded. Two subsequent submissions allow
    // async Vulkan readback callback to arrive when the originating slot
    // has completed, with no explicit CPU GPU-wait in the render path.
    for(int frame=0;frame<3 && (!callbackCalled || !commandsReady);++frame){
        device->beginFrame(64,64);
        device->endFrame();
    }
    assert(callbackCalled && result.valid());
    assert(result.counters.invalid==0 && result.counters.overflow==0);
    assert(compareGpuSelection(asset,{instance},view,400,result,&error));
    // Stage16.4c: real headless GPU selection on two *different* cameras
    // (spot-depth projection and mirrored planar view). No main-view
    // range or indirect buffer may be treated as their valid output.
    const mat4 auxiliaryCameras[]={
        mat4::scale({.75f,.65f,1.f}),
        mat4::scale({-1.f,1.f,1.f})
    };
    for(const auto& projection:auxiliaryCameras){
        auto perPass=view;
        perPass.viewProjection=projection;
        bool passReady=false;
        GpuSelectionReadback selected{};
        device->beginFrame(64,64);
        GpuSelectionSubmission submitted{};
        assert(culling.dispatch(*device,gpuMetadata,{instance},perPass,
                                device->frameSerial(),submitted));
        assert(submitted.gpuIndirectPrepared());
        assert(culling.requestReadback(*device,submitted,
            [&](GpuSelectionReadback read){
                selected=std::move(read);
                passReady=true;
            }));
        device->endFrame();
        for(int n=0;n<3 && !passReady;++n){
            device->beginFrame(64,64);
            device->endFrame();
        }
        assert(passReady && selected.valid());
        assert(compareGpuSelection(asset,{instance},perPass,400,
                                   selected,&error));
    }
    assert(commandsReady && commands.size()==
           sizeof(gfx::DrawIndexedIndirectCommand)*view.maxOutputClusters);
    gfx::DrawIndexedIndirectCommand first{};
    std::memcpy(&first,commands.data(),sizeof(first));
    assert(first.indexCount==3 && first.instanceCount==1);
    assert(first.firstIndex==400 && first.baseVertex==7);
    assert(first.firstInstance==0);
    for(uint32_t i=1;i<view.maxOutputClusters;++i){
        gfx::DrawIndexedIndirectCommand zero{};
        std::memcpy(&zero,commands.data()+
                    size_t(i)*sizeof(zero),sizeof(zero));
        assert(zero.indexCount==0 && zero.instanceCount==0);
    }
    // GPU-side overflow/invalid guard is independent of CPU readback:
    // malformed primitive references must zero every indirect command.
    const auto invalidInstance=makeGpuSceneInstance(mat4::identity(),99,121);
    device->beginFrame(64,64);
    GpuSelectionSubmission rejected;
    assert(culling.dispatch(*device,gpuMetadata,{invalidInstance},view,
                            device->frameSerial(),rejected));
    assert(rejected.gpuIndirectPrepared());
    std::vector<uint8_t> rejectedCommands;
    bool rejectedReady=false;
    assert(device->readBufferAsync(rejected.indirect,
        sizeof(gfx::DrawIndexedIndirectCommand)*view.maxOutputClusters,
        [&](std::vector<uint8_t> bytes){
            rejectedCommands=std::move(bytes);
            rejectedReady=true;
        }));
    device->endFrame();
    for(int frame=0;frame<3 && !rejectedReady;++frame){
        device->beginFrame(64,64);
        device->endFrame();
    }
    assert(rejectedReady);
    for(uint32_t i=0;i<view.maxOutputClusters;++i){
        gfx::DrawIndexedIndirectCommand command{};
        std::memcpy(&command,rejectedCommands.data()+
                    size_t(i)*sizeof(command),sizeof(command));
        assert(command.indexCount==0);
    }
    // With a strictly validated full-primitive fallback contract, invalid
    // GPU selection must submit ONE original source command instead of
    // leaving an empty frame. This test only checks GPU command bytes; the
    // material and transform gate is tested independently by the router.
    auto guarded=view;
    guarded.gpuFallbackEnabled=true;
    guarded.gpuFallbackFirstIndex=123;
    guarded.gpuFallbackIndexCount=3;
    guarded.gpuFallbackBaseVertex=7;
    device->beginFrame(64,64);
    GpuSelectionSubmission fallback;
    assert(culling.dispatch(*device,gpuMetadata,{invalidInstance},guarded,
                            device->frameSerial(),fallback));
    assert(fallback.gpuIndirectPrepared());
    bool fallbackReady=false;
    std::vector<uint8_t> fallbackCommands;
    assert(device->readBufferAsync(fallback.indirect,
        sizeof(gfx::DrawIndexedIndirectCommand)*guarded.maxOutputClusters,
        [&](std::vector<uint8_t> bytes){
            fallbackReady=true;
            fallbackCommands=std::move(bytes);
        }));
    device->endFrame();
    for(int frame=0;frame<3 && !fallbackReady;++frame){
        device->beginFrame(64,64);
        device->endFrame();
    }
    assert(fallbackReady && fallbackCommands.size()==
           guarded.maxOutputClusters*sizeof(gfx::DrawIndexedIndirectCommand));
    gfx::DrawIndexedIndirectCommand original{};
    std::memcpy(&original,fallbackCommands.data(),sizeof(original));
    assert(original.indexCount==3 && original.instanceCount==1);
    assert(original.firstIndex==123 && original.baseVertex==7);
    for(uint32_t i=1;i<guarded.maxOutputClusters;++i){
        gfx::DrawIndexedIndirectCommand empty{};
        std::memcpy(&empty,fallbackCommands.data()+
                    size_t(i)*sizeof(empty),sizeof(empty));
        assert(empty.indexCount==0 && empty.instanceCount==0);
    }

    culling.shutdown(*device);
    device->destroyBuffer(gpuMetadata);
    std::cout<<"[strata-vulkan] compute GPU selection parity PASS\n";
    return 0;
}
