// UI-independent editor API for native HTML5 hosts; intentionally no ImGui.
#pragma once
#include <string>
#include <future>
#include <optional>
#include <set>
#include <vector>
#include <filesystem>
#include <functional>
#include "apps/studio-native/StudioAssetTransfers.h"
#include "apps/studio-native/StudioFacialPreviewBinding.h"
#include "apps/editor/core/Workspace.h"
namespace aiverse::studio {
class StudioSession {
public:
    explicit StudioSession(const std::string& engineRoot, const std::string& project = {});
    Json dispatch(const Json& request);
    const editor::Workspace& workspace() const { return ws_; }
    int selectedEntityIndex() const;
    const std::string& selectedObjectId() const { return selected_; }
    // Multi-selection (primary = selectedObjectId(), always last) and editor-only
    // Hierarchy flags: hidden entities are not drawn or picked, locked ones not picked.
    std::vector<std::string> selectedObjectIds() const;
    const std::set<std::string>& hiddenIds() const { return hidden_; }
    const std::set<std::string>& lockedIds() const { return locked_; }
    const std::string& previewMaterialName() const { return previewMaterialName_; }
    const Json& previewMaterialFactors() const { return previewMaterialFactors_; }
    uint64_t previewMaterialRevision() const { return previewMaterialRevision_; }
    Json saveMapAs(const std::string& path); // trusted native Save panel only
    const StudioFacialPreviewBinding& facialPreviewBinding() const noexcept {
        return facialPreview_;
    }
    void setFacialPreviewGpuStatus(bool rendered,const std::string& reason) {
        facialPreview_.setGpuStatus(rendered,reason);
    }
    Json openProject(const std::string& path); // only call with path returned by a trusted native picker
    // aiversebuild used by the Build panel (resolved by the native host next to Studio).
    void setBuildTool(const std::string& path) { ws_.buildTool = path; }
    // Host-provided material slot names of a loaded content model (the native
    // Scene owns model loading). Without it, slots come from authored overrides.
    using MaterialSlotProvider=std::function<std::vector<std::string>(const std::string& asset)>;
    void setMaterialSlotProvider(MaterialSlotProvider provider) { materialSlots_=std::move(provider); }
    Json previewExternalAssets(const std::vector<std::filesystem::path>& files,
                               const std::string& destination); // native picker ONLY
private:
    Json snapshot() const;
    Json buildRequest(const std::string& method, const Json& params);
    Json propertiesFor(const std::string& entityId) const;
    Json componentCatalog(const std::string& entityId) const;
    editor::DocEntity promotedAuthored(int index) const;
    Json inspectorFor(const std::string& entityId) const; // entity header + components
    Json setMeshRenderer(int index, const std::string& key, const Json& value);
    MaterialSlotProvider materialSlots_;
    int indexFor(const std::string& id) const;
    editor::Workspace ws_;
    std::string selected_;
    std::vector<std::string> selection_;          // extra selected ids (primary kept in selected_)
    std::set<std::string> hidden_, locked_;
    void selectOnly(const std::string& id) { selected_=id; selection_.clear(); }
    void clearSelection() { selected_.clear(); selection_.clear(); }
    bool documentHas(const std::string& id) const;
    void dropOrdinalFlags(); // positional ids shift after structural edits
    StudioFacialPreviewBinding facialPreview_;
    std::future<Json> humanBuild_;
    Json humanBuildResult_=Json::object();
    std::string humanBuildProject_;
    std::string previewMaterialName_;
    Json previewMaterialFactors_;
    uint64_t previewMaterialRevision_ = 0;
    void refreshMaterialPreview();
    std::optional<StudioTransferPlan> pendingAssetTransfer_;
};
}
