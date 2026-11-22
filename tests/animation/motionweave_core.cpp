// MotionWeave core unit tests: clip sampling, world matrices, blend / sync
// groups, morph-free fast path, two-bone IK, rig world reuse, motion matching
// (search, one-shot clips, facing, crossfade continuity) and a locomotion
// foot-slide regression on the procedural humanoid fixture. CPU only.
#include "HumanoidLocomotionFixture.h"
#include "../AlwaysCheck.h"
#include "engine/render/AnimationGraph.h"
#include "engine/render/AnimationMotion.h"
#include "engine/render/AnimationRig.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

using namespace aiverse;
using namespace aiverse::render;
namespace fx = aiverse::motionweave_fixture;
namespace aiverse::platform { bool readContentFile(const std::string&, std::string&) { return false; } }

namespace {
int checks = 0;
#define CHECK(x) do { AIVERSE_TEST_CHECK(x); ++checks; } while (0)
bool near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) <= eps; }
float dist(float3 a, float3 b) { return length(a - b); }
bool rejects(const Model& m, const std::string& json) {
    try { AnimationGraph::compile(m, Json::parse(json)); } catch (const std::exception&) { return true; }
    return false;
}

Model chain() {
    Model m; m.nodes.resize(3); m.order = {0, 1, 2};
    m.nodes[0].name = "root"; m.nodes[0].children = {1};
    m.nodes[1].name = "knee"; m.nodes[1].parent = 0; m.nodes[1].children = {2}; m.nodes[1].t = {0, 1, 0};
    m.nodes[2].name = "foot"; m.nodes[2].parent = 1; m.nodes[2].t = {0, 1, 0};
    AnimClip a; a.name = "A"; a.duration = 1;
    AnimChannel t; t.node = 0; t.times = {0, 1}; t.values = {0, 0, 0, 2, 0, 0}; a.channels.push_back(t);
    AnimClip b = a; b.name = "B"; b.duration = 2; b.channels[0].times = {0, 2}; b.channels[0].values = {0, 0, 0, 0, 0, 4};
    m.clips = {a, b};
    return m;
}

void sampling() {
    Model m = chain(); Pose p; bindPose(m, p);
    sampleClip(m.clips[0], .25f, p); CHECK(near(p[0].t.x, .5f));
    sampleClip(m.clips[0], -1.f, p); CHECK(near(p[0].t.x, 0));   // clamps before first key
    sampleClip(m.clips[0], 9.f, p); CHECK(near(p[0].t.x, 2));    // clamps after last key
    AnimClip step = m.clips[0]; step.channels[0].interp = AnimChannel::Step;
    sampleClip(step, .9f, p); CHECK(near(p[0].t.x, 0));
    AnimClip rot; rot.duration = 1;
    AnimChannel r; r.node = 1; r.path = AnimChannel::Rotation; r.comps = 4; r.times = {0, 1};
    const quat q = quat::axisAngle({0, 0, 1}, 1.5f);
    r.values = {0, 0, 0, 1, q.x, q.y, q.z, q.w}; rot.channels.push_back(r);
    sampleClip(rot, .5f, p);
    CHECK(near(dot(p[1].r, p[1].r), 1.f, 1e-5f));                   // nlerp stays normalized
    CHECK(near(2 * std::acos(std::clamp(p[1].r.w, -1.f, 1.f)), .75f, .02f));
    bindPose(m, p); sampleClip(m.clips[0], .5f, p, .5f); CHECK(near(p[0].t.x, .5f)); // weighted sample
}

void worldMatrixProduct() {
    fx::HumanoidBones b; Model m = fx::makeHumanoid(&b); Pose p; bindPose(m, p);
    sampleClip(m.clips[size_t(m.findClip("Run"))], .33f, p);
    std::vector<mat4> w; worldMatrices(m, p, w);
    std::vector<mat4> ref(m.nodes.size());
    for (int n : m.order) {
        const mat4 l = mat4::trs(p[size_t(n)].t, p[size_t(n)].r, p[size_t(n)].s);
        ref[size_t(n)] = m.nodes[size_t(n)].parent >= 0 ? ref[size_t(m.nodes[size_t(n)].parent)] * l : l;
    }
    float err = 0;
    for (size_t i = 0; i < w.size(); ++i) for (int k = 0; k < 16; ++k) err = std::max(err, std::abs(w[i].m[k] - ref[i].m[k]));
    CHECK(err < 1e-5f);
}

