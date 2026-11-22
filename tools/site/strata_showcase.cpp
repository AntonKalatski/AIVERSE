// STRATA showcase host: loads a static scene through GeometryScene with the
// portable STRATA route and (optionally) the presentation-only debug view, so a
// frame can be captured with the platform hook:
//   AIVERSE_CONTENT=<dir with the model>  AIVERSE_SCREENSHOT=out.ppm  AIVERSE_EXIT_AFTER_FRAMES=N
//   STRATA_MODEL=a.gltf,b.gltf  comma-separated models relative to AIVERSE_CONTENT (one instance each)
//   STRATA_SCENE=scene.json   optional full GeometryScene JSON instead of STRATA_MODEL
//   STRATA_VIEW=normal|cluster|lod|error|material   (default cluster)
//   STRATA_FILL=1 STRATA_WIRE=1 STRATA_BOUNDS=0     debug overlay parts
//   STRATA_CAM="tx,ty,tz,distance,yawDeg,pitchDeg,fovDeg"
// Website frames only: no engine behaviour changes, debug view defaults OFF.
#include "engine/platform/Platform.h"
#include "engine/render/GeometryScene.h"
#include "engine/render/Renderer3D.h"
#include "engine/data/Json.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>

using namespace aiverse;
using namespace aiverse::render;

namespace {
const char* env(const char* k, const char* d) { const char* v = std::getenv(k); return v && *v ? v : d; }
bool flag(const char* k, bool d) { const char* v = std::getenv(k); return v && *v ? v[0] == '1' : d; }

struct Host {
    Renderer3D r3d;
    GeometryScene scene;
    Camera cam;
    std::string view = env("STRATA_VIEW", "cluster");
    int frame = 0;

    void init(gfx::Device& d) {
        r3d.init(d);
        r3d.setShadowQuality(ShadowQuality::High);
        const bool debug = view != "normal";
        scene.setStrataStagingEnabled(true);
        scene.setStrataPortableRenderingEnabled(true);
        scene.setStrataDebugGeometryCaptureEnabled(debug && (flag("STRATA_FILL", true) || flag("STRATA_WIRE", false)));
        std::string instances;
        {
            std::stringstream models(env("STRATA_MODEL", "sponza.gltf"));
            std::string m; int id = 0;
            while (std::getline(models, m, ',')) {
                if (m.empty()) continue;
                if (!instances.empty()) instances += ",";
                instances += R"({"id":"m)" + std::to_string(id++) + R"(","model":")" + m +
                             R"(","position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]})";
            }
        }
        Json json = Json::parse(R"({"version":1,"instances":[)" + instances + "]}");
        if (const char* file = std::getenv("STRATA_SCENE")) { // full GeometryScene JSON (positions, scales)
            std::string text;
            if (platform::readContentFile(file, text)) json = Json::parse(text);
            else std::fprintf(stderr, "[strata-showcase] scene not found: %s\n", file);
        }
        scene.load(d, json, [](const std::string& p, std::string& out) { return platform::readContentFile(p, out); });
        {
            const auto& s = scene.stats();
            std::fprintf(stderr, "[strata-showcase] staged=%u skipped=%u failed=%u prims=%u skippedPrims=%u nodes=%llu meshlets=%llu portable=%u declined=%u\n",
                         s.strataSources, s.strataSkippedSources, s.strataFailures, s.strataPrimitives, s.strataSkippedPrimitives,
                         (unsigned long long)s.strataNodes, (unsigned long long)s.strataMeshlets, s.strataPortableSources, s.strataPortableDeclined);
            for (const auto& w : scene.warnings()) std::fprintf(stderr, "[strata-showcase] warning: %s\n", w.c_str());
        }
        if (debug) {
            strata::DebugViewOptions o;
            o.enabled = true;
            o.clusterBounds = flag("STRATA_BOUNDS", false);
            o.clusterFill = flag("STRATA_FILL", true);
            o.clusterWireframe = flag("STRATA_WIRE", false);
            o.instanceBounds = false;
            o.includeLegacy = false;
            o.fillAlpha = float(std::atof(env("STRATA_ALPHA", "0.85")));
            o.maxTriangles = uint32_t(std::atol(env("STRATA_MAXTRI", "2097152")));
            o.maxLines = 4u * 1024u * 1024u;
            o.color = view == "lod" ? strata::DebugColorMode::LodDepth
                    : view == "error" ? strata::DebugColorMode::Error
                    : view == "material" ? strata::DebugColorMode::Material
                    : strata::DebugColorMode::Cluster;
            if (!scene.setStrataDebugView(o)) std::fprintf(stderr, "[strata-showcase] invalid debug options\n");
        }
        float v[7] = {0, 3, 0, 14, 90, 8, 55};
        std::stringstream ss(env("STRATA_CAM", "0,3,0,14,90,8,55"));
        std::string part; int n = 0;
        while (n < 7 && std::getline(ss, part, ',')) v[n++] = float(std::atof(part.c_str()));
        constexpr float deg = 0.0174532925f;
        cam.target = {v[0], v[1], v[2]};
        cam.distance = v[3]; cam.minDistance = 0.1f; cam.maxDistance = 1e4f;
        cam.yaw = v[4] * deg; cam.pitch = v[5] * deg; cam.fovY = v[6] * deg;
        cam.zNear = 0.05f; cam.zFar = 500.f;
        r3d.lighting.sunDir = {0.75f, 0.42f, -0.35f};
        r3d.lighting.sunIntensity = 1.25f;
        r3d.lighting.fogDensity = 0.f;
    }

    void tick(gfx::Device& d) {
        d.beginFrame(platform::framebufferWidth(), platform::framebufferHeight());
        r3d.beginScene(cam, d.framebufferWidth(), d.framebufferHeight());
        scene.draw(r3d);
        gfx::PassDesc pass;
        pass.clearColor[0] = 0.55f; pass.clearColor[1] = 0.62f; pass.clearColor[2] = 0.72f;
        r3d.render(pass);
        if (++frame % 30 == 0) {
            const auto& s = scene.stats();
            const auto& ds = scene.strataDebugStats();
            std::fprintf(stderr,
                         "[strata-showcase] frame %d view=%s instances=%u strataSources=%u portableSources=%u "
                         "portableDraws=%llu portableClusters=%llu fallbacks=%llu debugClusters=%llu "
                         "debugTriangles=%llu debugLines=%llu truncated=%llu wireUnavailable=%llu\n",
                         frame, view.c_str(), s.instances, s.strataSources, s.strataPortableSources,
                         (unsigned long long)s.strataPortableDraws, (unsigned long long)s.strataPortableClusters,
                         (unsigned long long)s.strataPortableFallbacks, (unsigned long long)ds.clusters,
                         (unsigned long long)ds.triangles, (unsigned long long)ds.lines,
                         (unsigned long long)ds.truncated, (unsigned long long)ds.wireframeUnavailable);
        }
    }
};
std::unique_ptr<Host> g;
} // namespace

int aiverseMain(int, char**) {
    platform::AppConfig cfg;
    cfg.title = "AIVERSE STRATA showcase";
    cfg.width = std::atoi(env("STRATA_W", "1920"));
    cfg.height = std::atoi(env("STRATA_H", "1080"));
    cfg.highDpi = false;
    platform::AppCallbacks cb;
    cb.init = [](gfx::Device& d) { g = std::make_unique<Host>(); g->init(d); };
    cb.frame = [](gfx::Device& d, double) { g->tick(d); };
    cb.event = [](const platform::Event&) {};
    cb.shutdown = [] { g.reset(); };
    return platform::run(cfg, cb);
}
