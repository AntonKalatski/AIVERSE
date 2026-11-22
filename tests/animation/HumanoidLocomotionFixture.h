// Procedural humanoid + locomotion clips for MotionWeave tests, review renders
// and CPU benchmarks. No asset files: the skeleton (53 bones, humanoid
// proportions in metres, +Y up, +Z forward) and every clip are generated here.
//
// Clips are authored with ROOT MOTION on bone "root" (translation + yaw) and
// planted feet: during stance a foot is fixed in model space while the root
// moves, so playing the in-place variant ("*_IP", root channels removed) on a
// host that moves the character at the clip speed gives zero foot slide.
// Legs are posed with the engine's solveTwoBoneIK at authoring time, then
// baked into 30 Hz linear keys like an imported clip.
//
//   Idle      2.0 s  loop, breathing, feet together
//   Walk      1.0 s  loop, 1.2 m/s, duty 0.60
//   Run       0.7 s  loop, 3.0 m/s, duty 0.38
//   Stop      1.0 s  walk -> idle, root decelerates 0.36 m
//   TurnLeft  1.0 s  in place, root yaw 0 -> +90 deg, two steps
//   Walk_IP, Run_IP, Stop_IP, TurnLeft_IP: root channels stripped.
#pragma once
#include "engine/render/Animation.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace aiverse::motionweave_fixture {
using render::AnimChannel;
using render::AnimClip;
using render::float3;
using render::mat4;
using render::Model;
using render::Pose;
using render::quat;

inline constexpr float kPi = 3.14159265358979f;
inline constexpr float kAnkleHeight = .08f;
inline constexpr float kWalkSpeed = 1.2f, kWalkCycle = 1.f, kWalkDuty = .6f;
inline constexpr float kRunSpeed = 3.f, kRunCycle = .7f, kRunDuty = .38f;
inline constexpr float kStopDistance = .36f;

struct HumanoidBones {
    int root = -1, hips = -1, spine = -1, chest = -1, upperChest = -1, neck = -1, head = -1;
    int thigh[2]{-1, -1}, shin[2]{-1, -1}, foot[2]{-1, -1}, toe[2]{-1, -1};
    int clavicle[2]{-1, -1}, upperArm[2]{-1, -1}, foreArm[2]{-1, -1}, hand[2]{-1, -1};
    std::vector<int> fingers; // 30 finger joints
};

inline quat conj(quat q) { return {-q.x, -q.y, -q.z, q.w}; }
inline quat yawQ(float a) { return quat::axisAngle({0, 1, 0}, a); }
inline quat pitchQ(float a) { return quat::axisAngle({1, 0, 0}, a); }
inline quat rollQ(float a) { return quat::axisAngle({0, 0, 1}, a); }
inline float3 posOf(const mat4& m) { return {m.at(0, 3), m.at(1, 3), m.at(2, 3)}; }
inline float smooth01(float x) { x = std::clamp(x, 0.f, 1.f); return x * x * (3 - 2 * x); }
inline float frac(float x) { return x - std::floor(x); }

inline int addBone(Model& m, const std::string& name, int parent, float3 t) {
    render::Node n;
    n.name = name; n.parent = parent; n.t = t;
    m.nodes.push_back(n);
    const int id = int(m.nodes.size()) - 1;
    if (parent >= 0) m.nodes[size_t(parent)].children.push_back(id);
    return id;
}

