// MotionWeave locomotion review: drives a procedural 53-bone humanoid through
// idle -> walk -> run -> walk+turn -> stop -> idle -> turn-in-place with three
// MotionWeave graphs and reports foot sliding, pops and broken poses.
//
//   aiverse_motionweave_locomotion_review <out-dir>
//
// Writes <out-dir>/<variant>.csv (per-frame joint positions in world space for
// the contact-sheet renderer tools/animation/motionweave_review_render.py) and
// <out-dir>/metrics.json. CPU only: it evaluates the real AnimationGraph /
// MotionPlayer / solveTwoBoneIK code, it does not render with Metal.
#include "tests/animation/HumanoidLocomotionFixture.h"
#include "engine/render/AnimationGraph.h"
#include "engine/render/AnimationMotion.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::render;
namespace fx = aiverse::motionweave_fixture;

namespace aiverse::platform {
bool readContentFile(const std::string&, std::string&) { return false; }
}

namespace {
constexpr float kDt = 1.f / 60.f;
constexpr float kDuration = 9.5f;
constexpr float kContact = .004f; // planted-ankle tolerance (30 Hz key interpolation is ~3 mm)

struct Command { float speed = 0, yawRate = 0; bool stopping = false, turnInPlace = false; const char* segment = ""; };

// The host's movement plan (what a gameplay controller would request).
Command commandAt(float t) {
    Command c;
    if (t < 1.f) c = {0, 0, false, false, "idle"};
    else if (t < 1.4f) c = {fx::kWalkSpeed * (t - 1.f) / .4f, 0, false, false, "start"};
    else if (t < 3.f) c = {fx::kWalkSpeed, 0, false, false, "walk"};
    else if (t < 3.6f) c = {fx::kWalkSpeed + (fx::kRunSpeed - fx::kWalkSpeed) * (t - 3.f) / .6f, 0, false, false, "walk-run"};
    else if (t < 4.8f) c = {fx::kRunSpeed, 0, false, false, "run"};
    else if (t < 5.4f) c = {fx::kRunSpeed - (fx::kRunSpeed - fx::kWalkSpeed) * (t - 4.8f) / .6f, 0, false, false, "run-walk"};
    else if (t < 6.4f) c = {fx::kWalkSpeed, fx::kPi * .5f, false, false, "walk-turn"};
    else if (t < 7.f) c = {fx::kWalkSpeed * (1 - (t - 6.4f) / .6f), 0, true, false, "stop"};
    else if (t < 7.8f) c = {0, 0, false, false, "idle2"};
    else if (t < 8.8f) c = {0, 0, false, true, "turn-in-place"};
    else c = {0, 0, false, false, "idle3"};
    return c;
}

const char* kBlendGraph = R"({
  "parameters":{"moving":0,"stopping":0,"turning":0,"walkRate":1,"runRate":1,"runBlend":0},
  "nodes":[
    {"id":"idle","kind":"clip","clip":"Idle"},
    {"id":"walk","kind":"clip","clip":"Walk_IP","parameter":"walkRate"},
    {"id":"run","kind":"clip","clip":"Run_IP","parameter":"runRate"},
    {"id":"loco","kind":"blend","a":"walk","b":"run","parameter":"runBlend"SYNC},
    {"id":"stop","kind":"clip","clip":"Stop_IP","loop":false},
    {"id":"turn","kind":"clip","clip":"TurnLeft_IP","loop":false}
  ],
  "initial":"idle",
  "states":[
    {"id":"idle","output":"idle","transitions":[
      {"to":"loco","fade":0.25,"when":[{"parameter":"moving","op":"gt","value":0.5}]},
      {"to":"turn","fade":0.15,"when":[{"parameter":"turning","op":"gt","value":0.5}]}]},
    {"id":"loco","output":"loco","transitions":[
      {"to":"stop","fade":0.2,"when":[{"parameter":"stopping","op":"gt","value":0.5}]},
      {"to":"idle","fade":0.25,"when":[{"parameter":"moving","op":"lt","value":0.5}]}]},
    {"id":"stop","output":"stop","transitions":[
      {"to":"idle","fade":0.2,"after":0.9,"when":[]}]},
    {"id":"turn","output":"turn","transitions":[
      {"to":"idle","fade":0.2,"after":0.95,"when":[]}]}
  ]
})";

