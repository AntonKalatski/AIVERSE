// SpectraFX GPU emitter asset editor. Project-scoped optional authoring UI.
// Uses the canonical v1 GPU emitter parser before committing files.
// Actual GPU rendering is owned by the opt-in SpectraFxHybridPlayback host.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "../Editor.h"
#include "../EditorUtil.h"
#include "engine/fx/SpectraFxGpuEmitterAsset.h"
#include "engine/fx/SpectraFxGpuEmitterLibrary.h"

namespace aiverse::editor {
namespace fs = std::filesystem;

class SpectraFxGpuAssetsWindow final : public EditorWindow {
public:
    const char* id() const override { return "spectrafx_gpu_assets"; }
    const char* title() const override { return "SpectraFX GPU Assets"; }
    const char* group() const override { return "look"; }
    const char* dockArea() const override { return "center"; }
    bool openByDefault() const override { return false; }

    void update(Editor& ed,double) override {
        const auto& dir=ed.ws().project.dir;
        if(project_==dir) return;
        project_=dir;
        selected_.clear();
        text_.clear();saved_.clear();pending_.clear();
        dirty_=false;message_.clear();
        refresh();
    }

    void draw(Editor& ed) override {
        if(project_.empty()) {
            ImGui::TextUnformatted("Open an AIVERSE project to edit GPU emitter assets.");
            return;
        }
        ImGui::BeginChild("##gpu_asset_list",ImVec2(200,0),
                          ImGuiChildFlags_Borders|ImGuiChildFlags_ResizeX);
        if(ImGui::Button("Refresh list")) refresh();
        for(const auto& name:files_) {
            if(ImGui::Selectable(name.c_str(),selected_==name)) {
                if(dirty_) {pending_=name;pendingCreate_=false;}
                else load(name);
            }
        }
        ImGui::Separator();
        ui::inputString("New asset",newName_);
        const bool nameOK=fx::SpectraFxGpuEmitterLibrary::validName(newName_) &&
             std::find(files_.begin(),files_.end(),newName_)==files_.end();
        ImGui::BeginDisabled(!nameOK);
        if(ImGui::Button("Create")) {
            if(dirty_) {pending_=newName_;pendingCreate_=true;}
            else create(newName_);
        }
        ImGui::EndDisabled();
        if(!pending_.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("Discard current unsaved changes?");
            if(ImGui::Button("Discard and open")) {
                const std::string next=pending_;
                const bool createNow=pendingCreate_;
                pending_.clear();pendingCreate_=false;
                if(createNow) create(next);
                else load(next);
            }
            ImGui::SameLine();
            if(ImGui::Button("Cancel")) {
                pending_.clear();pendingCreate_=false;
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##gpu_asset_details",ImVec2(0,0));
        if(selected_.empty()) {
            ImGui::TextUnformatted("Select or create a GPU emitter asset.");
            ImGui::EndChild();
            return;
        }
        ImGui::Text("%s%s",selected_.c_str(),dirty_?" *":"");
        if(ImGui::Button("Validate")) validate();
        ImGui::SameLine();
        if(ImGui::Button("Save")) save(ed);
        ImGui::SameLine();
        ImGui::BeginDisabled(dirty_);
        if(ImGui::Button("Reload")) load(selected_);
        ImGui::EndDisabled();
        if(ImGui::Button("Discard changes")) {
            text_=saved_;dirty_=false;message_.clear();
        }
        if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        ImGui::Separator();
        ImGui::TextWrapped("GPU/CPU execution config (version 1). GPU is optional; "
                           "unsupported modules remain on CPU through the authored fallback.");
        if(ui::inputMultiline("##spectrafx_gpu_config",text_,ImVec2(-1,-1)))
            dirty_=true;
        ImGui::EndChild();
    }

private:
    fs::path directory() const { return fs::path(project_)/"fx"/"gpu"; }
    fs::path filePath(const std::string& name) const {
        return directory()/(name+".gpu.json");
    }
    static std::string defaultAsset() {
        return R"JSON({
  "version": 1,
  "cpuEffect": "explosion",
  "preference": "auto",
  "particles": 256,
  "allowCpuFallback": true,
  "quality": "high",
  "spawn": {
    "rate": 20,
    "perMeter": 0,
    "burst": 12,
    "life": 1.5,
    "speed": 2,
    "spread": 0.25,
    "seed": 1,
    "maxPerFrame": 128
  },
  "appearance": {
    "color": [1, 0.75, 0.25],
    "radius": 0.18,
    "additive": true
  },
  "environment": {
    "acceleration": [0, -2, 0],
    "damping": 0.99
  }
}
)JSON";
    }
    void refresh() {
        files_.clear();
        if(project_.empty()) return;
        std::error_code ec;
        for(fs::directory_iterator it(directory(),ec),end;
            !ec && it!=end;it.increment(ec)) {
            if(!it->is_regular_file(ec)) continue;
            const std::string filename=it->path().filename().string();
            static constexpr const char* suffix=".gpu.json";
            const size_t len=9;
            if(filename.size()<=len || filename.substr(filename.size()-len)!=suffix)
                continue;
            const auto name=filename.substr(0,filename.size()-len);
            if(fx::SpectraFxGpuEmitterLibrary::validName(name))
                files_.push_back(name);
        }
        std::sort(files_.begin(),files_.end());
    }
    void create(const std::string& name) {
        if(!fx::SpectraFxGpuEmitterLibrary::validName(name)) return;
        selected_=name;
        text_=defaultAsset();
        saved_.clear();
        dirty_=true;
        message_.clear();
    }
    bool load(const std::string& name) {
        if(!fx::SpectraFxGpuEmitterLibrary::validName(name)) return false;
        std::ifstream file(filePath(name),std::ios::binary);
        if(!file) {message_="Unable to open GPU asset";return false;}
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        fx::SpectraFxHybridEmitter::Config candidate;
        std::string error;
        if(!fx::SpectraFxGpuEmitterAsset::readText(content,candidate,&error)) {
            message_=error;return false;
        }
        selected_=name;text_=saved_=std::move(content);
        dirty_=false;message_.clear();
        return true;
    }
    bool validate() {
        fx::SpectraFxHybridEmitter::Config parsed;
        std::string error;
        if(!fx::SpectraFxGpuEmitterAsset::readText(text_,parsed,&error)) {
            message_=error;return false;
        }
        message_="Valid GPU/CPU emitter configuration";
        return true;
    }
    bool save(Editor& ed) {
        if(!validate()||selected_.empty()) return false;
        std::error_code ec;
        fs::create_directories(directory(),ec);
        if(ec) {message_=ec.message();return false;}
        const auto target=filePath(selected_);
        const fs::path temp=target.string()+".tmp";
        {
            std::ofstream file(temp,std::ios::binary|std::ios::trunc);
            file<<text_;
            file.flush();
            if(!file) {message_="Unable to save GPU asset";return false;}
        }
        fs::rename(temp,target,ec);
        if(ec) {
            fs::remove(temp);
            message_=ec.message();
            return false;
        }
        saved_=text_;
        dirty_=false;
        refresh();
        ed.reloadFx();
        message_="Saved GPU emitter: "+target.string();
        return true;
    }

    std::string project_,selected_,newName_,pending_,text_,saved_,message_;
    std::vector<std::string> files_;
    bool dirty_=false,pendingCreate_=false;
};
AIVERSE_EDITOR_WINDOW(SpectraFxGpuAssetsWindow);
} // namespace aiverse::editor
