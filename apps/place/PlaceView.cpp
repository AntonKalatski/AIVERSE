// Graphical place client: draws the replicated bodies and characters through Renderer3D (the physics
// sandbox's approach), third-person camera, WASD + mouse look, HUD through ImGui (strings from i18n).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include "apps/place/PlaceClient.h"
#include "engine/i18n/Localization.h"
#include "engine/platform/EngineDir.h"
#include "engine/platform/Platform.h"
#include "engine/render/ImGuiLayer.h"
#include "engine/render/Renderer3D.h"
#include "imgui.h"

using namespace aiverse;
using namespace aiverse::place;
using namespace aiverse::render;

namespace {
float4 colorOf(int64_t rgb, float k = 1) {
    return {float((rgb >> 16) & 255) / 255 * k, float((rgb >> 8) & 255) / 255 * k, float(rgb & 255) / 255 * k, 1};
}
std::string fixed1(double v) { char b[32]; std::snprintf(b, sizeof b, "%.1f", v); return b; }

struct View {
    std::unique_ptr<PlaceClient> client;
    Renderer3D renderer;
    ImGuiLayer gui;
    i18n::Localization loc;
    std::string language;
    gfx::Device* device = nullptr;
    Mesh cube, sphere;
    std::map<std::pair<int, int>, Mesh> capsules;   // (radius, halfHeight) in cm
    std::set<platform::Key> keys;
    float yaw = 0, pitch = 0.35f, distance = 6;
    bool pushQueued = false, haveYaw = false;
    double acc = 0;

    const Mesh& capsule(float r, float hh) {
        auto key = std::make_pair(int(std::lround(r * 100)), int(std::lround(hh * 100)));
        auto it = capsules.find(key);
        if (it != capsules.end()) return it->second;
        MeshBuilder m;
        m.color(rgba(255, 255, 255));
        m.cylinder({0, -hh, 0}, r, hh * 2, 20, false);
        m.sphere({0, -hh, 0}, r, 10, 20);
        m.sphere({0, hh, 0}, r, 10, 20);
        return capsules.emplace(key, m.upload(*device)).first->second;
    }
    void drawShape(int shape, const float* size, float3 p, render::quat q, float4 color) {
        if (shape == int(Shape::Sphere)) renderer.drawMesh(sphere, mat4::trs(p, q, {size[0], size[0], size[0]}), color);
        else if (shape == int(Shape::Capsule)) renderer.drawMesh(capsule(size[0], size[1]), mat4::trs(p, q, {1, 1, 1}), color);
        else renderer.drawMesh(cube, mat4::trs(p, q, {size[0], size[1], size[2]}), color);
    }

    void init(gfx::Device& d) {
        device = &d;
        for (auto lang : {"en", "ru"}) {
            std::string text;
            if (platform::readContentFile(std::string("engine/assets/lang/") + lang + ".json", text)) loc.addTable(lang, Json::parse(text));
        }
        loc.setLanguage(language.empty() ? i18n::negotiate(i18n::systemLanguages(), {"en", "ru"}, "en") : language);
        renderer.init(d);
        renderer.setShadowQuality(ShadowQuality::Medium);
        gui.init(d, "engine/assets/fonts/ui.ttf", 16);
        MeshBuilder c;
        c.color(rgba(255, 255, 255)).box({0, 0, 0}, {1, 1, 1});
        cube = c.upload(d);
        MeshBuilder s;
        s.color(rgba(255, 255, 255)).sphere({0, 0, 0}, 1, 12, 20);
        sphere = s.upload(d);
    }

