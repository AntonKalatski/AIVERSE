#include "Editor.h"
#include "EditorGenreUi.h"
#include "SceneRenderable.h"
#include "modules/fps/FpsModule.h"
#include <filesystem>
#include <cmath>
namespace aiverse::editor {
namespace {
std::string quote(const std::string& s) {
#ifdef _WIN32
    // cmd.exe expands these even within quotes; refuse paths/addresses containing them.
    if(s.find_first_of("\"%\r\n!")!=std::string::npos)throw std::runtime_error("unsupported shell characters in FPS launch path");
    return "\""+s+"\"";
#else
    std::string result="'";for(char c:s)result+=c=='\''?"'\\''":std::string(1,c);return result+"'";
#endif
}
render::float3 point(const Json& j){return {float(j[0].toDouble()),float(j[1].toDouble()),float(j[2].toDouble())};}
}
void Editor::startFpsPlay(bool listen,const std::string& address) {
    if(!ws_.project.isFps()||ws_.building())return;
    try {
        auto cfg=fps::readConfig(ws_.project.rulesJson);Simulation check({1,cfg.tickRate,0},std::make_unique<fps::Module>());check.setup(ws_.project.rulesJson,doc().toJson());
        std::string error;
        if(doc().dirty&&!saveMap(doc().path))return;
        if(!ws_.project.saveRules(&error)||!ws_.project.saveManifest(&error))throw std::runtime_error(error);
        namespace fs=std::filesystem;
        std::string name="aiverse_fps_client";
#ifdef _WIN32
        name+=".exe";
#endif
        std::vector<fs::path> roots{fs::path(opt_.editorExe).parent_path(),fs::path(ws_.buildTool).parent_path(),fs::path(ws_.engineDir)/"bin"};
        auto parent=fs::path(opt_.editorExe).parent_path();for(int i=0;i<4;++i){roots.push_back(parent);parent=parent.parent_path();}
        fs::path exe;std::error_code ec;
        for(const auto& root:roots)for(const auto& rel:{fs::path(name),fs::path("modules/fps")/name,fs::path("modules/fps/Release")/name})if(exe.empty()&&fs::is_regular_file(root/rel,ec))exe=root/rel;
        if(exe.empty()){status=genreText(ws_,"fps.editor.missing");ws_.log("fps",status,true);return;}
        std::string cmd="cd "+quote(ws_.engineDir)+" && "+quote(exe.string())+" --project "+quote(ws_.project.dir);
#ifdef _WIN32
        cmd="cd /d "+quote(ws_.engineDir)+" && "+quote(exe.string())+" --project "+quote(ws_.project.dir);
#endif
        if(listen)cmd+=" --listen";else if(!address.empty())cmd+=" --connect "+quote(address);
        ws_.startCommand(cmd,cmd,"fps-play");status=genreText(ws_,"fps.editor.running");
    }catch(const std::exception& e){status=genreText(ws_,"fps.editor.invalid");ws_.log("fps",e.what(),true);}
}
// FPS contributes scene geometry to the shared editor renderer. The scene
// lifecycle, post-processing, depth buffer and UI overlays are owned by
// Editor::renderScene, not by a genre-specific render pass.
void Editor::enqueueFpsSceneGeometry(const render::RTSCamera& camera) {
    using namespace render;
    if (!fpsPreviewMesh_.vb.valid()) {
        MeshBuilder mesh;
        mesh.color(rgba(255,255,255)).box({0,0.5f,0},{1,1,1});
        fpsPreviewMesh_ = mesh.upload(*dev_);
    }
    const uint64_t selectedKey =
        fpsSelectedSceneObject_ >= 0 && fpsSelectedSceneObject_ < int(fpsSceneObjects_.size())
            ? fpsSceneObjects_[size_t(fpsSelectedSceneObject_)].entity : 0;
    fpsSceneObjects_.clear();
    auto& instances = fpsSceneObjects_;
    const auto& fps = doc().data.fps;
    size_t colliderIndex = 0;
    for (const auto& collider : fps["colliders"].items()) {
        const size_t sourceIndex = colliderIndex++;
        if (!collider["min"].isArray() || !collider["max"].isArray() ||
            collider["min"].size() < 3 || collider["max"].size() < 3) continue;
        const float3 lo = point(collider["min"]), hi = point(collider["max"]);
        const float3 size = hi - lo;
        if (!std::isfinite(lo.x) || !std::isfinite(lo.y) || !std::isfinite(lo.z) ||
            !std::isfinite(hi.x) || !std::isfinite(hi.y) || !std::isfinite(hi.z) ||
            size.x <= 0 || size.y <= 0 || size.z <= 0) continue;
        const float3 base{(lo.x+hi.x)*0.5f,lo.y,(lo.z+hi.z)*0.5f};
        if (length(base-camera.target)>1000 && hi.y>0) continue;
        SceneRenderable object;
        object.world = mat4::translate(base)*mat4::scale(size);
        object.tint = {0.35f,0.42f,0.44f,1};
        object.mesh = &fpsPreviewMesh_;
        object.entity = uint64_t(sourceIndex) + 1;
        object.boundsMin = lo;
        object.boundsMax = hi;
        object.pickable = true;
        instances.push_back(object);
    }
    size_t spawnIndex = 0;
    for (const auto& spawn : fps["spawns"].items()) {
        const size_t sourceIndex = spawnIndex++;
        if (!spawn.isArray() || spawn.size() < 3) continue;
        const float3 pos = point(spawn);
        if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) continue;
        SceneRenderable object;
        object.world = mat4::translate(pos)*mat4::scale({0.6f,1.8f,0.6f});
        object.tint = {0.1f,0.65f,0.85f,1};
        object.mesh = &fpsPreviewMesh_;
        object.entity = (uint64_t(1) << 63) | (uint64_t(sourceIndex) + 1);
        object.boundsMin = {pos.x - 0.3f, pos.y, pos.z - 0.3f};
        object.boundsMax = {pos.x + 0.3f, pos.y + 1.8f, pos.z + 0.3f};
        object.pickable = true;
        instances.push_back(object);
    }
    // Preserve the selected source identity when filtering or visibility changes
    // shift the renderable indices (e.g. an invalid collider is skipped).
    fpsSelectedSceneObject_ = -1;
    if (selectedKey != 0) {
        for (size_t i = 0; i < instances.size(); ++i) {
            if (instances[i].entity == selectedKey) {
                fpsSelectedSceneObject_ = int(i);
                break;
            }
        }
    }
    if (fpsSelectedSceneObject_ >= 0) instances[size_t(fpsSelectedSceneObject_)].selected = true;
    submitSceneRenderables(r3d_, instances);
}

