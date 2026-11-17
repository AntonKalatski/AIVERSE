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
    std::vector<SceneRenderable> instances;
    const auto& fps = doc().data.fps;
    for (const auto& collider : fps["colliders"].items()) {
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
        object.boundsMin = lo;
        object.boundsMax = hi;
        object.pickable = true;
        instances.push_back(object);
    }
    for (const auto& spawn : fps["spawns"].items()) {
        if (!spawn.isArray() || spawn.size() < 3) continue;
        const float3 pos = point(spawn);
        if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) continue;
        SceneRenderable object;
        object.world = mat4::translate(pos)*mat4::scale({0.6f,1.8f,0.6f});
        object.tint = {0.1f,0.65f,0.85f,1};
        object.mesh = &fpsPreviewMesh_;
        object.boundsMin = {pos.x - 0.3f, pos.y, pos.z - 0.3f};
        object.boundsMax = {pos.x + 0.3f, pos.y + 1.8f, pos.z + 0.3f};
        object.pickable = true;
        instances.push_back(object);
    }
    // Both primitive and authored-model objects now share the same submitter.
    submitSceneRenderables(r3d_, instances);
}

}
