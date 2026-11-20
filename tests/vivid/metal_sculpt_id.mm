// Real Metal offscreen ID/depth pass and asynchronous RGBA8 readback.
// This is an opt-in selected-mesh fixture, NOT full Renderer3D occlusion.
// It deliberately verifies BGRA8 target -> RGBA8 host pixel conversion.
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>

#include "engine/character/vivid/SculptGpuIdPass.h"
#include "engine/character/vivid/SculptGpuPickContract.h"
#include "engine/character/vivid/SkinnedWeightPick.h"
#include "engine/gfx/Gfx.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <vector>

using namespace aiverse;
using namespace aiverse::vivid;

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&) {return false;}
}

namespace {
void require(bool ok,const char* reason) {
    if(!ok)throw std::runtime_error(reason);
}
std::vector<std::uint8_t> runSample(gfx::Device& device,
                                    SculptGpuIdPass& pass,
                                    const render::Model& model,
                                    const SkinnedWeightSurface& surface,
                                    std::uint32_t x,std::uint32_t y,
                                    std::span<const SculptGpuIdVertex> occluders={},
                                    const SculptGpuVertexSource* gpuSource=nullptr,
                                    bool* usedGpuSource=nullptr) {
    device.beginFrame(64,64);
    SculptGpuPickRequest req;
    req.generation=1;
    req.modelRevision=1;
    req.frameSerial=device.frameSerial();
    req.meshId=0;
    req.width=req.height=64;
    req.x=x;req.y=y;
    const auto viewProj=render::mat4::identity();
    require(pass.record(model,surface,model.vertices.size(),{},
                        viewProj,req,occluders,{},gpuSource),
            "VIVID Metal offscreen ID pass could not record");
    device.endFrame();
    SculptGpuIdPass::Completed completed;
    for(int attempt=0;attempt<60;++attempt) {
        device.beginFrame(64,64);
        const bool ready=pass.takeCompleted(completed);
        device.endFrame();
        if(ready) {
            require(completed.request==req &&
                    completed.rgba.size()==4,
                    "VIVID Metal async ID callback missing or stale");
            if(usedGpuSource)*usedGpuSource=completed.actualOverride;
            return completed.rgba;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("VIVID Metal async ID readback timeout");
}
void test() {
    @autoreleasepool {
        id<MTLDevice> metal=MTLCreateSystemDefaultDevice();
        if(!metal)throw std::runtime_error("no Metal device");
        CAMetalLayer* layer=[CAMetalLayer layer];
        layer.device=metal;
        layer.drawableSize=CGSizeMake(64,64);
        layer.pixelFormat=MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly=YES;
        auto device=gfx::createMetalDevice((__bridge void*)layer);
        require(bool(device),"VIVID Metal RHI device creation failed");

        render::Model model;
        model.vertices.resize(6);
        // Two CCW triangles cover pixel (32,32): ordinal 0 is FAR,
        // ordinal 1 is NEAR. A working depth test must return ordinal 1.
        const std::array<render::float3,3> xy{{
            {-0.7f,-0.7f,0},{0.7f,-0.7f,0},{0,0.7f,0}
        }};
        for(int i=0;i<3;++i) {
            auto near=xy[std::size_t(i)];
            auto far=near;
            far.z=0.8f;
            near.z=0.2f;
            model.vertices[std::size_t(i)].pos=far;
            model.vertices[std::size_t(i+3)].pos=near;
        }
        model.indices={0,1,2,3,4,5};
        render::MeshDef mesh;
        render::Primitive p;
        p.firstIndex=0;p.indexCount=6;
        mesh.primitives.push_back(p);
        model.meshes.push_back(std::move(mesh));
        render::Node node;
        node.mesh=0;
        model.nodes.push_back(node);
        const std::array<render::mat4,1> pose{{render::mat4::identity()}};
        SkinnedWeightSurface surface;
        require(prepareSkinnedWeightSurface(model,0,pose,surface),
                "VIVID Metal fixture skinned surface preparation failed");
        SculptGpuIdPass pass;
        require(pass.initialize(*device),
                "VIVID Metal shader/pipeline for ID pass unavailable");
        const auto center=runSample(*device,pass,model,surface,32,32);
        require(center[0]==2 && center[1]==0 &&
                center[2]==0 && center[3]==0,
                "VIVID Metal ID/depth pixel oracle did not select nearer face");
        const auto outside=runSample(*device,pass,model,surface,2,2);
        require(outside==std::vector<std::uint8_t>({0,0,0,0}),
                "VIVID Metal GPU ID clear pixel is not zero");
        // 7AP: a second world-space scene element, not in the selected
        // model, must occlude the selectable near face by hardware depth.
        const std::array<SculptGpuIdVertex,3> blocker{{
            {{-0.7f,-0.7f,0.1f},{0,0,0,0}},
            {{ 0.7f,-0.7f,0.1f},{0,0,0,0}},
            {{ 0.0f, 0.7f,0.1f},{0,0,0,0}}
        }};
        const auto blocked=runSample(*device,pass,model,surface,32,32,blocker);
        require(blocked==std::vector<std::uint8_t>({0,0,0,0}),
                "VIVID Metal ignored nearer zero-ID depth occluder");
        const std::array<SculptGpuIdVertex,3> behind{{
            {{-0.7f,-0.7f,0.9f},{0,0,0,0}},
            {{ 0.7f,-0.7f,0.9f},{0,0,0,0}},
            {{ 0.0f, 0.7f,0.9f},{0,0,0,0}}
        }};
        const auto unobstructed=runSample(*device,pass,model,surface,32,32,behind);
        require(unobstructed==std::vector<std::uint8_t>({2,0,0,0}),
                "VIVID Metal allowed a farther occluder to hide nearer face");
        // 7AQ: real GPU baseTexture alpha is sampled per fragment. When
        // the source texel is transparent, no masked triangle may write
        // depth/ID; when opaque, the near triangle ID must be preserved.
        const std::array<std::uint8_t,4> alphaHole{{255,255,255,0}};
        gfx::TextureDesc maskDesc;
        maskDesc.width=1;maskDesc.height=1;
        maskDesc.format=gfx::PixelFormat::RGBA8;
        maskDesc.filter=gfx::Filter::Nearest;
        maskDesc.data=alphaHole.data();
        maskDesc.label="VIVID alpha mask ID oracle";
        auto alphaTexture=device->createTexture(maskDesc);
        require(alphaTexture.valid(),"Metal test alpha texture unavailable");
        model.images.emplace_back();
        model.images[0].tex=alphaTexture;
        model.materials.emplace_back();
        model.materials[0].alpha=render::AlphaMode::Mask;
        model.materials[0].baseTex=0;
        model.materials[0].alphaCutoff=0.5f;
        const auto hole=runSample(*device,pass,model,surface,32,32);
        require(hole==std::vector<std::uint8_t>({0,0,0,0}),
                "Metal alpha-tested GPU triangle failed to discard a hole");
        const std::array<std::uint8_t,4> alphaSolid{{255,255,255,255}};
        device->updateTexture(alphaTexture,0,0,1,1,alphaSolid.data(),4);
        const auto solid=runSample(*device,pass,model,surface,32,32);
        require(solid==std::vector<std::uint8_t>({2,0,0,0}),
                "Metal alpha-tested GPU triangle lost opaque texel ID");

        // 7AT: the actual 60B GPU override is the positional authority.
        // Move every GPU vertex completely off-screen WITHOUT changing the
        // CPU SkinnedWeightSurface. A true GPU-sourced ID pass must clear at
        // the center; a CPU-fed pose would incorrectly keep triangle ID 2.
        model.materials[0].alpha=render::AlphaMode::Opaque;
        std::vector<render::ModelVertex> actorBytes=model.vertices;
        for(auto& vertex:actorBytes)vertex.pos.x+=2.0f;
        gfx::BufferDesc gpuDesc;
        gpuDesc.type=gfx::BufferType::Storage;
        gpuDesc.usage=gfx::Usage::Immutable;
        gpuDesc.size=actorBytes.size()*sizeof(render::ModelVertex);
        gpuDesc.data=actorBytes.data();
        gpuDesc.label="VIVID actual actor 60B vertex stream fixture";
        auto displacedStream=device->createBuffer(gpuDesc);
        require(displacedStream.valid(),"Metal fixture GPU 60B actor input missing");
        SculptGpuVertexSource source;
        require(prepareSculptGpuVertexSource(
                    model,0,pose,render::mat4::identity(),
                    displacedStream,std::uint32_t(actorBytes.size()),
                    {},actorBytes.size(),source),
                "Metal fixture GPU-source index/palette preparation failed");
        bool usedOverride=false;
        const auto fromActual=runSample(
            *device,pass,model,surface,32,32,{},&source,&usedOverride);
        require(usedOverride &&
                fromActual==std::vector<std::uint8_t>({0,0,0,0}),
                "ID pass rendered CPU pose instead of exact 60B GPU override");
        // 7AU: a sampled selected Mask must also follow the ACTUAL
        // displaced GPU actor vertices, not the CPU surface position.
        model.materials[0].alpha=render::AlphaMode::Mask;
        require(prepareSculptGpuVertexSource(
                    model,0,pose,render::mat4::identity(),
                    displacedStream,std::uint32_t(actorBytes.size()),
                    {},actorBytes.size(),source) && source.maskedSelection,
                "Metal selected GPU Mask source could not prepare");
        usedOverride=false;
        const auto displacedMasked=runSample(
            *device,pass,model,surface,32,32,{},&source,&usedOverride);
        require(usedOverride &&
                displacedMasked==std::vector<std::uint8_t>({0,0,0,0}),
                "Metal Mask used stale CPU positions rather than GPU source");
        device->destroyBuffer(displacedStream);
        gpuDesc.data=model.vertices.data();
        auto originalStream=device->createBuffer(gpuDesc);
        require(originalStream.valid(),"Metal original GPU actor source missing");
        require(prepareSculptGpuVertexSource(
                    model,0,pose,render::mat4::identity(),
                    originalStream,std::uint32_t(model.vertices.size()),
                    {},model.vertices.size(),source) &&
                source.maskedSelection,
                "Metal identity GPU-selected Mask source preparation failed");
        // 7AV: a stale or edited UV mapping cannot be corroborated
        // against a different actual GPU ModelVertex source.
        auto staleUvSource=source;
        staleUvSource.maskInputs[0].uvAlpha[0]+=0.25f;
        device->beginFrame(64,64);
        SculptGpuPickRequest staleRequest;
        staleRequest.generation=17u;
        staleRequest.modelRevision=1u;
        staleRequest.frameSerial=device->frameSerial();
        staleRequest.meshId=0;
        staleRequest.width=staleRequest.height=64u;
        staleRequest.x=staleRequest.y=32u;
        const bool staleAccepted=pass.record(
            model,surface,model.vertices.size(),{},
            render::mat4::identity(),staleRequest,{}, {},&staleUvSource);
        device->endFrame();
        require(!staleAccepted,
                "GPU selected Mask silently accepted stale source UVs");
        device->updateTexture(alphaTexture,0,0,1,1,alphaHole.data(),4);
        usedOverride=false;
        const auto gpuMaskHole=runSample(
            *device,pass,model,surface,32,32,{},&source,&usedOverride);
        require(usedOverride &&
                gpuMaskHole==std::vector<std::uint8_t>({0,0,0,0}),
                "Metal actual GPU source Mask failed to discard alpha hole");
        device->updateTexture(alphaTexture,0,0,1,1,alphaSolid.data(),4);
        usedOverride=false;
        const auto unchanged=runSample(
            *device,pass,model,surface,32,32,{},&source,&usedOverride);
        require(usedOverride &&
                unchanged==std::vector<std::uint8_t>({2,0,0,0}),
                "GPU-selected sampled Mask lost in-view actor ModelVertex");
        device->destroyBuffer(originalStream);
        model.materials[0].alpha=render::AlphaMode::Opaque;

        // Same packed 60B input but a GPU compute skinning transform moves
        // the triangles. This proves the ID pass does not silently bypass
        // MotionWeave's current skeleton pose for an actor using GPU morph.
        model.nodes.resize(2);
        model.nodes[0].skin=0;
        model.skins.resize(1);
        model.skins[0].joints={1};
        model.skins[0].inverseBind={render::mat4::identity()};
        model.meshes[0].primitives[0].jointPalette={0};
        for(auto& vertex:model.vertices) {
            vertex.joints[0]=0;vertex.weights[0]=255;
        }
        gpuDesc.data=model.vertices.data();
        auto skinStream=device->createBuffer(gpuDesc);
        require(skinStream.valid(),"Metal GPU skinned actor buffer missing");
        const std::array<render::mat4,2> animatedPose{{
            render::mat4::identity(),
            render::mat4::translate({2.0f,0.0f,0.0f})
        }};
        require(prepareSculptGpuVertexSource(
                    model,0,animatedPose,render::mat4::identity(),
                    skinStream,std::uint32_t(model.vertices.size()),
                    {},model.vertices.size(),source) &&
                source.params.skinned==1,
                "Metal joint palette stream preparation failed");
        usedOverride=false;
        const auto animatedGpu=runSample(
            *device,pass,model,surface,32,32,{},&source,&usedOverride);
        require(usedOverride &&
                animatedGpu==std::vector<std::uint8_t>({0,0,0,0}),
                "GPU ModelVertex ID pass ignored actual skinned joint pose");
        device->destroyBuffer(skinStream);

        // 7AV: two unequal UNORM8 skin influences follow Renderer3D's
        // 3x4 row-blend order. A following model-world transform MUST
        // apply after skinning; the original GPU source positions are
        // still borrowed and never reconstructed on the CPU.
        model.nodes.resize(3);
        model.skins[0].joints={1,2};
        model.skins[0].inverseBind={
            render::mat4::identity(),render::mat4::identity()};
        model.meshes[0].primitives[0].jointPalette={0,1};
        for(auto& vertex:model.vertices) {
            vertex.joints[0]=0;vertex.weights[0]=128;
            vertex.joints[1]=1;vertex.weights[1]=64;
        }
        gpuDesc.data=model.vertices.data();
        auto parityStream=device->createBuffer(gpuDesc);
        require(parityStream.valid(),
                "Metal renderer 3x4 palette parity actor source missing");
        const std::array<render::mat4,3> twoJointPose{{
            render::mat4::identity(),
            render::mat4::translate({ 2.0f,0,0}),
            render::mat4::translate({-2.0f,0,0})
        }};
        require(prepareSculptGpuVertexSource(
                    model,0,twoJointPose,render::mat4::identity(),
                    parityStream,std::uint32_t(model.vertices.size()),
                    {},model.vertices.size(),source) &&
                source.params.skinned==1,
                "Metal two-joint Renderer3D parity preparation failed");
        usedOverride=false;
        const auto shiftedBySkin=runSample(
            *device,pass,model,surface,32,32,{},&source,&usedOverride);
        require(usedOverride &&
                shiftedBySkin==std::vector<std::uint8_t>({0,0,0,0}),
                "Metal native renderer row-order weighted skin did not move pixel");
        require(prepareSculptGpuVertexSource(
                    model,0,twoJointPose,
                    render::mat4::translate({-2.0f/3.0f,0,0}),
                    parityStream,std::uint32_t(model.vertices.size()),
                    {},model.vertices.size(),source),
                "Metal GPU parity post-skin world transform preparation failed");
        usedOverride=false;
        const auto restoredByWorld=runSample(
            *device,pass,model,surface,32,32,{},&source,&usedOverride);
        require(usedOverride &&
                restoredByWorld==std::vector<std::uint8_t>({2,0,0,0}),
                "Metal post-skin model world transform broke renderer algebra");
        device->destroyBuffer(parityStream);

        require(pass.shutdownAfterIdle(),
                "VIVID Metal ID resources did not pass GPU idle retirement");
        device->destroyTexture(alphaTexture);
        std::cout<<"VIVID Metal sculpt ID depth/readback pass=1\n";
    }
}
}
int main() {
    try {test();return 0;}
    catch(const std::exception& error) {
        std::cerr<<"VIVID Metal sculpt ID fail: "<<error.what()<<"\n";
        return 1;
    }
}