bool Editor::moveSelectedFpsSceneObject(float dx, float dy, float dz) {
    if (fpsSelectedSceneObject_ < 0 || fpsSelectedSceneObject_ >= int(fpsSceneObjects_.size()) ||
        !std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz)) return false;
    const uint64_t key = fpsSceneObjects_[size_t(fpsSelectedSceneObject_)].entity;
    if (!key) return false;
    const bool spawn = (key & (uint64_t(1) << 63)) != 0;
    const size_t sourceIndex = size_t((key & ~(uint64_t(1) << 63)) - 1);
    auto& fps = doc().data.fps;
    const char* group = spawn ? "spawns" : "colliders";
    if (sourceIndex >= fps[group].size()) return false;
    Json& item = fps.member(group)[sourceIndex];
    // Validate every affected endpoint before changing the document. A failed
    // second endpoint must never leave a collider half-translated.
    auto shifted = [&](const Json& xyz, Json& out) {
        if (!xyz.isArray() || xyz.size() < 3 ||
            !xyz[0].isNumber() || !xyz[1].isNumber() || !xyz[2].isNumber()) return false;
        const double x = xyz[0].toDouble() + dx, y = xyz[1].toDouble() + dy, z = xyz[2].toDouble() + dz;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
        out = Json::array();
        out.push(Json::number(x, 3));
        out.push(Json::number(y, 3));
        out.push(Json::number(z, 3));
        return true;
    };
    if (spawn) {
        Json moved;
        if (!shifted(item, moved)) return false;
        item = moved;
    } else {
        Json minMoved, maxMoved;
        if (!shifted(item["min"], minMoved) || !shifted(item["max"], maxMoved)) return false;
        item.member("min") = minMoved;
        item.member("max") = maxMoved;
    }
    doc().touched();
    return true;
}