const char* kMotionGraph = R"({
  "parameters":{"traj1":[0,0,0],"traj2":[0,0,0]},
  "nodes":[{"id":"mm","kind":"motionMatching","database":{
    "root":"root","clips":["Idle","Walk","Run","Stop","TurnLeft"],
    "bones":["footL","footR"],
    "trajectory":[{"time":0.3,"parameter":"traj1"},{"time":0.6,"parameter":"traj2"}],
    "fps":30,"searchInterval":0.1,"blend":0.2,"stripRoot":true}}],
  "output":"mm"
})";

struct Host { float3 pos{}; float yaw = 0; };

struct Metrics {
    double slideDist = 0, slideMax = 0, contactTime = 0;
    double popAccel = 0, popRot = 0; float popAt = 0, popRotAt = 0;
    int nan = 0, belowGround = 0, kneeBack = 0, frames = 0;
};

struct SegmentStats { std::map<std::string, Metrics> segments; Metrics all; };

void accumulate(Metrics& m, double slide, double dt, bool contact, double accel, double rot, float t) {
    if (contact) { m.contactTime += dt; m.slideDist += slide * dt; m.slideMax = std::max(m.slideMax, slide); }
    if (accel > m.popAccel) { m.popAccel = accel; m.popAt = t; }
    if (rot > m.popRot) { m.popRot = rot; m.popRotAt = t; }
}

// Predicted host displacement (character space) after `ahead` seconds.
float3 predict(float t, float ahead) {
    Host h; const int steps = 12; const float dt = ahead / steps;
    for (int i = 0; i < steps; ++i) {
        const Command c = commandAt(t + (float(i) + .5f) * dt);
        h.yaw += c.yawRate * dt;
        h.pos = h.pos + fx::yawQ(h.yaw).rotate({0, 0, c.speed * dt});
    }
    return h.pos;
}

