// SpectraFX graph authoring window. Edits the canonical fx/VfxGraph.h schema
// (Spawn / Initialize / Update / SpriteOutput, MeshOutput, RibbonOutput).
// This is not the separate SpectraFxGraph cue-routing DAG.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>
#include "../Editor.h"
#include "../EditorUtil.h"
#include "engine/fx/SpectraFX.h"

namespace aiverse::editor {
namespace fs = std::filesystem;

class SpectraFxGraphWindow : public EditorWindow {
public:
    const char* id() const override { return "spectrafx_graph"; }
    const char* title() const override { return "SpectraFX Graph"; }
    const char* group() const override { return "look"; }
    const char* dockArea() const override { return "center"; }
    bool openByDefault() const override { return false; }

    void update(Editor& ed,double) override {
        if (project_!=ed.ws().project.dir) {
            project_=ed.ws().project.dir;
            current_.clear();
            document_=defaultGraph();
            saved_=document_;
            selected_=1;
            undo_.clear();redo_.clear();
            dirty_=false;
            refresh();
        }
    }

    void draw(Editor& ed) override {
        if (project_.empty()) {
            ImGui::TextUnformatted("Open an AIVERSE project to edit SpectraFX graphs.");
            return;
        }
        ImGui::BeginChild("##spectrafx_assets",ImVec2(175,0),
                          ImGuiChildFlags_Borders|ImGuiChildFlags_ResizeX);
        if (ImGui::Button("Refresh")) refresh();
        for (const auto& name : files_) {
            if (ImGui::Selectable(name.c_str(),name==current_)) {
                if (dirty_) pending_=name;
                else load(name);
            }
        }
        ImGui::Separator();
        ui::inputString("New name",newName_);
        if (ImGui::Button("Create") && validName(newName_) &&
            std::find(files_.begin(),files_.end(),newName_)==files_.end()) {
            if (dirty_) pending_=newName_, createPending_=true;
            else create(newName_);
        }
        if (!pending_.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("Discard unsaved graph edits?");
            if (ImGui::Button("Discard edits")) {
                if (createPending_) create(pending_);
                else load(pending_);
                pending_.clear();createPending_=false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                pending_.clear();createPending_=false;
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##spectrafx_editor",ImVec2(0,0));
        if (current_.empty()) {
            ImGui::TextUnformatted("Select or create an effect graph.");
            ImGui::EndChild();return;
        }
        ImGui::Text("%s%s",current_.c_str(),dirty_?" *":"");
        if(ImGui::Button("Save")) save(ed);
        ImGui::SameLine();
        if(ImGui::Button("Reload") && !dirty_) load(current_);
        ImGui::SameLine();
        ImGui::BeginDisabled(undo_.empty());
        if(ImGui::Button("Undo")) {
            redo_.push_back(document_);
            document_=undo_.back();undo_.pop_back();
            dirty_=true;selected_=-1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(redo_.empty());
        if(ImGui::Button("Redo")) {
            undo_.push_back(document_);
            document_=redo_.back();redo_.pop_back();
            dirty_=true;selected_=-1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(ImGui::Button("Validate")) validate();
        if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        ImGui::Separator();
        ImGui::BeginChild("##spectrafx_nodes",ImVec2(230,0),ImGuiChildFlags_Borders);
        auto& nodes=document_.find("nodes")->itemsMut();
        for(size_t i=0;i<nodes.size();++i) {
            const auto title=nodes[i].getStr("id")+" : "+nodes[i].getStr("type");
            if(ImGui::Selectable(title.c_str(),selected_==int(i))) {
                selected_=int(i);propertiesText_=nodes[i]["properties"].dump(2);
            }
        }
        ImGui::Separator();
        if(ImGui::BeginCombo("Add node","Choose")) {
            for(auto type : {"Spawn","Initialize","Update","SpriteOutput",
                             "MeshOutput","RibbonOutput"}) {
                if(ImGui::Selectable(type)) add(type);
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(selected_<0 || selected_>=int(nodes.size()));
        if(ImGui::Button("Remove selected")) removeSelected();
        ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##spectrafx_properties",ImVec2(0,0));
        if(selected_>=0 && selected_<int(nodes.size())) {
            // All changes to canonical VFX Graph JSON go through a full
            // compile before they're committed to the editor document.
            const Json& node=nodes[size_t(selected_)];
            ImGui::Text("Node: %s",node.getStr("id").c_str());
            ImGui::Text("Type: %s",node.getStr("type").c_str());
            if(node.getStr("type")!="Spawn") {
                const std::string cur=parentOf(node.getStr("id"));
                if(ImGui::BeginCombo("Input",cur.empty()?"(unconnected)":cur.c_str())) {
                    for(const auto& parent : nodes) {
                        const std::string id=parent.getStr("id"), type=parent.getStr("type");
                        if(id==node.getStr("id") || isOutput(type)) continue;
                        if(ImGui::Selectable(id.c_str(),id==cur)) relink(node.getStr("id"),id);
                    }
                    ImGui::EndCombo();
                }
            }
            ui::inputMultiline("Properties (JSON)",propertiesText_,ImVec2(-1,220));
            if(ImGui::Button("Apply properties")) {
                try {
                    Json properties=Json::parse(propertiesText_);
                    if(!properties.isObject()) message_="Properties must be a JSON object";
                    else {
                        Json next=document_;
                        next.find("nodes")->at(size_t(selected_)).set("properties",std::move(properties));
                        commit(std::move(next));
                    }
                } catch(const std::exception& e) {message_=e.what();}
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Full graph JSON");
        }
        ui::inputMultiline("##raw_spectrafx",raw_,ImVec2(-1,220));
        if(ImGui::Button("Apply full JSON")) {
            try { commit(Json::parse(raw_)); }
            catch(const std::exception& e) {message_=e.what();}
        }
        ImGui::EndChild();
        ImGui::EndChild();
    }

private:
    static bool validName(const std::string& name) {
        if(name.empty() || name.size()>64) return false;
        for(unsigned char c:name)
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
                 (c>='0'&&c<='9')||c=='_'||c=='-')) return false;
        return true;
    }
    static bool isOutput(const std::string& s) {
        return s=="SpriteOutput"||s=="MeshOutput"||s=="RibbonOutput";
    }
    static Json node(const std::string& id,const std::string& type,Json props=Json::object()) {
        Json n=Json::object();
        n.set("id",Json::string(id));n.set("type",Json::string(type));
        n.set("properties",std::move(props));return n;
    }
    static Json defaultGraph() {
        Json g=Json::object();
        g.set("version",Json::number(int64_t(1)));
        g.set("parameters",Json::object());
        Json nodes=Json::array();
        Json spawn=Json::object();spawn.set("burst",Json::number(int64_t(16)));
        Json init=Json::object();
        init.set("life",Json::number(1.0));
        init.set("size",Json::number(0.4));
        Json output=Json::object();output.set("texture",Json::string("builtin:soft"));
        nodes.push(node("spawn","Spawn",std::move(spawn)));
        nodes.push(node("init","Initialize",std::move(init)));
        nodes.push(node("out","SpriteOutput",std::move(output)));
        g.set("nodes",std::move(nodes));
        Json links=Json::array();
        for(auto pair: {std::pair{"spawn","init"},std::pair{"init","out"}}) {
            Json link=Json::object();
            link.set("from",Json::string(pair.first));
            link.set("to",Json::string(pair.second));
            links.push(std::move(link));
        }
        g.set("links",std::move(links));
        Json outputs=Json::array();outputs.push(Json::string("out"));
        g.set("outputs",std::move(outputs));
        return g;
    }
    fs::path dir() const {return fs::path(project_)/"fx"/"graphs";}
    void refresh() {
        files_.clear();
        std::error_code ec;
        for(fs::directory_iterator it(dir(),ec),end;!ec&&it!=end;it.increment(ec)) {
            if(!it->is_regular_file(ec)) continue;
            const auto file=it->path().filename().string();
            if(file.size()>9 && file.substr(file.size()-9)==".vfx.json")
                files_.push_back(file.substr(0,file.size()-9));
        }
        std::sort(files_.begin(),files_.end());
    }
    bool compile(const Json& doc,std::string& error) const {
        fx::EffectDef effect;
        std::vector<std::string> errors;
        if(fx::compileSpectraGraph(doc,effect,errors)) { error.clear();return true; }
        error=errors.empty()?"Invalid SpectraFX graph":errors.front();
        return false;
    }
    void validate() {
        if(compile(document_,message_)) message_="Graph valid";
    }
    bool commit(Json next) {
        std::string error;
        if(!compile(next,error)) {message_=error;return false;}
        if(undo_.size()>=64) undo_.erase(undo_.begin());
        undo_.push_back(document_);redo_.clear();
        document_=std::move(next);raw_=document_.dump(2);dirty_=true;
        if(selected_>=0 && selected_<int(document_["nodes"].size()))
            propertiesText_=document_["nodes"][size_t(selected_)]["properties"].dump(2);
        message_.clear();return true;
    }
    void create(const std::string& name) {
        current_=name;document_=defaultGraph();saved_=document_;
        raw_=document_.dump(2);dirty_=true;selected_=1;
        propertiesText_=document_["nodes"][1]["properties"].dump(2);
        undo_.clear();redo_.clear();message_.clear();
    }
    void load(const std::string& name) {
        std::ifstream stream(dir()/(name+".vfx.json"),std::ios::binary);
        if(!stream) {message_="Unable to open graph";return;}
        std::string raw((std::istreambuf_iterator<char>(stream)),{});
        try {
            Json doc=Json::parse(raw);
            std::string error;
            if(!compile(doc,error)) {message_=error;return;}
            current_=name;document_=saved_=std::move(doc);
            raw_=document_.dump(2);dirty_=false;selected_=0;
            propertiesText_=document_["nodes"][0]["properties"].dump(2);
            undo_.clear();redo_.clear();message_.clear();
        } catch(const std::exception& e) {message_=e.what();}
    }
    void save(Editor& ed) {
        std::string error;
        if(!compile(document_,error)) {message_=error;return;}
        std::error_code ec;fs::create_directories(dir(),ec);
        if(ec) {message_=ec.message();return;}
        const fs::path target=dir()/(current_+".vfx.json");
        const fs::path temp=target.string()+".tmp";
        {
            std::ofstream out(temp,std::ios::binary|std::ios::trunc);
            out<<document_.dump(2)<<"\n";out.flush();
            if(!out) {message_="Unable to write graph";return;}
        }
        fs::rename(temp,target,ec);
        if(ec) {fs::remove(temp);message_=ec.message();return;}
        saved_=document_;dirty_=false;refresh();ed.reloadFx();
        message_="Saved "+target.string();
    }
    std::string parentOf(const std::string& id) const {
        for(const auto& l:document_["links"].items())
            if(l.getStr("to")==id) return l.getStr("from");
        return {};
    }
    void relink(const std::string& to,const std::string& from) {
        Json next=document_;
        Json& links=*next.find("links");
        for(size_t i=links.size();i-- >0;)
            if(links[i].getStr("to")==to) links.eraseAt(i);
        Json l=Json::object();l.set("from",Json::string(from));
        l.set("to",Json::string(to));links.push(std::move(l));
        commit(std::move(next));
    }
    void add(const std::string& type) {
        Json next=document_;
        Json& nodes=*next.find("nodes");
        if(nodes.size()>=256) {message_="Node limit reached";return;}
        std::set<std::string> ids;
        for(const auto& n:nodes.items()) ids.insert(n.getStr("id"));
        std::string id;for(int i=1;i<10000;++i) {
            id="node"+std::to_string(i);
            if(!ids.count(id)) break;
        }
        Json properties=Json::object();
        if(type=="MeshOutput") {
            Json meshes=Json::array();meshes.push(Json::string("builtin:cube"));
            properties.set("meshes",std::move(meshes));
        }
        if(isOutput(type)) properties.set("texture",Json::string("builtin:soft"));
        next.find("nodes")->push(node(id,type,std::move(properties)));
        if(isOutput(type)) next.find("outputs")->push(Json::string(id));
        // A new node starts disconnected. It is intentionally *not* validated
        // until connected by the editor, so keep it in a local draft.
        // Existing valid graph stays intact if compile rejects this proposal.
        if(commit(std::move(next))) selected_=int(document_["nodes"].size())-1;
    }
    void removeSelected() {
        if(selected_<0 || selected_>=int(document_["nodes"].size())) return;
        const std::string id=document_["nodes"][size_t(selected_)].getStr("id");
        Json next=document_;
        next.find("nodes")->eraseAt(size_t(selected_));
        for(auto key:{"links","outputs"}) {
            auto* arr=next.find(key);
            for(size_t i=arr->size();i-- >0;) {
                if(std::string(key)=="links"?
                   (arr->at(i).getStr("from")==id||arr->at(i).getStr("to")==id):
                   arr->at(i).str()==id) arr->eraseAt(i);
            }
        }
        if(commit(std::move(next))) selected_=-1;
    }
    std::string project_,current_,pending_,newName_,propertiesText_,raw_,message_;
    std::vector<std::string> files_;
    Json document_=defaultGraph(),saved_;
    std::vector<Json> undo_,redo_;
    int selected_=-1;
    bool dirty_=false,createPending_=false;
};

AIVERSE_EDITOR_WINDOW(SpectraFxGraphWindow);
} // namespace aiverse::editor
