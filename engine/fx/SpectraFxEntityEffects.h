// SpectraFX data-driven entity effects: ONE mechanism for every host.
//
// Effects are declared in data (rules.json / map.json "spectraFx" block, or
// inline "spectraFx" objects on rules entries such as unit types, weapons or
// abilities). They spawn with an entity, follow it, react to its cues and die
// with it. Hosts only enumerate their presented entities each frame with a
// list of selector strings and forward named cues; the presenter owns every
// effect instance. No genre knowledge here: RTS clients, FPS clients and any
// other host use the same table and presenter (docs/spectrafx-entity-effects.md).
//
//   "spectraFx": {
//     "entities": {
//       "type:tank": {
//         "attach": [ {"effect": "engine_smoke", "offset": [0, 0.8, -0.6],
//                      "when": "moving", "direction": "backward"} ],
//         "on": { "spawned": "spawn_flash", "died": {"effect": "explosion", "scale": 1.4},
//                 "fired": "muzzle_flash" }
//       },
//       "unit": { "attach": [ {"effect": "smoke_damaged", "when": "damaged", "below": 0.4} ] }
//     }
//   }
//
// Effect names resolve to a SpectraFX GPU asset fx/gpu/<name>.gpu.json when it
// exists (GPU compute or the CPU twin, see SpectraFxHybridEmitter), otherwise
// to a CPU effect fx/<name>.json of ParticleSystem.
//
// Presentation only: reads poses, never writes gameplay state.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include "../data/Json.h"
#include "ParticleSystem.h"
#include "SpectraFxCueBridge.h"

namespace aiverse::fx {

struct SpectraFxEffectRef {
    enum class When : uint8_t { Always, Moving, Idle, Damaged };
    enum class Axis : uint8_t { Up, Forward, Backward };
    std::string effect;
    render::float3 offset{};   // x right, y up, z forward of the entity heading
    float scale = 1;
    When when = When::Always;
    float below = 0.5f;        // Damaged: health fraction under which it plays
    Axis axis = Axis::Up;      // emission axis
    bool kill = false;         // on stop: remove particles instead of fading
    float seconds = 3;         // GPU one-shot ownership (cues)
};

class SpectraFxEntityTable {
public:
    struct Rule {
        std::vector<SpectraFxEffectRef> attach;
        // cue -> effect; nullopt = explicitly switched off (null in data)
        std::map<std::string, std::optional<SpectraFxEffectRef>> on;
        bool attachSet = false;
    };
    struct Resolved {
        std::vector<SpectraFxEffectRef> attach;
        std::unordered_map<std::string, SpectraFxEffectRef> on;
    };

