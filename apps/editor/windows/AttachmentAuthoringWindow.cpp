// AIVERSE Studio — genre-independent imported attachment authoring.
// Dedicated ModelAssetProvider owns BOTH parent and child GPU/CPU Models.
// We never borrow a Modeling/AnimationGraph window's directly-owned Model
// into this cache, and never create a second animation evaluator.
#include "../Editor.h"
#include "../EditorUtil.h"
#include "engine/view/ImportedAttachmentAuthoring.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::editor {
namespace fs = std::filesystem;

class AttachmentAuthoringWindow final : public EditorWindow {
    using Imported = view::ImportedAttachmentAuthoring;
    using Registry = view::AppearanceAssetRegistry;
    struct AuthoredChild {
        std::string model;
        std::string socket;
        bool enabled=true;
    };
    struct Session {
        std::unique_ptr<render::ModelAssetProvider> provider;
        std::unique_ptr<Registry> registry;
        Registry::Snapshot source;
        Imported::Compiled compiled;
        std::vector<Registry::Snapshot> children;
    };

    gfx::Device* device_=nullptr; // Editor owner, never freed here
    std::string project_;
    std::string sourcePath_;
    std::string layoutPath_="attachments.json";
    std::string newChild_,newSocket_,message_;
    std::vector<AuthoredChild> authored_;
    std::unique_ptr<Session> session_;
    bool dirty_=false;

    static bool readAsset(const std::string& path,std::string& result) {
        std::ifstream input(path,std::ios::binary);
        if(!input)return false;
        result.assign(std::istreambuf_iterator<char>(input),
                      std::istreambuf_iterator<char>());
        return !input.bad();
    }

    // Restrict authoring to the current project, including through symlinks.
    // Input files must exist; a new layout JSON may be created in an existing
    // project directory. No absolute or parent-traversal authoring paths.
    std::string withinProject(const std::string& path) const {
        if(project_.empty() || path.empty())
            throw std::invalid_argument("Open a project and choose a file");
        const fs::path relative(path);
        if(relative.is_absolute())
            throw std::invalid_argument("Only project-relative files are supported");
        for(const auto& part:relative)
            if(part=="..")
                throw std::invalid_argument("Parent directory escape is forbidden");
        const auto root=fs::weakly_canonical(fs::path(project_));
        const auto target=fs::weakly_canonical(root/relative);
        const auto local=target.lexically_relative(root);
        if(local.empty() || local.is_absolute())
            throw std::invalid_argument("Asset path is outside the project");
        for(const auto& part:local)
            if(part=="..")
                throw std::invalid_argument("Symlink escapes the project");
        return target.string();
    }

    // Full device-idle proof is deliberately conservative for authoring
    // transactions. If it fails, keep the provider alive (leak rather than
    // destruct GPU-visible Models with no completion proof).
    bool retire(std::unique_ptr<Session>& owned) noexcept {
        if(!owned)return true;
        if(!device_)return false;
        try {
            if(!device_->waitForResourceIdle())return false;
            owned->compiled={};
            owned->children.clear();
            owned->source.reset();
            owned->registry.reset();
            if(owned->provider)
                owned->provider->shutdown(*device_);
            owned.reset();
            return true;
        }catch(...) {return false;}
    }
    void preserveUnsafe(std::unique_ptr<Session>& owned) noexcept {
        // Never let an unproven GPU owner destruct at window teardown.
        if(owned)(void)owned.release();
    }

