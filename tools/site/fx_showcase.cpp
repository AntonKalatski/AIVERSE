// SpectraFX showcase host: a static GeometryScene plus fx::ParticleSystem (the CPU particle
// backend) playing JSON effects, advanced to a chosen moment and then frozen, so a frame can be
// captured with the platform hook:
//   AIVERSE_CONTENT=<dir>  AIVERSE_SCREENSHOT=out.ppm  AIVERSE_EXIT_AFTER_FRAMES=N
//   FX_SCENE=scene.json         GeometryScene JSON relative to AIVERSE_CONTENT (optional)
//   FX_PLAY="name@x,y,z,scale,start;..."  effects read from <content>/fx/<name>.json; start in seconds
//   FX_ADVANCE=1.2              simulated seconds before the frame is frozen
//   FX_CAM="tx,ty,tz,distance,yawDeg,pitchDeg,fovDeg"
//   FX_SUN=0.35 FX_SKY="r,g,b"  light level (dusk shows the effect lights)
// Website frames only: no engine behaviour changes.
#include "engine/platform/Platform.h"
#include "engine/render/GeometryScene.h"
#include "engine/render/Renderer3D.h"
#include "engine/fx/ParticleSystem.h"
#include "engine/data/Json.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::render;

namespace {
const char* env(const char* k, const char* d) { const char* v = std::getenv(k); return v && *v ? v : d; }

std::vector<float> floats(const std::string& s) {
    std::vector<float> out; std::stringstream ss(s); std::string p;
    while (std::getline(ss, p, ',')) out.push_back(float(std::atof(p.c_str())));
    return out;
}

struct Shot { std::string name; float3 pos; float scale = 1, start = 0; bool played = false; };

struct Host {
    Renderer3D r3d;
    GeometryScene scene;
    fx::ParticleSystem fx;
    Camera cam;
    std::vector<Shot> shots;
    float advance = float(std::atof(env("FX_ADVANCE", "1.0")));
    bool advanced = false;
    int frame = 0;

    void init(gfx::Device& d) {
        r3d.init(d);
        r3d.setShadowQuality(ShadowQuality::High);
        if (const char* file = std::getenv("FX_SCENE")) {
            std::string text;
            if (platform::readContentFile(file, text))
                scene.load(d, Json::parse(text), [](const std::string& p, std::string& out) { return platform::readContentFile(p, out); });
            else std::fprintf(stderr, "[fx-showcase] scene not found: %s\n", file);
        }
        fx.setQuality(fx::FxQuality::High);
        fx.setFileReader([](const std::string& p, std::string& out) { return platform::readContentFile(p, out); });
        fx.setGroundHeight([](float, float) { return 0.f; });
        if (!fx.init(&d)) std::fprintf(stderr, "[fx-showcase] fx init failed: %s\n", fx.lastError().c_str());
        std::stringstream list(env("FX_PLAY", "explosion_big@0,0,0,1,0"));
        std::string item;
        while (std::getline(list, item, ';')) {
            const auto at = item.find('@');
            if (at == std::string::npos) continue;
            auto v = floats(item.substr(at + 1));
            v.resize(5, 0.f);
            shots.push_back({item.substr(0, at), {v[0], v[1], v[2]}, v[3] > 0 ? v[3] : 1.f, v[4]});
        }
        auto c = floats(env("FX_CAM", "0,1.5,0,12,30,18,50"));
        c.resize(7, 0.f);
        constexpr float deg = 0.0174532925f;
        cam.target = {c[0], c[1], c[2]};
        cam.distance = c[3]; cam.minDistance = 0.1f; cam.maxDistance = 1e4f;
        cam.yaw = c[4] * deg; cam.pitch = c[5] * deg; cam.fovY = c[6] * deg;
        cam.zNear = 0.05f; cam.zFar = 500.f;
        r3d.lighting.sunDir = {0.6f, 0.35f, -0.5f};
        r3d.lighting.sunIntensity = float(std::atof(env("FX_SUN", "0.45")));
        auto sky = floats(env("FX_SKY", "0.16,0.18,0.24"));
        sky.resize(3, 0.2f);
        r3d.lighting.fogDensity = 0.f;
        clear = {sky[0], sky[1], sky[2]};
    }
    float3 clear;

    void step(float t, float dt) {
        for (auto& s : shots)
            if (!s.played && t >= s.start) {
                s.played = true;
                if (!fx.play(s.name, s.pos, {0, 1, 0}, s.scale).valid())
                    std::fprintf(stderr, "[fx-showcase] could not play %s: %s\n", s.name.c_str(), fx.lastError().c_str());
            }
        fx.update(dt);
    }

    void tick(gfx::Device& d) {
        const int w = d.framebufferWidth(), h = d.framebufferHeight();
        d.beginFrame(platform::framebufferWidth(), platform::framebufferHeight());
        r3d.beginScene(cam, w, h);
        fx.setView(cam, r3d.viewProj(), w, h);
        if (!advanced) { // deterministic: fixed 1/60 steps to the chosen moment, then frozen
            advanced = true;
            const float dt = 1.f / 60.f;
            float t = 0;
            for (; t + dt <= advance; t += dt) step(t, dt);
            step(t, 0.f);
        } else fx.update(0.f);
        std::vector<fx::FxPointLight> lights;
        fx.collectLights(lights);
        for (auto& l : lights) r3d.lights.add(l.pos, l.radius, l.color, l.intensity);
        scene.draw(r3d);
        gfx::PassDesc pass;
        pass.clearColor[0] = clear.x; pass.clearColor[1] = clear.y; pass.clearColor[2] = clear.z;
        r3d.render(pass);
        fx.render({{clear.x, clear.y, clear.z}, 0.f});
        d.endPass();
        if (++frame % 30 == 0) {
            const auto& s = fx.stats();
            std::fprintf(stderr, "[fx-showcase] frame %d particles=%d draws=%d lights=%zu fill=%.3f\n",
                         frame, s.particles, s.drawCalls, lights.size(), s.fill);
        }
    }
};
std::unique_ptr<Host> g;
} // namespace

int aiverseMain(int, char**) {
    platform::AppConfig cfg;
    cfg.title = "AIVERSE SpectraFX showcase";
    cfg.width = std::atoi(env("FX_W", "1920"));
    cfg.height = std::atoi(env("FX_H", "1080"));
    cfg.highDpi = false;
    platform::AppCallbacks cb;
    cb.init = [](gfx::Device& d) { g = std::make_unique<Host>(); g->init(d); };
    cb.frame = [](gfx::Device& d, double) { g->tick(d); };
    cb.event = [](const platform::Event&) {};
    cb.shutdown = [] { g.reset(); };
    return platform::run(cfg, cb);
}
