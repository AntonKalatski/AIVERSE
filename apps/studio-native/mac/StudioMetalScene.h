// macOS-only native preview: AIVERSE gfx::Device + Renderer3D over a CAMetalLayer.
// No ImGui, no CPU texture readback, no dependency on UI component hierarchy.
#pragma once
#include <chrono>
#include <memory>
#include <cstdint>
#include <limits>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <filesystem>
#include "apps/studio-native/StudioModelPicking.h"
#include "apps/studio-native/StudioDocumentVisuals.h"
#include "apps/editor/core/MapDoc.h"
#include "engine/gfx/Gfx.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/Prisma.h"
#include "engine/render/RenderLightingRuntime.h"
#include "engine/world/NereidManifestHost.h"
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
               uint64_t previewRevision, float levelHeight=1.5f,
               const std::string& projectPath={}, const Json& rules=Json{},
               const StudioFacialPreviewBinding* facialBinding=nullptr);
    void captureNextFrame(gfx::Device::CaptureFn callback) { device_->captureNextFrame(std::move(callback)); }
    const std::string& lightingStatus() const { return lightingStatus_; }
    bool facialReady() const noexcept{return facialReady_;}
    const std::string& facialError() const noexcept{return facialError_;}
    std::string pick(float x, float y, float width, float height,
                     const editor::MapDoc& map, bool fpsProject) const;
    void orbit(float dx, float dy);
    void zoom(float wheel);
    void pan(float dx, float dy);
    void look(float dx, float dy);
    void move(float right, float up, float forward);
    void focusSelected(const editor::MapDoc& map, bool fpsProject, const std::string& selectedId);
    void resetView();
    // Unity-style Scene view helpers: animated orbit to an axis view and the
    // Persp/Iso toggle. Orientation/projection are editor camera state only.
    void setOrientation(float yaw, float pitch);
    void setOrthographic(bool ortho);
    struct CameraState { float yaw = 0, pitch = 0, distance = 0; bool ortho = false; };
    CameraState cameraState() const { return {camera_.yaw, camera_.pitch, camera_.distance, camera_.ortho}; }
    // Corner orientation gizmo rendered by Renderer3D, for the default host where
    // the Metal view covers the WebView (the HTML gizmo is used in composite mode).
    void setOrientationGizmo(bool enabled) { orientationGizmo_ = enabled; }
    // Hit test in the same units as width/height: -1 none, 0..5 arm
    // (axis*2 + negative), 6 centre (projection toggle).
    int orientationGizmoHit(float x, float y, float width, float height) const;
    // Applies a hit: snaps to the axis view (auto Iso) or toggles the projection.
    void activateOrientationGizmo(int hit);
    void resetCamera() { lightingRevision_ = std::numeric_limits<uint64_t>::max(); cameraInitialized_ = false; terrainRevision_ = std::numeric_limits<uint64_t>::max(); clearDocumentModels(); }
private:
    std::unique_ptr<gfx::Device> device_;
    std::unique_ptr<render::RenderDeviceLifetime> deviceLifetime_;
    render::Renderer3D renderer_;
    world::NereidManifestHost nereid_;
    double underwaterFrameTime_=0;
    render::Prisma post_;
    render::RenderLightingRuntime lightingRuntime_;
    std::unique_ptr<render::Model> lightingModel_;
    uint64_t lightingRevision_ = std::numeric_limits<uint64_t>::max();
    std::string lightingStatus_ = "Preview geometry · GI: off · Direct: standard";
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
    // Each model is owned by the native Scene; upload keeps CPU vertices for
    // triangle picking. The draw list and hit list share the exact instances.
    std::map<std::string,std::unique_ptr<render::Model>> documentModels_;
    std::set<std::string> rejectedDocumentModels_;
    std::map<std::string,std::filesystem::file_time_type> documentModelTimes_;
    std::vector<StudioModelPickInstance> modelInstances_;
    void releasePreviewModel(render::Model& model);
    void clearDocumentModels();
    void syncDocumentModels(const editor::MapDoc& doc, const std::string& projectPath,
                            const Json& rules, const std::vector<StudioDocumentProxy>& proxies);
    // Facial/material preview FXAA (LDR); the map view keeps PRISMA HDR post_.
    render::PostFx antialiasing_;
    render::RTSCamera camera_;
    render::AdaptiveShadowResolution shadowResolution_;
    bool cameraInitialized_ = false;
    // Short eased transition for focus / axis snaps; any direct input cancels it.
    struct CameraTween {
        bool active = false;
        render::float3 fromTarget{}, toTarget{};
        float fromDistance = 0, toDistance = 0, fromYaw = 0, toYaw = 0, fromPitch = 0, toPitch = 0;
        std::chrono::steady_clock::time_point start{};
    } tween_;
    void startTween(render::float3 target, float distance, float yaw, float pitch);
    void stepTween();
    // Camera actually submitted/picked: clip planes follow distance, ortho keeps
    // its framing from `distance` while the eye stays inside the depth range.
    render::RTSCamera viewCamera() const;
    bool orientationGizmo_ = false;
    bool autoIso_ = false; // Iso entered by an axis snap; orbit/look returns to Persp
    void drawOrientationGizmo(const render::RTSCamera& view, int width, int height);
    bool lodDiagnostics_ = false;
    uint64_t diagnosticFrames_ = 0;
    int lastMapWidth_ = 0;
    int lastMapHeight_ = 0;
};
}
