// UI-independent editor API for native HTML5 hosts; intentionally no ImGui.
#pragma once
#include <string>
#include <future>
#include <optional>
#include <vector>
#include <filesystem>
#include "apps/studio-native/StudioAssetTransfers.h"
#include "apps/studio-native/StudioFacialPreviewBinding.h"
#include "apps/editor/core/Workspace.h"
#include "apps/studio-native/StudioWindPreview.h"
namespace aiverse::studio {
class StudioSession {
public:
    explicit StudioSession(const std::string& engineRoot, const std::string& project = {});
    Json dispatch(const Json& request);
    const editor::Workspace& workspace() const { return ws_; }
    int selectedEntityIndex() const;
    const std::string& selectedObjectId() const { return selected_; }
    const std::string& previewMaterialName() const { return previewMaterialName_; }
    const Json& previewMaterialFactors() const { return previewMaterialFactors_; }
    uint64_t previewMaterialRevision() const { return previewMaterialRevision_; }
    // «Ветер» panel: preview wind + rig debug view (session only, not saved).
    const StudioWindState& windPreview() const { return wind_; }
    void setWindSceneStats(Json stats) { windSceneStats_ = std::move(stats); }
    Json saveMapAs(const std::string& path); // trusted native Save panel only
    const StudioFacialPreviewBinding& facialPreviewBinding() const noexcept {
        return facialPreview_;
    }
    void setFacialPreviewGpuStatus(bool rendered,const std::string& reason) {
        facialPreview_.setGpuStatus(rendered,reason);
    }
    Json openProject(const std::string& path); // only call with path returned by a trusted native picker
    Json previewExternalAssets(const std::vector<std::filesystem::path>& files,
                               const std::string& destination); // native picker ONLY
private:
    Json snapshot() const;
    Json propertiesFor(const std::string& entityId) const;
    int indexFor(const std::string& id) const;
    editor::Workspace ws_;
    std::string selected_;
    StudioFacialPreviewBinding facialPreview_;
    std::future<Json> humanBuild_;
    Json humanBuildResult_=Json::object();
    std::string humanBuildProject_;
    std::string previewMaterialName_;
    Json previewMaterialFactors_;
    uint64_t previewMaterialRevision_ = 0;
    void refreshMaterialPreview();
    std::optional<StudioTransferPlan> pendingAssetTransfer_;
    StudioWindState wind_;
    Json windSceneStats_;
};
}
