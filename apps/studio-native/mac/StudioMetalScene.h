// macOS-only native preview: AIVERSE gfx::Device + Renderer3D over a CAMetalLayer.
// No ImGui, no CPU texture readback, no dependency on UI component hierarchy.
#pragma once
#include <memory>
#include <cstdint>
#include <limits>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <filesystem>
#include <chrono>
#include <optional>
#include "apps/studio-native/StudioModelPicking.h"
#include "apps/studio-native/StudioDocumentVisuals.h"
#include "apps/editor/core/MapDoc.h"
#include "engine/gfx/Gfx.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/Prisma.h"
#include "engine/render/RenderLightingRuntime.h"
#include "engine/world/NereidManifestHost.h"
#include "apps/studio-native/StudioWindPreview.h"

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
               const std::string& projectPath={}, const Json& rules=Json{});
    void captureNextFrame(gfx::Device::CaptureFn callback) { device_->captureNextFrame(std::move(callback)); }
    const std::string& lightingStatus() const { return lightingStatus_; }
    // Preview wind for every rigged model (<model>.wind.json) + debug view.
    void setWindPreview(const StudioWindState& state) { wind_ = state; }
    Json windStats() const;
    std::string pick(float x, float y, float width, float height,
                     const editor::MapDoc& map, bool fpsProject) const;
    void orbit(float dx, float dy);
    void zoom(float wheel);
    void pan(float dx, float dy);
    void look(float dx, float dy);
    void move(float right, float up, float forward);
    void focusSelected(const editor::MapDoc& map, bool fpsProject, const std::string& selectedId);
    void resetView();
    // Automation (AIVERSE_STUDIO_WIND_SHOTS): frame a point of interest.
    void setShotCamera(float tx, float ty, float tz, float distance, float yaw, float pitch) {
        camera_.target={tx,ty,tz}; camera_.distance=distance; camera_.yaw=yaw; camera_.pitch=pitch;
        cameraInitialized_=true;
    }
    void resetCamera() { lightingRevision_=std::numeric_limits<uint64_t>::max(); cameraInitialized_ = false; terrainRevision_ = std::numeric_limits<uint64_t>::max(); clearDocumentModels(); }
private:
    std::unique_ptr<gfx::Device> device_;
    render::Renderer3D renderer_;
    world::NereidManifestHost nereid_;
    double underwaterFrameTime_=0;
    render::Prisma post_;
    render::RenderLightingRuntime lightingRuntime_;
    std::unique_ptr<render::Model> lightingModel_;
    uint64_t lightingRevision_ = std::numeric_limits<uint64_t>::max();
    std::string lightingStatus_ = "Preview geometry · GI: off · Direct: standard";
    render::Mesh cube_;
    render::Mesh terrain_;
    std::unique_ptr<render::Model> materialSphere_;
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
    StudioWindState wind_;
    size_t riggedInstances_ = 0, riggedParts_ = 0;
    std::map<const render::Model*, std::optional<render::wind::ModelWindRigView>> windRigs_;
    std::chrono::steady_clock::time_point windClock_ = std::chrono::steady_clock::now();
    render::RTSCamera camera_;
    bool cameraInitialized_ = false;
    bool lodDiagnostics_ = false;
    uint64_t diagnosticFrames_ = 0;
    int lastMapWidth_ = 0;
    int lastMapHeight_ = 0;
};
}