// Skeleton only (no clips). Order is parents-before-children by construction.
inline Model makeHumanoidSkeleton(HumanoidBones* out = nullptr) {
    Model m;
    HumanoidBones b;
    b.root = addBone(m, "root", -1, {0, 0, 0});
    b.hips = addBone(m, "hips", b.root, {0, .93f, 0});
    b.spine = addBone(m, "spine", b.hips, {0, .1f, 0});
    b.chest = addBone(m, "chest", b.spine, {0, .14f, 0});
    b.upperChest = addBone(m, "upperChest", b.chest, {0, .14f, 0});
    b.neck = addBone(m, "neck", b.upperChest, {0, .12f, 0});
    b.head = addBone(m, "head", b.neck, {0, .1f, 0});
    for (int s = 0; s < 2; ++s) {
        const float x = s == 0 ? .1f : -.1f;
        const std::string side = s == 0 ? "L" : "R";
        b.thigh[s] = addBone(m, "thigh" + side, b.hips, {x, -.06f, 0});
        b.shin[s] = addBone(m, "shin" + side, b.thigh[s], {0, -.42f, 0});
        b.foot[s] = addBone(m, "foot" + side, b.shin[s], {0, -.42f, 0});
        b.toe[s] = addBone(m, "toe" + side, b.foot[s], {0, -.07f, .14f});
    }
    for (int s = 0; s < 2; ++s) {
        const float x = s == 0 ? 1.f : -1.f;
        const std::string side = s == 0 ? "L" : "R";
        b.clavicle[s] = addBone(m, "clavicle" + side, b.upperChest, {.04f * x, .1f, 0});
        b.upperArm[s] = addBone(m, "upperArm" + side, b.clavicle[s], {.14f * x, 0, 0});
        b.foreArm[s] = addBone(m, "foreArm" + side, b.upperArm[s], {0, -.28f, 0});
        b.hand[s] = addBone(m, "hand" + side, b.foreArm[s], {0, -.25f, 0});
        for (int f = 0; f < 5; ++f) {
            int parent = b.hand[s];
            const float spread = (f - 2) * .018f * x;
            for (int j = 0; j < 3; ++j) {
                parent = addBone(m, "finger" + side + std::to_string(f) + "_" + std::to_string(j), parent,
                                 j == 0 ? float3{spread, -.08f, 0} : float3{0, -.03f, 0});
                b.fingers.push_back(parent);
            }
        }
    }
    m.order.resize(m.nodes.size());
    for (size_t i = 0; i < m.order.size(); ++i) m.order[i] = int(i);
    if (out) *out = b;
    return m;
}