    // `block` = the "spectraFx" object. Later reads override earlier ones
    // selector by selector (rules first, then map).
    bool read(const Json& block, std::string* error = nullptr) {
        if (block.isNull()) return true;
        if (!block.isObject()) return fail(error, "spectraFx must be an object");
        for (const auto& member : block.members())
            if (member.first != "entities" && member.first != "version")
                return fail(error, "spectraFx: unknown field " + member.first);
        const Json& entities = block["entities"];
        if (entities.isNull()) return true;
        if (!entities.isObject()) return fail(error, "spectraFx.entities must be an object");
        for (const auto& [selector, body] : entities.members())
            if (!readRule(selector, body, error)) return false;
        return true;
    }
    // Inline declarations on a data list: every element (array) or member
    // (object) with a "spectraFx" object becomes the rule of selector
    // prefix + element[idKey] (or the member key). Example: rules "types"
    // with prefix "type:" and id "id".
    bool readInline(const Json& list, const std::string& prefix,
                    const char* idKey = "id", std::string* error = nullptr) {
        size_t index = 0;
        auto one = [&](const std::string& key, const Json& item) {
            if (!item.isObject() || item["spectraFx"].isNull()) return true;
            return readRule(prefix + key, item["spectraFx"], error);
        };
        if (list.isArray()) {
            for (const auto& item : list.items()) {
                std::string key = item.isObject() ? item.getStr(idKey, "") : std::string{};
                if (key.empty()) key = std::to_string(index);
                if (!one(key, item)) return false;
                ++index;
            }
        } else if (list.isObject()) {
            for (const auto& [key, item] : list.members())
                if (!one(key, item)) return false;
        }
        return true;
    }
    bool empty() const { return rules_.empty(); }
    size_t size() const { return rules_.size(); }
    // Every effect name mentioned (hosts may preload / probe GPU assets).
    std::vector<std::string> effectNames() const {
        std::vector<std::string> out;
        for (const auto& [_, rule] : rules_) {
            for (const auto& ref : rule.attach) out.push_back(ref.effect);
            for (const auto& [__, ref] : rule.on) if (ref) out.push_back(ref->effect);
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }
    // Merge every matching selector, general first: attach lists add up,
    // later cue bindings override earlier ones (null removes).
    int resolve(const std::vector<std::string>& selectors) {
        std::string key;
        for (const auto& s : selectors) { key += s; key += '\n'; }
        const auto found = cache_.find(key);
        if (found != cache_.end()) return found->second;
        Resolved out;
        for (const auto& s : selectors) {
            const auto it = rules_.find(s);
            if (it == rules_.end()) continue;
            for (const auto& ref : it->second.attach) out.attach.push_back(ref);
            for (const auto& [cue, ref] : it->second.on) {
                if (ref) out.on[cue] = *ref;
                else out.on.erase(cue);
            }
        }
        resolved_.push_back(std::move(out));
        const int index = int(resolved_.size() - 1);
        cache_.emplace(std::move(key), index);
        return index;
    }
    const Resolved& resolved(int index) const { return resolved_.at(size_t(index)); }

private:
    static bool fail(std::string* error, const std::string& why) {
        if (error) *error = why;
        return false;
    }
    static bool readVec3(const Json& v, render::float3& out) {
        if (v.isNull()) return true;
        if (!v.isArray() || v.size() != 3) return false;
        for (size_t i = 0; i < 3; ++i) if (!v[i].isNumber()) return false;
        out = {float(v[0].toDouble()), float(v[1].toDouble()), float(v[2].toDouble())};
        return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
    }
    static bool readRef(const Json& v, SpectraFxEffectRef& ref, std::string* error,
                        const std::string& where) {
        if (v.isString()) { ref.effect = v.str(); }
        else if (v.isObject()) {
            for (const auto& member : v.members()) {
                const auto& k = member.first;
                if (k != "effect" && k != "offset" && k != "scale" && k != "when" &&
                    k != "below" && k != "direction" && k != "kill" && k != "seconds")
                    return fail(error, where + ": unknown field " + k);
            }
            ref.effect = v.getStr("effect", "");
            if (!readVec3(v["offset"], ref.offset)) return fail(error, where + ": offset must be [x,y,z]");
            ref.scale = float(v["scale"].isNumber() ? v["scale"].toDouble() : 1.0);
            ref.below = float(v["below"].isNumber() ? v["below"].toDouble() : 0.5);
            ref.seconds = float(v["seconds"].isNumber() ? v["seconds"].toDouble() : 3.0);
            ref.kill = v.getBool("kill", false);
            const auto when = v.getStr("when", "always");
            if (when == "always") ref.when = SpectraFxEffectRef::When::Always;
            else if (when == "moving") ref.when = SpectraFxEffectRef::When::Moving;
            else if (when == "idle") ref.when = SpectraFxEffectRef::When::Idle;
            else if (when == "damaged") ref.when = SpectraFxEffectRef::When::Damaged;
            else return fail(error, where + ": when must be always, moving, idle or damaged");
            const auto axis = v.getStr("direction", "up");
            if (axis == "up") ref.axis = SpectraFxEffectRef::Axis::Up;
            else if (axis == "forward") ref.axis = SpectraFxEffectRef::Axis::Forward;
            else if (axis == "backward") ref.axis = SpectraFxEffectRef::Axis::Backward;
            else return fail(error, where + ": direction must be up, forward or backward");
        } else return fail(error, where + ": effect must be a name or an object");
        if (ref.effect.empty() || ref.effect.size() > 128)
            return fail(error, where + ": effect name missing");
        if (!std::isfinite(ref.scale) || ref.scale <= 0 || ref.scale > 100 ||
            !std::isfinite(ref.below) || !std::isfinite(ref.seconds) ||
            ref.seconds <= 0 || ref.seconds > 60)
            return fail(error, where + ": scale/below/seconds out of range");
        return true;
    }
    bool readRule(const std::string& selector, const Json& body, std::string* error) {
        const std::string where = "spectraFx." + selector;
        if (!body.isObject()) return fail(error, where + " must be an object");
        Rule& rule = rules_[selector];
        for (const auto& member : body.members())
            if (member.first != "attach" && member.first != "on")
                return fail(error, where + ": unknown field " + member.first);
        if (body.has("attach")) {
            const Json& list = body["attach"];
            rule.attach.clear();
            rule.attachSet = true;
            auto add = [&](const Json& item) {
                SpectraFxEffectRef ref;
                if (!readRef(item, ref, error, where + ".attach")) return false;
                rule.attach.push_back(std::move(ref));
                return true;
            };
            if (list.isArray()) { for (const auto& item : list.items()) if (!add(item)) return false; }
            else if (!list.isNull() && !add(list)) return false;
        }
        if (body.has("on")) {
            const Json& on = body["on"];
            if (!on.isObject()) return fail(error, where + ".on must be an object");
            for (const auto& [cue, value] : on.members()) {
                if (value.isNull()) { rule.on[cue] = std::nullopt; continue; }
                SpectraFxEffectRef ref;
                if (!readRef(value, ref, error, where + ".on." + cue)) return false;
                rule.on[cue] = std::move(ref);
            }
        }
        cache_.clear();
        resolved_.clear();
        return true;
    }
    std::map<std::string, Rule> rules_;
    std::unordered_map<std::string, int> cache_;
    std::vector<Resolved> resolved_;
};

// Per-frame presenter. Host order each frame (render thread):
//   beginFrame(dt); entity(...) for every presented entity; cue(...) for
//   events; endFrame(); then at the host's GPU safe point flushGpu(...).
class SpectraFxEntityEffects {
public:
    struct Pose {
        render::float3 position{};
        render::float3 forward{0, 0, 1};
    };
    struct Backend {
        ParticleSystem* cpu = nullptr;
        // True when fx/gpu/<name>.gpu.json is a valid SpectraFX GPU asset.
        std::function<bool(const std::string&)> isGpuAsset;
        // Register cue id -> GPU asset (HybridPlayback::route).
        std::function<void(uint32_t id, const std::string& asset, float oneShotSeconds)> routeGpu;
    };
    struct Stats {
        uint64_t spawned = 0, died = 0, despawned = 0, cues = 0;
        uint64_t attachedStarted = 0, attachedStopped = 0;
        uint64_t cpuPlays = 0, gpuPlays = 0, missingEffects = 0;
        size_t entities = 0, liveAttachments = 0;
    };
    static constexpr uint32_t kRouteBase = 0x5F000000u;

