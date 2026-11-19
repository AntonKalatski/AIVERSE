#include "StudioMetalScene.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace aiverse::studio {
using namespace aiverse::render;
StudioMetalScene::StudioMetalScene(void* metalLayer)
    : device_(gfx::createMetalDevice(metalLayer)) {
    if (!device_) throw std::runtime_error("Metal device unavailable");
    if (!renderer_.init(*device_)) throw std::runtime_error("AIVERSE Renderer3D init failed");
    renderer_.setShadowQuality(ShadowQuality::Off); // cheap editor preview until full scene parity
    MeshBuilder builder;
    builder.color(rgba(255,255,255));
    builder.box({0,0,0}, {1,1,1});
    cube_ = builder.upload(*device_);
    if (!cube_.vb.valid() || !cube_.ib.valid()) throw std::runtime_error("Preview cube mesh upload failed");
    camera_.pitch = 0.85f;
    camera_.yaw = 0.55f;
}
StudioMetalScene::~StudioMetalScene() {
    if (device_) {
        if (cube_.vb.valid()) device_->destroyBuffer(cube_.vb);
        if (cube_.ib.valid()) device_->destroyBuffer(cube_.ib);
        renderer_.shutdown();
    }
}
void StudioMetalScene::orbit(float dx, float dy) {
    camera_.yaw += dx * 0.007f;
    camera_.pitch = std::clamp(camera_.pitch - dy * 0.007f, 0.18f, 1.45f);
}
void StudioMetalScene::zoom(float wheel) {
    camera_.distance = std::clamp(camera_.distance * std::exp(-wheel * 0.035f), 2.0f, 100000.0f);
}
void StudioMetalScene::frame(int framebufferWidth, int framebufferHeight, const editor::MapData& map) {
    if (!device_ || framebufferWidth < 2 || framebufferHeight < 2) return;
    if (!cameraInitialized_ || lastMapWidth_ != map.w || lastMapHeight_ != map.h) {
        camera_.target = {map.w * .5f, 0.0f, map.h * .5f};
        camera_.distance = std::clamp(float(std::max(map.w, map.h)) * 1.4f, 16.0f, 100000.0f);
        cameraInitialized_ = true;
        lastMapWidth_ = map.w;
        lastMapHeight_ = map.h;
    }
    device_->beginFrame(framebufferWidth, framebufferHeight);
    renderer_.beginScene(camera_, framebufferWidth, framebufferHeight);

    // Real map dimensions and positions from the active native Workspace.
    // Temporary primitive preview (NOT legacy editor terrain, animation or materials).
    const float w = float(std::max(map.w, 1));
    const float h = float(std::max(map.h, 1));
    renderer_.drawMesh(cube_,
                       mat4::translate({w*.5f,-0.08f,h*.5f}) * mat4::scale({w,.12f,h}),
                       {0.18f,0.26f,0.33f,1}, kDrawNoCastShadow);
    const int xStep = std::max(1, map.w / 48);
    const int zStep = std::max(1, map.h / 48);
    for (int x=0; x<=map.w; x+=xStep)
        renderer_.line({float(x),.01f,0},{float(x),.01f,h},rgba(55,84,109,120));
    for (int z=0; z<=map.h; z+=zStep)
        renderer_.line({0,.01f,float(z)},{w,.01f,float(z)},rgba(55,84,109,120));

    const size_t limit = std::min(map.entities.size(),size_t(2000));
    for (size_t i=0;i<limit;++i) {
        const auto& e = map.entities[i];
        // Flat world preview; planet surfaces require a separate adapter.
        if (!e.at.isNull()) continue;
        float terrainHeight = 0;
        const int gx = int(std::floor(e.x)), gz = int(std::floor(e.z));
        if (map.inside(gx,gz)) {
            const auto index = map.idx(gx,gz);
            if (index < map.heights.size()) terrainHeight += map.heights[index] / 16.0f;
            if (index < map.levels.size()) terrainHeight += map.levels[index] * 1.0f;
        }
        const float3 at{e.x, terrainHeight + .6f, e.z};
        const float4 color = e.owner == 0 ? float4{.65f,.75f,.84f,1} :
                             e.owner == 1 ? float4{.44f,.70f,.98f,1} :
                             e.owner == 2 ? float4{.94f,.52f,.42f,1} : float4{.83f,.76f,.48f,1};
        renderer_.drawMesh(cube_, mat4::translate(at) * mat4::scale({1,1.2f,1}), color, kDrawNoCastShadow);
    }
    gfx::PassDesc pass;
    pass.clearColor[0] = 0.038f; pass.clearColor[1] = 0.064f;
    pass.clearColor[2] = 0.098f; pass.clearColor[3] = 1.0f;
    renderer_.render(pass);
    device_->endPass();
    device_->endFrame();
}
}
