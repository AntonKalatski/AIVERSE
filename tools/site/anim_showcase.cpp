// MotionWeave showcase host: two armies of a skinned glTF character, each actor driven by its own
// AnimationGraphInstance (state machine, walk/run blend, additive breathing, upper-body attack
// masked to the spine, death), a sword attached to the right arm by the evaluated pose, over a
// static GeometryScene. Advanced to a chosen moment in fixed steps, then frozen for a capture:
//   AIVERSE_CONTENT=<dir>  AIVERSE_SCREENSHOT=out.ppm  AIVERSE_EXIT_AFTER_FRAMES=N
//   ANIM_MODEL=golem.glb  ANIM_PROP=sword.glb   (relative to AIVERSE_CONTENT)
//   ANIM_SCENE=scene.json  ANIM_ADVANCE=3  ANIM_SEED=7  ANIM_COLS=9  ANIM_ROWS=5  ANIM_SPACING=1.9
//   ANIM_CAM="tx,ty,tz,distance,yawDeg,pitchDeg,fovDeg"  ANIM_POST=post.json
//   ANIM_SUN / ANIM_SUNCOLOR / ANIM_SUNDIR / ANIM_AMBIENT / ANIM_SKY  as in fx_showcase
// Website frames only: no engine behaviour changes.
#include "engine/platform/Platform.h"
#include "engine/render/GeometryScene.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/Prisma.h"
#include "engine/render/Animation.h"
#include "engine/render/AnimationGraph.h"
#include "engine/data/Json.h"

#include <cmath>
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

// One graph for every actor: march (walk/run blend + breathing), fight (walk or idle with the
// attack on the upper body), dead. The host only sets parameters.
const char* kGraph = R"({
  "parameters": {"mode": 0, "blend": 0.3, "breath": 0.4, "attack": 1, "moving": 1},
  "nodes": [
    {"id": "idle", "kind": "clip", "clip": "Idle"},
    {"id": "walk", "kind": "clip", "clip": "Walk"},
    {"id": "run", "kind": "clip", "clip": "Run"},
    {"id": "locomotion", "kind": "blend", "a": "walk", "b": "run", "parameter": "blend"},
    {"id": "breath", "kind": "clip", "clip": "Breathe"},
    {"id": "alive", "kind": "additive", "a": "locomotion", "b": "breath", "mask": "spine", "parameter": "breath"},
    {"id": "swing", "kind": "clip", "clip": "Attack"},
    {"id": "stance", "kind": "blend", "a": "idle", "b": "walk", "parameter": "moving"},
    {"id": "fight", "kind": "blend", "a": "stance", "b": "swing", "mask": "spine", "parameter": "attack"},
    {"id": "death", "kind": "clip", "clip": "Death", "loop": false}
  ],
  "initial": "march",
  "states": [
    {"id": "march", "output": "alive", "transitions": [
      {"to": "fight", "fade": 0.25, "when": [{"parameter": "mode", "op": "eq", "value": 1}]},
      {"to": "dead", "fade": 0.15, "when": [{"parameter": "mode", "op": "eq", "value": 2}]}]},
    {"id": "fight", "output": "fight", "transitions": [
      {"to": "dead", "fade": 0.15, "when": [{"parameter": "mode", "op": "eq", "value": 2}]}]},
    {"id": "dead", "output": "death"}
  ]
})";

uint32_t hashU(uint32_t x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }
float rnd(uint32_t& s) { s = hashU(s + 0x9e3779b9u); return float(s & 0xffffff) / float(0x1000000); }

struct Actor {
    AnimationGraphInstance graph;
    Pose pose;
    std::vector<mat4> nodeWorld;
    mat4 world;
    float speed = 1, delay = 0;
    float3 team;
    bool hasProp = true;
};

struct Host {
    Renderer3D r3d;
    GeometryScene scene;
    Prisma prisma;
    bool post = false;
    std::unique_ptr<Model> model, prop;
    std::vector<Actor> actors;
    int arm = -1;
    Camera cam;
    float3 clear;
    float advance = float(std::atof(env("ANIM_ADVANCE", "3")));
    bool advanced = false;
    int frame = 0;

