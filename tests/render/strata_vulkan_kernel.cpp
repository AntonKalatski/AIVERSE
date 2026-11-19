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
    auto* header=reinterpret_cast<GpuMetadataHeader*>(metadata.data());
    header->version=2;
    auto* primitive=reinterpret_cast<GpuPrimitive*>(
        metadata.data()+header->primitivesByteOffset);
    primitive->gpuFirstIndex=400;
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
    bool callbackCalled=false;
    GpuSelectionReadback result;
    device->beginFrame(64,64);
    GpuSelectionSubmission submission;
    const uint64_t serial=device->frameSerial();
    assert(culling.dispatch(*device,gpuMetadata,{instance},
                            view,serial,submission));
    assert(submission.valid());
    assert(culling.requestReadback(*device,submission,
        [&](GpuSelectionReadback read){
            callbackCalled=true;
            result=std::move(read);
        }));
    assert(!callbackCalled); // strictly asynchronous, no render-thread stall
    device->endFrame();
    // Frame slots are fence-guarded. Two subsequent submissions allow
    // async Vulkan readback callback to arrive when the originating slot
    // has completed, with no explicit CPU GPU-wait in the render path.
    for(int frame=0;frame<3 && !callbackCalled;++frame){
        device->beginFrame(64,64);
        device->endFrame();
    }
    assert(callbackCalled && result.valid());
    assert(result.counters.invalid==0 && result.counters.overflow==0);
    assert(compareGpuSelection(asset,{instance},view,400,result,&error));
    culling.shutdown(*device);
    device->destroyBuffer(gpuMetadata);
    std::cout<<"[strata-vulkan] compute GPU selection parity PASS\n";
    return 0;
}