    void releaseInput() { platform::setPointerLock(false); keys.clear(); pushQueued = false; }
    void event(const platform::Event& e) {
        using E = platform::EventType;
        using K = platform::Key;
        gui.handleEvent(e);
        if (e.type == E::FocusLost) releaseInput();
        if (e.type == E::KeyDown) {
            if (e.key == K::Escape) { releaseInput(); return; }
            if (!platform::pointerLocked()) return;
            keys.insert(e.key);
            if (!e.repeat && e.key == K::E) pushQueued = true;
            if (!e.repeat && e.key == K::F && client->ready()) client->spawnBox();
        }
        if (e.type == E::KeyUp) keys.erase(e.key);
        if (e.type == E::MouseDown && e.button == 0 && !platform::pointerLocked()) platform::setPointerLock(true);
        if (e.type == E::MouseMove && platform::pointerLocked()) {
            yaw -= e.deltaX * 0.0035f;
            pitch = std::clamp(pitch + e.deltaY * 0.003f, -0.2f, 1.3f);
        }
        if (e.type == E::MouseWheel) distance = std::clamp(distance - e.wheelY * 0.5f, 2.5f, 20.0f);
    }

    Input input() {
        using K = platform::Key;
        Input in;
        in.move.moveX = float(keys.count(K::D)) - float(keys.count(K::A));
        in.move.moveZ = float(keys.count(K::W)) - float(keys.count(K::S));
        in.move.yaw = yaw;
        in.move.jump = keys.count(K::Space) > 0;
        in.move.run = keys.count(K::LeftShift) || keys.count(K::RightShift);
        if (pushQueued) { in.actions |= kActionPush; pushQueued = false; }
        return in;
    }

    void frame(gfx::Device& d, double dt) {
        uint64_t now = net::nowMs();
        client->poll(now);
        if (!haveYaw && client->ready()) { yaw = client->predicted().yaw; haveYaw = true; }
        acc += std::min(dt, 0.25);
        const double step = client->dt();
        int budget = 8;
        while (acc >= step && budget--) { client->update(now, input()); acc -= step; }
        if (budget < 0) acc = 0;
        client->advanceSmoothing(float(dt * 1000));

        d.beginFrame(platform::framebufferWidth(), platform::framebufferHeight());
        gui.newFrame(dt);
        auto& r = client->replica();
        const auto& params = client->params();
        b3Vec3 me = client->drawPosition();
        RTSCamera cam;
        cam.target = {me.x, me.y + params.height * 0.85f, me.z};
        cam.distance = distance;
        cam.pitch = pitch;
        cam.yaw = yaw;
        cam.fovY = 1.0f;
        cam.zNear = 0.1f;
        cam.zFar = 400;
        renderer.beginScene(cam, d.framebufferWidth(), d.framebufferHeight());
        if (auto* scene = client->staticScene()) {
            for (const auto& look : client->staticLooks()) {
                if (look.motion != physics::Motion::Static || !look.drawable) continue;
                auto h = scene->find(look.id);
                if (h == physics::kNoBody) continue;
                auto p = scene->position(h);
                auto q = scene->rotation(h);
                drawShape(int(look.shape), look.size.data(), {p.x, p.y, p.z}, {q.v.x, q.v.y, q.v.z, q.s}, colorOf(look.color));
            }
        }
        size_t bodies = 0, players = 0;
        const Schema& sc = client->schema();
        for (auto id : r.objects()) {
            if (r.classOf(id) == sc.body) {
                ++bodies;
                auto p = r.sample(id, Schema::kBodyPos).f;
                auto q = r.sample(id, Schema::kBodyRot).f;
                auto size = r.latest(id, Schema::kBodySize).f;
                bool sleeping = r.latest(id, Schema::kBodySleeping).i != 0;
                drawShape(int(r.latest(id, Schema::kBodyShape).i), size.data(), {p[0], p[1], p[2]}, {q[0], q[1], q[2], q[3]},
                          colorOf(r.latest(id, Schema::kBodyColor).i, sleeping ? 0.85f : 1.0f));
            } else if (r.classOf(id) == sc.character) {
                ++players;
                float3 p;
                render::quat q;
                if (id == client->own()) {
                    p = {me.x, me.y, me.z};
                    q = {0, std::sin(yaw / 2), 0, std::cos(yaw / 2)};
                } else {
                    auto s = r.sample(id, Schema::kCharPos).f;
                    auto y = r.sample(id, Schema::kCharYaw).f;
                    p = {s[0], s[1], s[2]};
                    q = {y[0], y[1], y[2], y[3]};
                }
                float hh = params.height * 0.5f - params.radius;
                float4 color = colorOf(r.latest(id, Schema::kCharColor).i);
                renderer.drawMesh(capsule(params.radius, hh), mat4::trs({p.x, p.y + params.height * 0.5f, p.z}, q, {1, 1, 1}), color);
                // a nose shows where the character looks (forward = -Z of its yaw)
                float sy = 2 * q.y * q.w, cy = 1 - 2 * q.y * q.y;   // sin / cos of yaw from the Y-axis quaternion
                renderer.drawMesh(cube, mat4::trs({p.x - sy * params.radius, p.y + params.height * 0.8f, p.z - cy * params.radius}, q, {0.18f, 0.12f, 0.25f}),
                                  colorOf(0x202830));
            }
        }
        gfx::PassDesc pass;
        pass.clearColor[0] = 0.52f; pass.clearColor[1] = 0.66f; pass.clearColor[2] = 0.80f;
        renderer.render(pass);
        hud(bodies, players);
        gui.render();
        d.endPass();
        d.endFrame();
    }