    void configure(SpectraFxEntityTable table, Backend backend) {
        reset(true);
        table_ = std::move(table);
        backend_ = std::move(backend);
        kinds_.clear();
        routes_.clear();
    }
    bool enabled() const { return !table_.empty() && backend_.cpu; }
    SpectraFxEntityTable& table() { return table_; }
    int rule(const std::vector<std::string>& selectors) { return table_.resolve(selectors); }

    void beginFrame(float dt) {
        dt_ = std::isfinite(dt) && dt > 0 ? std::min(dt, 0.25f) : 0.0f;
        ++frame_;
    }
    // health: 0..1, negative = unknown. alive=false keeps the entity known
    // (corpse) but stops its attachments and fires "died" once.
    void entity(uint64_t id, int rule, const Pose& pose, bool alive = true,
                float health = -1) {
        if (!enabled() || rule < 0) return;
        auto [it, inserted] = entities_.try_emplace(id);
        Entity& e = it->second;
        const bool sameRule = !inserted && e.rule == rule;
        if (!inserted && !sameRule) stopAll(e, true);
        const auto& resolved = table_.resolved(rule);
        if (inserted || !sameRule) {
            e.rule = rule;
            e.attached.assign(resolved.attach.size(), {});
            e.alive = alive;
            e.speed = 0;
            e.pose = pose;
            if (inserted && alive) { play(resolved, "spawned", pose); ++stats_.spawned; }
        } else if (dt_ > 0) {
            const render::float3 d = pose.position - e.pose.position;
            const float v = render::length(d) / dt_;
            e.speed = std::isfinite(v) ? e.speed * 0.6f + v * 0.4f : 0.0f;
        }
        e.pose = pose;
        e.health = health;
        e.seen = frame_;
        if (e.alive && !alive) kill(e, pose);
        e.alive = alive;
        for (size_t i = 0; i < resolved.attach.size(); ++i)
            updateAttachment(e, resolved.attach[i], e.attached[i]);
    }
    // Named cue on a presented entity ("fired", "hit", "ability:heal" ...).
    // Unknown cue names without a binding are ignored. "died" marks the
    // entity dead (stops attachments) even when it vanishes this frame.
    void cue(uint64_t id, std::string_view name, const Pose& pose) {
        if (!enabled()) return;
        const auto it = entities_.find(id);
        if (it == entities_.end()) return;
        Entity& e = it->second;
        if (name == "died") {
            if (e.alive) kill(e, pose);
            e.alive = false;
            return;
        }
        ++stats_.cues;
        play(table_.resolved(e.rule), std::string(name), pose);
    }
    // Same, at the entity's last presented pose.
    void cue(uint64_t id, std::string_view name) {
        const auto it = entities_.find(id);
        if (it != entities_.end()) cue(id, name, it->second.pose);
    }
    // Pose of a presented entity (false when unknown).
    bool pose(uint64_t id, Pose& out) const {
        const auto it = entities_.find(id);
        if (it == entities_.end()) return false;
        out = it->second.pose;
        return true;
    }
    // Cue without an entity (impact at a point) resolved by selectors.
    void cueAt(int rule, std::string_view name, const Pose& pose) {
        if (!enabled() || rule < 0) return;
        ++stats_.cues;
        play(table_.resolved(rule), std::string(name), pose);
    }
    // Entities not reported since beginFrame left the scene: "despawned"
    // (if still alive), attachments stop.
    void endFrame() {
        for (auto it = entities_.begin(); it != entities_.end();) {
            if (it->second.seen == frame_) { ++it; continue; }
            Entity& e = it->second;
            if (e.alive) {
                play(table_.resolved(e.rule), "despawned", e.pose);
                ++stats_.despawned;
            }
            stopAll(e, false);
            it = entities_.erase(it);
        }
        stats_.entities = entities_.size();
        size_t live = 0;
        for (const auto& [_, e] : entities_)
            for (const auto& a : e.attached) live += a.active ? 1 : 0;
        stats_.liveAttachments = live;
    }
    // GPU cues queued since the last flush, for the host's safe point
    // (HybridPlayback::dispatch inside beginFrame, outside passes).
    template<class Dispatch>
    void flushGpu(Dispatch&& dispatch) {
        for (const auto& cue : pendingGpu_) dispatch(cue);
        pendingGpu_.clear();
    }
    size_t pendingGpu() const { return pendingGpu_.size(); }
    // New match / rollback / floating origin: forget entities. kill=true
    // removes CPU particles at once; GPU instances get Kill cues.
    void reset(bool killParticles = true) {
        for (auto& [_, e] : entities_) stopAll(e, killParticles);
        entities_.clear();
    }
    const Stats& stats() const { return stats_; }
    bool tracked(uint64_t id) const { return entities_.count(id) != 0; }

private:
    struct Instance {
        bool active = false, gpu = false;
        FxHandle cpu{};
        uint64_t key = 0;
    };
    struct Entity {
        int rule = -1;
        Pose pose;
        float speed = 0, health = -1;
        bool alive = true;
        uint64_t seen = 0;
        std::vector<Instance> attached;
    };
    enum class Kind : uint8_t { Unknown, Gpu, Cpu, Missing };