void blends() {
    Model m = chain();
    auto g = AnimationGraph::compile(m, Json::parse(R"({"parameters":{"w":0.25},"nodes":[
        {"id":"a","kind":"clip","clip":"A"},{"id":"b","kind":"clip","clip":"B"},
        {"id":"mix","kind":"blend","a":"a","b":"b","parameter":"w"}],"output":"mix"})"));
    CHECK(g->morphFree());
    AnimationGraphInstance inst; inst.reset(g); Pose p;
    inst.update(.5f); inst.evaluate(p);                      // A: x=1 ; B at 0.5 s: z=1
    CHECK(near(p[0].t.x, .75f) && near(p[0].t.z, .25f));
    inst.parameters().set("w", 0.f); inst.update(0); inst.evaluate(p); CHECK(near(p[0].t.x, 1) && near(p[0].t.z, 0));
    inst.parameters().set("w", 1.f); inst.update(0); inst.evaluate(p); CHECK(near(p[0].t.x, 0) && near(p[0].t.z, 1));
    // Masked blend only touches the subtree.
    Model mm = chain(); mm.clips[1].channels[0].node = 1; mm.clips[1].channels[0].values = {0, 1, 0, 0, 3, 0};
    auto masked = AnimationGraph::compile(mm, Json::parse(R"({"nodes":[{"id":"a","kind":"clip","clip":"A"},{"id":"b","kind":"clip","clip":"B"},
        {"id":"mix","kind":"blend","a":"a","b":"b","mask":"knee"}],"output":"mix"})"));
    inst.reset(masked); inst.update(.75f); inst.evaluate(p);
    CHECK(near(p[0].t.x, 1.5f) && near(p[1].t.y, 1.75f));
    // Morph weights: a model whose clip animates weights is not morph-free and blends them.
    Model mw = chain();
    for (int c = 0; c < 2; ++c) {
        AnimChannel w; w.node = 2; w.path = AnimChannel::Weights; w.comps = 1; w.times = {0, 1}; w.values = {float(c), float(c)};
        mw.clips[size_t(c)].channels.push_back(w);
    }
    auto morph = AnimationGraph::compile(mw, Json::parse(R"({"nodes":[{"id":"a","kind":"clip","clip":"A"},{"id":"b","kind":"clip","clip":"B"},
        {"id":"mix","kind":"blend","a":"a","b":"b","weight":0.5}],"output":"mix"})"));
    CHECK(!morph->morphFree());
    inst.reset(morph); inst.update(.1f); inst.evaluate(p); CHECK(near(p[2].w[0], .5f));
}

void syncGroups() {
    fx::HumanoidBones b; Model m = fx::makeHumanoid(&b);
    const std::string base = R"({"parameters":{"w":0.5},"nodes":[
        {"id":"walk","kind":"clip","clip":"Walk_IP"},{"id":"run","kind":"clip","clip":"Run_IP"},
        {"id":"stop","kind":"clip","clip":"Stop_IP","loop":false},)";
    CHECK(rejects(m, base + R"({"id":"x","kind":"blend","a":"walk","b":"stop","sync":true}],"output":"x"})"));
    CHECK(rejects(m, base + R"({"id":"x","kind":"additive","a":"walk","b":"run","sync":true}],"output":"x"})"));
    CHECK(rejects(m, base + R"({"id":"x","kind":"blend","a":"walk","b":"run","sync":true},{"id":"y","kind":"blend","a":"run","b":"walk","sync":true}],"output":"y"})"));
    auto g = AnimationGraph::compile(m, Json::parse(base + R"({"id":"x","kind":"blend","a":"walk","b":"run","parameter":"w","sync":true}],"output":"x"})"));
    AnimationGraphInstance inst; inst.reset(g);
    const float tw = fx::kWalkCycle, tr = fx::kRunCycle;
    for (int i = 0; i < 37; ++i) inst.update(1.f / 60.f);
    // Phase lock: normalized phases equal; blended cycle = 1/((1-w)/Tw^-1 + w/Tr^-1) -> T = 0.85 s.
    const float pw = *inst.clipTime("walk") / tw, pr = *inst.clipTime("run") / tr;
    CHECK(near(pw, pr, 1e-4f));
    CHECK(near(pw, std::fmod(37.f / 60.f / (.5f * tw + .5f * tr), 1.f), 1e-3f));
    // Weight 0 plays the walk at its own rate; the run follows its phase.
    inst.parameters().set("w", 0.f);
    const float before = *inst.clipTime("walk");
    inst.update(.1f);
    CHECK(near(*inst.clipTime("walk"), std::fmod(before + .1f, tw), 1e-4f));
    CHECK(near(*inst.clipTime("walk") / tw, *inst.clipTime("run") / tr, 1e-4f));
    // A seek of one clip is re-aligned on the next update.
    CHECK(inst.seekClipNode("run", .1f)); inst.update(0);
    CHECK(near(*inst.clipTime("walk") / tw, *inst.clipTime("run") / tr, 1e-4f));
}