    void loadLayout() {
        const auto path=withinProject(layoutPath_);
        std::string contents;
        if(!readAsset(path,contents))
            throw std::runtime_error("Attachment JSON file is not readable");
        const auto parsed=Json::parse(contents);
        if(!parsed["attachments"].isArray())
            throw std::invalid_argument("Expected attachments array");
        std::vector<AuthoredChild> next;
        for(const auto& entry:parsed["attachments"].items()) {
            const auto model=entry.getStr("model");
            const auto socket=entry.getStr("socket");
            if(model.empty() || socket.empty())
                throw std::invalid_argument("Attachment needs model and socket");
            (void)withinProject(model);
            next.push_back({model,socket,entry.getBool("enabled",true)});
        }
        const auto parent=parsed.getStr("parent",sourcePath_);
        if(!parent.empty())(void)withinProject(parent);
        sourcePath_=parent;
        authored_.swap(next);
        dirty_=true;
        message_="Loaded authored layout; choose Reimport to publish";
    }

    void saveLayout() {
        const auto path=withinProject(layoutPath_);
        Json root=Json::object();
        root.set("parent",Json::string(sourcePath_));
        Json attachments=Json::array();
        for(const auto& child:authored_) {
            Json item=Json::object();
            item.set("model",Json::string(child.model));
            item.set("socket",Json::string(child.socket));
            item.set("enabled",Json::fromBool(child.enabled));
            attachments.push(item);
        }
        root.set("attachments",attachments);
        std::ofstream output(path,std::ios::binary|std::ios::trunc);
        if(!output || !(output<<root.dump(2)))
            throw std::runtime_error("Unable to write attachment layout");
        dirty_=false;
        message_="Saved attachment JSON";
    }

    void stageAndPublish(Editor& ed) {
        // Owner-thread, no render submissions from this editor window.
        // A candidate provider is independent from the current last-good
        // owner: even a failed GPU upload cannot evict old published Models.
        auto staged=std::make_unique<Session>();
        staged->provider=std::make_unique<render::ModelAssetProvider>(readAsset);
        // Share the Editor Device's explicit generation token. Pointer
        // identity alone is insufficient after graphics device recreation.
        staged->provider->setGpuDeviceLease(ed.gpuDeviceLease());
        staged->registry=std::make_unique<Registry>();
        std::string error;
        try {
            const auto parentPath=withinProject(sourcePath_);
            auto* parent=staged->provider->load(ed.device(),parentPath);
            if(!parent)
                throw std::runtime_error("Parent Model import failed");
            Registry::Request parentRequest;
            parentRequest.model=parent;
            parentRequest.sources.push_back(parentPath);
            if(!staged->registry->publish("studio/attachments/parent",
                                           parentRequest,{}, {},&error))
                throw std::runtime_error("Parent appearance: "+error);
            staged->source=staged->registry->find("studio/attachments/parent");
            std::map<std::string,Registry::Snapshot> imported;
            std::vector<Imported::ChildRequest> requested;
            for(const auto& child:authored_) {
                if(!child.enabled)continue;
                if(child.socket.empty())
                    throw std::invalid_argument("Child socket name must not be empty");
                const auto source=withinProject(child.model);
                auto it=imported.find(source);
                if(it==imported.end()) {
                    Registry::Snapshot appearance;
                    Registry::Request request;
                    if(!Imported::importChild(
                        *staged->provider,ed.device(),*staged->registry,
                        "studio/attachments/child/"+source,
                        source,request,appearance,{},&error))
                        throw std::runtime_error("Child appearance: "+error);
                    it=imported.emplace(source,std::move(appearance)).first;
                }
                Imported::ChildRequest request;
                request.appearance=it->second;
                request.socketName=child.socket;
                requested.push_back(std::move(request));
            }
            if(!Imported::compile(staged->source,staged->provider->resources(),
                                  requested,staged->compiled,&error))
                throw std::runtime_error("Socket compile: "+error);
            // Read leases in compile have already left scope. Before the
            // LAST-GOOD provider may be destroyed, prove all previous GPU
            // submits finished. This is a blocking, explicit Studio action.
            if(!ed.device().waitForResourceIdle())
                throw std::runtime_error(
                    "GPU idle proof failed; last-good attachments retained");
            // Drop old runtime snapshots before destroying its resource owner.
            auto old=std::move(session_);
            session_=std::move(staged);
            if(!retire(old))preserveUnsafe(old);
            message_="Published "+std::to_string(session_->compiled.children.size())+
                     " compiled socket attachments";
        }catch(...) {
            // A failed candidate may have completed speculative GPU uploads.
            // It must not be destroyed until the device is known idle.
            if(!retire(staged))preserveUnsafe(staged);
            throw;
        }
    }

public:
    ~AttachmentAuthoringWindow() override {
        if(!retire(session_))preserveUnsafe(session_);
    }
    const char* id() const override{return "animation_attachments";}
    const char* title() const override{return "MotionWeave — Attachments";}
    const char* group() const override{return "look";}
    const char* dockArea() const override{return "right";}
    bool openByDefault() const override{return false;}
    int order() const override{return 35;}