namespace detail {
struct FootTarget { float3 ankle; float yaw = 0; };
struct Frame {
    float3 rootT{}; float rootYaw = 0;
    float hipsY = .93f, hipsPitch = 0, hipsRoll = 0;
    float chestYaw = 0, breath = 0;
    float armSwing[2]{0, 0}, elbow[2]{.15f, .15f}, fingerCurl = .2f;
    FootTarget feet[2];
};

inline void poseFrame(const Model& m, const HumanoidBones& b, const Frame& f, Pose& pose, std::vector<mat4>& world) {
    render::bindPose(m, pose);
    pose[size_t(b.root)].t = f.rootT;
    pose[size_t(b.root)].r = yawQ(f.rootYaw);
    pose[size_t(b.hips)].t = {0, f.hipsY, 0};
    pose[size_t(b.hips)].r = render::qnormalize(pitchQ(f.hipsPitch) * rollQ(f.hipsRoll));
    pose[size_t(b.spine)].r = pitchQ(f.breath * .5f);
    pose[size_t(b.chest)].r = yawQ(f.chestYaw);
    pose[size_t(b.upperChest)].r = pitchQ(-f.breath);
    pose[size_t(b.neck)].r = yawQ(-f.chestYaw * .5f);
    for (int s = 0; s < 2; ++s) {
        const float side = s == 0 ? 1.f : -1.f;
        pose[size_t(b.upperArm[s])].r = render::qnormalize(pitchQ(-f.armSwing[s]) * rollQ(.08f * side));
        pose[size_t(b.foreArm[s])].r = pitchQ(-f.elbow[s]);
    }
    for (int j : b.fingers) pose[size_t(j)].r = pitchQ(-f.fingerCurl);
    render::worldMatrices(m, pose, world);
    for (int s = 0; s < 2; ++s) {
        const quat footYaw = yawQ(f.feet[s].yaw);
        const float3 hipJoint = posOf(world[size_t(b.thigh[s])]);
        const float3 forward = footYaw.rotate({0, 0, 1});
        const float3 pole = float3{f.feet[s].ankle.x, hipJoint.y - .4f, f.feet[s].ankle.z} + forward * 1.5f;
        render::solveTwoBoneIK(m, pose, world, b.thigh[s], b.shin[s], b.foot[s], f.feet[s].ankle, pole, 1.f);
        const quat shinWorld = render::rotationOf(world[size_t(b.shin[s])]);
        pose[size_t(b.foot[s])].r = render::qnormalize(conj(shinWorld) * footYaw);
        render::worldMatrices(m, pose, world);
    }
}

// Cyclic gait foot target, root-local (before root transform).
inline float3 gaitFoot(float phase, float duty, float stride, float lift, float x) {
    phase = frac(phase);
    if (phase < duty) return {x, kAnkleHeight, stride * .5f - stride * (phase / duty)};
    const float s = (phase - duty) / (1 - duty);
    return {x, kAnkleHeight + lift * std::sin(kPi * s), -stride * .5f + stride * (.5f - .5f * std::cos(kPi * s))};
}

template <class Fn>
AnimClip bake(const Model& m, const HumanoidBones& b, const std::string& name, float duration, Fn&& frameAt) {
    AnimClip clip; clip.name = name; clip.duration = duration;
    const int keys = int(std::lround(duration * 30.f)) + 1;
    std::vector<Pose> poses(static_cast<size_t>(keys));
    std::vector<mat4> world;
    for (int k = 0; k < keys; ++k) poseFrame(m, b, frameAt(std::min(duration, float(k) / 30.f)), poses[size_t(k)], world);
    for (size_t node = 0; node < m.nodes.size(); ++node) {
        AnimChannel r; r.node = int(node); r.path = AnimChannel::Rotation; r.comps = 4;
        quat prev{};
        for (int k = 0; k < keys; ++k) {
            r.times.push_back(std::min(duration, float(k) / 30.f));
            quat q = poses[size_t(k)][node].r;
            if (k > 0 && render::dot(prev, q) < 0) q = {-q.x, -q.y, -q.z, -q.w}; // keep hemisphere
            prev = q;
            r.values.insert(r.values.end(), {q.x, q.y, q.z, q.w});
        }
        clip.channels.push_back(std::move(r));
        if (int(node) == b.root || int(node) == b.hips) {
            AnimChannel t; t.node = int(node); t.path = AnimChannel::Translation; t.comps = 3;
            for (int k = 0; k < keys; ++k) {
                t.times.push_back(std::min(duration, float(k) / 30.f));
                const float3 v = poses[size_t(k)][node].t;
                t.values.insert(t.values.end(), {v.x, v.y, v.z});
            }
            clip.channels.push_back(std::move(t));
        }
    }
    return clip;
}

inline Frame gaitFrame(float t, float speed, float cycle, float duty, float lift, float bob, float swing, float elbow) {
    Frame f;
    const float phase = t / cycle, stride = speed * cycle * duty;
    f.rootT = {0, 0, speed * t};
    // Lowest hips that keep the stance leg (0.84 m) reachable with 1 cm slack.
    const float half = stride * .5f, reach = std::sqrt(std::max(0.f, .83f * .83f - half * half));
    f.hipsY = std::min(.87f, reach + .06f + kAnkleHeight - bob) + bob * std::cos(4 * kPi * phase);
    f.hipsPitch = speed > 2 ? .12f : .04f;
    f.hipsRoll = .03f * std::sin(2 * kPi * phase);
    f.chestYaw = .08f * std::sin(2 * kPi * phase);
    for (int s = 0; s < 2; ++s) {
        const float ph = phase + (s == 0 ? 0.f : .5f);
        const float3 local = gaitFoot(ph, duty, stride, lift, s == 0 ? .1f : -.1f);
        f.feet[s].ankle = local + f.rootT;
        // Arm swings opposite to the same-side leg.
        f.armSwing[s] = -swing * std::cos(2 * kPi * ph);
        f.elbow[s] = elbow;
    }
    f.fingerCurl = speed > 2 ? .6f : .25f;
    return f;
}
} // namespace detail

inline AnimClip stripRootMotion(const AnimClip& clip, int root, const std::string& name) {
    AnimClip out = clip; out.name = name;
    out.channels.erase(std::remove_if(out.channels.begin(), out.channels.end(),
                       [&](const AnimChannel& c) { return c.node == root; }), out.channels.end());
    return out;
}

