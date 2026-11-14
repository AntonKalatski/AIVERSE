// Environment data (RtsEnv.h): rules "environment" + map "environment" (map keys win), readable errors.
#include "RtsEnv.h"
#include "RtsInternal.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::game {

using namespace detail;

namespace {

struct Src {
    const Json& rules;
    const Json& map;
    const Json& operator[](const std::string& k) const { return map.has(k) ? map[k] : rules[k]; }
    bool has(const std::string& k) const { return map.has(k) || rules.has(k); }
};

[[noreturn]] void fail(const std::string& where, const std::string& what) { throw std::runtime_error("environment." + where + ": " + what); }

Vec3 pos(const Json& j) { return {j[0].fx(), Fixed{}, j[j.size() >= 3 ? 2 : 1].fx()}; }

std::pair<Fixed, Fixed> interval(const Json& j) {
    if (j.isArray()) return {j[0].fx(), j[1].fx(j[0].fx())};
    if (j.isObject()) return {j.getFx("min"), j.getFx("max", j.getFx("min"))};
    return {j.fx(), j.fx()};
}
} // namespace

void loadEnvironment(EnvDef& d, const Rules& r, const Json& rulesJ, const Json& mapJ) {
    const Json& rj = rulesJ["environment"];
    const Json& mj = mapJ["environment"];
    if (!rj.isObject() && !mj.isObject()) return;
    Src s{rj, mj};
    d.active = true;
    d.scale = s["scale"].fx(Fixed::fromInt(1));
    if (d.scale.raw <= 0) fail("scale", "must be > 0");
    if (const Json& fg = s["fog"]; fg.isObject()) {
        d.fog.memory = fg.getBool("memory", true);
        d.fog.startSize = fg.getFx("startFowSize") / d.scale;
        d.fog.startTime = fg.getFx("startDuration", d.fog.startSize.raw > 0 ? Fixed::fromInt(3) : Fixed{});
    }
    auto D = [&](Fixed v) { return v / d.scale; };   // authored distance -> world
    auto skillRef = [&](const Json& j, const std::string& where) -> int16_t {
        if (j.isNull()) return -1;
        int k = r.findPlayerAbility(j.str());
        if (k < 0) fail(where, "unknown player ability '" + j.str() + "' (rules playerAbilities)");
        return int16_t(k);
    };
    auto statusRef = [&](const Json& j, const std::string& where) -> int16_t {
        if (j.isNull()) return -1;
        int k = r.findStatus(j.str());
        if (k < 0) fail(where, "unknown status '" + j.str() + "'");
        return int16_t(k);
    };

    // ---- weather: "weather": ["rain", "snow", "lightning", "blizzard"] + "season": "winter" + tuning
    const Json& wj = s["weather"];
    auto flag = [&](const char* n) {
        if (wj.isArray()) { for (auto& x : wj.items()) if (x.str() == n) return true; return false; }
        return wj.getBool(n, false);
    };
    EnvWeatherDef& W = d.weather;
    W.rain = flag("rain"); W.snow = flag("snow"); W.lightning = flag("lightning"); W.blizzard = flag("blizzard");
    W.winter = s["season"].str() == "winter";
    const Json& wt = s["weatherParams"];
    W.puddlesFill = wt.getFx("puddlesFillSpeed", W.puddlesFill); W.puddlesDry = wt.getFx("puddlesDrySpeed", W.puddlesDry);
    W.rainWait = wt.getFx("rainWaitTime", W.rainWait); W.noRainWait = wt.getFx("noRainWaitTime", W.noRainWait);
    W.snowRestore = wt.getFx("snowRestoringSpeed", W.snowRestore); W.snowWait = wt.getFx("snowWaitTime", W.snowWait);
    W.snowFillChance = wt.getFx("snowFillRandomFactor", W.snowFillChance);
    if (wt.has("snowFillTimeRange")) { auto iv = interval(wt["snowFillTimeRange"]); W.snowFillMin = iv.first; W.snowFillMax = iv.second; }
    W.blizzardWait = wt.getFx("blizzardWaitTime", W.blizzardWait); W.blizzardFill = wt.getFx("blizzardFillTime", W.blizzardFill);
    W.blizzardKillSpeed = wt.getFx("blizzardUnitKillSpeed", W.blizzardKillSpeed); W.blizzardSpeed = wt.getFx("blizzardUnitSpeedFactor", W.blizzardSpeed);
    if (wt.has("blizzardUnitKillFactor")) { auto iv = interval(wt["blizzardUnitKillFactor"]); W.blizzardResistMin = iv.first; W.blizzardResistMax = iv.second; }
    W.lightningKillTime = wt.getFx("lightningKillTime", W.lightningKillTime);
    W.lightningKillChance = int(wt.getInt("lightningKillFactor", W.lightningKillChance));
    W.lightningRange = D(wt.getFx("lightningKillRange", W.lightningRange * d.scale));
    W.lightningStructureKill = int(wt.getInt("lightningPointKillValue", W.lightningStructureKill));
    W.lightningTime = wt.getFx("lightningTime", W.lightningTime); W.lightningWait = wt.getFx("lightningWaitTime", W.lightningWait);
    if (wt.has("lightningBurn")) {
        const Json& b = wt["lightningBurn"];
        W.burnId = b.getStr("field");
        W.burnValue = int32_t(b.getInt("value", 100));
        W.burnRadius = D(b.getFx("radius", Fixed{}));
    }
    W.rainSpread = fxClamp(wt.getFx("grassRainBurnRandomFactor", W.rainSpread), Fixed{}, Fixed::fromInt(1));
    W.blizzardVar = wt.getStr("blizzardVar", W.blizzardVar);
    if (W.blizzardKillSpeed.raw <= 0 || W.puddlesFill.raw <= 0 || W.puddlesDry.raw <= 0) fail("weatherParams", "speeds and times must be > 0");

    // ---- wind
    const Json& wn = s["wind"];
    if (wn.isObject()) {
        EnvWindDef& N = d.wind;
        N.enabled = wn.getBool("enabled", true);
        N.idleMin = wn.getFx("minIdleDuration", N.idleMin); N.idleMax = wn.getFx("maxIdleDuration", N.idleMax);
        N.blowMin = wn.getFx("minBlowDuration", N.blowMin); N.blowMax = wn.getFx("maxBlowDuration", N.blowMax);
        N.forceMin = D(wn.getFx("minForce", N.forceMin * d.scale)); N.forceMax = D(wn.getFx("maxForce", N.forceMax * d.scale));
    }

    // ---- zones
    for (auto& zj : s["zones"].items()) {
        EnvZone z;
        z.id = zj.getStr("id");
        std::string k = zj.getStr("kind", "other");
        z.tag = k;
        z.kind = k == "water" ? EnvZoneKind::Water : k == "road" ? EnvZoneKind::Road : EnvZoneKind::Other;
        z.slowdown = zj.getFx("slowdown", Fixed::fromInt(1));
        if (z.slowdown.raw <= 0) fail("zones." + z.id, "slowdown must be > 0");
        if (zj.has("circle")) { z.center = pos(zj["circle"]); z.radius = zj["circle"][2].fx(); }
        else if (zj.has("rect")) {
            const Json& rr = zj["rect"];
            Fixed x = rr[0].fx(), y = rr[1].fx(), w = rr[2].fx(), h = rr[3].fx();
            z.poly = {{x, Fixed{}, y}, {x + w, Fixed{}, y}, {x + w, Fixed{}, y + h}, {x, Fixed{}, y + h}};
        } else for (auto& p : zj["poly"].items()) z.poly.push_back(pos(p));
        if (z.radius.raw <= 0 && z.poly.size() < 3) fail("zones." + z.id, "needs \"poly\" (3+ points), \"rect\" or \"circle\"");
        d.zones.push_back(std::move(z));
    }

    // ---- splines (paths, auto skills)
    for (auto& sj : s["splines"].items()) {
        EnvSpline sp;
        sp.id = sj.getStr("id");
        std::vector<Vec3> pts;
        for (auto& p : sj["points"].items()) pts.push_back(pos(p));
        if (pts.size() < 2) fail("splines." + sp.id, "needs 2+ points");
        sp.build(pts, sj.getBool("loop", false));
        sp.skill = skillRef(sj["skill"], "splines." + sp.id + ".skill");
        sp.velocity = D(sj.getFx("velocity", Fixed::fromInt(100) * d.scale));
        sp.emitPeriod = sj.getFx("emitPeriod", sp.emitPeriod);
        sp.firstEmission = sj.getFx("firstEmissionCooldown", sp.firstEmission);
        sp.enabled = sj.getBool("enabled", true);
        d.splines.push_back(std::move(sp));
    }
    // ---- trample fields (grass / snow); the runtime resolves the grid field ids
    for (auto& tj : s["trample"].items()) {
        EnvTrampleDef t;
        t.fieldId = tj.getStr("field");
        if (t.fieldId.empty()) fail("trample", "needs \"field\" (a grid field id)");
        t.perSecond = tj.getFx("perSecond");
        for (auto& row : tj["speed"].items()) t.speed.push_back({row[0].fx(), row[1].fx()});
        t.lerpSpeed = tj.getFx("lerpSpeed", Fixed::fromInt(-1));
        t.restore = tj.getFx("restore");
        t.snowRestore = tj.getBool("snowRestore", false);
        t.killId = tj.getStr("killField");
        t.killAbove = int32_t(tj.getInt("killAbove", 1));
        t.hideStatus = statusRef(tj["hideStatus"], "trample.hideStatus");
        t.hideAbove = tj.getFx("hideAbove");
        t.skipRoads = tj.getBool("skipRoads", true);
        d.trample.push_back(std::move(t));
    }

}

} // namespace aiverse::game
