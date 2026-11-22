// SpectraFX showcase host: a static GeometryScene plus fx::ParticleSystem (the CPU particle
// backend) playing JSON effects, advanced to a chosen moment and then frozen, so a frame can be
// captured with the platform hook:
//   AIVERSE_CONTENT=<dir>  AIVERSE_SCREENSHOT=out.ppm  AIVERSE_EXIT_AFTER_FRAMES=N
//   FX_SCENE=scene.json         GeometryScene JSON relative to AIVERSE_CONTENT (optional)
//   FX_PLAY="name@x,y,z,scale,start[,dx,dy,dz];..."  effects from <content>/fx/<name>.json; start in
//                               seconds; optional direction = the effect's Y axis
//   FX_ADVANCE=1.2              simulated seconds before the frame is frozen
//   FX_CAM="tx,ty,tz,distance,yawDeg,pitchDeg,fovDeg"
//   FX_SUN=0.35 FX_SKY="r,g,b"  light level (dusk shows the effect lights); FX_SUNDIR="x,y,z"
//   FX_SUNCOLOR="r,g,b"         sun (moon) colour
//   FX_AMBIENT="sky r,g,b,ground r,g,b"   hemisphere ambient colours
//   FX_LIGHTS="x,y,z,radius,r,g,b,intensity;..."  extra static point lights
//   FX_POST=post.json           PRISMA block as game.json "postfx" (effects, hdr, physicalSky)
//   FX_FOG="cx,cy,cz,hx,hy,hz,density,r,g,b[,feather,noise];..."  local volumetric fog boxes (PRISMA)
//   FX_FOG_AMBIENT=0.5          local fog ambient term; FX_TIME=seconds  frozen sky/fog clock
// Website frames only: no engine behaviour changes.
#include "engine/platform/Platform.h"
#include "engine/render/GeometryScene.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/Prisma.h"
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

struct Shot { std::string name; float3 pos; float scale = 1, start = 0; float3 dir{0, 1, 0}; bool played = false; };

struct Host {
    Renderer3D r3d;
    GeometryScene scene;
    fx::ParticleSystem fx;
    Prisma prisma;
    bool post = false;
    std::vector<float> extraLights;
    Camera cam;
    std::vector<Shot> shots;
    float advance = float(std::atof(env("FX_ADVANCE", "1.0")));
    bool advanced = false;
    int frame = 0;

    void init(gfx::Device& d) {
        r3d.init(d);
        r3d.setShadowQuality(ShadowQuality::High);
        r3d.setMaterialQuality(MaterialQuality::High);
        if (const char* file = std::getenv("FX_POST")) {
            std::string text;
            if (platform::readContentFile(file, text) && prisma.init(d)) {
                prisma.configure(Json::parse(text), Json());
                prisma.setQuality(PrismaQuality::High);
                post = true;
                const double frozen = std::atof(env("FX_TIME", "12"));
                prisma.setPhysicalSkyTime(frozen);
                prisma.setLocalFogTime(frozen);
                if (const char* fogList = std::getenv("FX_FOG")) {
                    LocalFogSettings fs;
                    fs.enabled = true;
                    fs.maxDistance = 200;
                    fs.ambient = float(std::atof(env("FX_FOG_AMBIENT", "0.5")));
                    prisma.setLocalFog(fs);
                    std::stringstream fl(fogList);
                    std::string item;
                    uint32_t id = 1;
                    while (std::getline(fl, item, ';')) {
                        auto f = floats(item);
                        if (f.size() < 10) continue;
                        f.resize(12, 0.f);
                        FogVolume v;
                        v.id = id++;
                        v.region.shape = FogShape::Obb;
                        v.region.center = {f[0], f[1], f[2]};
                        v.region.halfExtents = {f[3], f[4], f[5]};
                        v.region.feather = f[10] > 0 ? f[10] : 1.f;
                        v.density = f[6];
                        v.color = {f[7], f[8], f[9]};
                        if (f[11] > 0) {
                            v.structure.amount = f[11];
                            v.structure.scale = {4, 2, 4};
                            v.structure.wind = {0.3f, 0, 0.1f};
                        }
                        if (!prisma.localFog().upsert(v)) std::fprintf(stderr, "[fx-showcase] fog volume rejected: %s\n", item.c_str());
                    }
                }
            } else std::fprintf(stderr, "[fx-showcase] post-processing unavailable: %s\n", file);
        }
        extraLights = floats(env("FX_LIGHTS", ""));
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
            const bool hasDir = v.size() >= 8;
            v.resize(8, 0.f);
            shots.push_back({item.substr(0, at), {v[0], v[1], v[2]}, v[3] > 0 ? v[3] : 1.f, v[4],
                             hasDir ? float3{v[5], v[6], v[7]} : float3{0, 1, 0}});
        }
        auto c = floats(env("FX_CAM", "0,1.5,0,12,30,18,50"));
        c.resize(7, 0.f);
        constexpr float deg = 0.0174532925f;
        cam.target = {c[0], c[1], c[2]};
        cam.distance = c[3]; cam.minDistance = 0.1f; cam.maxDistance = 1e4f;
        cam.yaw = c[4] * deg; cam.pitch = c[5] * deg; cam.fovY = c[6] * deg;
        cam.zNear = 0.05f; cam.zFar = 500.f;
        auto sd = floats(env("FX_SUNDIR", "0.6,0.35,-0.5"));
        sd.resize(3, 0.5f);
        r3d.lighting.sunDir = {sd[0], sd[1], sd[2]};
        if (const char* c = std::getenv("FX_SUNCOLOR")) {
            auto sc = floats(c);
            sc.resize(3, 1.f);
            r3d.lighting.sunColor = {sc[0], sc[1], sc[2]};
        }
        if (const char* a = std::getenv("FX_AMBIENT")) {
            auto am = floats(a);
            am.resize(6, 0.1f);
            r3d.lighting.skyColor = {am[0], am[1], am[2]};
            r3d.lighting.groundColor = {am[3], am[4], am[5]};
        }
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
                if (!fx.play(s.name, s.pos, s.dir, s.scale).valid())
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
        for (size_t i = 0; i + 8 <= extraLights.size(); i += 8) {
            const float* e = &extraLights[i];
            r3d.lights.add({e[0], e[1], e[2]}, e[3], {e[4], e[5], e[6]}, e[7]);
        }
        scene.draw(r3d);
        gfx::PassDesc pass;
        pass.clearColor[0] = clear.x; pass.clearColor[1] = clear.y; pass.clearColor[2] = clear.z;
        r3d.render(pass, post ? &prisma : nullptr);
        fx.render({{clear.x, clear.y, clear.z}, 0.f});
        if (post) r3d.resolvePrisma();
        d.endPass();
        if (frame == 2 && std::getenv("FX_DUMP")) {
            std::vector<fx::FxParticleInfo> ps;
            fx.snapshot(ps);
            for (size_t i = 0; i < ps.size() && i < 2000; i += std::max<size_t>(1, ps.size() / 12))
                std::fprintf(stderr, "[fx-showcase] p%zu e%d/%d pos %.2f %.2f %.2f vel %.2f %.2f %.2f\n", i, ps[i].effect, ps[i].emitter,
                             ps[i].pos.x, ps[i].pos.y, ps[i].pos.z, ps[i].vel.x, ps[i].vel.y, ps[i].vel.z);
        }
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
