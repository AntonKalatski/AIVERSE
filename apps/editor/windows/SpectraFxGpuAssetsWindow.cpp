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
#include "engine/fx/SpectraFxGpuRoutes.h"
#include "engine/fx/SpectraFxSceneHost.h"
#include "engine/render/Renderer3D.h"
#include <cmath>
#include <cstdint>

namespace aiverse::editor {
namespace fs = std::filesystem;

class SpectraFxGpuAssetsWindow final : public EditorWindow {
public:
    const char* id() const override { return "spectrafx_gpu_assets"; }
    const char* title() const override { return "SpectraFX GPU Assets"; }
    const char* group() const override { return "look"; }
    const char* dockArea() const override { return "center"; }
    bool openByDefault() const override { return false; }
    ~SpectraFxGpuAssetsWindow() override {
        shutdownPreview();
    }

    void update(Editor& ed,double) override {
        const auto& dir=ed.ws().project.dir;
        if(project_==dir) return;
        shutdownPreview(); // before changing the project reader/cpu file cache
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
        if(ImGui::Selectable("Event routes (routes.json)",selected_=="@routes")) {
            if(dirty_) { pending_="@routes";pendingCreate_=false; }
            else load("@routes");
        }
        ImGui::Separator();
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
        ImGui::Text("%s%s",selected_=="@routes"?"Event routes":selected_.c_str(),dirty_?" *":"");
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
        drawQuickControls();
        ImGui::Separator();
        if(selected_=="@routes") {
            ImGui::TextWrapped("Optional eventType -> GPU emitter asset routes (version 1). "
                               "Only local-player events are permitted; hidden enemy events "
                               "must never be rendered from authoritative event streams. "
                               "The game client reads this file on startup, so restart the "
                               "play-test to apply route changes. Avoid duplicating FX "
                               "already bound in fx/fx.json.");
        } else {
            ImGui::TextWrapped("GPU/CPU execution config (version 1). GPU is optional; "
                               "unsupported modules remain on CPU through the authored fallback.");
        }
        // Real offscreen preview renders the unsaved GPU JSON through the
        // same SceneHost/HybridPlayback used by FPS. Route files have no
        // single emitter to preview, so the canvas is disabled for routes.
        if(selected_!="@routes") {
            ImGui::Separator();
            ImGui::TextUnformatted("Live CPU/GPU emitter preview");
            if(ImGui::Button("Restart live preview")) {
                previewHash_=0;
                previewRestartRequested_=true;
            }
            ImGui::SameLine();
            if(ImGui::Button(previewPaused_?"Resume":"Pause"))
                previewPaused_=!previewPaused_;
            ImGui::SameLine();
            if(ImGui::Button("Step 1/60 s")) {
                previewPaused_=true;
                previewStepRequested_=true;
            }
            drawPreview(ed,std::max(200.0f,ImGui::GetContentRegionAvail().x),230.0f);
            ImGui::Separator();
        }
        if(ui::inputMultiline("##spectrafx_gpu_config",text_,ImVec2(-1,-1)))
            dirty_=true;
        ImGui::EndChild();
    }

private:
    fs::path directory() const { return fs::path(project_)/"fx"/"gpu"; }
    fs::path filePath(const std::string& name) const {
        return name=="@routes" ? directory()/"routes.json" : directory()/(name+".gpu.json");
    }
    static std::string defaultRoutes() {
        return R"JSON({
  "version": 1,
  "routes": []
}
)JSON";
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
    // Edits always pass through the same strict authoring parsers used by
    // project runtime. Invalid intermediate combinations are rejected;
    // the original JSON and its unsaved changes are preserved.
    bool applyDraft(Json changed) {
        std::string issue;
        if(selected_=="@routes") {
            fx::SpectraFxGpuRouteAsset::Routes routes;
            if(!fx::SpectraFxGpuRouteAsset::read(changed,routes,&issue)) {
                message_=issue;return false;
            }
        } else {
            fx::SpectraFxHybridEmitter::Config effect;
            if(!fx::SpectraFxGpuEmitterAsset::read(changed,effect,&issue)) {
                message_=issue;return false;
            }
        }
        text_=changed.dump(2);
        dirty_=text_!=saved_;
        message_.clear();
        return true;
    }
    void drawQuickControls() {
        Json doc;
        try {doc=Json::parse(text_);}
        catch(const std::exception&) {
            ImGui::TextDisabled("Quick controls require valid JSON syntax.");
            return;
        }
        if(!doc.isObject()) return;
        if(!ImGui::CollapsingHeader("Quick settings",ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if(selected_=="@routes") {
            const std::string oldQuality=doc.getStr("sceneQuality","");
            const char* qualities[]={"Project default","Off","Low","Medium","High"};
            const char* values[]={"","off","low","medium","high"};
            int selection=0;
            for(int i=1;i<5;++i)
                if(oldQuality==values[i]) selection=i;
            if(ImGui::Combo("Scene quality",&selection,qualities,5)) {
                if(selection==0) doc.erase("sceneQuality");
                else doc.set("sceneQuality",Json::string(values[selection]));
                applyDraft(std::move(doc));
                return;
            }
            float gpuBudget=float(doc["gpuFrameBudgetMs"].toDouble(0));
            if(ImGui::DragFloat("GPU frame budget (ms)",&gpuBudget,
                                0.05f,0.0f,1000.0f,"%.2f")) {
                doc.set("gpuFrameBudgetMs",Json::number(
                        double(std::clamp(gpuBudget,0.0f,1000.0f)),2));
                applyDraft(std::move(doc));
                return;
            }
            float spawnFloor=float(doc["minGpuSpawnScale"].toDouble(0.25));
            if(ImGui::SliderFloat("Minimum GPU spawn scale",&spawnFloor,
                                  0.0f,1.0f,"%.2f")) {
                doc.set("minGpuSpawnScale",Json::number(double(spawnFloor),3));
                applyDraft(std::move(doc));
                return;
            }
            ImGui::TextDisabled("Frame budget 0 disables adaptive scaling.");
            ImGui::Separator();
            drawRouteList(doc);
            return;
        }

        const char* modes[]={"Auto","CPU only","GPU preferred"};
        const std::string mode=doc.getStr("preference","auto");
        int backend=mode=="cpu"?1:mode=="gpu"?2:0;
        if(ImGui::Combo("Execution mode",&backend,modes,3)) {
            const char* selected=backend==1?"cpu":backend==2?"gpu":"auto";
            doc.set("preference",Json::string(selected));
            applyDraft(std::move(doc));
            return;
        }
        const char* levels[]={"Off","Low","Medium","High"};
        const char* levelValues[]={"off","low","medium","high"};
        const std::string level=doc.getStr("quality","high");
        int quality=3;
        for(int i=0;i<4;++i)
            if(level==levelValues[i]) quality=i;
        if(ImGui::Combo("Authored emitter quality",&quality,levels,4)) {
            doc.set("quality",Json::string(levelValues[quality]));
            applyDraft(std::move(doc));
            return;
        }
        int capacity=int(doc.getInt("particles",256));
        if(ImGui::SliderInt("GPU particle capacity",&capacity,1,65536)) {
            doc.set("particles",Json::number(int64_t(capacity)));
            applyDraft(std::move(doc));
            return;
        }
        bool allowFallback=doc.getBool("allowCpuFallback",true);
        if(ImGui::Checkbox("Allow CPU fallback",&allowFallback)) {
            doc.set("allowCpuFallback",Json::fromBool(allowFallback));
            applyDraft(std::move(doc));
            return;
        }
        bool indirect=doc.getBool("gpuIndirectCompaction",false);
        if(ImGui::Checkbox("Metal indirect compaction (experimental)",&indirect)) {
            doc.set("gpuIndirectCompaction",Json::fromBool(indirect));
            applyDraft(std::move(doc));
            return;
        }
        bool sorted=doc.getBool("gpuSortAlpha",false);
        if(ImGui::Checkbox("Metal alpha sort (experimental)",&sorted)) {
            doc.set("gpuSortAlpha",Json::fromBool(sorted));
            applyDraft(std::move(doc));
            return;
        }
        bool unsorted=doc.getBool("allowUnsortedAlpha",false);
        if(ImGui::Checkbox("Allow unsorted GPU alpha",&unsorted)) {
            doc.set("allowUnsortedAlpha",Json::fromBool(unsorted));
            applyDraft(std::move(doc));
            return;
        }
        ImGui::TextDisabled("Alpha GPU sorting requires additive=false and no compaction.");
        ImGui::TextDisabled("Remaining authoring fields are editable in the JSON below.");
    }

    // Common route authoring without editing raw JSON. Every changed
    // field is committed transactionally through applyDraft() and validated
    // together with all other routes (duplicate IDs/capacity conflicts).
    void drawRouteList(const Json& source) {
        const Json& routes=source["routes"];
        if(!routes.isArray()) return;
        ImGui::Text("Event routes (%zu / 64)",routes.size());
        if(ImGui::Button("Add event route")) {
            if(routes.size()>=64) {
                message_="At most 64 event routes";
                return;
            }
            uint32_t unused=1;
            while(unused<=65535) {
                bool exists=false;
                for(const auto& entry:routes.items())
                    if(entry.getInt("eventType",-1)==unused) {
                        exists=true;break;
                    }
                if(!exists) break;
                ++unused;
            }
            if(unused>65535) {
                message_="No unused event type available";
                return;
            }
            Json next=source;
            Json row=Json::object();
            row.set("eventType",Json::number(int64_t(unused)));
            row.set("asset",Json::string(
                files_.empty()?"new_effect":files_.front()));
            row.set("enabled",Json::fromBool(true));
            row.set("ownPlayerOnly",Json::fromBool(true));
            row.set("oneShotSeconds",Json::number(3.0,2));
            next.member("routes").push(std::move(row));
            applyDraft(std::move(next));
            return;
        }
        for(size_t i=0;i<routes.size();++i) {
            const Json& entry=routes[i];
            if(!entry.isObject()) continue;
            ImGui::PushID(int(i));
            const int eventId=int(entry.getInt("eventType",0));
            const std::string label="Event "+std::to_string(eventId)+
                    " — "+entry.getStr("asset");
            const bool opened=ImGui::TreeNode(label.c_str());
            if(opened) {
                int editedId=eventId;
                if(ImGui::InputInt("Event type",&editedId)) {
                    if(editedId<0 || editedId>65535) {
                        message_="Event ID must be between 0 and 65535";
                    } else {
                        Json next=source;
                        next.member("routes").at(i).set(
                            "eventType",Json::number(int64_t(editedId)));
                        applyDraft(std::move(next));
                    }
                    ImGui::TreePop();ImGui::PopID();return;
                }
                const std::string asset=entry.getStr("asset");
                if(ImGui::BeginCombo("Emitter asset",asset.c_str())) {
                    for(const auto& name:files_) {
                        if(ImGui::Selectable(name.c_str(),name==asset)) {
                            Json next=source;
                            next.member("routes").at(i).set(
                                "asset",Json::string(name));
                            applyDraft(std::move(next));
                        }
                    }
                    ImGui::EndCombo();
                }
                if(std::find(files_.begin(),files_.end(),asset)==files_.end())
                    ImGui::TextDisabled("No matching GPU asset in this project yet.");
                bool enabled=entry.getBool("enabled",true);
                if(ImGui::Checkbox("Enabled",&enabled)) {
                    Json next=source;
                    next.member("routes").at(i).set(
                        "enabled",Json::fromBool(enabled));
                    applyDraft(std::move(next));
                    ImGui::TreePop();ImGui::PopID();return;
                }
                float seconds=float(entry["oneShotSeconds"].toDouble(3));
                if(ImGui::DragFloat("One-shot seconds",&seconds,
                                    0.05f,0.05f,60.0f,"%.2f")) {
                    Json next=source;
                    next.member("routes").at(i).set(
                        "oneShotSeconds",Json::number(
                            double(std::clamp(seconds,0.05f,60.0f)),2));
                    applyDraft(std::move(next));
                    ImGui::TreePop();ImGui::PopID();return;
                }
                if(ImGui::Button("Remove route")) {
                    Json next=source;
                    next.member("routes").eraseAt(i);
                    applyDraft(std::move(next));
                    ImGui::TreePop();ImGui::PopID();return;
                }
                const std::string pool=entry.getStr("sharedPool");
                if(!pool.empty())
                    ImGui::TextDisabled("Pool: %s (capacity %lld)",
                        pool.c_str(),(long long)entry.getInt(
                            "sharedPoolCapacity",8192));
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TextDisabled("Pool names and other advanced fields: edit JSON below.");
    }

    void shutdownPreview() {
        previewHost_.shutdown();
        if(previewDevice_)
            previewViewport_.release(*previewDevice_);
        previewDevice_=nullptr;
        previewHash_=0;
        previewInitialized_=false;
        previewRestartRequested_=true;
        previewLastTime_=-1;
        previewPaused_=false;
        previewStepRequested_=false;
        previewMessage_.clear();
    }
    static uint64_t previewHash(const std::string& text) {
        uint64_t hash=14695981039346656037ull;
        for(unsigned char ch:text){hash^=ch;hash*=1099511628211ull;}
        return hash;
    }
    void initPreview(Editor& ed) {
        if(previewInitialized_) return;
        if(!previewDevice_) previewDevice_=&ed.device();
        // The two synthetic assets exist only inside this editor preview,
        // never in the project's file tree or the running FPS/RTS client.
        const std::string sourceName="fx/gpu/__spectrafx_preview.gpu.json";
        const std::string routeName="fx/gpu/routes.json";
        const std::string routeText=R"JSON({
          "version":1,
          "routes":[{
             "eventType":1,
             "asset":"__spectrafx_preview",
             "enabled":true,
             "ownPlayerOnly":true,
             "oneShotSeconds":10
          }]
        })JSON";
        const bool ok=previewHost_.init(*previewDevice_,
            [this,sourceName,routeName,routeText](
                    const std::string& path,std::string& output) {
                if(path==routeName) {output=routeText;return true;}
                if(path==sourceName) {output=text_;return true;}
                std::ifstream in(fs::path(project_)/path,std::ios::binary);
                if(!in) return false;
                output.assign(std::istreambuf_iterator<char>(in),
                              std::istreambuf_iterator<char>());
                return !in.bad();
            },project_+"/");
        if(!ok) previewMessage_=previewHost_.lastError();
        else previewMessage_.clear();
        previewInitialized_=ok;
    }
    void drawPreview(Editor& ed,float width,float height) {
        if(!previewDevice_) previewDevice_=&ed.device();
        const double now=ed.time();
        float dt=previewLastTime_<0?0:float(now-previewLastTime_);
        previewLastTime_=now;
        dt=std::clamp(dt,0.0f,0.05f);
        if(previewStepRequested_) {
            dt=1.0f/60.0f;
            previewStepRequested_=false;
        } else if(previewPaused_) dt=0.0f;
        fx::SpectraFxHybridEmitter::Config candidate;
        std::string error;
        const bool valid=fx::SpectraFxGpuEmitterAsset::readText(
            text_,candidate,&error);
        bool canDraw=valid && previewInitialized_;
        if(!valid) {
            previewMessage_=error;
            canDraw=false;
            // Do not display stale particles from the previous valid draft.
            if(previewInitialized_) {
                if(previewHost_.resetScene()) previewHash_=0;
                else previewMessage_="GPU preview reset deferred to safe frame";
            }
        } else {
            initPreview(ed);
            if(previewInitialized_) {
                const uint64_t hash=previewHash(text_);
                if(previewRestartRequested_ || previewHash_!=hash) {
                    if(previewHost_.resetScene()) {
                        fx::SpectraFxCue cue{};
                        cue.id=1;
                        cue.key=1; // persistent preview attachment
                        cue.action=fx::SpectraFxCue::Action::Play;
                        cue.position={0,0.7f,0};
                        cue.direction={0,1,0};
                        if(previewHost_.dispatch(cue)) {
                            previewMessage_.clear();
                        } else previewMessage_="Preview effect unavailable: "
                            "check CPU effect asset and GPU backend compatibility";
                        previewHash_=hash;
                        previewRestartRequested_=false;
                    } else {
                        previewMessage_="GPU preview reset deferred to safe frame";
                        canDraw=false;
                    }
                }
                if(canDraw || (previewInitialized_ &&
                   !previewRestartRequested_ && previewHash_==hash)) {
                    canDraw=true;
                    previewHost_.prepareGpu(dt,previewCamera_);
                }
            }
        }
        const float dpi=ImGui::GetIO().DisplayFramebufferScale.x;
        previewViewport_.ensure(*previewDevice_,int(width*dpi),int(height*dpi));
        auto& renderer=ed.renderer();
        renderer.beginScene(previewCamera_,previewViewport_.w,previewViewport_.h);
        const uint32_t grid=render::rgba(70,80,95,255);
        for(int i=-5;i<=5;++i) {
            renderer.line({float(i),0,-5},{float(i),0,5},grid);
            renderer.line({-5,0,float(i)},{5,0,float(i)},grid);
        }
        if(canDraw)
            previewHost_.prepareCpu(dt,previewCamera_,renderer.viewProj(),
                                    previewViewport_.w,previewViewport_.h);
        gfx::PassDesc pass;
        pass.colorTarget=previewViewport_.color;
        pass.depthTarget=previewViewport_.depth;
        pass.clearColor[0]=0.12f;
        pass.clearColor[1]=0.13f;
        pass.clearColor[2]=0.16f;
        renderer.render(pass);
        if(canDraw)
            previewHost_.render(renderer.lighting,renderer.viewProj(),
                                previewCamera_);
        previewDevice_->endPass();
        previewViewport_.image(*previewDevice_,width,height);
        if(ImGui::IsItemHovered()) {
            const auto& io=ImGui::GetIO();
            if(ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                previewCamera_.yaw-=io.MouseDelta.x*0.01f;
                previewCamera_.pitch=std::clamp(
                    previewCamera_.pitch+io.MouseDelta.y*0.01f,0.05f,1.5f);
            }
            if(io.MouseWheel)
                previewCamera_.distance=std::clamp(
                    previewCamera_.distance*
                    (io.MouseWheel>0?0.9f:1.1f),2.0f,60.0f);
        }
        if(!previewMessage_.empty())
            ImGui::TextWrapped("%s",previewMessage_.c_str());
        if(previewInitialized_ && previewHost_.playback()) {
            const auto snapshot=previewHost_.playback()->backendSnapshot();
            ImGui::TextDisabled(
                "Backend: GPU %zu | CPU %zu | pooled %zu | suspended %zu",
                snapshot.dedicatedGpu,snapshot.dedicatedCpu,
                snapshot.pooledSources,snapshot.suspendedGpu);
            ImGui::TextDisabled(
                "GPU slots: active %llu | retiring %llu | nominal KiB %llu",
                static_cast<unsigned long long>(snapshot.activeGpuSlots),
                static_cast<unsigned long long>(snapshot.retiredGpuSlots),
                static_cast<unsigned long long>(snapshot.nominalGpuBytes/1024));
            if(previewPaused_)
                ImGui::TextDisabled(
                    "Paused: existing GPU particle state is re-rendered; "
                    "new spawns need a nonzero frame step");
        }
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
        if(name!="@routes" && !fx::SpectraFxGpuEmitterLibrary::validName(name)) return false;
        std::ifstream file(filePath(name),std::ios::binary);
        if(!file) {
            if(name=="@routes") {
                selected_="@routes";text_=defaultRoutes();saved_.clear();
                dirty_=true;message_="New route file; save to enable routing";
                return true;
            }
            message_="Unable to open GPU asset";return false;
        }
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        std::string error;
        if(name=="@routes") {
            fx::SpectraFxGpuRouteAsset::Routes routes;
            if(!fx::SpectraFxGpuRouteAsset::readText(content,routes,&error)) {
                message_=error;return false;
            }
        } else {
            fx::SpectraFxHybridEmitter::Config candidate;
            if(!fx::SpectraFxGpuEmitterAsset::readText(content,candidate,&error)) {
                message_=error;return false;
            }
        }
        selected_=name;text_=saved_=std::move(content);
        dirty_=false;message_.clear();
        return true;
    }
    bool validate() {
        std::string error;
        if(selected_=="@routes") {
            fx::SpectraFxGpuRouteAsset::Routes routes;
            if(!fx::SpectraFxGpuRouteAsset::readText(text_,routes,&error)) {
                message_=error;return false;
            }
            message_="Valid GPU event routes";
        } else {
            fx::SpectraFxHybridEmitter::Config parsed;
            if(!fx::SpectraFxGpuEmitterAsset::readText(text_,parsed,&error)) {
                message_=error;return false;
            }
            message_="Valid GPU/CPU emitter configuration";
        }
        return true;
    }
    // Keep the last known on-disk source as a compare-and-swap guard.
    // A source edited by another AIVERSE instance or agent is never
    // overwritten without an explicit reload. Back up the old JSON before
    // the cross-platform rename (Windows does not replace existing files).
    bool save(Editor& ed) {
        if(!validate()||selected_.empty()) return false;
        std::error_code ec;
        fs::create_directories(directory(),ec);
        if(ec) {message_="Unable to create GPU asset folder: "+ec.message();return false;}
        const auto target=filePath(selected_);
        const fs::path temp=target.string()+".tmp";
        const fs::path backup=target.string()+".bak";
        const bool existing=fs::exists(target,ec);
        if(ec) {message_="Unable to stat GPU asset: "+ec.message();return false;}
        if(existing) {
            std::ifstream current(target,std::ios::binary);
            if(!current) {message_="Unable to read current GPU asset";return false;}
            const std::string disk((std::istreambuf_iterator<char>(current)),
                                   std::istreambuf_iterator<char>());
            if(!current.eof() && current.bad()) {
                message_="Unable to compare current GPU asset";
                return false;
            }
            if(saved_.empty() || disk!=saved_) {
                message_="GPU asset changed on disk; reload before saving";
                return false;
            }
        }
        {
            std::ofstream file(temp,std::ios::binary|std::ios::trunc);
            if(!file) {message_="Unable to open temporary GPU asset";return false;}
            file.write(text_.data(),std::streamsize(text_.size()));
            file.flush();
            if(!file) {
                message_="Unable to write temporary GPU asset";
                file.close();
                fs::remove(temp,ec);
                return false;
            }
        }
        if(existing) {
            ec.clear();
            fs::remove(backup,ec);
            if(ec) {
                fs::remove(temp);
                message_="Unable to replace old GPU asset backup: "+ec.message();
                return false;
            }
            fs::rename(target,backup,ec);
            if(ec) {
                fs::remove(temp);
                message_="Unable to back up GPU asset: "+ec.message();
                return false;
            }
        }
        ec.clear();
        fs::rename(temp,target,ec);
        if(ec) {
            const std::string failure=ec.message();
            std::error_code rollback;
            if(existing) fs::rename(backup,target,rollback);
            fs::remove(temp);
            message_="GPU asset save failed: "+failure;
            if(rollback)
                message_+="; backup retained at "+backup.string()+
                          "; rollback failed: "+rollback.message();
            return false;
        }
        saved_=text_;
        dirty_=false;
        refresh();
        ed.reloadFx();
        message_=(selected_=="@routes"?"Saved routes (restart game client): ":"Saved GPU emitter: ")+target.string();
        return true;
    }

    std::string project_,selected_,newName_,pending_,text_,saved_,message_;
    std::vector<std::string> files_;
    bool dirty_=false,pendingCreate_=false;
    gfx::Device* previewDevice_=nullptr;
    fx::SpectraFxSceneHost previewHost_{};
    Viewport previewViewport_{};
    render::RTSCamera previewCamera_{{0,0.8f,0},9.0f,0.45f,0.7f};
    uint64_t previewHash_=0;
    double previewLastTime_=-1;
    bool previewInitialized_=false,previewRestartRequested_=true;
    bool previewPaused_=false,previewStepRequested_=false;
    std::string previewMessage_;
};
AIVERSE_EDITOR_WINDOW(SpectraFxGpuAssetsWindow);
} // namespace aiverse::editor
