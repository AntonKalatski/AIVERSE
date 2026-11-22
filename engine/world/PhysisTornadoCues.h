// PHYSIS tornado cues (2026-11-22): pure, device-free planners that turn the
// tornadoes and debris of a NaturePresentationFrame into SpectraFX emitter
// commands (dust funnel, ground skirt, flying debris) and a positional roar,
// plus the built-in effect / sound definitions. Applied by
// PhysisNatureFxHost / PhysisNatureAudioHost; floats, presentation only.
#pragma once
#include "engine/world/PhysisNaturePresentation.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace aiverse::world {

enum class TornadoFxLayer : std::uint8_t { Funnel = 0, Debris = 1 };
enum class TornadoFxOp : std::uint8_t { Start, Move, Stop };

struct TornadoFxCommand {
    TornadoFxOp op = TornadoFxOp::Start;
    TornadoFxLayer layer = TornadoFxLayer::Funnel;
    std::uint64_t key = 0;
    float x = 0, y = 0, z = 0;
    float scale = 1;
    float tint[4] = {1, 1, 1, 1};
};

struct TornadoFxPolicy {
    std::uint32_t maxFunnels = 4;
    std::uint32_t maxDebris = 96;
    std::uint32_t levels = 4;          // intensity quantization (restart on change)
    float funnelScalePerCoreMeter = 1; // built-in funnel: scale 1 = 1 m core
    float minFunnelScale = 0.5f, maxFunnelScale = 60;
};

// Per render frame. `since` = seconds since the frame's tick was committed:
// positions are extrapolated with the last tick's motion so the funnel and
// debris glide instead of stepping at the simulation rate.
class TornadoFxPlanner {
public:
    explicit TornadoFxPlanner(TornadoFxPolicy p = {}) : policy_(p) {}
    std::size_t active(TornadoFxLayer l) const {
        return l == TornadoFxLayer::Funnel ? funnels_.size() : debris_.size();
    }
    std::vector<TornadoFxCommand> clear() {
        std::vector<TornadoFxCommand> out;
        for (const auto& [k, s] : funnels_) out.push_back(stop(TornadoFxLayer::Funnel, k));
        for (const auto& [k, s] : debris_) out.push_back(stop(TornadoFxLayer::Debris, k));
        funnels_.clear(); debris_.clear();
        return out;
    }

    std::vector<TornadoFxCommand> plan(const NaturePresentationFrame& f, float since,
                                       double secondsPerTick) {
        std::vector<TornadoFxCommand> out;
        if (!f.valid) return clear();
        since = std::isfinite(since) ? std::clamp(since, 0.f, 0.5f) : 0.f;
        const float spt = float(std::max(1e-3, secondsPerTick));
        std::map<std::uint64_t, Slot> funnels, debris;
        std::size_t n = 0;
        for (const auto& t : f.tornadoes) {
            if (n++ >= policy_.maxFunnels) break;
            TornadoFxCommand c;
            c.layer = TornadoFxLayer::Funnel; c.key = t.key;
            const float k = since / spt;
            c.x = t.x + t.moveX * k; c.y = t.y; c.z = t.z + t.moveZ * k;
            const auto lv = level(t.intensity);
            const float strength = 0.45f + 0.55f * float(lv) / float(std::max<std::uint32_t>(1, policy_.levels));
            c.scale = std::clamp(t.coreRadius * policy_.funnelScalePerCoreMeter * strength,
                                 policy_.minFunnelScale, policy_.maxFunnelScale);
            // Clockwise vortices are mirrored by tinting nothing: the built-in
            // effect spins counter-clockwise around +Y like StormEvent.
            sync(funnels_, Slot{lv, c.x, c.z}, c, out, true);
            funnels[t.key] = Slot{lv, c.x, c.z};
        }
        n = 0;
        for (const auto& d : f.debris) {
            if (!d.airborne) continue;           // landed pieces stop emitting
            if (n++ >= policy_.maxDebris) break;
            TornadoFxCommand c;
            c.layer = TornadoFxLayer::Debris; c.key = d.key;
            c.x = d.x + d.vx * since; c.y = std::max(0.f, d.y + d.vy * since); c.z = d.z + d.vz * since;
            c.scale = std::clamp(d.size, 0.1f, 2.f);
            sync(debris_, Slot{1, c.x, c.z}, c, out, true);
            debris[d.key] = Slot{1, c.x, c.z};
        }
        for (const auto& [k, s] : funnels_) if (!funnels.count(k)) out.push_back(stop(TornadoFxLayer::Funnel, k));
        for (const auto& [k, s] : debris_) if (!debris.count(k)) out.push_back(stop(TornadoFxLayer::Debris, k));
        funnels_ = std::move(funnels);
        debris_ = std::move(debris);
        return out;
    }

private:
    struct Slot { std::uint32_t level = 0; float x = 0, z = 0; };
    std::uint32_t level(float v) const {
        const auto n = std::max<std::uint32_t>(1, policy_.levels);
        return std::uint32_t(std::clamp(int(std::ceil(v * float(n))), 1, int(n)));
    }
    static TornadoFxCommand stop(TornadoFxLayer l, std::uint64_t key) {
        TornadoFxCommand c; c.op = TornadoFxOp::Stop; c.layer = l; c.key = key; return c;
    }
    static void sync(const std::map<std::uint64_t, Slot>& prev, const Slot& s, TornadoFxCommand c,
                     std::vector<TornadoFxCommand>& out, bool alwaysMove) {
        const auto it = prev.find(c.key);
        if (it == prev.end()) { c.op = TornadoFxOp::Start; out.push_back(c); return; }
        if (it->second.level != s.level) {
            out.push_back(stop(c.layer, c.key));
            c.op = TornadoFxOp::Start; out.push_back(c); return;
        }
        if (alwaysMove || std::abs(it->second.x - s.x) > 0.01f || std::abs(it->second.z - s.z) > 0.01f) {
            c.op = TornadoFxOp::Move; out.push_back(c);
        }
    }
    TornadoFxPolicy policy_;
    std::map<std::uint64_t, Slot> funnels_, debris_;
};

