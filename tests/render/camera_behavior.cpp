// LENS camera behaviours: presets, the overhead / follow / chase / first-person bodies and the ECS rig modes that
// run the same chase / look math. CPU only.
#include "engine/render/CameraRig.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
using namespace aiverse;
static void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
static bool near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
static bool near(render::float3 a, render::float3 b, float e = 1e-3f) { return near(a.x, b.x, e) && near(a.y, b.y, e) && near(a.z, b.z, e); }

int main() {
    // presets: every one parses and the block's own keys win
    check(render::cameraPresetNames().size() >= 8, "preset table");
    std::string error;
    Json block = render::resolveCameraPreset(Json::parse(R"({"preset": "follow-speed", "distance": 24})"), &error);
    check(error.empty() && block["distance"].toDouble() == 24 && block["speedZoom"].isObject(), "preset + own keys");
    render::resolveCameraPreset(Json::parse(R"({"preset": "nope"})"), &error);
    check(error.find("nope") != std::string::npos, "unknown preset reported");

    // overhead: a preset gives the same camera as its keys written out
    const auto pixel = render::overheadCameraFrom(Json::parse(R"({"camera": {"preset": "overhead-pixel"}})"));
    const auto spelled = render::overheadCameraFrom(Json::parse(R"({"camera": {"projection": "ortho", "pitch": 90, "yaw": 0, "pixelsPerCell": 32, "rotate": false}})"));
    check(pixel.present && pixel.ortho && !pixel.rotate && near(pixel.pitch, spelled.pitch) && pixel.pixelsPerCell == 32, "overhead preset");
    render::Camera cam;
    check(render::applyOverheadCamera(pixel, cam, 640) && cam.ortho, "pixel-perfect zoom applied");
    check(near(cam.distance, render::overheadDistanceFor(pixel, 20)), "640 px / 32 px per cell = 20 cells");

    // follow: preset values land in the settings; the body keeps the focus inside the map
    render::CameraFollowSettings fs;
    render::readCameraFollow(Json::parse(R"({"preset": "follow-speed", "smoothing": 0})"), fs);
    render::finishCameraFollow(fs);
    check(fs.pitch == 90 && fs.speedTo > fs.speedFrom && fs.velocityLookAhead > 0 && fs.smoothing == 0, "follow-speed preset");
    render::CameraFollow follow;
    render::CameraFollowInput in; in.focus = {50, 0, 50}; in.mapW = 100; in.mapH = 100; in.dt = 0;
    render::Camera fc;
    follow.update(fs, in, fc);
    check(near(fc.target.x, 50) && near(fc.target.z, 50) && near(fc.distance, fs.distance), "snap to the focus");
    // driving east fast: speed zoom pulls back and the velocity look-ahead leads the motion
    for (int i = 1; i <= 120; ++i) { in.focus = {50 + i * 0.3f, 0, 50}; in.dt = 1.f / 60; follow.update(fs, in, fc); }
    check(follow.speed() > 15 && fc.distance > fs.distance + 2 && fc.target.x > in.focus.x + 1, "speed zoom + lead");
    in.focus = {-30, 0, 50}; in.dt = 0; follow.reset(); follow.update(fs, in, fc);
    check(fc.target.x >= 0, "focus kept inside the map");

    // chase: the defaults are the old fixed camera; a block reads in degrees
    const auto chase = render::chaseCamera({1, 0, 2}, 0.25f);
    check(near(chase.distance, 11) && near(chase.pitch, 0.26f) && near(chase.target, {3.5f, 1.4f, 2}), "chase defaults");
    const auto low = render::cameraChaseFrom(Json::parse(R"({"preset": "chase-low"})"));
    check(near(low.distance, 7) && near(low.pitch, 8 * render::kCameraDeg), "chase-low preset");

    // first person: sensitivity and limits from data
    render::CameraLook look{0, 0, render::cameraLookFrom(Json::parse(R"({"preset": "first-person", "sensitivity": 0.001})"))};
    look.motion(0, -10000);
    check(near(look.pitch, look.settings.pitchLimit), "pitch limit");
    look = {0, 0, look.settings}; look.motion(10, 0); const float hip = look.yaw;
    look = {0, 0, look.settings}; look.motion(10, 0, true);
    check(std::fabs(look.yaw - 1) < std::fabs(hip - 1), "aiming turns slower");

    // ECS: Chase / FirstPerson rig modes follow the target's heading with the same math
    World world;
    auto car = world.create(), view = world.create();
    LocalTransform at{}; at.position = {10_fx, 0_fx, 5_fx};
    const float s = std::sin(3.14159265f / 4);   // facing +X: a quarter turn about Y
    at.rotation = {0_fx, Fixed::fromRaw(int64_t(s * 65536)), 0_fx, Fixed::fromRaw(int64_t(s * 65536))};
    world.add<WorldTransform>(car, hierarchy_detail::root(at));
    world.add<WorldTransform>(view, hierarchy_detail::root({}));
    world.add<CameraComponent>(view, CameraComponent{});
    CameraRigComponent rig; rig.mode = CameraRigMode::Chase; rig.target = car; rig.smoothing = 0_fx;
    rig.distance = 11_fx; rig.pitch = Fixed::fromRatio(26, 100); rig.offset = {0_fx, Fixed::fromRatio(14, 10), Fixed::fromRatio(5, 2)};
    world.add<CameraRigComponent>(view, rig);
    render::EcsCameraView snap;
    check(render::resolveEcsCamera(world, view, snap), "ecs camera");
    render::CameraRigEvaluator rigs;
    check(rigs.apply(world, snap, 0, 0.016f), "chase rig");
    const auto want = render::chaseCamera({10, 0, 5}, 0.25f);
    check(near(snap.camera.eye(), want.eye(), 0.02f), "ecs chase = view chase");
    auto& r = world.get<CameraRigComponent>(view);
    r.mode = CameraRigMode::FirstPerson; r.offset = {0_fx, Fixed::fromRatio(17, 10), 0_fx}; r.yaw = 0_fx; r.pitch = 0_fx;
    rigs.reset();
    check(rigs.apply(world, snap, 0, 0.016f), "first-person rig");
    check(near(snap.camera.eye(), {10, 1.7f, 5}, 0.02f), "eye at the target");
    check(near(snap.camera.rotation.rotate({0, 0, -1}), {1, 0, 0}, 0.02f), "looks along the target's heading");
    std::puts("camera_behavior: OK");
    return 0;
}
