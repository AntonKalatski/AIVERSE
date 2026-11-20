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
                                    std::span<const SculptGpuIdVertex> occluders={}) {
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
                        viewProj,req,occluders),
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
        require(pass.shutdownAfterIdle(),
                "VIVID Metal ID resources did not pass GPU idle retirement");
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
