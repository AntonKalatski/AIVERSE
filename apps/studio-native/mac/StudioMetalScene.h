// macOS-only native preview: AIVERSE gfx::Device + Renderer3D over a CAMetalLayer.
// No ImGui, no CPU texture readback, no dependency on UI component hierarchy.
#pragma once
#include <memory>
#include <cstdint>
#include <limits>
#include <string>
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
    void frame(int framebufferWidth, int framebufferHeight, const editor::MapDoc& doc,
               bool fpsProject, const std::string& selectedId, float levelHeight = 1.5f);
    std::string pick(float x, float y, float width, float height,
                     const editor::MapDoc& map, bool fpsProject) const;
    void orbit(float dx, float dy);
    void zoom(float wheel);
    void resetCamera() { cameraInitialized_ = false; terrainRevision_ = std::numeric_limits<uint64_t>::max(); }
private:
    std::unique_ptr<gfx::Device> device_;
    render::Renderer3D renderer_;
    render::Mesh cube_;
    render::Mesh terrain_;
    uint64_t terrainRevision_ = std::numeric_limits<uint64_t>::max();
    float terrainLevelHeight_ = 1.5f;
    render::RTSCamera camera_;
    bool cameraInitialized_ = false;
    int lastMapWidth_ = 0;
    int lastMapHeight_ = 0;
};
}