// Built-in funnel: scale 1 = 1 m core radius; the column is ~14 core radii
// tall and widens upward. Local space, so ps.move carries the live particles
// with the vortex. A project overrides it with fx/physis_tornado.json.
inline const char* physisTornadoEffectJson() {
    return R"({
  "emitters": [
    { "texture": "builtin:smoke", "blend": "alpha", "align": "camera", "space": "local", "rate": 110, "loop": true,
      "prewarm": true, "life": [1.8, 2.6], "speed": [0, 0.2], "shape": "disc", "radius": 0.7,
      "size": [0.9, 1.5], "sizeOverLife": [[0, 0.5], [0.3, 1], [1, 2.4]], "rotation": [0, 360], "spin": [-60, 60],
      "velocityOverLife": { "y": { "curve": [[0, 4.5], [1, 6.5]] }, "orbital": [0, 330, 0],
                            "radial": { "curve": [[0, 0.15], [1, 1.4]] }, "space": "local" },
      "noise": { "strength": 0.25, "frequency": 0.6, "scroll": 0.8 },
      "color": [[0, "#6b5d4c00"], [0.12, "#6b5d4cb0"], [0.7, "#7d7266a0"], [1, "#8a827800"]] },
    { "texture": "builtin:smoke", "blend": "alpha", "align": "camera", "space": "local", "rate": 45, "loop": true,
      "prewarm": true, "life": [1.0, 1.8], "speed": [0.8, 1.8], "shape": "ring", "radius": 1.6, "radial": 1,
      "size": [1.2, 2.2], "sizeOverLife": [[0, 0.6], [1, 2.0]], "rotation": [0, 360],
      "velocityOverLife": { "y": 0.6, "orbital": [0, 220, 0], "space": "local" },
      "color": [[0, "#5a4a3800"], [0.2, "#5a4a38a0"], [1, "#6e604f00"]] },
    { "texture": "builtin:soft", "blend": "alpha", "align": "camera", "space": "local", "rate": 26, "loop": true,
      "prewarm": true, "life": [1.6, 2.8], "speed": [0, 0.3], "shape": "disc", "radius": 1.0,
      "size": [0.12, 0.3], "rotation": [0, 360], "spin": [-400, 400],
      "velocityOverLife": { "y": { "curve": [[0, 3], [1, 7]] }, "orbital": [0, 420, 0],
                            "radial": { "curve": [[0, 0.3], [1, 1.8]] }, "space": "local" },
      "color": [[0, "#241c14ff"], [0.85, "#2c2219ff"], [1, "#2c221900"]], "minQuality": "medium" }
  ]
})";
}

// One flying debris piece: a dark tumbling chunk with a short dust trail.
inline const char* physisTornadoDebrisEffectJson() {
    return R"({
  "emitters": [
    { "texture": "builtin:soft", "blend": "alpha", "align": "camera", "space": "local", "loop": true,
      "rate": 4, "maxParticles": 1, "life": [0.6, 0.6], "speed": [0, 0], "size": [0.9, 1.1],
      "rotation": [0, 360], "spin": [-540, 540], "color": [[0, "#2a2018ff"], [1, "#2a2018ff"]] },
    { "texture": "builtin:smoke", "blend": "alpha", "align": "camera", "rate": 10, "loop": true,
      "life": [0.5, 0.9], "speed": [0, 0.3], "size": [0.6, 1.0], "sizeOverLife": [[0, 0.6], [1, 1.8]],
      "color": [[0, "#6a5c4a80"], [1, "#6a5c4a00"]], "minQuality": "medium" }
  ]
})";
}

