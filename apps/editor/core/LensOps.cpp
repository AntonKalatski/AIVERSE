// LENS project authoring shared by Studio and the standalone MCP server.
#include "Workspace.h"
#include "engine/sim/CameraSceneAsset.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace aiverse::editor {
namespace {
namespace fs = std::filesystem;
constexpr size_t maxBytes = 1024 * 1024;
Json emptyScene() { return Json::parse(R"({"version":1,"cameras":[]})"); }
fs::path assetPath(const Workspace& ws) {
    if (ws.project.dir.empty()) throw std::runtime_error("open_game first");
    const fs::path path = fs::path(ws.project.dir) / "cameras.json";
    if (fs::is_symlink(fs::symlink_status(path)))
        throw std::runtime_error("LENS refuses a symlink cameras.json");
    return path;
}
Json readScene(const Workspace& ws) {
    const auto path = assetPath(ws);
    if (!fs::exists(path)) return emptyScene();
    if (!fs::is_regular_file(path) || fs::file_size(path) > maxBytes)
        throw std::runtime_error("cameras.json must be a regular file of at most 1 MiB");
    std::ifstream input(path, std::ios::binary);
    std::string text(maxBytes + 1, '\0');
    input.read(text.data(), std::streamsize(text.size()));
    if (input.bad()) throw std::runtime_error("cannot read cameras.json");
    text.resize(size_t(input.gcount()));
    if (text.size() > maxBytes) throw std::runtime_error("cameras.json exceeds 1 MiB");
    return Json::parse(text);
}
Json validate(const Json& scene) {
    if (scene.dump(-1).size() > maxBytes) throw std::runtime_error("camera scene exceeds 1 MiB");
    World world;
    SceneHierarchy hierarchy;
    std::string error;
    Json response = Json::object();
    const bool valid = loadCameraScene(world, hierarchy, scene, nullptr, &error);
    response.set("ok", Json::fromBool(valid));
    if (!valid) response.set("error", Json::string(error));
    else {
        response.set("normalizedScene", cameraSceneToJson(world));
        response.set("count", Json::number(int64_t(scene["cameras"].size())));
        bool clientCompatible = true;
        for (const auto& camera : response["normalizedScene"]["cameras"].items())
            clientCompatible &= camera["output"].getStr("kind") == "texture";
        response.set("clientCompatible", Json::fromBool(clientCompatible));
    }
    return response;
}
Json checkedCurrent(const Workspace& ws, const Json& p) {
    const Json current = readScene(ws);
    if (!p["expectedScene"].isObject() || !(current == p["expectedScene"]))
        throw std::runtime_error("camera scene changed or expectedScene missing; call lens_scene_get and reapply your edit");
    return current;
}
Json save(Workspace& ws, const Json& scene) {
    Json response = validate(scene);
    if (!response.getBool("ok")) return response;
    const std::string text = scene.dump(2) + "\n";
    if (text.size() > maxBytes) throw std::runtime_error("camera scene exceeds 1 MiB");
    const auto destination = assetPath(ws);
    // An exclusively created sibling directory avoids shared temporary files and
    // leaves the previous asset untouched on validation/write/rename failures.
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporary = destination.parent_path() / (".lens-save-" + std::to_string(stamp));
    if (!fs::create_directory(temporary)) throw std::runtime_error("cannot stage camera scene");
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{temporary};
    const auto staged = temporary / "cameras.json";
    std::ofstream out(staged, std::ios::binary);
    out << text;
    out.close();
    if (!out) throw std::runtime_error("cannot write staged camera scene");
    fs::rename(staged, destination); // Never delete last-good data as a fallback.
    ws.note({Change::Project, 0, 0, -1, -1, -1, "LENS: cameras.json saved; Reload camera window"});
    response.set("scene", scene);
    response.set("reloadRequired", Json::fromBool(true));
    return response;
}
int64_t positiveId(const Json& value) {
    const auto id = value.integer();
    if (!value.isNumber() || id <= 0 || value.toDouble() != double(id))
        throw std::runtime_error("camera id must be a positive integer");
    return id;
}
} // namespace

void registerLensOps(Workspace& ws) {
    auto add = [&ws](const char* name, const char* description, const char* schema, Workspace::OpFn fn) {
        ws.registerOp({name, description, Json::parse(schema), std::move(fn)});
    };
    add("lens_scene_get", "Read project cameras.json (missing = empty v1 scene), runtime validation and normalized defaults. Does not render. Pass scene back as expectedScene when editing.",
        R"({"type":"object","properties":{}})", [&ws](const Json&) {
            Json scene = readScene(ws), result = validate(scene);
            result.set("scene", scene);
            result.set("exists", Json::fromBool(fs::exists(assetPath(ws))));
            result.set("clientEnabled", Json::fromBool(ws.project.manifest["lens"].getBool("enabled")));
            return result;
        });
    add("lens_scene_validate", "Validate a v1 camera scene with the native loader, without changing files. normalizedScene exposes defaults/clamping; clientCompatible means texture-only outputs, not GPU acceptance.",
        R"({"type":"object","properties":{"scene":{"type":"object"}},"required":["scene"]})",
        [](const Json& p) { return validate(p["scene"]); });
    add("lens_scene_set", "Replace cameras.json after native validation and expectedScene comparison. Saves immediately; no editor undo. Reload the LENS window/restart the client to consume it. Does not enable game.json lens.enabled.",
        R"({"type":"object","properties":{"scene":{"type":"object"},"expectedScene":{"type":"object"}},"required":["scene","expectedScene"]})",
        [&ws](const Json& p) { checkedCurrent(ws, p); return save(ws, p["scene"]); });
    add("lens_camera_set", "Create or replace one complete camera record by positive id, preserving other records. Supply the scene returned by lens_scene_get as expectedScene. For partial edits merge locally first. Saves immediately; Reload LENS afterward.",
        R"({"type":"object","properties":{"camera":{"type":"object"},"expectedScene":{"type":"object"}},"required":["camera","expectedScene"]})",
        [&ws](const Json& p) {
            Json scene = checkedCurrent(ws, p);
            if (!validate(scene).getBool("ok")) throw std::runtime_error("repair invalid scene with lens_scene_set first");
            const auto id = positiveId(p["camera"]["id"]);
            bool found = false;
            for (auto& item : scene.member("cameras").itemsMut())
                if (item.getInt("id") == id) { item = p["camera"]; found = true; break; }
            if (!found) scene.member("cameras").push(p["camera"]);
            return save(ws, scene);
        });
    add("lens_camera_delete", "Remove one camera record by positive id. Requires expectedScene from lens_scene_get; unknown ids fail without writes. Saves immediately; no undo or live GPU resource deletion.",
        R"({"type":"object","properties":{"id":{"type":"integer","minimum":1},"expectedScene":{"type":"object"}},"required":["id","expectedScene"]})",
        [&ws](const Json& p) {
            Json scene = checkedCurrent(ws, p);
            if (!validate(scene).getBool("ok")) throw std::runtime_error("repair invalid scene with lens_scene_set first");
            const auto id = positiveId(p["id"]);
            for (size_t i = 0; i < scene["cameras"].size(); ++i)
                if (scene["cameras"][i].getInt("id") == id) {
                    scene.member("cameras").eraseAt(i);
                    return save(ws, scene);
                }
            throw std::runtime_error("camera id not found");
        });
}
} // namespace aiverse::editor
