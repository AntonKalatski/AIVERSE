// Field operations (RtsFieldOps.h): repair / reclaim / assist / patrol work, self-destruct, stockpiles and
// interceptors, ambient income (wind, tide), aircraft attack runs, repair pads.
#include "RtsFieldOps.h"
#include <algorithm>
#include <stdexcept>
#include "../../engine/core/Archive.h"
#include "../../engine/core/Hash.h"
#include "RtsInternal.h"

namespace aiverse::game {
using namespace detail;

namespace {
[[noreturn]] void bad(const std::string& where, const std::string& what) { throw std::runtime_error(where + ": " + what); }

void amounts(const Rules& R, const std::string& where, const Json& j, std::vector<Amount>& out) {
    if (j.isNull()) return;
    if (!j.isObject()) bad(where, "expected an object {\"resource\": amount}");
    for (const auto& [id, v] : j.members()) {
        const int r = R.findResource(id);
        if (r < 0) bad(where, "unknown resource '" + id + "'");
        out.push_back({uint16_t(r), v.fx()});
    }
}

template <class V> auto findEntry(V& list, Entity e) {
    auto it = std::lower_bound(list.begin(), list.end(), e.index, [](const auto& x, uint32_t i) { return x.e.index < i; });
    return it != list.end() && it->e == e ? it : list.end();
}
template <class V, class T> T& insertEntry(V& list, T entry) {
    auto it = std::lower_bound(list.begin(), list.end(), entry.e.index, [](const auto& x, uint32_t i) { return x.e.index < i; });
    if (it != list.end() && it->e.index == entry.e.index) { *it = entry; return *it; }
    return *list.insert(it, entry);
}
template <class V> void dropDead(World& w, V& list) {
    list.erase(std::remove_if(list.begin(), list.end(), [&](const auto& x) { return !w.alive(x.e); }), list.end());
}
} // namespace

FieldOpsStore* FieldOps::store(Simulation& sim) const { return enabled_ ? sim.world().resource<FieldOpsStore>() : nullptr; }

void FieldOps::setup(Simulation& sim, RtsModule& m, const Json& rulesJson, const Json& map) {
    m_ = &m;
    const Rules& R = m.rules();
    types_.assign(R.types.size(), {});
    bool any = false;
    if (const Json& j = rulesJson["repair"]; !j.isNull()) {
        if (!j.isObject()) bad("rules repair", "expected an object {\"cost\": 0, \"units\": true}");
        repairCost_ = fxMax(Fixed{}, j.getFx("cost", repairCost_));
        repairUnits_ = j.getBool("units", false);
        any = true;
    }
    if (const Json& j = rulesJson["reclaim"]; !j.isNull()) {
        if (!j.isObject()) bad("rules reclaim", "expected an object {\"units\": true, \"refund\": 1}");
        reclaimUnits_ = j.getBool("units", true);
        reclaimRefund_ = fxMax(Fixed{}, j.getFx("refund", reclaimRefund_));
        any = true;
    }
    if (const Json& j = rulesJson["selfDestruct"]; !j.isNull()) {
        if (j.isObject()) selfDestructTime_ = j.getFx("time", selfDestructTime_);
        selfDestructOn_ = j.isObject() || j.boolean(false);
        if (selfDestructTime_.raw < 0) bad("rules selfDestruct", "time must be >= 0");
        any = any || selfDestructOn_;
    }
    // ambient: rules block, map block merged over it key by key
    for (const Json* aj : {&rulesJson["ambient"], &map["ambient"]}) {
        if (aj->isNull()) continue;
        if (!aj->isObject()) bad("ambient", "expected an object {\"wind\": {...}, \"tidal\": n}");
        if (const Json& wj = (*aj)["wind"]; !wj.isNull()) {
            if (!wj.isObject()) bad("ambient wind", "expected an object {\"min\", \"max\", \"every\": [a, b]}");
            ambient_.wind = true;
            ambient_.windMin = wj.getFx("min", ambient_.windMin);
            ambient_.windMax = wj.getFx("max", ambient_.windMax);
            if (const Json& ev = wj["every"]; ev.isArray() && ev.size() == 2) { ambient_.everyMin = ev[size_t(0)].fx(); ambient_.everyMax = ev[size_t(1)].fx(); }
            else if (ev.isNumber()) ambient_.everyMin = ambient_.everyMax = ev.fx();
            if (ambient_.windMax < ambient_.windMin || ambient_.everyMin.raw <= 0 || ambient_.everyMax < ambient_.everyMin)
                bad("ambient wind", "needs min <= max and 0 < every[0] <= every[1]");
        }
        ambient_.tidal = aj->getFx("tidal", ambient_.tidal);
        any = true;
    }
    for (const auto& [id, tj] : rulesJson["types"].members()) {
        if (!tj.isObject()) continue;
        const int ti = R.findType(id);
        if (ti < 0) continue;
        FieldOpsTypeDef& d = types_[size_t(ti)];
        const UnitType& ut = R.types[size_t(ti)];
        const std::string where = "type '" + id + "'";
        d.assist = tj.getBool("assist", false);
        d.patrolWork = tj.getBool("patrolWork", false);
        if (const Json& sj = tj["stockpile"]; !sj.isNull()) {
            if (!sj.isObject()) bad(where + " stockpile", "expected an object {\"max\", \"time\", \"cost\", \"abilities\"}");
            d.stockpile.max = int(sj.getInt("max", 1));
            d.stockpile.time = sj.getFx("time", d.stockpile.time);
            if (d.stockpile.max <= 0 || d.stockpile.time.raw <= 0) bad(where + " stockpile", "max and time must be > 0");
            amounts(R, where + " stockpile cost", sj["cost"], d.stockpile.cost);
            if (const Json& ab = sj["abilities"]; ab.isArray()) {
                d.stockpile.abilities = 0;
                for (const Json& a : ab.items()) {
                    int k = -1;
                    for (size_t i = 0; i < ut.abilities.size() && i < 8; ++i) if (ut.abilities[i].id == a.str()) k = int(i);
                    if (k < 0) bad(where + " stockpile abilities", "unknown ability '" + a.str() + "' of this type");
                    d.stockpile.abilities |= uint8_t(1u << k);
                }
            }
            anyStock_ = true;
        }
        if (const Json& ij = tj["intercept"]; !ij.isNull()) {
            if (!ij.isObject()) bad(where + " intercept", "expected an object {\"radius\", \"tags\": [...]}");
            d.intercept.radius = ij.getFx("radius");
            if (d.intercept.radius.raw <= 0) bad(where + " intercept", "radius must be > 0");
            for (const Json& tg : ij["tags"].items()) {
                const uint64_t bit = R.tagMask(tg.str());
                if (!bit) bad(where + " intercept tags", "no type has the tag '" + tg.str() + "'");
                d.intercept.tags |= bit;
            }
            if (!d.intercept.tags) bad(where + " intercept", "needs \"tags\": the shooters whose projectiles it stops");
            anyIntercept_ = true;
        }
        if (const Json& aj = tj["ambientIncome"]; !aj.isNull()) {
            if (!aj.isObject()) bad(where + " ambientIncome", "expected an object {\"wind\": {\"energy\": 1}, \"tidal\": {...}}");
            for (const auto& [src, rj] : aj.members()) {
                const uint8_t s = src == "wind" ? 0 : src == "tidal" ? 1 : 0xFF;
                if (s == 0xFF) bad(where + " ambientIncome", "unknown source '" + src + "' (wind, tidal)");
                std::vector<Amount> am;
                amounts(R, where + " ambientIncome " + src, rj, am);
                for (auto& a : am) d.ambient.push_back({s, a.res, a.value});
            }
        }
        if (const Json& rj = tj["attackRun"]; !rj.isNull()) {
            d.overshoot = rj.isObject() ? rj.getFx("overshoot", Fixed::fromInt(4)) : Fixed::fromInt(4);
            if (d.overshoot.raw <= 0) bad(where + " attackRun", "overshoot must be > 0");
            anyRun_ = true;
        }
        if (const Json& pj = tj["repairPad"]; !pj.isNull()) {
            if (!pj.isObject()) bad(where + " repairPad", "expected an object {\"heal\", \"radius\"}");
            d.padHeal = pj.getFx("heal");
            d.padRadius = pj.getFx("radius", d.padRadius);
            if (d.padHeal.raw <= 0) bad(where + " repairPad", "heal must be > 0");
            anyPad_ = true;
        }
        d.seekRepair = tj.getFx("seekRepair");
        anyPatrol_ = anyPatrol_ || d.patrolWork;
        anyAssist_ = anyAssist_ || d.assist;
        any = any || d.assist || d.patrolWork || d.stockpile.max > 0 || d.intercept.radius.raw > 0 || !d.ambient.empty() ||
              d.overshoot.raw > 0 || d.padHeal.raw > 0 || d.seekRepair.raw > 0;
    }
    enabled_ = any;
    if (!enabled_) return;
    sim.world().setResource<FieldOpsStore>();
    store(sim)->wind = ambient_.windMin;
}

bool FieldOps::builder(uint16_t type) const { return !m_->rules().types[type].builds.empty(); }

Fixed FieldOps::wind(Simulation& sim) const { auto* st = store(sim); return st ? st->wind : Fixed{}; }

// Progressive payment: pays `cost * step`, less when short (the ratio of the scarcest resource). Returns the step paid.
Fixed FieldOps::payStep(Simulation& sim, uint8_t player, const std::vector<Amount>& cost, Fixed step) {
    GameState& gs = m_->state(sim);
    if (step.raw <= 0 || player >= gs.players.size()) return Fixed{};
    PlayerEconomy& eco = gs.players[player];
    const Rules& R = m_->rules();
    Fixed ratio = kOne;
    for (auto& c : cost) {
        if (R.resources[c.res].kind == ResourceKind::Capacity || c.value.raw <= 0) continue;
        const Fixed need = c.value * step;
        if (need.raw > 0 && eco.stock[c.res] < need) ratio = fxMin(ratio, fxMax(Fixed{}, eco.stock[c.res]) / need);
    }
    if (ratio.raw <= 0) return Fixed{};
    for (auto& c : cost)
        if (R.resources[c.res].kind != ResourceKind::Capacity && c.value.raw > 0) eco.stock[c.res] -= c.value * step * ratio;
    return step * ratio;
}

// ---------------------------------------------------------------- commands

bool FieldOps::command(Simulation& sim, const Command& cmd) {
    if (cmd.type != kCmdRepair && cmd.type != kCmdReclaim && cmd.type != kCmdSelfDestruct) return false;
    if (!enabled_) return true;
    World& w = sim.world();
    const Rules& R = m_->rules();
    const bool queue = cmd.flags & kCmdQueue;
    for (Entity e : cmd.units) {
        auto* o = w.tryGet<Owner>(e);
        auto* u = w.tryGet<Unit>(e);
        if (!o || !u || o->player != cmd.player || u->hp.raw <= 0 || (u->flags & (kUnitOffMap | kUnitPossessed))) continue;
        const UnitType& t = R.types[u->type];
        if (cmd.type == kCmdSelfDestruct) {
            if (!selfDestructOn_ || (u->flags & kUnitUnderConstruction)) continue;
            FieldOpsStore& st = *store(sim);
            if (auto it = findEntry(st.selfDestruct, e); it != st.selfDestruct.end()) {
                st.selfDestruct.erase(it);
                sim.emit({kEvFieldOp, cmd.player, e, kNullEntity, kFieldOpDisarmed, w.get<Transform>(e).pos});
                continue;
            }
            const uint32_t ticks = uint32_t(std::max<int64_t>(1, (selfDestructTime_ * int64_t(sim.tickRate())).toInt()));
            insertEntry(st.selfDestruct, FieldOpsStore::Countdown{e, sim.tick() + ticks});
            sim.emit({kEvFieldOp, cmd.player, e, kNullEntity, kFieldOpArmed | int64_t(ticks) << 8, w.get<Transform>(e).pos});
            continue;
        }
        if (!w.alive(cmd.target) || cmd.target == e || !w.has<Unit>(cmd.target) || !w.has<Orders>(e) || !builder(u->type)) continue;
        const Unit& tu = w.get<Unit>(cmd.target);
        const UnitType& tt = R.types[tu.type];
        const uint8_t towner = w.get<Owner>(cmd.target).player;
        Order ord;
        ord.target = cmd.target;
        ord.point = flat(w.get<Transform>(cmd.target).pos);
        if (cmd.type == kCmdRepair) {
            if (towner == 0 || m_->hostile(sim, cmd.player, towner) || w.has<Construction>(cmd.target)) continue;
            if (!tt.structure && !repairUnits_) continue;
            if (tu.hp >= m_->stat(sim, cmd.target, Stat::MaxHp)) continue;
            ord.type = OrderType::Repair;
        } else {
            if (auto* rs = w.tryGet<ResourceStock>(cmd.target)) {   // a wreck / rock / tree: gather it
                bool can = false;
                for (auto& gd : t.gathers) if (gd.res == rs->res) can = true;
                if (!can) continue;
                ord.type = OrderType::Gather;
            } else {
                if (!reclaimUnits_ || tt.invulnerable || towner == 0 || tt.cost.empty()) continue;
                ord.type = OrderType::Reclaim;
            }
        }
        m_->issue(sim, e, ord, queue);
    }
    return true;
}

// ---------------------------------------------------------------- reclaim order

bool FieldOps::runReclaim(Simulation& sim, Entity e, Order& o) {
    World& w = sim.world();
    if (!enabled_ || !w.alive(o.target) || !w.has<Unit>(o.target)) return true;
    Unit& tu = w.get<Unit>(o.target);
    if (tu.hp.raw <= 0 || (tu.flags & kUnitOffMap)) return true;
    const Rules& R = m_->rules();
    const UnitType& t = R.types[w.get<Unit>(e).type];
    const UnitType& tt = R.types[tu.type];
    auto* mob = w.tryGet<Mobile>(e);
    if (m_->contactDistance(sim, e, o.target) > Fixed::fromRatio(8, 10)) {
        if (!mob) return true;
        if (!mob->moving) m_->moveTo(sim, e, w.get<Transform>(o.target).pos, tt.radius + t.radius + Fixed::fromRatio(3, 10));
        return false;
    }
    if (mob && mob->moving) m_->stopMoving(sim, e);
    // taking apart runs at the builder's speed over the target's build time, scaled by its health left
    const Fixed maxHp = fxMax(m_->stat(sim, o.target, Stat::MaxHp), kOne);
    const Fixed share = fxMin(tu.hp / maxHp, t.buildPower * sim.dt() / fxMax(tt.buildTime, kOne));
    tu.hp -= maxHp * share;
    const uint8_t owner = w.get<Owner>(e).player;
    for (auto& c : tt.cost)
        if (R.resources[c.res].kind != ResourceKind::Capacity) m_->addResource(sim, owner, c.res, c.value * share * reclaimRefund_);
    if (tu.hp.raw <= 0) { tu.hp = Fixed{}; tu.flags |= kUnitSilent; return true; }   // gone: no wreck, no death blast
    return false;
}

// ---------------------------------------------------------------- hooks

Fixed FieldOps::assistMult(Simulation& sim, Entity producer) {
    if (!anyAssist_) return kOne;
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    const Rules& R = m_->rules();
    const Unit& pu = w.get<Unit>(producer);
    const UnitType& pt = R.types[pu.type];
    const uint8_t owner = w.get<Owner>(producer).player;
    const Vec3 pos = w.get<Transform>(producer).pos;
    Fixed add{};
    const Fixed reach = pt.radius + Fixed::fromInt(6);
    gs.hash.query(pos, reach, [&](Entity b) {
        auto* bu = w.tryGet<Unit>(b);
        if (!bu || bu->hp.raw <= 0 || bu->type >= types_.size() || !types_[bu->type].assist) return;
        if (w.get<Owner>(b).player != owner || (bu->flags & (kUnitUnderConstruction | kUnitDisabled | kUnitOffMap))) return;
        auto* ord = w.tryGet<Orders>(b);
        if (!ord || ord->queue.empty()) return;
        const Order& f = ord->queue.front();
        if (f.type != OrderType::Follow || f.param != 1 || f.target != producer) return;
        if (m_->contactDistance(sim, b, producer) > Fixed::fromInt(5)) return;
        add += R.types[bu->type].buildPower;
    });
    return kOne + add / fxMax(pt.buildPower, Fixed::fromRatio(1, 10));
}

int FieldOps::stockpile(Simulation& sim, Entity e) const {
    auto* st = store(sim);
    if (!st) return -1;
    auto* u = sim.world().tryGet<Unit>(e);
    if (!u || u->type >= types_.size() || types_[u->type].stockpile.max <= 0) return -1;
    auto it = findEntry(st->stocks, e);
    return it == st->stocks.end() ? 0 : it->count;
}

bool FieldOps::castReady(Simulation& sim, Entity caster, int ability) const {
    if (!anyStock_ || ability < 0 || ability >= 8) return true;
    auto* u = sim.world().tryGet<Unit>(caster);
    if (!u || u->type >= types_.size()) return true;
    const StockpileDef& sd = types_[u->type].stockpile;
    if (sd.max <= 0 || !(sd.abilities & (1u << ability))) return true;
    return stockpile(sim, caster) > 0;
}

void FieldOps::onCast(Simulation& sim, Entity caster, int ability) {
    if (!anyStock_ || ability < 0 || ability >= 8) return;
    auto* u = sim.world().tryGet<Unit>(caster);
    if (!u || u->type >= types_.size()) return;
    const StockpileDef& sd = types_[u->type].stockpile;
    if (sd.max <= 0 || !(sd.abilities & (1u << ability))) return;
    FieldOpsStore& st = *store(sim);
    if (auto it = findEntry(st.stocks, caster); it != st.stocks.end() && it->count > 0) --it->count;
}

uint32_t FieldOps::selfDestructAt(Simulation& sim, Entity e) const {
    auto* st = store(sim);
    if (!st) return 0;
    auto it = findEntry(st->selfDestruct, e);
    return it == st->selfDestruct.end() ? 0 : it->at;
}

void FieldOps::economy(Simulation& sim) {
    if (!enabled_) return;
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    const Fixed wnd = wind(sim);
    w.each<Unit, Owner>([&](Entity, Unit& u, Owner& o) {
        if (u.type >= types_.size() || types_[u.type].ambient.empty() || o.player == 0 || o.player >= gs.players.size()) return;
        if (u.hp.raw <= 0 || (u.flags & (kUnitUnderConstruction | kUnitDisabled))) return;
        for (const AmbientIncome& a : types_[u.type].ambient) gs.players[o.player].income[a.res] += a.amount * (a.source == 0 ? wnd : ambient_.tidal);
    });
}

// ---------------------------------------------------------------- tick

void FieldOps::tick(Simulation& sim) {
    if (!enabled_) return;
    World& w = sim.world();
    FieldOpsStore& st = *store(sim);
    const uint32_t now = sim.tick();
    // wind: a new strength every few seconds (sim.rng, only in games with ambient wind)
    if (ambient_.wind && now >= st.nextWind) {
        st.wind = ambient_.windMin + (ambient_.windMax - ambient_.windMin) * sim.rng().unit();
        const Fixed secs = ambient_.everyMin + (ambient_.everyMax - ambient_.everyMin) * sim.rng().unit();
        st.nextWind = now + uint32_t(std::max<int64_t>(1, (secs * int64_t(sim.tickRate())).toInt()));
        sim.emit({kEvFieldOp, 0, kNullEntity, kNullEntity, kFieldOpWind | int64_t(st.wind.toInt()) << 8, {}});
    }
    // self-destruct countdowns
    dropDead(w, st.selfDestruct);
    for (auto& c : st.selfDestruct)
        if (c.at <= now) if (auto* u = w.tryGet<Unit>(c.e); u && u->hp.raw > 0) { u->hp = Fixed{}; u->lastAttacker = 0; }
    st.selfDestruct.erase(std::remove_if(st.selfDestruct.begin(), st.selfDestruct.end(), [&](const auto& c) { return c.at <= now; }), st.selfDestruct.end());
    // stockpiles: build charges while paying for them
    if (anyStock_) {
        dropDead(w, st.stocks);
        const Rules& R = m_->rules();
        std::vector<FieldOpsStore::Stock> fresh;
        w.each<Unit, Owner>([&](Entity e, Unit& u, Owner& o) {
            if (u.type >= types_.size()) return;
            const StockpileDef& sd = types_[u.type].stockpile;
            if (sd.max <= 0 || o.player == 0 || u.hp.raw <= 0 || (u.flags & (kUnitUnderConstruction | kUnitDisabled))) return;
            auto it = findEntry(st.stocks, e);
            if (it == st.stocks.end()) { fresh.push_back({e, 0, Fixed{}}); return; }
            if (it->count >= sd.max) return;
            const Fixed mult = m_->state(sim).players[o.player].productionMult;
            Fixed step = fxMin(kOne - it->progress, sim.dt() * mult / sd.time);
            step = sd.cost.empty() ? step : payStep(sim, o.player, sd.cost, step);
            it->progress += step;
            if (it->progress >= kOne) {
                it->progress = Fixed{};
                ++it->count;
                sim.emit({kEvFieldOp, o.player, e, kNullEntity, kFieldOpStockpiled | int64_t(it->count) << 8, w.get<Transform>(e).pos});
            }
            (void)R;
        });
        for (auto& f : fresh) insertEntry(st.stocks, f);
    }
    if (anyIntercept_) interceptors(sim, st);
    if (anyPatrol_ && now % 8 == 0) patrolWork(sim);
    if (anyRun_) attackRuns(sim, st);
    if (anyPad_) pads(sim);
}

void FieldOps::interceptors(Simulation& sim, FieldOpsStore& st) {
    World& w = sim.world();
    const Rules& R = m_->rules();
    struct Guard { Entity e; Vec3 pos; Fixed radius; uint64_t tags; uint8_t owner; bool stock; };
    std::vector<Guard> guards;
    w.each<Unit, Owner, Transform>([&](Entity e, Unit& u, Owner& o, Transform& tr) {
        if (u.type >= types_.size() || types_[u.type].intercept.radius.raw <= 0 || o.player == 0) return;
        if (u.hp.raw <= 0 || (u.flags & (kUnitUnderConstruction | kUnitDisabled))) return;
        guards.push_back({e, tr.pos, types_[u.type].intercept.radius, types_[u.type].intercept.tags, o.player, types_[u.type].stockpile.max > 0});
    });
    if (guards.empty()) return;
    std::vector<std::pair<Entity, Entity>> hits;   // projectile, interceptor
    w.each<Projectile, Transform>([&](Entity p, Projectile& pr, Transform& tr) {
        if (pr.sourceType >= R.types.size() || pr.owner == 0) return;
        const uint64_t tags = R.types[pr.sourceType].tags;
        for (Guard& g : guards) {
            if (!(tags & g.tags) || !m_->hostile(sim, g.owner, pr.owner) || dist2D(g.pos, tr.pos) > g.radius) continue;
            if (g.stock) {
                auto it = findEntry(st.stocks, g.e);
                if (it == st.stocks.end() || it->count <= 0) continue;
                --it->count;
            }
            hits.push_back({p, g.e});
            break;
        }
    });
    for (auto& [p, g] : hits) {
        sim.emit({kEvFieldOp, w.get<Owner>(g).player, g, p, kFieldOpIntercepted, w.get<Transform>(p).pos});
        w.destroyLater(p);
    }
}

void FieldOps::patrolWork(Simulation& sim) {
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    const Rules& R = m_->rules();
    struct Job { Entity e; Order o; };
    std::vector<Job> jobs;
    w.each<Unit, Owner, Orders, Transform>([&](Entity e, Unit& u, Owner& ow, Orders& ord, Transform& tr) {
        if (u.type >= types_.size() || !types_[u.type].patrolWork || ord.queue.empty() || ord.queue.front().type != OrderType::Patrol) return;
        if (u.flags & (kUnitUnderConstruction | kUnitDisabled | kUnitOffMap)) return;
        const UnitType& t = R.types[u.type];
        const Fixed reach = m_->stat(sim, e, Stat::Sight);
        Entity best; Fixed bestD{}; bool repair = false;
        gs.hash.query(tr.pos, reach, [&](Entity x) {
            if (x == e) return;
            auto* xu = w.tryGet<Unit>(x);
            if (!xu || xu->hp.raw <= 0 || (xu->flags & kUnitOffMap)) return;
            const UnitType& xt = R.types[xu->type];
            const uint8_t xo = w.get<Owner>(x).player;
            const Fixed d = dist2D(w.get<Transform>(x).pos, tr.pos);
            if (d > reach) return;
            bool fix = false, take = false;
            if (xo != 0 && !m_->hostile(sim, ow.player, xo) && (xt.structure || repairUnits_) && !w.has<Construction>(x) && !t.builds.empty() &&
                xu->hp < m_->stat(sim, x, Stat::MaxHp))
                fix = true;
            else if (auto* rs = w.tryGet<ResourceStock>(x); rs && rs->res >= 0 && R.resources[size_t(rs->res)].kind == ResourceKind::Flow &&
                     !w.has<Construction>(x) && (!xt.ownerOnly || xo == ow.player))
                for (auto& gd : t.gathers) if (gd.res == rs->res) take = true;
            if (!fix && !take) return;
            // repairs first, then the nearest
            if (best.valid() && (repair && !fix)) return;
            if (!best.valid() || (fix && !repair) || d < bestD || (d == bestD && x.index < best.index)) { best = x; bestD = d; repair = fix; }
        });
        if (!best.valid()) return;
        Order o;
        o.type = repair ? OrderType::Repair : OrderType::Gather;
        o.target = best;
        o.point = flat(w.get<Transform>(best).pos);
        jobs.push_back({e, o});
    });
    for (auto& j : jobs) {
        auto& q = w.get<Orders>(j.e).queue;
        q.insert(q.begin(), j.o);   // the patrol goes on when the job is done
    }
}

void FieldOps::attackRuns(Simulation& sim, FieldOpsStore& st) {
    World& w = sim.world();
    const GameState& gs = m_->state(sim);
    dropDead(w, st.runs);
    std::vector<FieldOpsStore::Run> keep;
    w.each<Unit, Owner, Combat, Mobile, Transform>([&](Entity e, Unit& u, Owner& o, Combat& c, Mobile& m, Transform& tr) {
        if (u.type >= types_.size() || types_[u.type].overshoot.raw <= 0 || o.player == 0) return;
        if (u.flags & (kUnitUnderConstruction | kUnitDisabled | kUnitOffMap | kUnitHold)) return;
        if (!w.alive(c.target) || !w.has<Unit>(c.target) || w.get<Unit>(c.target).hp.raw <= 0) return;
        if (auto* ord = w.tryGet<Orders>(e); ord && !ord->queue.empty()) {
            const OrderType ot = ord->queue.front().type;
            if (ot != OrderType::Attack && ot != OrderType::AttackMove && ot != OrderType::Patrol) return;
        }
        const Vec3 tp = flat(w.get<Transform>(c.target).pos), me = flat(tr.pos);
        const Fixed over = types_[u.type].overshoot;
        FieldOpsStore::Run run{e, c.target, {}, {}};
        auto it = findEntry(st.runs, e);
        const bool fresh = it == st.runs.end() || it->target != c.target;
        if (!fresh) run = *it;
        if (fresh || dist2D(me, run.goal) <= kOne) {
            Vec3 d = tp - me;
            const Fixed len = d.length();
            if (len > kHalf) d = d / len;
            else if (!fresh) d = run.dir * Fixed::fromInt(-1);
            else d = Vec3{u.facing.x, Fixed{}, u.facing.z};
            run.dir = d;
            Vec3 g = tp + d * over;
            g.x = std::clamp(g.x, Fixed{}, Fixed::fromInt(gs.grid.w) - kHalf);
            g.z = std::clamp(g.z, Fixed{}, Fixed::fromInt(gs.grid.h) - kHalf);
            run.goal = g;
        }
        keep.push_back(run);
        if (!m.moving || dist2D(m.goal, run.goal) > kHalf) m_->moveTo(sim, e, run.goal, kHalf);
    });
    st.runs = std::move(keep);   // ECS order = entity index order
    std::sort(st.runs.begin(), st.runs.end(), [](const auto& a, const auto& b) { return a.e.index < b.e.index; });
}

void FieldOps::pads(Simulation& sim) {
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    const Rules& R = m_->rules();
    struct Pad { Entity e; Vec3 pos; Fixed heal, radius; uint8_t owner; };
    std::vector<Pad> list;
    w.each<Unit, Owner, Transform>([&](Entity e, Unit& u, Owner& o, Transform& tr) {
        if (u.type >= types_.size() || types_[u.type].padHeal.raw <= 0 || o.player == 0) return;
        if (u.hp.raw <= 0 || (u.flags & (kUnitUnderConstruction | kUnitDisabled))) return;
        list.push_back({e, tr.pos, types_[u.type].padHeal, types_[u.type].padRadius, o.player});
    });
    const Fixed dt = sim.dt();
    std::vector<std::pair<Entity, Vec3>> seek;
    w.each<Unit, Owner, Transform, Mobile>([&](Entity e, Unit& u, Owner& o, Transform& tr, Mobile& m) {
        if (u.type >= types_.size() || o.player == 0 || u.hp.raw <= 0 || (u.flags & kUnitOffMap)) return;
        const Fixed maxHp = m_->stat(sim, e, Stat::MaxHp);
        if (u.hp >= maxHp) return;
        const Pad* nearest = nullptr; Fixed nd{};
        for (const Pad& p : list) {
            if (p.owner != o.player && m_->hostile(sim, p.owner, o.player)) continue;
            const Fixed d = dist2D(p.pos, tr.pos);
            if (d <= p.radius && !m.moving) { u.hp = fxMin(maxHp, u.hp + p.heal * dt); return; }
            if (p.owner == o.player && (!nearest || d < nd)) { nearest = &p; nd = d; }
        }
        const Fixed seekAt = types_[u.type].seekRepair;
        if (seekAt.raw <= 0 || !nearest || u.hp > maxHp * seekAt || m.moving || (sim.tick() + e.index) % 16 != 0) return;
        if (auto* ord = w.tryGet<Orders>(e); ord && !ord->queue.empty()) return;
        seek.push_back({e, nearest->pos});
    });
    for (auto& [e, p] : seek) {
        Order o; o.type = OrderType::Move; o.point = flat(p); o.param = 1;
        m_->issue(sim, e, o, false);
    }
    (void)gs; (void)R;
}

// ---------------------------------------------------------------- hash / save

void FieldOps::hash(Simulation& sim, Hasher& h) const {
    auto* st = store(sim);
    if (!st) return;
    h.fx(st->wind); h.u32(st->nextWind);
    h.u32(uint32_t(st->selfDestruct.size())); for (auto& c : st->selfDestruct) { h.u32(c.e.index); h.u32(c.at); }
    h.u32(uint32_t(st->stocks.size())); for (auto& s : st->stocks) { h.u32(s.e.index); h.i32(s.count); h.fx(s.progress); }
    h.u32(uint32_t(st->runs.size())); for (auto& r : st->runs) { h.u32(r.e.index); h.u32(r.target.index); h.v3(r.goal); h.v3(r.dir); }
}

void FieldOps::io(Simulation& sim, Archive& a) {
    auto* st = store(sim);
    if (!st) return;
    a(st->wind, st->nextWind);
    uint32_t n = a.count(st->selfDestruct.size());
    if (!a.writing()) st->selfDestruct.assign(n, {});
    for (uint32_t i = 0; i < n && a.ok(); ++i) a(st->selfDestruct[i].e, st->selfDestruct[i].at);
    n = a.count(st->stocks.size());
    if (!a.writing()) st->stocks.assign(n, {});
    for (uint32_t i = 0; i < n && a.ok(); ++i) a(st->stocks[i].e, st->stocks[i].count, st->stocks[i].progress);
    n = a.count(st->runs.size());
    if (!a.writing()) st->runs.assign(n, {});
    for (uint32_t i = 0; i < n && a.ok(); ++i) a(st->runs[i].e, st->runs[i].target, st->runs[i].goal, st->runs[i].dir);
}

} // namespace aiverse::game