// ------------------------------------------------------------------ audio

enum class TornadoAudioOp : std::uint8_t { Start, Update, Stop };
struct TornadoAudioCommand {
    TornadoAudioOp op = TornadoAudioOp::Start;
    std::uint64_t key = 0;
    float x = 0, y = 0, z = 0, volume = 0, pitch = 1;
};
struct TornadoAudioPolicy {
    std::uint32_t maxVoices = 2;
    float audibleMeters = 260;
    float volumeSlewPerSecond = 1.2f;
};

class TornadoAudioPlanner {
public:
    explicit TornadoAudioPlanner(TornadoAudioPolicy p = {}) : policy_(p) {}
    std::size_t voices() const { return voices_.size(); }
    std::vector<TornadoAudioCommand> clear() {
        std::vector<TornadoAudioCommand> out;
        for (const auto& [k, v] : voices_) { TornadoAudioCommand c; c.op = TornadoAudioOp::Stop; c.key = k; out.push_back(c); }
        voices_.clear();
        return out;
    }
    std::vector<TornadoAudioCommand> plan(const NaturePresentationFrame& f, float lx, float ly, float lz,
                                          float dt) {
        std::vector<TornadoAudioCommand> out;
        if (!f.valid) return clear();
        dt = std::isfinite(dt) ? std::clamp(dt, 0.f, 0.5f) : 0.f;
        struct Cand { const NatureTornado* t; float d; };
        std::vector<Cand> cands;
        for (const auto& t : f.tornadoes) {
            const float dx = t.x - lx, dy = t.y - ly, dz = t.z - lz;
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d <= policy_.audibleMeters + t.outerRadius) cands.push_back({&t, d});
        }
        std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
            return a.d != b.d ? a.d < b.d : a.t->key < b.t->key;
        });
        if (cands.size() > policy_.maxVoices) cands.resize(policy_.maxVoices);
        std::map<std::uint64_t, float> next;
        const float slew = policy_.volumeSlewPerSecond * dt;
        for (const auto& c : cands) {
            const auto it = voices_.find(c.t->key);
            const bool fresh = it == voices_.end();
            const float target = std::clamp(0.35f + 0.65f * c.t->intensity, 0.f, 1.f);
            const float vol = fresh ? std::min(target, 0.2f)
                                    : it->second + std::clamp(target - it->second, -slew, slew);
            TornadoAudioCommand cmd;
            cmd.op = fresh ? TornadoAudioOp::Start : TornadoAudioOp::Update;
            cmd.key = c.t->key;
            cmd.x = c.t->x; cmd.y = c.t->y + c.t->coreRadius * 2; cmd.z = c.t->z;
            cmd.volume = vol;
            cmd.pitch = 0.75f + 0.35f * std::clamp(c.t->tangentialWind / 60.f, 0.f, 1.f);
            out.push_back(cmd);
            next[c.t->key] = vol;
        }
        for (const auto& [k, v] : voices_)
            if (!next.count(k)) { TornadoAudioCommand c; c.op = TornadoAudioOp::Stop; c.key = k; out.push_back(c); }
        voices_ = std::move(next);
        return out;
    }
private:
    TornadoAudioPolicy policy_;
    std::map<std::uint64_t, float> voices_;
};

inline const char* tornadoSoundName() { return "physis_tornado_loop"; }

// Adds the synthesized roar unless the project ships "physis_tornado_loop".
inline int addPhysisTornadoSounds(Json& bank) {
    if (!bank.isObject()) bank = Json::object();
    Json& sounds = bank.member("sounds");
    if (sounds.has(tornadoSoundName())) return 0;
    sounds.set(tornadoSoundName(), Json::parse(R"({
      "synth": [
        { "wave": "noise", "freq": 220, "duration": 3.2, "attack": 0.0, "release": 0.0, "volume": 0.75, "lowpass": 240 },
        { "wave": "noise", "freq": 220, "duration": 3.2, "attack": 0.0, "release": 0.0, "volume": 0.3, "lowpass": 900, "vibrato": [0.45, 0.5] },
        { "wave": "triangle", "freq": [38, 31], "duration": 3.2, "attack": 0.0, "release": 0.0, "volume": 0.22, "vibrato": [1.7, 0.25] }
      ],
      "loop": true, "bus": "ambient", "volume": 1.0, "maxInstances": 2, "steal": "quietest",
      "minDistance": 12, "maxDistance": 280, "priority": 3
    })"));
    return 1;
}

} // namespace aiverse::world
