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
#include "engine/render/PostFx.h"
#include "engine/render/AdaptiveShadowResolution.h"
#include "engine/render/RenderDeviceLifetime.h"
#include "apps/studio-native/mac/StudioFacialGpuPreview.h"

namespace aiverse::studio {
class StudioMetalScene final {
public:
    explicit StudioMetalScene(void* metalLayer);
    ~StudioMetalScene();
    StudioMetalScene(const StudioMetalScene&) = delete;
    StudioMetalScene& operator=(const StudioMetalScene&) = delete;
    void frame(int framebufferWidth, int framebufferHeight, const editor::MapDoc& doc,
               bool fpsProject, const std::string& selectedId,
               const std::string& previewName, const Json& previewFactors,
               uint64_t previewRevision, float levelHeight,
               const std::string& projectRoot,
               const StudioFacialPreviewBinding& facialBinding);
    bool facialReady() const noexcept{return facialReady_;}
    const std::string& facialError() const noexcept{return facialError_;}
    std::string pick(float x, float y, float width, float height,
                     const editor::MapDoc& map, bool fpsProject) const;
    void orbit(float dx, float dy);
    void zoom(float wheel);
    void resetCamera() { cameraInitialized_ = false; terrainRevision_ = std::numeric_limits<uint64_t>::max(); }
private:
    std::unique_ptr<gfx::Device> device_;
    std::unique_ptr<render::RenderDeviceLifetime> deviceLifetime_;
    render::Renderer3D renderer_;
    render::Mesh cube_;
    render::Mesh studioCyclorama_;
    render::Mesh terrain_;
    std::unique_ptr<render::Model> materialSphere_;
    StudioFacialGpuPreview facialGpu_;
    bool facialMode_=false,facialReady_=false;
    std::string facialError_;
    uint64_t materialRevision_ = std::numeric_limits<uint64_t>::max();
    bool materialMode_ = false;
    uint64_t terrainRevision_ = std::numeric_limits<uint64_t>::max();
    float terrainLevelHeight_ = 1.5f;
    render::PostFx antialiasing_;
    render::RTSCamera camera_;
    render::AdaptiveShadowResolution shadowResolution_;
    bool cameraInitialized_ = false;
    int lastMapWidth_ = 0;
    int lastMapHeight_ = 0;
};
}