    void update(Editor& ed,double) override {
        device_=&ed.device();
        const auto current=ed.ws().project.dir;
        if(current==project_)return;
        // A project switch must release all old borrowed sources BEFORE
        // letting its content mounts disappear.
        if(!retire(session_)) {
            message_="Cannot change attachment project: GPU idle unavailable";
            return;
        }
        project_=current;
        authored_.clear();
        sourcePath_.clear();
        layoutPath_="attachments.json";
        newChild_.clear();
        newSocket_.clear();
        dirty_=false;
        message_.clear();
    }

    void draw(Editor& ed) override {
        if(project_!=ed.ws().project.dir) {
            ImGui::TextUnformatted("Project switch awaiting GPU owner cleanup");
            return;
        }
        if(project_.empty()) {
            ImGui::TextUnformatted("Open a project to author Model attachments");
            return;
        }
        ImGui::TextUnformatted(
            "Source-neutral imported child Models. No gameplay/genre rules.");
        ImGui::TextDisabled(
            "Dedicated provider; does not borrow ModelingWindow GPU Models.");
        ui::inputString("Parent Model",sourcePath_);
        ui::inputString("Layout JSON",layoutPath_);
        if(ImGui::Button("Load JSON")) {
            try{loadLayout();}catch(const std::exception& e){message_=e.what();}
        }
        ImGui::SameLine();
        if(ImGui::Button("Save JSON")) {
            try{saveLayout();}catch(const std::exception& e){message_=e.what();}
        }
        ImGui::Separator();
        for(size_t i=0;i<authored_.size();++i) {
            ImGui::PushID(int(i));
            auto& child=authored_[i];
            if(ImGui::Checkbox("Enabled",&child.enabled))dirty_=true;
            if(ui::inputString("Child Model",child.model))dirty_=true;
            if(ui::inputString("Socket",child.socket))dirty_=true;
            if(ImGui::Button("Detach")) {
                authored_.erase(authored_.begin()+i);
                dirty_=true;
                ImGui::PopID();
                break;
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        ui::inputString("New child",newChild_);
        ui::inputString("New socket",newSocket_);
        if(ImGui::Button("Attach") && !newChild_.empty() && !newSocket_.empty()) {
            authored_.push_back({newChild_,newSocket_,true});
            newChild_.clear();
            newSocket_.clear();
            dirty_=true;
        }
        if(ImGui::Button("Reimport & Publish")) {
            try{stageAndPublish(ed);}
            catch(const std::exception& e){message_=e.what();}
        }
        if(dirty_)ImGui::TextDisabled("Unsaved attachment layout");
        if(session_) {
            ImGui::Text("Published source: %s",
                sourcePath_.c_str());
            ImGui::Text("Compiled sockets: %zu",
                session_->compiled.children.size());
            ImGui::TextDisabled("Old GPU models retired only after idle proof");
        }
        if(!message_.empty())ImGui::TextWrapped("%s",message_.c_str());
        ImGui::TextDisabled(
            "This window authors & validates real assets; live 3D preview "
            "and nonblocking GPU hot reload remain separate work.");
    }
};
AIVERSE_EDITOR_WINDOW(AttachmentAuthoringWindow);
} // namespace aiverse::editor