    void init(gfx::Device& d) {
        r3d.init(d);
        r3d.setShadowQuality(ShadowQuality::High);
        r3d.setMaterialQuality(MaterialQuality::High);
        const auto reader = [](const std::string& p, std::string& out) { return platform::readContentFile(p, out); };
        if (const char* file = std::getenv("ANIM_POST")) {
            std::string text;
            if (platform::readContentFile(file, text) && prisma.init(d)) {
                prisma.configure(Json::parse(text), Json());
                prisma.setQuality(PrismaQuality::High);
                prisma.setPhysicalSkyTime(std::atof(env("ANIM_TIME", "12")));
                post = true;
            }
        }
        if (const char* file = std::getenv("ANIM_SCENE")) {
            std::string text;
            if (platform::readContentFile(file, text)) scene.load(d, Json::parse(text), reader);
        }
        model = loadModel(env("ANIM_MODEL", "golem.glb"), reader);
        if (!model || !uploadModel(d, *model)) { std::fprintf(stderr, "[anim-showcase] model failed\n"); return; }
        // The team colour goes on the cloth, as a game's rules.json would set it.
        for (auto& m : model->materials)
            if (m.name == "TeamCloth") { m.team = TeamMode::Tint; m.teamStrength = 1; }
        model->markMaterialChanged();
        prop = loadModel(env("ANIM_PROP", "sword.glb"), reader);
        if (prop && !uploadModel(d, *prop)) prop.reset();
        arm = model->findNode(env("ANIM_SOCKET", "arm.R"));
        auto graph = AnimationGraph::compile(*model, Json::parse(kGraph));
        if (!graph) { std::fprintf(stderr, "[anim-showcase] graph failed\n"); return; }

        // Two armies facing each other along X; the front rows fight, a few have fallen.
        const int cols = std::atoi(env("ANIM_COLS", "9")), rows = std::atoi(env("ANIM_ROWS", "5"));
        const float sp = float(std::atof(env("ANIM_SPACING", "1.9")));
        uint32_t seed = uint32_t(std::atoi(env("ANIM_SEED", "7")));
        const float3 teams[2] = {{0.18f, 0.45f, 1.0f}, {1.0f, 0.16f, 0.06f}};
        for (int side = 0; side < 2; ++side)
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c) {
                    Actor a;
                    a.graph.reset(graph);
                    const float dir = side == 0 ? 1.f : -1.f;
                    const float x = -dir * (1.1f + r * sp + (rnd(seed) - 0.5f) * 0.6f);
                    const float z = (c - (cols - 1) * 0.5f) * sp + (rnd(seed) - 0.5f) * 0.7f;
                    const float yaw = dir * 1.5707963f + (rnd(seed) - 0.5f) * (r == 0 ? 0.9f : 0.35f);
                    a.world = mat4::translate({x, 0, z}) * mat4::rotateY(yaw);
                    auto& p = a.graph.parameters();
                    const float roll = rnd(seed);
                    float mode = 0;
                    if (r == 0) mode = roll < 0.18f ? 2.f : 1.f;          // front line: fighting or fallen
                    else if (r == 1) mode = roll < 0.45f ? 1.f : (roll < 0.52f ? 2.f : 0.f);
                    p.set("mode", mode);
                    p.set("blend", r >= 3 ? 0.55f + rnd(seed) * 0.45f : rnd(seed) * 0.35f); // rear ranks run up
                    p.set("breath", 0.3f + rnd(seed) * 0.4f);
                    p.set("attack", 0.75f + rnd(seed) * 0.25f);
                    p.set("moving", rnd(seed) < 0.5f ? 1.f : 0.f);
                    a.speed = 0.85f + rnd(seed) * 0.3f;
                    a.delay = mode == 2 ? rnd(seed) * 1.2f : rnd(seed) * 2.f;   // desync the clocks
                    a.team = teams[side];
                    a.hasProp = rnd(seed) < 0.85f;
                    actors.push_back(std::move(a));
                }
        auto c = floats(env("ANIM_CAM", "0,1.2,0,22,20,20,45"));
        c.resize(7, 0.f);
        constexpr float deg = 0.0174532925f;
        cam.target = {c[0], c[1], c[2]};
        cam.distance = c[3]; cam.minDistance = 0.1f; cam.maxDistance = 1e4f;
        cam.yaw = c[4] * deg; cam.pitch = c[5] * deg; cam.fovY = c[6] * deg;
        cam.zNear = 0.1f; cam.zFar = 600.f;
        auto sd = floats(env("ANIM_SUNDIR", "0.3,0.25,-1"));
        sd.resize(3, 0.5f);
        r3d.lighting.sunDir = {sd[0], sd[1], sd[2]};
        auto sc = floats(env("ANIM_SUNCOLOR", "1,0.7,0.45"));
        sc.resize(3, 1.f);
        r3d.lighting.sunColor = {sc[0], sc[1], sc[2]};
        auto am = floats(env("ANIM_AMBIENT", "0.16,0.15,0.18,0.06,0.05,0.04"));
        am.resize(6, 0.1f);
        r3d.lighting.skyColor = {am[0], am[1], am[2]};
        r3d.lighting.groundColor = {am[3], am[4], am[5]};
        r3d.lighting.sunIntensity = float(std::atof(env("ANIM_SUN", "1.1")));
        r3d.lighting.fogDensity = 0.f;
        auto sky = floats(env("ANIM_SKY", "0.3,0.25,0.2"));
        sky.resize(3, 0.2f);
        clear = {sky[0], sky[1], sky[2]};
    }

    void evaluate(Actor& a) {
        a.graph.evaluate(a.pose);
        worldMatrices(*model, a.pose, a.nodeWorld);
    }

    void tick(gfx::Device& d) {
        const int w = d.framebufferWidth(), h = d.framebufferHeight();
        d.beginFrame(platform::framebufferWidth(), platform::framebufferHeight());
        r3d.beginScene(cam, w, h);
        if (!advanced && model) { // fixed 1/60 steps per actor, then frozen
            advanced = true;
            const float dt = 1.f / 60.f;
            for (auto& a : actors) {
                const float total = advance + a.delay;
                for (float t = 0; t + dt <= total; t += dt) a.graph.update(dt * a.speed);
                evaluate(a);
            }
            int states[3] = {};
            for (auto& a : actors) states[a.graph.state() == "march" ? 0 : a.graph.state() == "fight" ? 1 : 2]++;
            std::fprintf(stderr, "[anim-showcase] actors=%zu march=%d fight=%d dead=%d\n", actors.size(), states[0], states[1], states[2]);
        }
        scene.draw(r3d);
        if (model)
            for (auto& a : actors) {
                ModelDraw md;
                md.model = model.get();
                md.world = a.world;
                md.nodeWorld = &a.nodeWorld;
                md.team = a.team;
                r3d.drawModel(md);
                if (prop && a.hasProp && arm >= 0 && size_t(arm) < a.nodeWorld.size() && a.graph.state() != "dead") {
                    ModelDraw pd;
                    pd.model = prop.get();
                    // Socket: the hand is at the end of the arm bone; the blade points forward.
                    pd.world = a.world * a.nodeWorld[size_t(arm)] * mat4::translate({0, -0.72f, 0.08f}) * mat4::rotateX(-1.5707963f) * mat4::scale({1.35f, 1.35f, 1.35f});
                    r3d.drawModel(pd);
                }
            }
        gfx::PassDesc pass;
        pass.clearColor[0] = clear.x; pass.clearColor[1] = clear.y; pass.clearColor[2] = clear.z;
        r3d.render(pass, post ? &prisma : nullptr);
        if (post) r3d.resolvePrisma();
        d.endPass();
        ++frame;
    }
};
std::unique_ptr<Host> g;
} // namespace

int aiverseMain(int, char**) {
    platform::AppConfig cfg;
    cfg.title = "AIVERSE MotionWeave showcase";
    cfg.width = std::atoi(env("ANIM_W", "1920"));
    cfg.height = std::atoi(env("ANIM_H", "1080"));
    cfg.highDpi = false;
    platform::AppCallbacks cb;
    cb.init = [](gfx::Device& d) { g = std::make_unique<Host>(); g->init(d); };
    cb.frame = [](gfx::Device& d, double) { g->tick(d); };
    cb.event = [](const platform::Event&) {};
    cb.shutdown = [] { g.reset(); };
    return platform::run(cfg, cb);
}
