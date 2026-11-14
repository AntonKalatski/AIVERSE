// Per-map overrides of rules blocks (map "rules", RtsMapRules.h, docs/game-format.md § Per-map rules overrides):
// top-down escape / squad tuning, raid keys, progression offerAt + xp table, encounters tuning; merge semantics
// (objects merge, lists replace, null removes) and the errors for blocks / keys a map may not touch.
#include "modules/rts/RtsMapRules.h"

namespace {
std::string withMapRules(const std::string& map, const std::string& patch) { return "{\"rules\":" + patch + "," + map.substr(1); }
bool mapRulesThrow(const std::string& rules, const std::string& map, const char* needle) {
    try { makeSimJson(rules, map); return false; } catch (const std::exception& e) { return std::string(e.what()).find(needle) != std::string::npos; }
}
} // namespace

TEST(map_rules_override_tuning_blocks) {
    // top-down: the map changes the escape cooldown (duration kept: objects merge) and switches squad fire on
    const std::string td = tdRules(R"("squad": {"types": ["mover"], "radius": 2}, "escape": {"duration": 1, "cooldown": 3},)");
    const std::string tdMap = topDownMap(R"({"type":"drone","owner":1,"pos":[4.5,8.5]},{"type":"dummy","owner":2,"pos":[30.5,1.5]})");
    {
        auto sim = makeSimJson(td, withMapRules(tdMap, R"({"topDown": {"escape": {"cooldown": 7}, "squad": {"fireWhileMoving": true}}})"));
        const auto& R = mod(*sim).topDown()->rules();
        CHECK(R.escape.enabled && R.escape.cooldown == Fixed::fromInt(7) && R.escape.duration == Fixed::fromInt(1));
        CHECK(R.squad.fireWhileMoving && R.squad.radius == Fixed::fromInt(2));
        auto off = makeSimJson(td, withMapRules(tdMap, R"({"topDown": {"escape": null}})"));   // null removes the key
        CHECK(!mod(*off).topDown()->rules().escape.enabled);
        auto plain = makeSimJson(td, tdMap);   // no "rules": the game's own values
        CHECK(mod(*plain).topDown()->rules().escape.cooldown == Fixed::fromInt(3));
    }
    // raid: any key of the block
    {
        auto sim = makeSimJson(kRaidRules, withMapRules(raidMap(""), R"({"raid": {"duration": 60, "pickupRadius": 4, "extraction": {"channel": 3}}})"));
        const auto& R = mod(*sim).rules().raid;
        CHECK(R.enabled && R.duration == Fixed::fromInt(60) && R.pickupRadius == Fixed::fromInt(4));
    }
    // progression: offers at the summon circles and a per-map xp table (a list replaces the game's list)
    {
        auto sim = makeSimJson(kProgRules, withMapRules(progMap(""), R"({"progression": {"offerAt": "summonPoints", "xp": {"toNext": [5, 6]}}})"));
        const auto& R = prog(*sim).rules();
        CHECK(R.offerAtCircles && R.defs.xpToNext == std::vector<int64_t>({5, 6}));
        auto plain = makeSimJson(kProgRules, progMap(""));
        CHECK(!prog(*plain).rules().offerAtCircles && prog(*plain).rules().defs.xpToNext.size() == 10);
    }
    // encounters: boss / mob tuning
    {
        auto sim = makeSimJson(kEncRules, withMapRules(encMap(R"({"type":"hero","owner":1,"pos":[5.5,5.5]})"),
            R"({"encounters": {"bosses": {"golem": {"engageRange": 20}}, "mobs": {"crawler": {"aggroRange": 9}}}})"));
        const auto& R = mod(*sim).encounters()->rules();
        CHECK(R.bosses.size() == 1 && R.bosses[0].engage == Fixed::fromInt(20));
    }
    // errors: blocks / keys a map may not touch, wrong shapes, and the block's own validation of the merged result
    CHECK(mapRulesThrow(td, withMapRules(tdMap, R"({"types": {}})"), "map rules.types"));
    CHECK(mapRulesThrow(td, withMapRules(tdMap, R"({"topDown": {"heroes": ["mover"]}})"), "map rules.topDown.heroes"));
    CHECK(mapRulesThrow(td, withMapRules(tdMap, R"([1])"), "map rules"));
    CHECK(mapRulesThrow(td, withMapRules(tdMap, R"({"topDown": 3})"), "map rules.topDown"));
    CHECK(mapRulesThrow(td, withMapRules(tdMap, R"({"topDown": {"escape": {"duration": -1}}})"), "topDown.escape"));
    CHECK(mapRulesThrow(kProgRules, withMapRules(progMap(""), R"({"progression": {"offerAt": "never"}})"), "progression.offerAt"));
    // the helper alone: no "rules" -> null (callers keep their rules)
    CHECK(game::applyMapRules(Json::parse(R"({"a": 1})"), Json::parse(R"({"size": [4, 4]})")).isNull());
}