SegmentStats run(const std::string& variant, const Model& model, const fx::HumanoidBones& b, const std::string& dir) {
    std::string def = variant == "motion-matching" ? kMotionGraph : kBlendGraph;
    const auto at = def.find("SYNC");
    if (at != std::string::npos) def.replace(at, 4, variant == "blend-sync" ? ",\"sync\":true" : "");
    auto graph = AnimationGraph::compile(model, Json::parse(def));
    AnimationGraphInstance inst; inst.reset(graph);

    std::vector<int> joints{b.root, b.hips, b.spine, b.chest, b.upperChest, b.neck, b.head};
    for (int s = 0; s < 2; ++s) for (int j : {b.thigh[s], b.shin[s], b.foot[s], b.toe[s], b.clavicle[s], b.upperArm[s], b.foreArm[s], b.hand[s]}) joints.push_back(j);
    std::ofstream csv(dir + "/" + variant + ".csv");
    csv << "frame,time,segment,state,hostX,hostZ,hostYaw";
    for (int j : joints) csv << "," << model.nodes[size_t(j)].name << "X," << model.nodes[size_t(j)].name << "Y," << model.nodes[size_t(j)].name << "Z";
    csv << "\n";

    Host host; float turnClock = -1;
    Pose pose, prevPose; std::vector<mat4> world;
    std::vector<float3> prev, prev2, prevFeet(2);
    SegmentStats out;
    const int frames = int(kDuration / kDt);
    for (int f = 0; f < frames; ++f) {
        const float t = float(f) * kDt;
        const Command c = commandAt(t);
        // Host movement: commanded velocity; turn-in-place follows the TurnLeft root curve.
        float yawRate = c.yawRate;
        if (c.turnInPlace) {
            if (turnClock < 0) turnClock = 0;
            const float u0 = fx::smooth01(turnClock / .9f), u1 = fx::smooth01((turnClock + kDt) / .9f);
            yawRate = (u1 - u0) * fx::kPi * .5f / kDt; turnClock += kDt;
        }
        auto& p = inst.parameters();
        if (variant == "motion-matching") {
            p.set("traj1", predict(t, .3f));
            p.set("traj2", predict(t, .6f));
        } else {
            p.set("moving", c.speed > .05f && !c.stopping ? 1.f : 0.f);
            p.set("stopping", c.stopping ? 1.f : 0.f);
            // One-shot request: the first 0.1 s of the turn segment only.
            p.set("turning", c.turnInPlace && turnClock < .1f ? 1.f : 0.f);
            const float w = std::clamp((c.speed - fx::kWalkSpeed) / (fx::kRunSpeed - fx::kWalkSpeed), 0.f, 1.f);
            p.set("runBlend", w);
            const float blended = fx::kWalkSpeed + (fx::kRunSpeed - fx::kWalkSpeed) * w;
            const float rate = c.speed < fx::kWalkSpeed ? c.speed / fx::kWalkSpeed : c.speed / blended;
            p.set("walkRate", std::max(rate, .05f));
            p.set("runRate", std::max(rate, .05f));
        }
        inst.update(kDt);
        inst.evaluate(pose);
        host.yaw += yawRate * kDt;
        host.pos = host.pos + fx::yawQ(host.yaw).rotate({0, 0, c.speed * kDt});
        const mat4 hostXf = mat4::trs(host.pos, fx::yawQ(host.yaw), {1, 1, 1});
        worldMatrices(model, pose, world);
        std::vector<float3> cur, charSpace;
        bool finite = pose.size() == model.nodes.size();
        for (int j : joints) {
            const float3 local = fx::posOf(world[size_t(j)]);
            finite = finite && std::isfinite(local.x) && std::isfinite(local.y) && std::isfinite(local.z);
            charSpace.push_back(local);
            cur.push_back(hostXf.transformPoint(local));
        }
        Metrics& seg = out.segments[c.segment];
        for (Metrics* m : {&seg, &out.all}) { ++m->frames; if (!finite) ++m->nan; }
        // Feet: contact when the ankle is within 4 mm of its planted height.
        for (int s = 0; s < 2; ++s) {
            const float3 ankle = hostXf.transformPoint(fx::posOf(world[size_t(b.foot[s])]));
            const bool contact = ankle.y < fx::kAnkleHeight + kContact && f > 0 && prevFeet[size_t(s)].y < fx::kAnkleHeight + kContact;
            const float3 d = ankle - prevFeet[size_t(s)];
            const double slide = f > 0 ? std::sqrt(double(d.x) * d.x + double(d.z) * d.z) / kDt : 0;
            if (ankle.y < fx::kAnkleHeight - .02f) { ++seg.belowGround; ++out.all.belowGround; }
            const float3 hip = fx::posOf(world[size_t(b.thigh[s])]), knee = fx::posOf(world[size_t(b.shin[s])]), ak = fx::posOf(world[size_t(b.foot[s])]);
            const float3 mid = (hip + ak) * .5f;
            const float3 fwd = fx::yawQ(0).rotate({0, 0, 1});
            const quat footYaw = rotationOf(world[size_t(b.foot[s])]);
            const float3 footFwd = footYaw.rotate({0, 0, 1});
            if (dot(knee - mid, normalize(float3{footFwd.x, 0, footFwd.z})) < -.01f) { ++seg.kneeBack; ++out.all.kneeBack; }
            (void)fwd;
            accumulate(seg, slide, kDt, contact, 0, 0, t);
            accumulate(out.all, slide, kDt, contact, 0, 0, t);
            prevFeet[size_t(s)] = ankle;
        }
        // Pops: character-space joint acceleration and per-frame local rotation change.
        if (prev2.size() == charSpace.size()) {
            double maxA = 0, maxR = 0;
            for (size_t i = 0; i < charSpace.size(); ++i) {
                const float3 a = (charSpace[i] - prev[i] * 2.f + prev2[i]) * (1.f / (kDt * kDt));
                maxA = std::max(maxA, double(length(a)));
            }
            for (size_t i = 0; i < pose.size() && prevPose.size() == pose.size(); ++i) {
                if (int(i) == b.root) continue;
                const double d = std::clamp(std::abs(double(dot(pose[i].r, prevPose[i].r))), 0., 1.);
                maxR = std::max(maxR, 2 * std::acos(d) * 180 / fx::kPi);
            }
            accumulate(seg, 0, 0, false, maxA, maxR, t);
            accumulate(out.all, 0, 0, false, maxA, maxR, t);
        }
        prev2 = prev; prev = charSpace; prevPose = pose;
        std::string state = inst.state();
        if (const MotionPlayer* mm = inst.motion("mm"); mm && mm->clip() >= 0) {
            char buf[64]; std::snprintf(buf, sizeof buf, "%s@%.2f", model.clips[size_t(mm->clip())].name.c_str(), mm->time());
            state = buf;
        }
        csv << f << "," << t << "," << c.segment << "," << state << "," << host.pos.x << "," << host.pos.z << "," << host.yaw;
        for (const auto& v : cur) csv << "," << v.x << "," << v.y << "," << v.z;
        csv << "\n";
    }
    return out;
}

