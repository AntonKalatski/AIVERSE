#include "Editor.h"
#include "EditorGenreUi.h"
#include "SceneRenderable.h"
#include "EditorEntityAdapter.h"
#include "EditorSceneNodes.h"
#include "EditorSelectionAdapter.h"
#include "EditorComponentPanel.h"
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
    const uint64_t hoveredKey = hover.kind == Selection::FpsObject ? hover.ref : 0;
    for (auto& object : instances) {
        object.selected = selectedKey != 0 && object.entity == selectedKey;
        if (!object.selected)
            for (const auto& chosen : selectionSet)
                if (chosen.kind == Selection::FpsObject &&
                    chosen.ref == object.entity) {
                    object.selected = true;
                    break;
                }
        object.hovered = !object.selected && hoveredKey != 0 && object.entity == hoveredKey;
    }
    submitSceneRenderables(r3d_, instances);
    // Draw transform axes from the selected renderable, if visible.
    for (const auto& selected : instances) {
        // The gizmo belongs to the primary selected entity, not whichever
        // selected collider happens to come first in source order.
        if (!selected.selected || selected.entity != selectedKey) continue;
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