void twoBoneIK() {
    Model m = chain(); Pose p; bindPose(m, p); std::vector<mat4> w; worldMatrices(m, p, w);
    const float3 target{.8f, .9f, .3f};
    solveTwoBoneIK(m, p, w, 0, 1, 2, target, {0, 1, 5}, 1);
    std::vector<mat4> fresh; worldMatrices(m, p, fresh);
    CHECK(dist(fx::posOf(fresh[2]), target) < 1e-3f);                 // reaches a reachable target
    CHECK(dist(fx::posOf(w[2]), fx::posOf(fresh[2])) < 1e-4f);       // world kept in sync with pose
    CHECK(near(dist(fx::posOf(fresh[1]), fx::posOf(fresh[0])), 1, 1e-4f) && near(dist(fx::posOf(fresh[2]), fx::posOf(fresh[1])), 1, 1e-4f));
    CHECK(fx::posOf(fresh[1]).z > .3f * fx::posOf(fresh[1]).y);       // knee bends toward the +z pole
    // Unreachable: straight chain pointing at the target, lengths preserved.
    bindPose(m, p); worldMatrices(m, p, w);
    solveTwoBoneIK(m, p, w, 0, 1, 2, {3, 3, 0}, {0, 0, 1}, 1);
    worldMatrices(m, p, fresh);
    const float3 dir = normalize(float3{3, 3, 0});
    CHECK(near(dot(normalize(fx::posOf(fresh[2])), dir), 1, 1e-4f) && near(length(fx::posOf(fresh[2])), 2, 1e-3f));
    // Zero weight and non-finite targets leave the pose untouched.
    bindPose(m, p); worldMatrices(m, p, w); const Pose orig = p;
    solveTwoBoneIK(m, p, w, 0, 1, 2, target, {0, 0, 1}, 0);
    solveTwoBoneIK(m, p, w, 0, 1, 2, {std::numeric_limits<float>::quiet_NaN(), 0, 0}, {0, 0, 1}, 1);
    CHECK(p[0].r.w == orig[0].r.w && p[1].r.x == orig[1].r.x);
}

void rigWorldReuse() {
    // Two twoBoneIK units in one rig (shared world pass) == two sequential solves.
    fx::HumanoidBones b; Model m = fx::makeHumanoid(&b);
    AnimationParameters params;
    params.set("tl", float3{.12f, .1f, .25f}); params.set("tr", float3{-.1f, .12f, -.2f});
    params.set("pl", float3{.1f, .5f, 2}); params.set("pr", float3{-.1f, .5f, 2});
    auto rig = AnimationRig::compile(m, Json::parse(R"([
        {"op":"twoBoneIK","bone":"thighL","knee":"shinL","end":"footL","target":"tl","pole":"pl"},
        {"op":"transform","bone":"head","rotation":[0,0.3,0],"additive":true},
        {"op":"twoBoneIK","bone":"thighR","knee":"shinR","end":"footR","target":"tr","pole":"pr"}])"), params);
    Pose a; bindPose(m, a); sampleClip(m.clips[size_t(m.findClip("Walk_IP"))], .4f, a);
    Pose ref = a; std::vector<mat4> w, rw;
    rig->execute(m, params, a, w);
    worldMatrices(m, ref, rw);
    solveTwoBoneIK(m, ref, rw, b.thigh[0], b.shin[0], b.foot[0], {.12f, .1f, .25f}, {.1f, .5f, 2}, 1);
    ref[size_t(b.head)].r = ref[size_t(b.head)].r * quat::axisAngle({0, 1, 0}, .3f);
    worldMatrices(m, ref, rw);
    solveTwoBoneIK(m, ref, rw, b.thigh[1], b.shin[1], b.foot[1], {-.1f, .12f, -.2f}, {-.1f, .5f, 2}, 1);
    std::vector<mat4> wa; worldMatrices(m, a, wa); worldMatrices(m, ref, rw);
    CHECK(dist(fx::posOf(wa[size_t(b.foot[0])]), {.12f, .1f, .25f}) < 2e-3f);
    CHECK(dist(fx::posOf(wa[size_t(b.foot[1])]), {-.1f, .12f, -.2f}) < 2e-3f);
    float err = 0; for (size_t i = 0; i < wa.size(); ++i) err = std::max(err, dist(fx::posOf(wa[i]), fx::posOf(rw[i])));
    CHECK(err < 1e-4f);
}