void writeMetrics(std::FILE* j, const Metrics& m) {
    std::fprintf(j, "{\"frames\":%d,\"contactSeconds\":%.3f,\"slideMetres\":%.4f,\"slideMeanMps\":%.4f,\"slideMaxMps\":%.3f,"
                    "\"popAccelMax\":%.1f,\"popAccelAt\":%.3f,\"rotStepMaxDeg\":%.2f,\"rotStepAt\":%.3f,"
                    "\"nanFrames\":%d,\"ankleBelowGround\":%d,\"kneeBackward\":%d}",
                 m.frames, m.contactTime, m.slideDist, m.contactTime > 0 ? m.slideDist / m.contactTime : 0., m.slideMax,
                 m.popAccel, m.popAt, m.popRot, m.popRotAt, m.nan, m.belowGround, m.kneeBack);
}
} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "motionweave-review";
    std::filesystem::create_directories(dir);
    fx::HumanoidBones b;
    const Model model = fx::makeHumanoid(&b);
    std::vector<std::string> variants{"blend", "blend-sync", "motion-matching"};
    if (argc > 2) variants.assign(argv + 2, argv + argc);
    std::FILE* j = std::fopen((dir + "/metrics.json").c_str(), "w");
    std::fprintf(j, "{");
    bool firstVariant = true;
    for (const auto& v : variants) {
        SegmentStats s;
        try { s = run(v, model, b, dir); }
        catch (const std::exception& e) { std::fprintf(stderr, "%s: %s\n", v.c_str(), e.what()); continue; }
        std::fprintf(j, "%s\"%s\":{\"all\":", firstVariant ? "" : ",", v.c_str());
        writeMetrics(j, s.all);
        for (const auto& [name, m] : s.segments) { std::fprintf(j, ",\"%s\":", name.c_str()); writeMetrics(j, m); }
        std::fprintf(j, "}");
        firstVariant = false;
        std::printf("%-16s slide %.3f m over %.2f s contact (mean %.3f m/s, max %.2f m/s)  pop %.0f m/s^2 @%.2fs  rot %.1f deg/frame @%.2fs  nan %d below %d kneeBack %d\n",
                    v.c_str(), s.all.slideDist, s.all.contactTime, s.all.contactTime > 0 ? s.all.slideDist / s.all.contactTime : 0.,
                    s.all.slideMax, s.all.popAccel, s.all.popAt, s.all.popRot, s.all.popRotAt, s.all.nan, s.all.belowGround, s.all.kneeBack);
        for (const auto& [name, m] : s.segments)
            std::printf("    %-14s slide %.3f m / %.2f s (mean %.3f m/s max %.2f)  pop %.0f @%.2f  rot %.1f @%.2f\n", name.c_str(), m.slideDist,
                        m.contactTime, m.contactTime > 0 ? m.slideDist / m.contactTime : 0., m.slideMax, m.popAccel, m.popAt, m.popRot, m.popRotAt);
    }
    std::fprintf(j, "}\n");
    std::fclose(j);
    return 0;
}
