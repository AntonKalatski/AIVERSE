#include "Editor.h"
#include "EditorGenreUi.h"
#include "SceneRenderable.h"
#include "EditorEntityAdapter.h"
#include "EditorSceneNodes.h"
#include "EditorSelectionAdapter.h"
#include "EditorComponentPanel.h"
#include "modules/fps/FpsModule.h"
#include <filesystem>
#include <algorithm>
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
// FPS actors live in a World resource, not in per-entity component pools.
// Expose a typed, read-only snapshot without pretending they are DocEntity or
// RTS World rows. This uses the same fps::Module::setup as the FPS client.
const Json& Editor::fpsRuntimeSnapshot() {
    const bool current = fpsRuntimeRevision_ == doc().revision &&
        fpsRuntimeRulesRevision_ == rulesRevision_ &&
        fpsRuntimeProjectDir_ == ws_.project.dir + "\n" + doc().path;
    if (current) return fpsRuntimeCache_;
    // Do not repeatedly construct the entire simulation while the user drags
    // a Transform or paints. Refresh on release, as the RTS preview does.
    if (fpsRuntimeRevision_ != UINT64_MAX && ImGui::GetCurrentContext() &&
        (ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
         ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
         ImGui::IsMouseDown(ImGuiMouseButton_Middle)) &&
        fpsRuntimeProjectDir_ == ws_.project.dir + "\n" + doc().path)
        return fpsRuntimeCache_;
    fpsRuntimeRevision_ = doc().revision;
    fpsRuntimeRulesRevision_ = rulesRevision_;
    fpsRuntimeProjectDir_ = ws_.project.dir + "\n" + doc().path;
    fpsRuntimeCache_ = Json::object();
    Json actors = Json::array();
    if (!ws_.project.isFps()) {
        fpsRuntimeCache_.set("actors", actors);
        return fpsRuntimeCache_;
    }
    try {
        const fps::Config config = fps::readConfig(ws_.project.rulesJson);
        Simulation preview({1, config.tickRate, 0}, std::make_unique<fps::Module>());
        preview.setup(ws_.project.rulesJson, doc().toJson());
        auto* module = dynamic_cast<fps::Module*>(&preview.module());
        if (!module) throw std::runtime_error("FPS module missing");
        const fps::State& state = module->state(preview);
        for (const fps::Actor& actor : state.actors) {
            Json data = Json::object();
            data.set("slot", Json::number(int64_t(actor.slot)));
            data.set("team", Json::number(int64_t(actor.team)));
            Json pos = Json::array(), velocity = Json::array();
            for (const Fixed c : {actor.pos.x, actor.pos.y, actor.pos.z})
                pos.push(Json::number(c.toDouble(), 3));
            for (const Fixed c : {actor.velocity.x, actor.velocity.y, actor.velocity.z})
                velocity.push(Json::number(c.toDouble(), 3));
            data.set("position", pos);
            data.set("velocity", velocity);
            data.set("yaw", Json::number(actor.yaw.toDouble(), 3));
            data.set("pitch", Json::number(actor.pitch.toDouble(), 3));
            data.set("health", Json::number(int64_t(actor.health)));
            data.set("ammo", Json::number(int64_t(actor.ammo)));
            data.set("reserve", Json::number(int64_t(actor.reserve)));
            actors.push(std::move(data));
        }
    } catch (const std::exception& e) {
        fpsRuntimeCache_.set("error", Json::string(e.what()));
    }
    fpsRuntimeCache_.set("actors", actors);
    return fpsRuntimeCache_;
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
    const uint64_t selectedKey = selection.kind == Selection::FpsObject ? selection.ref : 0;
    fpsSceneObjects_.clear();
    auto& instances = fpsSceneObjects_;
    const auto& fps = doc().data.fps;
    for (const auto& node : enumerateDocumentSceneNodes(doc(), true)) {
        if (!node.hasPosition) continue;
        const Json* source = fpsDocumentEntity(fps, node.ref);
        if (!source) continue;
        SceneRenderable object;
        object.entity = node.ref.fpsKey();
        object.mesh = &fpsPreviewMesh_;
        object.pickable = true;
        if (node.ref.source == DocumentEntityRef::Source::FpsCollider) {
            const Json& lower = (*source)["min"];
            const Json& upper = (*source)["max"];
            if (!lower.isArray() || !upper.isArray() ||
                lower.size() < 3 || upper.size() < 3) continue;
            const float3 lo = point(lower), hi = point(upper);
            const float3 size = hi - lo;
            if (!std::isfinite(lo.x) || !std::isfinite(lo.y) || !std::isfinite(lo.z) ||
                !std::isfinite(hi.x) || !std::isfinite(hi.y) || !std::isfinite(hi.z) ||
                size.x <= 0 || size.y <= 0 || size.z <= 0) continue;
            const float3 base{float(node.position.x), lo.y, float(node.position.z)};
            if (length(base - camera.target) > 1000 && hi.y > 0) continue;
            object.world = mat4::translate(base) * mat4::scale(size);
            object.tint = {0.35f, 0.42f, 0.44f, 1};
            object.boundsMin = lo;
            object.boundsMax = hi;
        } else if (node.ref.source == DocumentEntityRef::Source::FpsSpawn) {
            const float3 pos{float(node.position.x), float(node.position.y),
                             float(node.position.z)};
            if (!std::isfinite(pos.x) || !std::isfinite(pos.y) ||
                !std::isfinite(pos.z)) continue;
            object.world = mat4::translate(pos) * mat4::scale({0.6f, 1.8f, 0.6f});
            object.tint = {0.1f, 0.65f, 0.85f, 1};
            object.boundsMin = {pos.x - 0.3f, pos.y, pos.z - 0.3f};
            object.boundsMax = {pos.x + 0.3f, pos.y + 1.8f, pos.z + 0.3f};
        } else {
            continue;
        }
        instances.push_back(object);
    }
    // FPS actors belong to the live-style World State resource. Draw simple
    // readonly scene markers from its snapshot without fabricating Entity IDs.
    const Json& runtime = fpsRuntimeSnapshot();
    const Json& actors = runtime["actors"];
    if (actors.isArray()) {
        for (size_t i = 0; i < actors.size(); ++i) {
            const Json& pos = actors[i]["position"];
            if (!pos.isArray() || pos.size() != 3 ||
                !pos[0].isNumber() || !pos[1].isNumber() || !pos[2].isNumber())
                continue;
            const float3 point{float(pos[0].toDouble()),
                               float(pos[1].toDouble()),
                               float(pos[2].toDouble())};
            if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                !std::isfinite(point.z)) continue;
            SceneRenderable actor;
            actor.runtimeActor = int(i);
            actor.world = mat4::translate(point) * mat4::scale({0.5f, 1.7f, 0.5f});
            actor.tint = {0.85f, 0.68f, 0.25f, 1};
            actor.mesh = &fpsPreviewMesh_;
            actor.pickable = true;
            actor.boundsMin = {point.x - 0.25f, point.y, point.z - 0.25f};
            actor.boundsMax = {point.x + 0.25f, point.y + 1.7f, point.z + 0.25f};
            instances.push_back(actor);
        }
    }
    const uint64_t hoveredKey = hover.kind == Selection::FpsObject ? hover.ref : 0;
    for (auto& object : instances) {
        object.selected = object.runtimeActor >= 0
            ? selection.kind == Selection::FpsActor &&
              selection.index == object.runtimeActor
            : selectedKey != 0 && object.entity == selectedKey;
        if (!object.selected && object.runtimeActor < 0)
            for (const auto& chosen : selectionSet)
                if (chosen.kind == Selection::FpsObject &&
                    chosen.ref == object.entity) {
                    object.selected = true;
                    break;
                }
        object.hovered = !object.selected &&
            (object.runtimeActor >= 0
                ? hover.kind == Selection::FpsActor &&
                  hover.index == object.runtimeActor
                : hoveredKey != 0 && object.entity == hoveredKey);
    }
    submitSceneRenderables(r3d_, instances);
    // Draw transform axes from the selected renderable, if visible.
    for (const auto& selected : instances) {
        // The gizmo belongs to the primary selected entity, not whichever
        // selected collider happens to come first in source order.
        if (!selected.selected || selected.runtimeActor >= 0 ||
            selected.entity != selectedKey) continue;
        DocumentEntityRef ref;
        DocumentTransform position;
        if (!DocumentEntityRef::fromFpsKey(selected.entity, ref) ||
            !readDocumentTransform(doc(), ref, position)) break;
        const float3 centre{float(position.x), float(position.y),
                            float(position.z)};
        const float extent = std::max(0.35f, camera.distance * 0.075f);
        r3d_.line(centre, centre + float3{extent, 0, 0}, rgba(235, 84, 84));
        r3d_.line(centre, centre + float3{0, extent, 0}, rgba(93, 215, 113));
        r3d_.line(centre, centre + float3{0, 0, extent}, rgba(80, 153, 245));
        break;
    }
}