std::string mmGraph(const std::string& clips, bool facing) {
    return std::string(R"({"parameters":{"t1":[0,0,0],"t2":[0,0,0],"f1":[0,0,1],"f2":[0,0,1]},"nodes":[{"id":"mm","kind":"motionMatching","database":{
        "root":"root","clips":[)") + clips + R"(],"bones":["footL","footR"],"trajectory":[{"time":0.3,"parameter":"t1")" +
        (facing ? R"(,"facing":"f1")" : "") + R"(},{"time":0.6,"parameter":"t2")" + (facing ? R"(,"facing":"f2")" : "") +
        R"(}],"fps":30,"searchInterval":0.1,"blend":0.2}}],"output":"mm"})";
}

void motionMatching() {
    fx::HumanoidBones b; Model m = fx::makeHumanoid(&b);
    const std::string clips = R"("Idle","Walk","Run",{"clip":"Stop","loop":false},{"clip":"TurnLeft","loop":false})";
    auto g = AnimationGraph::compile(m, Json::parse(mmGraph(clips, true)));
    const auto& db = *g->nodes()[0].motion;
    CHECK(db.weights.size() == 2 * 6 + 2 * 6);
    CHECK(db.loops(m.findClip("Walk")) && !db.loops(m.findClip("Stop")));
    CHECK(db.scaled.size() == db.frames.size() * db.weights.size());
    // Exact search: every frame's own features find a frame of the same clip at ~the same time.
    size_t exact = 0;
    for (size_t i = 0; i < db.frames.size(); i += 7) {
        float cost; const size_t k = db.search(db.frames[i].features, db.frames[i].clip, db.frames[i].time, cost);
        exact += db.frames[k].clip == db.frames[i].clip && std::abs(db.frames[k].time - db.frames[i].time) < .05f;
    }
    CHECK(exact * 10 >= ((db.frames.size() + 6) / 7) * 9);
    std::vector<float> bad(db.weights.size(), std::numeric_limits<float>::infinity());
    float cost = 0; db.search(bad, -1, 0, cost); CHECK(std::isinf(cost));
    // Pose features are the prefix of the full features.
    std::vector<float> full, pose; db.features(1, .4f, full); db.poseFeatures(1, .4f, pose);
    CHECK(pose.size() == 12);
    bool prefix = true; for (size_t i = 0; i < pose.size(); ++i) prefix = prefix && near(pose[i], full[i], 1e-5f);
    CHECK(prefix);
    // Walk trajectory: 0.3 s ahead = 0.36 m forward, facing straight ahead.
    CHECK(near(full[12 + 2], fx::kWalkSpeed * .3f, 2e-3f) && near(full[12 + 3 + 2], 1, 1e-4f));
    // Loop extrapolation: 0.9 s + 0.6 s crosses the loop boundary.
    db.features(m.findClip("Walk"), .9f, full); CHECK(near(full[12 + 6 + 2], fx::kWalkSpeed * .6f, 3e-3f));
    // One-shot: Stop holds its final root (0.36 m) instead of extrapolating.
    db.features(m.findClip("Stop"), .8f, full); CHECK(near(full[12 + 6 + 2], 0, 1e-3f));

    // Player: walking trajectory selects Walk; facing rotation selects TurnLeft.
    AnimationGraphInstance inst; inst.reset(g); Pose p, prev;
    inst.parameters().set("t1", float3{0, 0, .36f}); inst.parameters().set("t2", float3{0, 0, .72f});
    for (int i = 0; i < 30; ++i) { inst.update(1.f / 60.f); inst.evaluate(p); }
    CHECK(inst.motion("mm")->clip() == m.findClip("Walk"));
    inst.reset(g);
    inst.parameters().set("f1", fx::yawQ(fx::kPi * .3f).rotate({0, 0, 1}));
    inst.parameters().set("f2", fx::yawQ(fx::kPi * .5f).rotate({0, 0, 1}));
    inst.update(1.f / 60.f);
    CHECK(inst.motion("mm")->clip() == m.findClip("TurnLeft"));
    // Without facing features the same request cannot be told apart from idle.
    auto noFacing = AnimationGraph::compile(m, Json::parse(mmGraph(clips, false)));
    inst.reset(noFacing); inst.update(1.f / 60.f);
    CHECK(inst.motion("mm")->clip() != m.findClip("TurnLeft"));

    // One-shot clips clamp at their end: no wrap back to frame 0.
    inst.reset(g); inst.parameters().set("t1", float3{0, 0, .2f}); inst.parameters().set("t2", float3{0, 0, .3f});
    float maxJump = 0; std::vector<mat4> w, pw;
    for (int i = 0; i < 240; ++i) {
        inst.update(1.f / 60.f); inst.evaluate(p); worldMatrices(m, p, w);
        if (i == 0 || i == 60) { inst.parameters().set("t1", float3{}); inst.parameters().set("t2", float3{}); }
        if (!pw.empty()) for (int s = 0; s < 2; ++s) maxJump = std::max(maxJump, dist(fx::posOf(w[size_t(b.foot[s])]), fx::posOf(pw[size_t(b.foot[s])])));
        pw = w;
        const auto* mp = inst.motion("mm");
        if (mp->clip() == m.findClip("Stop")) CHECK(mp->time() <= m.clips[size_t(mp->clip())].duration + 1e-6f);
    }
    // Crossfades keep feet continuous: < 6 cm per 60 Hz frame (walk swing peak is ~4.5 cm).
    CHECK(maxJump < .06f);
    // Legacy string-only databases still loop every clip.
    auto legacy = AnimationGraph::compile(m, Json::parse(mmGraph(R"("Idle","Walk","Stop")", false)));
    CHECK(legacy->nodes()[0].motion->loops(m.findClip("Stop")));
    CHECK(rejects(m, mmGraph(R"("Idle",{"clip":"Missing","loop":false})", false)));
}

// Planted-foot slide while sweeping a walk/run blend at 2.1 m/s (w = 0.5).
float blendSlide(bool sync) {
    fx::HumanoidBones b; Model m = fx::makeHumanoid(&b);
    auto g = AnimationGraph::compile(m, Json::parse(std::string(R"({"parameters":{"w":0.5},"nodes":[
        {"id":"walk","kind":"clip","clip":"Walk_IP"},{"id":"run","kind":"clip","clip":"Run_IP"},
        {"id":"x","kind":"blend","a":"walk","b":"run","parameter":"w")") + (sync ? R"(,"sync":true)" : "") + R"(}],"output":"x"})"));
    AnimationGraphInstance inst; inst.reset(g);
    Pose p; std::vector<mat4> w; float z = 0, slide = 0, contact = 0; float3 prev[2]; bool had[2]{false, false};
    // Host speed = speed the synced blend actually covers: stride-weighted average.
    const float speed = .5f * fx::kWalkSpeed + .5f * fx::kRunSpeed;
    for (int i = 0; i < 240; ++i) {
        inst.update(1.f / 60.f); inst.evaluate(p); worldMatrices(m, p, w); z += speed / 60.f;
        for (int s = 0; s < 2; ++s) {
            float3 a = fx::posOf(w[size_t(b.foot[s])]); a.z += z;
            const bool planted = a.y < fx::kAnkleHeight + .01f;
            if (planted && had[s] && i > 60) { slide += dist(float3{a.x, 0, a.z}, float3{prev[s].x, 0, prev[s].z}); contact += 1.f / 60.f; }
            had[s] = planted; prev[s] = a;
        }
    }
    return contact > 0 ? slide / contact : 1e9f;
}

void locomotionQuality() {
    const float unsynced = blendSlide(false), synced = blendSlide(true);
    std::printf("walk/run blend w=0.5 planted-foot slide: unsynced %.3f m/s, synced %.3f m/s\n", unsynced, synced);
    CHECK(synced < unsynced);
    CHECK(synced < .6f);
}
} // namespace

int main() {
    try {
        sampling(); worldMatrixProduct(); blends(); syncGroups(); twoBoneIK(); rigWorldReuse(); motionMatching(); locomotionQuality();
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
    std::printf("MotionWeave core checks: %d passed\n", checks);
    return 0;
}
