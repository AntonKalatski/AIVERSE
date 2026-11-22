// aiverse_catalogview: the website's 3D preview of catalog packages (docs/packages.md § Web viewer).
// Shows the materials of a material library on a sphere and a tiled ground slab, or glTF / GLB models,
// through the engine's own Renderer3D (the same PBR path games use), with an orbit camera.
//
// Input is a view folder (built by AIVERSE-PLUGINS tools/catalog.py for every package that has a
// "viewer" block): <src>/view.json + the files it lists, laid out with the project-relative paths
// the package uses (materials.json texture paths resolve as they would inside a project).
//
//   native:  aiverse_catalogview --src DIR [--item N] [--yaw deg] [--pitch deg]
//   web:     catalogview/index.html?src=<url of the view folder>&item=N
//            the page fetches view.json and the files of the shown item into the in-memory file
//            system (/view), then calls cv_select(N) (exported) — no other network access.
//
// view.json:
//   {"schema":1,"kind":"materials","library":"materials.json",
//    "items":[{"id":"terrain_grass","title":"Grass","files":["materials/terrain/textures/a.png",...],
//              "repeat":2.0}], "shared":["materials.json"]}
//   {"schema":1,"kind":"models","items":[{"id":"oak","title":"Oak","model":"models/oak.glb",
//              "files":["models/oak.glb","models/tex/x.png"]}]}
#include "../../engine/platform/Platform.h"
#include "../../engine/render/Model.h"
#include "../../engine/render/Renderer3D.h"
#include "../../engine/data/Json.h"
#include "../../engine/view/AppearanceAuthoringService.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

using namespace aiverse;
using namespace aiverse::render;

namespace {

constexpr float kPi = 3.14159265f;

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}

// Sphere with spherical UVs (u around Y, v pole to pole) and a ground slab with planar UVs that
// repeat every `repeat` metres, both as a Model so materials with texture maps render exactly as
// on imported models (tangents generated for normal maps).
std::unique_ptr<Model> makeMaterialModel(float repeat) {
    MeshBuilder sphere, ground;
    sphere.sphere({0, 1.05f, 0}, 1.0f, 48, 96);
    const float h = 3.0f;
    const int n = 12;
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const float x0 = -h + 2 * h * x / n, x1 = -h + 2 * h * (x + 1) / n;
            const float z0 = -h + 2 * h * z / n, z1 = -h + 2 * h * (z + 1) / n;
            ground.quad({x0, 0, z1}, {x1, 0, z1}, {x1, 0, z0}, {x0, 0, z0});
        }
    Material base;
    base.vertexColors = false;
    auto m = modelFromParts("catalogview/material", {{"sphere", &sphere, base, mat4{}},
                                                     {"ground", &ground, base, mat4{}}});
    if (!m) return m;
    const auto& sp = m->meshes[0].primitives[0];
    for (uint32_t i = sp.firstVertex; i < sp.firstVertex + sp.vertexCount; ++i) {
        auto& v = m->vertices[i];
        const float3 d = normalize(v.pos - float3{0, 1.05f, 0});
        v.uv = {0.5f + std::atan2(d.x, d.z) / (2 * kPi) * 2.0f,   // two repeats around: 1 m tiles look right
                std::acos(std::clamp(d.y, -1.0f, 1.0f)) / kPi};
    }
    const auto& gp = m->meshes[1].primitives[0];
    for (uint32_t i = gp.firstVertex; i < gp.firstVertex + gp.vertexCount; ++i) {
        auto& v = m->vertices[i];
        v.uv = {v.pos.x / repeat, v.pos.z / repeat};
    }
    m->materials[1] = m->materials[0];
    m->meshes[1].primitives[0].material = 0;
    generateTangents(m->vertices, m->indices, sp.firstIndex, sp.indexCount);
    generateTangents(m->vertices, m->indices, gp.firstIndex, gp.indexCount);
    m->batching.autoLod = false;
    return m;
}