bool Editor::moveSelectedFpsSceneObject(float dx, float dy, float dz) {
    // FPS uses the same atomic/deduplicated multi-document translation as
    // RTS. The selected set may contain colliders and spawn points together.
    std::vector<DocumentEntityRef> refs;
    for (const auto& chosen : selected()) {
        DocumentEntityRef ref;
        if (!documentEntityFromSelection(chosen, ref) ||
            ref.source == DocumentEntityRef::Source::MapEntity) continue;
        refs.push_back(ref);
    }
    return translateDocumentEntities(doc(), refs, {dx, dy, dz});
}

bool Editor::isFpsSceneSourceSelected(bool spawn, size_t sourceIndex) const {
    const auto ref = spawn ? DocumentEntityRef::fpsSpawn(sourceIndex)
                           : DocumentEntityRef::fpsCollider(sourceIndex);
    const Selection candidate = selectionForDocumentEntity(ref);
    return selection == candidate ||
        std::find(selectionSet.begin(), selectionSet.end(), candidate) != selectionSet.end();
}

void Editor::selectFpsSceneSource(bool spawn, size_t sourceIndex) {
    const auto ref = spawn ? DocumentEntityRef::fpsSpawn(sourceIndex)
                           : DocumentEntityRef::fpsCollider(sourceIndex);
    const Selection chosen = selectionForDocumentEntity(ref);
    if (chosen.kind != Selection::None) select(chosen);
}

int Editor::pickFpsSceneObject(const Viewport& vp, const render::RTSCamera& camera,
                               float sx, float sy) const {
    render::float3 origin, direction;
    if (!camera.screenRay(sx - vp.originX, sy - vp.originY,
                          vp.pointsW, vp.pointsH, origin, direction)) return -1;
    return pickSceneRenderable(fpsSceneObjects_, origin, direction);
}


}
