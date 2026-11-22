// SpectraFX data-driven entity effects: table parsing/merging and the
// presenter lifecycle (spawn, follow, conditions, cues, death, despawn).
// GPU-free: every effect name is reported as a GPU asset and the queued cues
// are inspected. No RTS/FPS code: hosts are just entity ids + selectors.
#include <cstdio>
#include <string>
#include <vector>
#include "engine/fx/SpectraFxEntityEffects.h"

using namespace aiverse;
using namespace aiverse::fx;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL [spectrafx-entities] %s\n", what); ++failures; }
}
struct Recorder {
    std::vector<SpectraFxCue> cues;
    std::vector<std::pair<uint32_t, std::string>> routes;
    size_t count(SpectraFxCue::Action a) const {
        size_t n = 0;
        for (const auto& c : cues) n += c.action == a;
        return n;
    }
    std::string assetOf(uint32_t id) const {
        for (const auto& [rid, asset] : routes) if (rid == id) return asset;
        return {};
    }
    size_t plays(const std::string& asset) const {
        size_t n = 0;
        for (const auto& c : cues)
            n += c.action == SpectraFxCue::Action::Play && assetOf(c.id) == asset;
        return n;
    }
};
} // namespace

int main() {
    const Json rules = Json::parse(R"JSON({
        "types": {
            "tank": {"spectraFx": {"attach": [{"effect": "exhaust", "offset": [0, 1, -2],
                                               "when": "moving", "direction": "backward"}],
                                   "on": {"fired": "muzzle"}}},
            "hut": {}
        },
        "spectraFx": {
            "entities": {
                "unit": {"attach": [{"effect": "smoke", "when": "damaged", "below": 0.4}],
                         "on": {"spawned": "spawn_flash", "died": "explosion", "fired": "generic_shot"}},
                "type:tank": {"on": {"died": {"effect": "big_explosion", "scale": 2}}}
            }
        }
    })JSON");
    const Json map = Json::parse(R"JSON({
        "spectraFx": {"entities": {"unit": {"on": {"spawned": null}}}}
    })JSON");

    SpectraFxEntityTable table;
    std::string error;
    check(table.readInline(rules["types"], "type:", "id", &error), "inline types parsed");
    check(table.read(rules["spectraFx"], &error), "rules block parsed");
    check(table.read(map["spectraFx"], &error), "map block parsed");
    check(table.size() == 2, "two selector rules (type:tank merged inline + block, unit)");
    {
        SpectraFxEntityTable bad;
        std::string why;
        check(!bad.read(Json::parse(R"({"entities":{"x":{"attach":[{"effect":"a","when":"sometimes"}]}}})"), &why) &&
              why.find("when") != std::string::npos, "invalid condition rejected with reason");
        check(!bad.read(Json::parse(R"({"entitys":{}})"), &why), "unknown block field rejected");
    }

    ParticleSystem cpu; // not initialized: every name is a GPU asset here
    Recorder rec;
    SpectraFxEntityEffects fx;
    SpectraFxEntityEffects::Backend backend;
    backend.cpu = &cpu;
    backend.isGpuAsset = [](const std::string&) { return true; };
    backend.routeGpu = [&](uint32_t id, const std::string& asset, float) { rec.routes.push_back({id, asset}); };
    fx.configure(std::move(table), std::move(backend));
    check(fx.enabled(), "presenter enabled");

    const int tank = fx.rule({"*", "unit", "type:tank"});
    const int hut = fx.rule({"*", "structure", "type:hut"});
    const auto& r = fx.table().resolved(tank);
    check(r.attach.size() == 2, "tank: unit smoke + inline exhaust");
    check(r.on.count("spawned") == 0, "map null switched spawned off");
    check(r.on.at("died").effect == "big_explosion" && r.on.at("died").scale == 2,
          "specific selector overrides died");
    check(r.on.at("fired").effect == "muzzle", "more specific selector (type:tank) wins over unit");
    auto flush = [&] { fx.flushGpu([&](const SpectraFxCue& c) { rec.cues.push_back(c); }); };

    const float dt = 1.0f / 30;
    SpectraFxEntityEffects::Pose pose{{0, 0, 0}, {0, 0, 1}};
    // Frame 1: tank appears, idle, healthy -> nothing attached.
    fx.beginFrame(dt); fx.entity(7, tank, pose, true, 1.0f); fx.entity(8, hut, pose); fx.endFrame(); flush();
    check(rec.cues.empty(), "idle healthy tank: no effects");
    // Frames 2..10: tank moves at 3 m/s -> exhaust starts once and follows.
    for (int i = 1; i <= 9; ++i) {
        pose.position.z = 0.1f * float(i);
        fx.beginFrame(dt); fx.entity(7, tank, pose, true, 1.0f); fx.entity(8, hut, pose); fx.endFrame(); flush();
    }
    check(rec.plays("exhaust") == 1, "exhaust started once while moving");
    check(rec.count(SpectraFxCue::Action::Move) >= 5, "exhaust follows the tank (Move cues)");
    bool behind = false;
    for (const auto& c : rec.cues)
        if (c.action == SpectraFxCue::Action::Play && rec.assetOf(c.id) == "exhaust")
            behind = c.position.z < pose.position.z && c.position.y > 0.9f && c.direction.z < -0.9f;
    check(behind, "offset is in the entity frame (behind, raised, emitting backward)");
    // Stops moving -> exhaust stops (fade, Stop not Kill).
    for (int i = 0; i < 12; ++i) { fx.beginFrame(dt); fx.entity(7, tank, pose, true, 1.0f); fx.entity(8, hut, pose); fx.endFrame(); flush(); }
    check(rec.count(SpectraFxCue::Action::Stop) == 1, "exhaust stops when the tank halts");
    // Damage under 40 % -> smoke; fired cue -> one-shot (key 0).
    fx.beginFrame(dt); fx.entity(7, tank, pose, true, 0.3f); fx.entity(8, hut, pose);
    fx.cue(7, "fired"); fx.cue(7, "unbound_cue"); fx.endFrame(); flush();
    check(rec.plays("smoke") == 1, "damaged smoke starts under 'below'");
    check(rec.plays("muzzle") == 1 && rec.plays("generic_shot") == 0, "fired cue plays the type's muzzle once");
    bool oneShotKey = false;
    for (const auto& c : rec.cues)
        if (c.action == SpectraFxCue::Action::Play && (rec.assetOf(c.id) == "muzzle" || rec.assetOf(c.id) == "generic_shot"))
            oneShotKey = c.key == 0;
    check(oneShotKey, "cue effects are one-shots (key 0)");
    // Death: died cue stops attachments, plays big_explosion once, corpse stays known.
    fx.beginFrame(dt); fx.cue(7, "died"); fx.entity(7, tank, pose, false, 0.0f); fx.entity(8, hut, pose); fx.endFrame(); flush();
    check(rec.plays("big_explosion") == 1, "death plays the type's died effect once");
    check(rec.count(SpectraFxCue::Action::Stop) == 2, "smoke stopped at death");
    fx.beginFrame(dt); fx.entity(7, tank, pose, false, 0.0f); fx.entity(8, hut, pose); fx.endFrame(); flush();
    check(rec.plays("big_explosion") == 1, "no second death effect for the corpse");
    // Corpse removed: dead entities do not fire despawned; hut despawns.
    fx.beginFrame(dt); fx.endFrame(); flush();
    check(!fx.tracked(7) && !fx.tracked(8), "entities forgotten when no longer presented");
    check(fx.stats().died == 1 && fx.stats().despawned == 1, "one death, one despawn");
    // Distinct asset routes: one id per (asset, seconds).
    check(rec.routes.size() == 4, "routes registered once per effect");
    std::printf("[spectrafx-entities] cues=%zu routes=%zu gpuPlays=%llu errors=%d\n",
                rec.cues.size(), rec.routes.size(),
                static_cast<unsigned long long>(fx.stats().gpuPlays), failures);
    return failures ? 1 : 0;
}
