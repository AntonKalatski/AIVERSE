// macOS-only native preview: AIVERSE gfx::Device + Renderer3D over a CAMetalLayer.
// No ImGui, no CPU texture readback, no dependency on UI component hierarchy.
#pragma once
#include <memory>
#include "apps/editor/core/MapDoc.h"
#include "engine/gfx/Gfx.h"
#include "engine/render/Renderer3D.h"

namespace aiverse::studio {
class StudioMetalScene final {
public:
    explicit StudioMetalScene(void* metalLayer);
    ~StudioMetalScene();
    StudioMetalScene(const StudioMetalScene&) = delete;
    StudioMetalScene& operator=(const StudioMetalScene&) = delete;
    void frame(int framebufferWidth, int framebufferHeight, const editor::MapData& map);
    void orbit(float dx, float dy);
    void zoom(float wheel);
private:
    std::unique_ptr<gfx::Device> device_;
    render::Renderer3D renderer_;
    render::Mesh cube_;
    render::RTSCamera camera_;
    bool cameraInitialized_ = false;
    int lastMapWidth_ = 0;
    int lastMapHeight_ = 0;
};
}