// Skeleton + Idle/Walk/Run/Stop/TurnLeft (+ *_IP in-place variants).
inline Model makeHumanoid(HumanoidBones* out = nullptr) {
    HumanoidBones b;
    Model m = makeHumanoidSkeleton(&b);
    using detail::Frame;
    m.clips.push_back(detail::bake(m, b, "Idle", 2.f, [&](float t) {
        Frame f;
        const float br = std::sin(kPi * t); // one breath per 2 s
        f.hipsY = .93f + .004f * br;
        f.breath = .03f * br;
        f.armSwing[0] = .02f * br; f.armSwing[1] = .02f * br;
        for (int s = 0; s < 2; ++s) f.feet[s].ankle = {s == 0 ? .1f : -.1f, kAnkleHeight, 0};
        return f;
    }));
    m.clips.push_back(detail::bake(m, b, "Walk", kWalkCycle, [&](float t) {
        return detail::gaitFrame(t, kWalkSpeed, kWalkCycle, kWalkDuty, .08f, .015f, .35f, .25f);
    }));
    m.clips.push_back(detail::bake(m, b, "Run", kRunCycle, [&](float t) {
        return detail::gaitFrame(t, kRunSpeed, kRunCycle, kRunDuty, .18f, .03f, .7f, 1.2f);
    }));
    // Walk phase 0 -> both feet together under the root after 0.36 m.
    m.clips.push_back(detail::bake(m, b, "Stop", 1.f, [&](float t) {
        Frame f = detail::gaitFrame(0, kWalkSpeed, kWalkCycle, kWalkDuty, .08f, .015f, .35f, .25f);
        const float brake = .6f, tc = std::min(t, brake);
        f.rootT = {0, 0, kWalkSpeed * (tc - tc * tc / (2 * brake))};
        const float settle = smooth01(t / .8f);
        f.hipsY = f.hipsY + (.93f - f.hipsY) * settle;
        f.hipsPitch *= 1 - settle; f.hipsRoll *= 1 - settle; f.chestYaw *= 1 - settle;
        const float zR0 = detail::gaitFoot(.5f, kWalkDuty, kWalkSpeed * kWalkCycle * kWalkDuty, 0, -.1f).z;
        f.feet[0].ankle = {.1f, kAnkleHeight, kStopDistance};
        const float s = std::clamp((t - .1f) / .45f, 0.f, 1.f);
        f.feet[1].ankle = {-.1f, kAnkleHeight + .08f * std::sin(kPi * s), zR0 + (kStopDistance - zR0) * (.5f - .5f * std::cos(kPi * s))};
        for (int k = 0; k < 2; ++k) { f.armSwing[k] *= 1 - settle; f.elbow[k] = .25f - .1f * settle; }
        return f;
    }));
    m.clips.push_back(detail::bake(m, b, "TurnLeft", 1.f, [&](float t) {
        Frame f;
        f.rootYaw = kPi * .5f * smooth01(t / .9f);
        const float start[2]{.15f, .5f};
        for (int s = 0; s < 2; ++s) {
            const float u = std::clamp((t - start[s]) / .3f, 0.f, 1.f);
            const float yaw = kPi * .5f * (.5f - .5f * std::cos(kPi * u));
            const float3 rest{s == 0 ? .1f : -.1f, 0, 0};
            const float3 p = yawQ(yaw).rotate(rest);
            f.feet[s].ankle = {p.x, kAnkleHeight + .07f * std::sin(kPi * u), p.z};
            f.feet[s].yaw = yaw;
        }
        f.chestYaw = .1f * std::sin(kPi * std::clamp(t / .9f, 0.f, 1.f));
        return f;
    }));
    const size_t authored = m.clips.size();
    for (size_t i = 1; i < authored; ++i) m.clips.push_back(stripRootMotion(m.clips[i], b.root, m.clips[i].name + "_IP"));
    if (out) *out = b;
    return m;
}
// Extra looping gaits for larger motion-matching databases: `count` speeds
// evenly spaced in [0.8, 3.6] m/s named "Gait00".."GaitNN" (root motion kept).
inline void addGaitVariants(Model& m, const HumanoidBones& b, int count) {
    for (int i = 0; i < count; ++i) {
        const float u = count > 1 ? float(i) / float(count - 1) : 0.f;
        const float speed = .8f + 2.8f * u, cycle = 1.1f - .45f * u, duty = .62f - .26f * u;
        char name[16]; std::snprintf(name, sizeof name, "Gait%02d", i);
        m.clips.push_back(detail::bake(m, b, name, cycle, [&](float t) {
            return detail::gaitFrame(t, speed, cycle, duty, .08f + .1f * u, .015f + .015f * u, .35f + .35f * u, .25f + .95f * u);
        }));
    }
}
} // namespace aiverse::motionweave_fixture