    void hud(size_t bodies, size_t players) {
        const auto& s = client->stats();
        ImGui::SetNextWindowPos({12, 12});
        ImGui::SetNextWindowBgAlpha(0.6f);
        ImGui::Begin("##place", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
        ImGui::TextUnformatted(loc.tr("place.title").c_str());
        if (!client->connected()) ImGui::TextUnformatted(loc.tr("place.disconnected").c_str());
        else if (!client->ready()) ImGui::TextUnformatted(loc.tr("place.connecting").c_str());
        else {
            ImGui::TextUnformatted(loc.tr("place.world", i18n::Args().num("players", int64_t(players)).num("bodies", int64_t(bodies))).c_str());
            ImGui::TextUnformatted(loc.tr("place.network", i18n::Args().num("rtt", s.rttMs).num("snapshots", std::lround(s.snapshotsPerSecond))
                                                                .str("kb", fixed1(s.bytesInPerSecond / 1000.0)))
                                       .c_str());
            double mean = s.reconciles ? s.sumCorrection / double(s.reconciles) : 0;
            ImGui::TextUnformatted(loc.tr("place.prediction", i18n::Args().num("last", std::lround(s.lastCorrection * 1000))
                                                                   .num("mean", std::lround(mean * 1000)).num("count", int64_t(s.corrections)))
                                       .c_str());
            if (s.spawnDenied) ImGui::TextUnformatted(loc.tr("place.spawnDenied", i18n::Args().num("count", int64_t(s.spawnDenied))).c_str());
        }
        ImGui::PushTextWrapPos(520);
        ImGui::TextUnformatted(loc.tr(platform::pointerLocked() ? "place.controls" : "place.clickToPlay").c_str());
        ImGui::PopTextWrapPos();
        ImGui::End();
    }

    void shutdown() {
        platform::setPointerLock(false);
        auto destroy = [&](Mesh& m) { device->destroyBuffer(m.vb); device->destroyBuffer(m.ib); };
        destroy(cube);
        destroy(sphere);
        for (auto& [k, m] : capsules) destroy(m);
        gui.shutdown();
        renderer.shutdown();
        client.reset();
    }
};
View view;
}

int runPlaceClient(const std::string& address, const std::string& language, const char* argv0) {
    auto conn = net::connectTo(address);
    if (!conn) throw std::runtime_error("invalid server address " + address);
    std::filesystem::current_path(platform::resolveEngineDir(AIVERSE_SOURCE_DIR, argv0));
    view.client = std::make_unique<PlaceClient>(std::move(conn));
    view.language = language;
    platform::AppConfig cfg;
    cfg.title = "AIVERSE Place";
    cfg.width = 1280;
    cfg.height = 800;
    platform::AppCallbacks cb;
    cb.init = [](gfx::Device& d) { view.init(d); };
    cb.frame = [](gfx::Device& d, double dt) { view.frame(d, dt); };
    cb.event = [](const platform::Event& e) { view.event(e); };
    cb.shutdown = [] { view.shutdown(); };
    return platform::run(cfg, cb);
}