struct App {
    std::string root;            // view folder
    Json view, library;
    gfx::Device* dev = nullptr;
    Renderer3D r3d;
    Camera cam;
    std::unique_ptr<Model> model;
    std::vector<std::unique_ptr<Model>> retired;  // released after the GPU is done (next frames)
    int item = -1, requested = 0;
    bool dragging = false, autoRotate = true;
    float lastX = 0, lastY = 0, idle = 0;
    std::string status;

    std::string path(const std::string& rel) const { return root + "/" + rel; }

    void log(const std::string& s) {
        status = s;
        std::printf("catalogview: %s\n", s.c_str());
        std::fflush(stdout);
    }

    bool loadView() {
        std::string text;
        if (!readFile(path("view.json"), text)) { log("no view.json in " + root); return false; }
        try { view = Json::parse(text); } catch (const std::exception& e) { log(std::string("view.json: ") + e.what()); return false; }
        if (view.getStr("kind") == "materials") {
            if (!readFile(path(view.getStr("library", "materials.json")), text)) { log("material library missing"); return false; }
            try { library = Json::parse(text); } catch (const std::exception& e) { log(std::string("library: ") + e.what()); return false; }
        }
        return true;
    }

    void show(int index) {
        const Json& items = view["items"];
        if (!items.isArray() || items.size() == 0) { log("view has no items"); return; }
        index = std::clamp(index, 0, int(items.size()) - 1);
        const Json& it = items[size_t(index)];
        std::unique_ptr<Model> next;
        if (view.getStr("kind") == "materials") {
            next = makeMaterialModel(float(it["repeat"].toDouble(2.0)));
            if (!next) { log("geometry failed"); return; }
            prepareModelUpload(*next);
            if (!uploadModel(*dev, *next)) { log("GPU upload failed"); return; }
            Model* m = next.get();
            Material mt = m->materials[0];
            auto loadTexture = [&](const std::string& rel) -> int {
                Image img;
                std::string bytes;
                if (!readFile(path(rel), bytes) || !decodeImage(bytes, img)) { log("texture missing: " + rel); return -1; }
                img.name = rel;
                img.repeat = true;
                return addImage(*dev, *m, std::move(img));
            };
            view::AppearanceAuthoringService::applyMaterial(mt, library, Json::string(it.getStr("id")), loadTexture,
                                                            [&](const std::string& w) { log("material: " + w); });
            mt.vertexColors = false;
            mt.uvScale = {1, 1};
            m->materials[0] = mt;
            m->materials[1] = mt;
            m->markMaterialChanged();
            cam.target = {0, 0.8f, 0};
            cam.distance = 5.2f;
            cam.minDistance = 2.0f;
            cam.maxDistance = 14.0f;
        } else {
            std::vector<std::string> warn;
            const std::string file = path(it.getStr("model"));
            next = loadModel(file, readFile, &warn);
            for (auto& w : warn) log(w);
            if (!next) { log("cannot load " + it.getStr("model")); return; }
            prepareModelUpload(*next);
            if (!uploadModel(*dev, *next)) { log("GPU upload failed"); return; }
            const float3 c = (next->boundsMin + next->boundsMax) * 0.5f;
            const float r = std::max(0.25f, length(next->boundsMax - next->boundsMin) * 0.5f);
            cam.target = c;
            cam.distance = r * 2.6f;
            cam.minDistance = r * 0.8f;
            cam.maxDistance = r * 8.0f;
        }
        if (model) retired.push_back(std::move(model));
        model = std::move(next);
        item = index;
        log("showing " + it.getStr("id"));
    }

    void init(gfx::Device& d) {
        dev = &d;
        r3d.init(d);
        r3d.setShadowQuality(ShadowQuality::Medium);
        cam.yaw = 0.6f;
        cam.pitch = 0.42f;
        cam.fovY = 0.7f;
        cam.zNear = 0.05f;
        cam.zFar = 200.0f;
        if (loadView()) show(requested);
    }