    static render::float3 flat(render::float3 f) {
        f.y = 0;
        const float l = render::length(f);
        return l > 1e-5f ? f * (1.0f / l) : render::float3{0, 0, 1};
    }
    static render::float3 place(const Pose& pose, const SpectraFxEffectRef& ref) {
        const render::float3 f = flat(pose.forward);
        const render::float3 right{-f.z, 0, f.x};
        return pose.position + right * ref.offset.x +
               render::float3{0, ref.offset.y, 0} + f * ref.offset.z;
    }
    static render::float3 axis(const Pose& pose, const SpectraFxEffectRef& ref) {
        const render::float3 f = render::normalize(pose.forward);
        switch (ref.axis) {
        case SpectraFxEffectRef::Axis::Forward: return f;
        case SpectraFxEffectRef::Axis::Backward: return f * -1.0f;
        default: return {0, 1, 0};
        }
    }
    Kind kind(const std::string& name) {
        const auto it = kinds_.find(name);
        if (it != kinds_.end()) return it->second;
        Kind k = Kind::Missing;
        if (backend_.isGpuAsset && backend_.routeGpu && backend_.isGpuAsset(name)) k = Kind::Gpu;
        else if (backend_.cpu && backend_.cpu->effect(name) >= 0) k = Kind::Cpu;
        kinds_[name] = k;
        return k;
    }
    uint32_t routeFor(const SpectraFxEffectRef& ref) {
        const std::string key = ref.effect + "#" + std::to_string(int(ref.seconds * 100));
        const auto it = routes_.find(key);
        if (it != routes_.end()) return it->second;
        const uint32_t id = kRouteBase + uint32_t(routes_.size());
        routes_[key] = id;
        backend_.routeGpu(id, ref.effect, ref.seconds);
        return id;
    }
    Instance start(const SpectraFxEffectRef& ref, const Pose& pose, bool keyed) {
        Instance inst;
        const Kind k = kind(ref.effect);
        if (k == Kind::Missing) { ++stats_.missingEffects; return inst; }
        const render::float3 at = place(pose, ref), dir = axis(pose, ref);
        if (k == Kind::Gpu) {
            SpectraFxCue cue;
            cue.id = routeFor(ref);
            cue.action = SpectraFxCue::Action::Play;
            cue.key = keyed ? ++nextKey_ : 0;
            cue.position = at;
            cue.direction = dir;
            cue.scale = ref.scale;
            pendingGpu_.push_back(cue);
            inst.active = true;
            inst.gpu = true;
            inst.key = cue.key;
            ++stats_.gpuPlays;
            return inst;
        }
        inst.cpu = backend_.cpu->play(ref.effect, at, dir, ref.scale);
        inst.active = inst.cpu.valid();
        if (inst.active) ++stats_.cpuPlays;
        return inst;
    }
    void move(Instance& inst, const SpectraFxEffectRef& ref, const Pose& pose) {
        const render::float3 at = place(pose, ref), dir = axis(pose, ref);
        if (inst.gpu) {
            SpectraFxCue cue;
            cue.action = SpectraFxCue::Action::Move;
            cue.key = inst.key;
            cue.position = at;
            cue.direction = dir;
            pendingGpu_.push_back(cue);
        } else backend_.cpu->move(inst.cpu, at, dir);
    }
    void stop(Instance& inst, bool killParticles) {
        if (!inst.active) return;
        if (inst.gpu) {
            SpectraFxCue cue;
            cue.action = killParticles ? SpectraFxCue::Action::Kill : SpectraFxCue::Action::Stop;
            cue.key = inst.key;
            pendingGpu_.push_back(cue);
        } else if (backend_.cpu) {
            if (killParticles) backend_.cpu->kill(inst.cpu);
            else backend_.cpu->stop(inst.cpu);
        }
        inst = {};
        ++stats_.attachedStopped;
    }
    void stopAll(Entity& e, bool killParticles) {
        if (e.rule < 0) return;
        const auto& resolved = table_.resolved(e.rule);
        for (size_t i = 0; i < e.attached.size(); ++i)
            stop(e.attached[i], killParticles || (i < resolved.attach.size() && resolved.attach[i].kill));
    }
    void kill(Entity& e, const Pose& pose) {
        stopAll(e, false);
        play(table_.resolved(e.rule), "died", pose);
        ++stats_.died;
    }
    bool wanted(const Entity& e, const SpectraFxEffectRef& ref) const {
        if (!e.alive) return false;
        constexpr float kMoving = 0.25f; // m/s, smoothed
        switch (ref.when) {
        case SpectraFxEffectRef::When::Moving: return e.speed > kMoving;
        case SpectraFxEffectRef::When::Idle: return e.speed <= kMoving;
        case SpectraFxEffectRef::When::Damaged: return e.health >= 0 && e.health < ref.below;
        default: return true;
        }
    }
    void updateAttachment(Entity& e, const SpectraFxEffectRef& ref, Instance& inst) {
        const bool want = wanted(e, ref);
        if (want && !inst.active) {
            inst = start(ref, e.pose, true);
            if (inst.active) ++stats_.attachedStarted;
        } else if (want) move(inst, ref, e.pose);
        else if (inst.active) stop(inst, ref.kill);
    }
    void play(const SpectraFxEntityTable::Resolved& resolved, const std::string& cue,
              const Pose& pose) {
        const auto it = resolved.on.find(cue);
        if (it == resolved.on.end()) return;
        start(it->second, pose, false);
    }

    SpectraFxEntityTable table_;
    Backend backend_;
    std::unordered_map<uint64_t, Entity> entities_;
    std::unordered_map<std::string, Kind> kinds_;
    std::unordered_map<std::string, uint32_t> routes_;
    std::vector<SpectraFxCue> pendingGpu_;
    uint64_t frame_ = 0, nextKey_ = 0x5F00000000000000ull;
    float dt_ = 0;
    Stats stats_{};
};

} // namespace aiverse::fx
