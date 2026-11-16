// Graphical place client: draws the replicated bodies and characters through Renderer3D (the physics
// sandbox's approach), third-person camera, WASD + mouse look, HUD through ImGui (strings from i18n).
// The network runs on its own thread at the fixed tick (PlaceLink: poll every 2 ms, input + prediction at the scene
// tick rate), so a slow frame neither loses snapshots (the latest channel keeps only the newest packet between polls)
// nor delays input; the render thread reads the client under the same mutex.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include "apps/place/PlaceLink.h"
#include "apps/place/PlaceSim.h"
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
    std::unique_ptr<PlaceLink> link;
    std::mutex mutex;               // guards link (and everything reached through it), shared, latches
    std::thread net;
    std::atomic<bool> quit{false};
    Input shared;                   // the render thread's current controls, sampled at each tick
    bool pushLatch = false, spawnLatch = false, suspendOnFocus = false;
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
    float groundTurn = 0;           // yaw a rotating platform turned the character by, not yet in `yaw` (mutex)
    std::unique_ptr<Bot> autopilot; // AIVERSE_PLACE_AUTOPILOT=<seed>: a bot plays (screenshots, soak runs)
    uint64_t resumedUntil = 0;

    void netLoop() {
        double next = double(net::nowMs());
        while (!quit) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                uint64_t now = net::nowMs();
                PlaceClient* any = link->view();
                double step = 1000.0 * (any ? any->dt() : 1.0f / 60);
                if (double(now) >= next) {
                    Input in = shared;
                    in.move.yaw += groundTurn;
                    if (autopilot && link->client()) in = autopilot->think(*link->client(), true);
                    if (pushLatch) { in.actions |= kActionPush; pushLatch = false; }
                    link->update(now, in);
                    if (PlaceClient* c = link->client()) {
                        groundTurn += c->takeGroundTurn();   // rotating platforms turn the view
                        if (spawnLatch) c->spawnBox();
                        c->advanceSmoothing(float(step));
                    }
                    spawnLatch = false;
                    next += step;
                    if (double(now) - next > 250) next = double(now);   // stalled (debugger, sleep): no burst of ticks
                } else {
                    link->poll(now);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

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
        if (e.type == E::FocusLost) {
            releaseInput();
            if (suspendOnFocus) { std::lock_guard<std::mutex> lock(mutex); link->suspend(net::nowMs()); }
        }
        if (e.type == E::FocusGained && suspendOnFocus) { std::lock_guard<std::mutex> lock(mutex); link->resume(net::nowMs()); }
        if (e.type == E::KeyDown) {
            if (e.key == K::Escape) { releaseInput(); return; }
            if (!platform::pointerLocked()) return;
            keys.insert(e.key);
            if (!e.repeat && e.key == K::E) pushQueued = true;
            if (!e.repeat && e.key == K::F) { std::lock_guard<std::mutex> lock(mutex); spawnLatch = true; }
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
        d.beginFrame(platform::framebufferWidth(), platform::framebufferHeight());
        gui.newFrame(dt);
        std::unique_lock<std::mutex> lock(mutex);
        {
            yaw += groundTurn;
            groundTurn = 0;
            if (autopilot && link->view() && link->view()->ready()) yaw = link->view()->predicted().yaw;   // camera behind the bot
            Input in = input();
            if (in.actions & kActionPush) pushLatch = true;
            in.actions = 0;
            shared = in;
        }
        PlaceClient* client = link->view();
        if (client && !haveYaw && client->ready()) { yaw = client->predicted().yaw; haveYaw = true; }
        gfx::PassDesc pass;
        pass.clearColor[0] = 0.52f; pass.clearColor[1] = 0.66f; pass.clearColor[2] = 0.80f;
        if (!client) {   // nothing received yet: sky and the HUD
            hud(nullptr, 0, 0);
            lock.unlock();
            RTSCamera cam;
            renderer.beginScene(cam, d.framebufferWidth(), d.framebufferHeight());
            renderer.render(pass);
            gui.render();
            d.endPass();
            d.endFrame();
            return;
        }
        auto& r = client->replica();
        const auto& params = client->params();
        b3Vec3 me = client->drawPosition(), view = client->cameraPosition();
        RTSCamera cam;
        cam.target = {view.x, view.y + params.height * 0.85f, view.z};
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
        hud(client, bodies, players);
        lock.unlock();
        renderer.render(pass);
        gui.render();
        d.endPass();
        d.endFrame();
    }

    void hud(PlaceClient* client, size_t bodies, size_t players) {
        ImGui::SetNextWindowPos({12, 12});
        ImGui::SetNextWindowBgAlpha(0.6f);
        ImGui::Begin("##place", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
        ImGui::TextUnformatted(loc.tr("place.title").c_str());
        uint64_t now = net::nowMs();
        LinkStatus st = link->status();
        if (link->resumedCharacter() && st == LinkStatus::Playing && link->reconnects() && !resumedUntil) resumedUntil = now + 6000;
        if (st != LinkStatus::Playing) resumedUntil = 0;
        if (st != LinkStatus::Playing) {
            i18n::Args args;
            args.num("attempt", int64_t(link->attempts()));
            args.str("seconds", fixed1(double(link->retryAt() > now ? link->retryAt() - now : 0) / 1000.0));
            args.str("reason", loc.tr(std::string("place.reject.") + std::to_string(int(link->rejectReason()))));
            ImGui::PushStyleColor(ImGuiCol_Text, st == LinkStatus::Unstable ? ImVec4(1, 0.85f, 0.3f, 1) : ImVec4(1, 0.5f, 0.4f, 1));
            const bool waiting = st == LinkStatus::Reconnecting && !link->dialing();
            ImGui::TextUnformatted(loc.tr(waiting ? "place.link.waiting" : linkStatusKey(st), args).c_str());
            ImGui::PopStyleColor();
        } else if (resumedUntil > now) {
            ImGui::TextUnformatted(loc.tr("place.link.resumed").c_str());
        }
        if (client && client->ready()) {
            const auto& s = client->stats();
            ImGui::TextUnformatted(loc.tr("place.world", i18n::Args().num("players", int64_t(players)).num("bodies", int64_t(bodies))).c_str());
            uint32_t best = link->bestPingMs();
            ImGui::TextUnformatted(loc.tr("place.network", i18n::Args().num("rtt", best ? best : s.rttMs).num("ping", s.rttMs)
                                                                .num("snapshots", std::lround(s.snapshotsPerSecond))
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
        quit = true;
        if (net.joinable()) net.join();
        link.reset();
    }
};
View view;
}

int runPlaceClient(LinkConfig config, const std::string& language, bool suspendOnFocus, const char* argv0) {
    std::filesystem::current_path(platform::resolveEngineDir(AIVERSE_SOURCE_DIR, argv0));
    view.link = std::make_unique<PlaceLink>(std::move(config));
    view.language = language;
    view.suspendOnFocus = suspendOnFocus;
    if (const char* seed = std::getenv("AIVERSE_PLACE_AUTOPILOT")) view.autopilot = std::make_unique<Bot>(uint32_t(std::strtoul(seed, nullptr, 10)));
    view.net = std::thread([] { view.netLoop(); });
    platform::AppConfig cfg;
    cfg.title = "AIVERSE Place";
    cfg.width = 1280;
    cfg.height = 800;
    platform::AppCallbacks cb;
    cb.init = [](gfx::Device& d) { view.init(d); };
    cb.frame = [](gfx::Device& d, double dt) { view.frame(d, dt); };
    cb.event = [](const platform::Event& e) { view.event(e); };
    cb.shutdown = [] { view.shutdown(); };
    int rc = platform::run(cfg, cb);
    view.quit = true;
    if (view.net.joinable()) view.net.join();
    return rc;
}