bool Editor::isFpsSceneSourceSelected(bool spawn, size_t sourceIndex) const {
    if (fpsSelectedSceneObject_ < 0 ||
        fpsSelectedSceneObject_ >= int(fpsSceneObjects_.size())) return false;
    const uint64_t key = (spawn ? (uint64_t(1) << 63) : 0) | (uint64_t(sourceIndex) + 1);
    return fpsSceneObjects_[size_t(fpsSelectedSceneObject_)].entity == key;
}

void Editor::selectFpsSceneSource(bool spawn, size_t sourceIndex) {
    const uint64_t key = (spawn ? (uint64_t(1) << 63) : 0) | (uint64_t(sourceIndex) + 1);
    fpsSelectedSceneObject_ = -1;
    for (size_t i = 0; i < fpsSceneObjects_.size(); ++i) {
        if (fpsSceneObjects_[i].entity == key) {
            fpsSelectedSceneObject_ = int(i);
            break;
        }
    }
}

int Editor::pickFpsSceneObject(const Viewport& vp, const render::RTSCamera& camera,
                               float sx, float sy) const {
    render::float3 origin, direction;
    if (!camera.screenRay(sx - vp.originX, sy - vp.originY,
                          vp.pointsW, vp.pointsH, origin, direction)) return -1;
    return pickSceneRenderable(fpsSceneObjects_, origin, direction);
}

void Editor::drawFpsSceneInspector() {
    const int selected = fpsSelectedSceneObject_;
    if (selected < 0 || selected >= int(fpsSceneObjects_.size())) return;
    auto& fps = doc().data.fps;
    const uint64_t key = fpsSceneObjects_[size_t(selected)].entity;
    if (!key) return;
    const bool collider = (key & (uint64_t(1) << 63)) == 0;
    const size_t sourceIndex = size_t((key & ~(uint64_t(1) << 63)) - 1);
    if (sourceIndex >= fps[collider ? "colliders" : "spawns"].size()) return;
    Json& source = collider ? fps.member("colliders")[sourceIndex]
                            : fps.member("spawns")[sourceIndex];
    ImGui::TextDisabled("SCENE OBJECT");
    ImGui::Text("%s #%d", collider ? "Box Collider" : "Spawn Point", sourceIndex + 1);
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto field = [&](const char* label, Json& array) {
            if (!array.isArray() || array.size() < 3) return;
            float xyz[3] = {float(array[0].toDouble()), float(array[1].toDouble()), float(array[2].toDouble())};
            ImGui::PushID(label);
            ImGui::TextDisabled("%s", label);
            ImGui::SetNextItemWidth(-1);
            const bool changed = ImGui::DragFloat3("##xyz", xyz, 0.05f, 0, 0, "%.3f");
            if (ImGui::IsItemActivated()) doc().beginEdit();
            if (changed && std::isfinite(xyz[0]) && std::isfinite(xyz[1]) && std::isfinite(xyz[2])) {
                bool valid = true;
                if (collider) {
                    const Json& other = source[label[0] == 'M' && label[1] == 'i' ? "max" : "min"];
                    if (other.isArray() && other.size() >= 3) {
                        for (int axis = 0; axis < 3; ++axis) {
                            const float limit = float(other[size_t(axis)].toDouble());
                            if (label[0] == 'M' && label[1] == 'i')
                                valid &= xyz[axis] < limit;
                            else
                                valid &= xyz[axis] > limit;
                        }
                    }
                }
                if (valid) {
                    Json value = Json::array();
                    for (float x : xyz) value.push(Json::number(double(x), 3));
                    array = value;
                    doc().touched();
                }
            }
            ImGui::PopID();
        };
        if (collider) {
            field("Min XYZ", source.member("min"));
            field("Max XYZ", source.member("max"));
            ImGui::TextDisabled("Collider bounds are defined by Min/Max.");
        } else {
            field("Position XYZ", source);
            ImGui::TextDisabled("Spawn orientation and scale are not part of the current map schema.");
        }
    }
    if (ImGui::CollapsingHeader("Raw data")) {
        ImGui::TextWrapped("%s", source.dump().c_str());
    }
}

}