    void event(const platform::Event& e) {
        using T = platform::EventType;
        if (e.type == T::MouseDown || e.type == T::TouchBegin) { dragging = true; lastX = e.x; lastY = e.y; autoRotate = false; }
        else if (e.type == T::MouseUp || e.type == T::TouchEnd || e.type == T::TouchCancel) { dragging = false; idle = 0; }
        else if ((e.type == T::MouseMove || e.type == T::TouchMove) && dragging) {
            cam.yaw -= (e.x - lastX) * 0.01f;
            cam.pitch = std::clamp(cam.pitch + (e.y - lastY) * 0.01f, -0.2f, 1.45f);
            lastX = e.x; lastY = e.y;
        } else if (e.type == T::MouseWheel) {
            cam.distance = std::clamp(cam.distance * std::pow(1.1f, -e.wheelY), cam.minDistance, cam.maxDistance);
        }
    }

    void frame(gfx::Device& d, double dt) {
        if (!dragging) {
            idle += float(dt);
            if (idle > 4) autoRotate = true;
        }
        if (autoRotate) cam.yaw += float(dt) * 0.25f;
        r3d.beginScene(cam, d.framebufferWidth(), d.framebufferHeight());
        if (model) {
            ModelDraw md;
            md.model = model.get();
            md.world = mat4{};
            r3d.drawModel(md);
        }
        gfx::PassDesc pass;
        pass.clearColor[0] = 0.953f; pass.clearColor[1] = 0.937f; pass.clearColor[2] = 0.910f;  // site paper #F3EFE8
        r3d.render(pass);
    }

    void afterFrame(gfx::Device& d) {
        // Two frames of slack before freeing replaced models' GPU resources.
        static int age = 0;
        if (retired.empty()) { age = 0; return; }
        if (++age < 3) return;
        for (auto& m : retired) {
            r3d.invalidateModelCachesFor(m.get());
            releaseModel(d, *m);
        }
        retired.clear();
        age = 0;
    }
};

App* g_app = nullptr;

}  // namespace

extern "C" {
// Web page: after fetching the files of item N into /view, show it.
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void cv_select(int index) {
    if (!g_app) return;
    if (!g_app->dev) { g_app->requested = index; return; }
    g_app->show(index);
}
}

int aiverseMain(int argc, char** argv) {
    static App app;
    g_app = &app;
    app.root = "/view";
    float yawDeg = -1, pitchDeg = -1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--src") app.root = next();
        else if (a == "--item") app.requested = std::atoi(next().c_str());
        else if (a == "--yaw") yawDeg = float(std::atof(next().c_str()));
        else if (a == "--pitch") pitchDeg = float(std::atof(next().c_str()));
    }
    platform::AppConfig cfg;
    cfg.title = "AIVERSE catalog preview";
    cfg.width = 960;
    cfg.height = 540;
    cfg.orientation = "any";
    platform::AppCallbacks cb;
    cb.init = [&, yawDeg, pitchDeg](gfx::Device& d) {
        app.init(d);
        if (yawDeg >= 0) { app.cam.yaw = yawDeg * kPi / 180; app.autoRotate = false; }
        if (pitchDeg >= 0) app.cam.pitch = pitchDeg * kPi / 180;
    };
    cb.frame = [&](gfx::Device& d, double dt) { app.frame(d, dt); };
    cb.afterFrame = [&](gfx::Device& d) { app.afterFrame(d); };
    cb.event = [&](const platform::Event& e) { app.event(e); };
    cb.shutdown = [&] {
        if (app.dev) {
            if (app.model) { app.r3d.invalidateModelCachesFor(app.model.get()); releaseModel(*app.dev, *app.model); }
            for (auto& m : app.retired) { app.r3d.invalidateModelCachesFor(m.get()); releaseModel(*app.dev, *m); }
        }
        app.r3d.shutdown();
    };
    return platform::run(cfg, cb);
}
