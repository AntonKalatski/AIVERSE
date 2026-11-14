// Map-level overrides of rules blocks (RtsMapRules.h). The merge runs before the rules are parsed, so every block
// validates the merged result with its usual errors; this file only guards which blocks a map may touch: tuning
// blocks of per-match systems (top-down controls, raids, progression, encounters), never type tables or anything
// the rest of the rules index (types, resources, abilities) — those change ids and saves.
#include "RtsMapRules.h"
#include <stdexcept>
#include <string>

namespace aiverse::game {

namespace {

struct Allowed { const char* block; std::initializer_list<const char*> keys; };   // keys empty: the whole block
const Allowed kAllowed[] = {
    {"topDown", {"respawn", "move", "aim", "abilities", "squad", "escape", "autoFire", "bot"}},
    {"raid", {}},
    {"progression", {}},
    {"encounters", {}},
};

void merge(Json& into, const Json& patch) {
    for (const auto& [k, v] : patch.members()) {
        if (v.isNull()) { into.erase(k); continue; }
        Json* cur = into.find(k);
        if (v.isObject() && cur && cur->isObject()) merge(*cur, v);
        else into.set(k, v);
    }
}

} // namespace

const char* mapRulesAllowed() {
    return "topDown.{respawn, move, aim, abilities, squad, escape, autoFire, bot}, raid, progression, encounters";
}

Json applyMapRules(const Json& rules, const Json& map) {
    const Json& patch = map["rules"];
    if (patch.isNull()) return Json();
    if (!patch.isObject()) throw std::runtime_error("map rules: expected an object of rules blocks to override");
    Json out = rules;
    for (const auto& [block, v] : patch.members()) {
        const Allowed* a = nullptr;
        for (const auto& x : kAllowed) if (block == x.block) a = &x;
        if (!a) throw std::runtime_error("map rules." + block + ": this block cannot be overridden per map (allowed: " + mapRulesAllowed() + ")");
        if (v.isNull()) { out.erase(block); continue; }
        if (!v.isObject()) throw std::runtime_error("map rules." + block + ": expected an object (or null to drop the block)");
        if (a->keys.size() > 0)
            for (const auto& [k, kv] : v.members()) {
                (void)kv;
                bool ok = false;
                for (const char* key : a->keys) ok |= k == key;
                if (!ok) throw std::runtime_error("map rules." + block + "." + k + ": this key cannot be overridden per map (allowed: " + mapRulesAllowed() + ")");
            }
        Json* cur = out.find(block);
        if (cur && cur->isObject()) merge(*cur, v);
        else if (out.isObject()) out.set(block, v);
    }
    return out;
}

} // namespace aiverse::game
