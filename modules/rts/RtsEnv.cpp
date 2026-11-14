// Environment runtime (RtsEnv.h): setup, weather, wind, zones, trample fields, per-unit state, auto skills on
// splines, speed hook, hash and saves, fog options.
#include "RtsEnv.h"
#include "RtsInternal.h"
#include "../../engine/core/FixedMath.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::game {

using namespace detail;

void loadEnvironment(EnvDef& d, const Rules& r, const Json& rulesJ, const Json& mapJ);   // RtsEnvData.cpp

// ================================================================= geometry

void EnvSpline::build(const std::vector<Vec3>& pts, bool lp, int smooth) {
    points = pts; loop = lp;
    nodes.clear(); cum.clear(); length = Fixed{};
    const size_t n = pts.size();
    if (n == 0) return;
    if (n == 1) { nodes = pts; cum = {Fixed{}}; return; }
    // uniform Catmull-Rom through every control point; open splines clamp the end tangents
    auto P = [&](long i) -> Vec3 {
        if (loop) return pts[size_t(((i % long(n)) + long(n)) % long(n))];
        return pts[size_t(std::clamp<long>(i, 0, long(n) - 1))];
    };
    const long segs = loop ? long(n) : long(n) - 1;
    const Fixed half = Fixed::fromRatio(1, 2);
    for (long sgm = 0; sgm < segs; ++sgm) {
        Vec3 p0 = P(sgm - 1), p1 = P(sgm), p2 = P(sgm + 1), p3 = P(sgm + 2);
        for (int k = 0; k < smooth; ++k) {
            Fixed t = Fixed::fromInt(k) / smooth, t2 = t * t, t3 = t2 * t;
            auto f = [&](Fixed a, Fixed b, Fixed c, Fixed dd) {
                return half * (b * 2 + (c - a) * t + (a * 2 - b * 5 + c * 4 - dd) * t2 + (b * 3 - a - c * 3 + dd) * t3);
            };
            nodes.push_back({f(p0.x, p1.x, p2.x, p3.x), Fixed{}, f(p0.z, p1.z, p2.z, p3.z)});
        }
    }
    nodes.push_back(loop ? pts[0] : pts[n - 1]);
    cum.assign(nodes.size(), Fixed{});
    for (size_t i = 1; i < nodes.size(); ++i) cum[i] = cum[i - 1] + (nodes[i] - nodes[i - 1]).xz().length();
    length = cum.back();
}

Vec3 EnvSpline::at(Fixed d) const {
    if (nodes.empty()) return {};
    if (d <= Fixed{} || nodes.size() == 1) return nodes.front();
    if (d >= length) return nodes.back();
    size_t i = size_t(std::upper_bound(cum.begin(), cum.end(), d) - cum.begin());
    i = std::clamp<size_t>(i, 1, nodes.size() - 1);
    Fixed seg = cum[i] - cum[i - 1];
    if (seg.raw <= 0) return nodes[i];
    return nodes[i - 1] + (nodes[i] - nodes[i - 1]) * ((d - cum[i - 1]) / seg);
}

bool EnvZone::contains(Vec3 p) const {
    if (radius.raw > 0) return (p - center).xz().lengthSq() <= radius * radius;
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec3& a = poly[i]; const Vec3& b = poly[j];
        if ((a.z > p.z) != (b.z > p.z)) {
            Fixed x = a.x + (b.x - a.x) * ((p.z - a.z) / (b.z - a.z));
            if (p.x < x) in = !in;
        }
    }
    return in;
}

// ================================================================= setup

EnvState* Environment::state(Simulation& sim) const { return sim.world().resource<EnvState>(); }

void Environment::setup(Simulation& sim, RtsModule& m, const Json& rules, const Json& map) {
    m_ = &m;
    def_ = EnvDef{};
    loadEnvironment(def_, m.rules(), rules, map);
    if (!def_.active) return;
    // grid field references (trample, lightning burn)
    GridFields* gf = m.gridFields();
    auto fieldRef = [&](const std::string& id, const char* where) -> int16_t {
        if (id.empty()) return -1;
        int f = gf ? gf->find(id) : -1;
        if (f < 0) throw std::runtime_error(std::string("environment.") + where + ": unknown grid field '" + id + "' (rules \"fields\")");
        return int16_t(f);
    };
    def_.weather.burnField = fieldRef(def_.weather.burnId, "weatherParams.lightningBurn");
    for (auto& t : def_.trample) {
        t.field = fieldRef(t.fieldId, "trample"); t.killField = fieldRef(t.killId, "trample.killField");
        // in rain a burning tile lights a neighbour only with the rain factor
        if (def_.weather.rain && t.killField >= 0) gf->setSpreadScale(t.killField, def_.weather.rainSpread);
    }
    EnvState& st = sim.world().setResource<EnvState>();
    // the puddle cycle starts at 0 filling; no rain = dry ground (puddles 1 = full water slowdown)
    st.puddles = def_.weather.rain ? Fixed{} : Fixed::fromInt(1);
    st.blizzardState = def_.weather.blizzard ? 0 : 1;
    if (def_.wind.enabled) { st.windIdle = range(sim, def_.wind.idleMin, def_.wind.idleMax); st.windVisual = -1; }
    st.fogSeen.assign(m.state(sim).players.size(), {});
    st.autos.assign(def_.splines.size(), {});
    for (size_t i = 0; i < def_.splines.size(); ++i) {
        const EnvSpline& sp = def_.splines[i];
        st.autos[i].timer = sp.firstEmission.raw > 0 ? sp.firstEmission : sp.emitPeriod;
    }
}

// ================================================================= helpers

Fixed Environment::range(Simulation& sim, Fixed lo, Fixed hi) const {
    if (hi <= lo) return lo;
    return lo + (hi - lo) * sim.rng().unit();
}

std::vector<Entity> Environment::units_(Simulation& sim) const {
    std::vector<Entity> out;
    World& w = sim.world();
    const Rules& R = m_->rules();
    w.each<Unit, Owner, Transform>([&](Entity e, Unit& u, Owner&, Transform&) {
        const UnitType& t = R.types[u.type];
        if (u.hp.raw <= 0 || t.structure || t.speed.raw <= 0 || (u.flags & kUnitOffMap) || t.invulnerable) return;
        out.push_back(e);
    });
    return out;
}

void Environment::kill(Simulation& sim, EnvState& st, Entity e, bool weather) {
    World& w = sim.world();
    if (!w.alive(e)) return;
    Unit& u = w.get<Unit>(e);
    if (u.hp.raw <= 0) return;
    if (m_->rules().types[u.type].invulnerable || (m_->statusFlags(sim, e) & kStInvulnerable)) return;
    u.hp = Fixed{};
    u.lastAttacker = 0;
    if (weather) st.weatherKilled.push_back(e.packed());
}

void Environment::cast(Simulation& sim, uint8_t player, int skill, Vec3 point, Entity target, std::vector<Entity>* areas) {
    const Rules& R = m_->rules();
    if (skill < 0 || size_t(skill) >= R.playerAbilities.size()) return;
    const size_t ev0 = sim.events().size();
    sim.emit({kEvPlayerAbility, player, kNullEntity, target, skill, flat(point)});
    EffectCtx ctx;
    ctx.owner = player; ctx.point = flat(point); ctx.origin = ctx.point; ctx.target = target;
    m_->applyPayload(sim, R.playerAbilities[size_t(skill)].payload, ctx);
    if (areas)
        for (size_t i = ev0; i < sim.events().size(); ++i)
            if (sim.events()[i].type == kEvAreaStart) areas->push_back(sim.events()[i].a);
}

bool Environment::killedByWeather(Simulation& sim, Entity e) const {
    EnvState* st = state(sim);
    return st && std::find(st->weatherKilled.begin(), st->weatherKilled.end(), e.packed()) != st->weatherKilled.end();
}

// ================================================================= tick

void Environment::tick(Simulation& sim) {
    EnvState* stp = state(sim);
    if (!stp) return;
    EnvState& st = *stp;
    st.weatherKilled.clear();
    weather(sim, st);
    wind(sim, st);
    units(sim, st);
    trample(sim, st);
    autos(sim, st);
}

// ================================================================= weather

void Environment::weather(Simulation& sim, EnvState& st) {
    const EnvWeatherDef& W = def_.weather;
    const Fixed dt = sim.dt(), one = Fixed::fromInt(1);
    if (W.rain) {
        const uint8_t before = st.puddlesState;
        if (st.puddlesState == 0) { st.puddles += dt / W.puddlesFill; if (st.puddles >= one) st.puddlesState = 1; }
        else if (st.puddlesState == 1) { st.rainTimer += dt / W.rainWait; if (st.rainTimer >= one) { st.rainTimer = Fixed{}; st.puddlesState = 2; } }
        else if (st.puddlesState == 2) { st.puddles -= dt / W.puddlesDry; if (st.puddles <= Fixed{}) st.puddlesState = 3; }
        else { st.rainTimer += dt / W.noRainWait; if (st.rainTimer >= one) { st.rainTimer = Fixed{}; st.puddlesState = 0; } }
        if (before != st.puddlesState) sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWeather << 16) | 1 | (int64_t(st.puddlesState) << 8), {}});
    } else st.puddles = one;
    if (W.winter && W.snow) {
        if (st.snowState == 1) {
            // restoring snow over every trample field that follows the snow phase
            for (auto& t : def_.trample)
                if (t.snowRestore && t.field >= 0) {
                    GridFields* gf = m_->gridFields();
                    const GridFieldDef& fd = gf->defs()[size_t(t.field)];
                    const auto& f = gf->store(sim)->fields[size_t(t.field)];
                    gf->fillRect(sim, t.field, 0, 0, f.w - 1, f.h - 1, 0, -fd.raw(dt * W.snowRestore), 1);
                }
            st.snowTimer += dt / st.snowFill;
            if (st.snowTimer >= one) { st.snowTimer = Fixed{}; st.snowState = 0; sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWeather << 16) | 2, {}}); }
        } else {
            st.snowTimer += dt / W.snowWait;
            if (st.snowTimer >= one) {
                st.snowTimer = Fixed{};
                if (sim.rng().unit() <= W.snowFillChance) {
                    st.snowFill = fxMax(range(sim, W.snowFillMin, W.snowFillMax), Fixed::fromRatio(1, 10));
                    st.snowState = 1;
                    sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWeather << 16) | 2 | (1 << 8), {}});
                }
            }
        }
    }
    if (W.blizzard) {
        if (st.blizzardState == 0) { st.blizzardTimer += dt / W.blizzardFill; if (st.blizzardTimer >= one) { st.blizzardTimer = Fixed{}; st.blizzardState = 1; sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWeather << 16) | 4 | (1 << 8), {}}); } }
        else { st.blizzardTimer += dt / W.blizzardWait; if (st.blizzardTimer >= one) { st.blizzardTimer = Fixed{}; st.blizzardState = 0; sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWeather << 16) | 4, {}}); } }
    } else st.blizzardState = 1;
    if (W.blizzard) {   // bots and scripts read the trigger var
        const Fixed v = Fixed::fromInt(st.blizzardState == 0 ? 1 : 0);
        auto& vars = m_->state(sim).trig.vars;
        auto it = vars.find(W.blizzardVar);
        if (it == vars.end() || it->second != v) vars[W.blizzardVar] = v;
    }
    if (W.lightning) {
        if (st.lightningState == 0) {
            st.lightningKill += dt / W.lightningKillTime;
            if (st.lightningKill >= one) {
                st.lightningKill = Fixed{};
                if (int(sim.rng().below(100)) <= W.lightningKillChance) {
                    World& w = sim.world();
                    std::vector<Entity> objs = units_(sim);
                    const size_t nUnits = objs.size();
                    w.each<Garrison, Unit>([&](Entity e, Garrison&, Unit& u) { if (u.hp.raw > 0) objs.push_back(e); });
                    if (!objs.empty()) {
                        const size_t k = sim.rng().below(uint32_t(objs.size()));
                        Entity obj = objs[k];
                        Vec3 p = w.get<Transform>(obj).pos;
                        if (k < nUnits) {
                            kill(sim, st, obj, true);
                            const Fixed rx = W.lightningRange, rz = W.lightningRange / 2;   // ellipse (range, range / 2)
                            for (size_t i = 0; i < nUnits; ++i) {
                                Vec3 q = w.get<Transform>(objs[i]).pos - p;
                                if (rx.raw <= 0 || rz.raw <= 0) break;
                                Fixed a = q.x / rx, b = q.z / rz;
                                if (a * a + b * b <= one) kill(sim, st, objs[i], true);
                            }
                            if (W.burnField >= 0) {
                                GridFields* gf = m_->gridFields();
                                gf->fillCircle(sim, W.burnField, p.x, p.z, W.burnRadius.raw > 0 ? W.burnRadius : W.lightningRange, 0, W.burnValue, 2);
                            }
                        } else if (auto* g = w.tryGet<Garrison>(obj)) {
                            int n = std::min(g->count, W.lightningStructureKill);
                            g->count -= n; g->lost += n;
                            if (n) sim.emit({kEvGarrisonChanged, w.get<Owner>(obj).player, obj, kNullEntity, -n, p});
                        }
                        st.lightningPos = p;
                        sim.emit({kEvEnv, 0, obj, kNullEntity, kEnvLightning << 16, p});
                    }
                }
            }
            st.lightningTimer += dt / W.lightningTime;
            if (st.lightningTimer >= one) { st.lightningTimer = Fixed{}; st.lightningState = 1; }
        } else {
            st.lightningTimer += dt / W.lightningWait;
            if (st.lightningTimer >= one) { st.lightningTimer = Fixed{}; st.lightningState = 0; }
        }
    }
}

// ================================================================= wind

void Environment::wind(Simulation& sim, EnvState& st) {
    const EnvWindDef& N = def_.wind;
    if (!N.enabled) return;
    const Fixed dt = sim.dt();
    if (st.windIdle.raw > 0) {
        st.windIdle -= dt;
        if (st.windIdle.raw <= 0) {
            st.windBlow = range(sim, N.blowMin, N.blowMax);
            st.windDir = (st.windDir + 1) % 8;
            st.windVisual = st.windDir;
            const Fixed f = range(sim, N.forceMin, N.forceMax);
            const Fixed turns = Fixed::fromInt(st.windDir) / 8;   // pi * dir / 4
            st.windForce = {f * fxSinTurns(turns), Fixed{}, f * fxCosTurns(turns)};
            sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWind << 16) | st.windDir, {}});
        }
    } else if (st.windBlow.raw > 0) {
        st.windBlow -= dt;
        if (st.windBlow.raw <= 0) {
            st.windIdle = range(sim, N.idleMin, N.idleMax);
            st.windVisual = -1;
            sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWind << 16) | 0xFF, {}});
        }
    }
}

Fixed Environment::windMultiplier(Simulation& sim, Vec3 dir) const {
    EnvState* st = state(sim);
    if (!st || !def_.wind.enabled || st->windBlow.raw <= 0) return {};
    Fixed len = dir.xz().length();
    if (len.raw <= 0) return {};
    return (dir.x * st->windForce.x + dir.z * st->windForce.z) / len;
}

// ================================================================= per-unit state (blizzard), trample

void Environment::units(Simulation& sim, EnvState& st) {
    const EnvWeatherDef& W = def_.weather;
    if (!W.blizzard) return;
    World& w = sim.world();
    const Fixed delta = (st.blizzardState == 0 ? sim.dt() : Fixed{}) / W.blizzardKillSpeed;
    for (Entity e : units_(sim)) {
        if (st.units.size() <= e.index) st.units.resize(e.index + 1);
        EnvUnit& eu = st.units[e.index];
        if (!eu.init || eu.gen != e.gen) { eu = EnvUnit{}; eu.gen = e.gen; eu.init = true; eu.resist = range(sim, W.blizzardResistMin, W.blizzardResistMax); }
        if (delta.raw <= 0 || !w.has<LaneWalker>(e)) continue;   // only units on the way between buildings freeze
        eu.blizzard -= delta / fxMax(eu.resist, Fixed::fromRatio(1, 100));
        if (eu.blizzard.raw <= 0) kill(sim, st, e, true);
    }
}

void Environment::trample(Simulation& sim, EnvState& st) {
    if (def_.trample.empty()) return;
    GridFields* gf = m_->gridFields();
    World& w = sim.world();
    for (auto& t : def_.trample) {
        if (t.field < 0) continue;
        const GridFieldDef& fd = gf->defs()[size_t(t.field)];
        const int32_t add = fd.raw(t.perSecond * sim.dt());
        for (Entity e : units_(sim)) {
            const bool moving = w.has<LaneWalker>(e) || (w.has<Mobile>(e) && w.get<Mobile>(e).moving);
            if (!moving || (m_->rules().types[w.get<Unit>(e).type].layer & kLayerAir)) continue;
            Vec3 p = w.get<Transform>(e).pos;
            int cx, cz;
            if (add != 0 && gf->cellOf(t.field, p.x, p.z, cx, cz)) gf->write(sim, t.field, cx, cz, 0, add, 1);
            if (t.killField >= 0 && gf->cellOf(t.killField, p.x, p.z, cx, cz) && gf->get(sim, t.killField, cx, cz) >= t.killAbove) kill(sim, st, e, false);
            if (t.hideStatus >= 0 && gf->cellOf(t.field, p.x, p.z, cx, cz) && fd.toFixed(gf->get(sim, t.field, cx, cz)) > t.hideAbove) {
                EffectCtx ctx; ctx.owner = 0;
                m_->addStatus(sim, e, t.hideStatus, Fixed::fromRatio(1, 5), ctx);
            }
        }
    }
}

// ================================================================= auto skills on splines

void Environment::autos(Simulation& sim, EnvState& st) {
    World& w = sim.world();
    const Fixed dt = sim.dt();
    for (size_t i = 0; i < def_.splines.size(); ++i) {
        const EnvSpline& sp = def_.splines[i];
        if (sp.skill < 0 || !sp.enabled) continue;
        bool onScreen = true;
        for (auto& p : sp.points) if (!m_->onScreen(sim, p)) onScreen = false;   // multiscreen maps: only splines on the screen emit
        if (!onScreen) { st.autos[i].late = true; continue; }
        EnvAuto& au = st.autos[i];
        // emission
        if (!(sp.emitPeriod.raw <= 0 && au.timer.raw <= 0 && !au.late)) {
            au.timer -= dt;
            if (au.timer.raw <= 0) {
                au.late = false;
                std::vector<Entity> areas;
                cast(sim, 0, sp.skill, sp.points.front(), kNullEntity, &areas);
                if (sp.points.size() > 1) for (Entity a : areas) au.moving.push_back({a, Fixed{}});
                au.timer += sp.emitPeriod;
            }
        }
        // the emitted areas ride along the spline
        for (size_t k = 0; k < au.moving.size();) {
            auto& mv = au.moving[k];
            if (!w.alive(mv.area)) { au.moving.erase(au.moving.begin() + long(k)); continue; }
            mv.dist += dt * sp.velocity;
            if (sp.loop && sp.emitPeriod.raw <= 0 && sp.length.raw > 0) while (mv.dist >= sp.length) mv.dist -= sp.length;
            Vec3 target = sp.at(mv.dist);
            Transform& t = w.get<Transform>(mv.area);
            Vec3 cur{t.pos.x, Fixed{}, t.pos.z};
            moveTowards(cur, target, dt * sp.velocity);
            t.pos.x = cur.x; t.pos.z = cur.z;
            if (mv.dist.raw < 0 || mv.dist > sp.length) { w.destroyLater(mv.area); au.moving.erase(au.moving.begin() + long(k)); continue; }
            ++k;
        }
    }
}

bool RtsModule::payloadAccepts(Simulation& sim, int payload, Entity e, uint8_t owner) const {
    for (int depth = 0; depth < 4 && payload >= 0 && size_t(payload) < rules_.payloads.size() && !rules_.payloads[size_t(payload)].empty(); ++depth) {
        const EffectDef& ef = rules_.payloads[size_t(payload)][0];
        if ((ef.kind == EffectKind::Area || ef.kind == EffectKind::Projectile || ef.kind == EffectKind::Chain) && ef.payload >= 0) { payload = ef.payload; continue; }
        return filterOk(sim, ef.filter, e, owner, kNullEntity);
    }
    return sim.world().has<Owner>(e) && hostile(sim, owner, sim.world().get<Owner>(e).player);
}

// ================================================================= speed hook

void Environment::speed(Simulation& sim, Entity e, Fixed& add, Fixed& mul) const {
    EnvState* st = state(sim);
    if (!st) return;
    World& w = sim.world();
    const Unit& u = w.get<Unit>(e);
    const UnitType& t = m_->rules().types[u.type];
    if (t.structure) return;
    const Vec3 p = w.get<Transform>(e).pos;
    const bool air = t.layer & kLayerAir;
    const Fixed one = Fixed::fromInt(1);
    // blizzard health: lerp(blizzardSpeed * speed, speed, health)
    if (def_.weather.blizzard && e.index < st->units.size() && st->units[e.index].init && st->units[e.index].gen == e.gen) {
        const Fixed hb = fxClamp(st->units[e.index].blizzard, Fixed{}, one);
        mul *= def_.weather.blizzardSpeed + (one - def_.weather.blizzardSpeed) * hb;
    }
    // zones (ground only): speed / slowdown; water zones dry out with the puddles (summer rain)
    bool road = false;
    if (!air)
        for (auto& z : def_.zones) {
            if (!z.contains(p)) continue;
            if (z.kind == EnvZoneKind::Road) road = true;
            Fixed slow = z.slowdown;
            if (z.kind == EnvZoneKind::Water && !def_.weather.winter) slow = slow + (one - slow) * (one - st->puddles);
            if (slow.raw > 0) mul = mul / slow;
            break;   // the first zone containing the point
        }
    // trample fields: winter speed table / grass lerp
    if (!air && !def_.trample.empty()) {
        GridFields* gf = m_->gridFields();
        for (auto& tr : def_.trample) {
            int cx, cz;
            if (tr.field < 0 || !gf->cellOf(tr.field, p.x, p.z, cx, cz)) continue;
            const Fixed v = gf->defs()[size_t(tr.field)].toFixed(gf->get(sim, tr.field, cx, cz));
            if (!tr.speed.empty() && !(road && tr.skipRoads)) {
                Fixed m = one;
                for (auto& row : tr.speed) if (v <= row.first) m = row.second;
                mul *= m;
            }
            if (tr.lerpSpeed.raw >= 0 && v.raw > 0) mul *= one + (tr.lerpSpeed - one) * fxClamp(v, Fixed{}, one);
        }
    }
    // wind: distance per second along the movement direction
    if (def_.wind.enabled && st->windBlow.raw > 0) add += windMultiplier(sim, u.facing);
}

// ================================================================= hash / saves

void Environment::hash(Simulation& sim, Hasher& h) const {
    EnvState* st = state(sim);
    if (!st) return;
    h.u32(st->puddlesState | st->snowState << 8 | st->blizzardState << 16 | st->lightningState << 24);
    h.fx(st->puddles); h.fx(st->rainTimer); h.fx(st->snowTimer); h.fx(st->blizzardTimer); h.fx(st->lightningTimer); h.fx(st->lightningKill);
    h.v3(st->windForce); h.fx(st->windBlow); h.fx(st->windIdle); h.i32(st->windDir);
    for (auto& a : st->autos) { h.fx(a.timer); h.u32(uint32_t(a.moving.size())); for (auto& mv : a.moving) h.fx(mv.dist); }
    for (auto& u : st->units) h.fx(u.blizzard);
}

// save io of the environment state (found by ADL from Archive)
static void io(Archive& a, EnvAuto::Moving& x) { a(x.area, x.dist); }
static void io(Archive& a, EnvAuto& x) { a(x.timer, x.late, x.moving); }
static void io(Archive& a, EnvUnit& x) { a(x.gen, x.blizzard, x.resist, x.init); }
void Environment::io(Simulation& sim, Archive& a) {
    EnvState* st = state(sim);
    if (!st) return;
    a(st->puddlesState, st->snowState, st->blizzardState, st->lightningState, st->puddles, st->rainTimer, st->snowTimer, st->snowFill, st->blizzardTimer,
      st->lightningTimer, st->lightningKill, st->lightningPos, st->windForce, st->windBlow, st->windIdle, st->windDir, st->windVisual);
    a(st->autos, st->units, st->weatherKilled, st->fogSeen);
}

// ================================================================= fog of war

// Runs after the vision system. Without explored memory, cells that went out of sight become unexplored again;
// during the first startDuration seconds every player also sees a disc around its first building that shrinks
// from startFowSize to nothing.
void Environment::fogTick(Simulation& sim) {
    EnvState* st = state(sim);
    if (!st || sim.tick() % 5 != 0) return;   // the vision system runs every 5 ticks
    GameState& gs = m_->state(sim);
    if (gs.fogMode == uint8_t(FogMode::None)) return;
    const size_t np = std::min(gs.vision.size(), gs.visibleCells.size());
    if (st->fogSeen.size() < np) st->fogSeen.resize(np);
    const Fixed t = Fixed::fromInt(int64_t(sim.tick())) / int64_t(std::max<uint32_t>(sim.tickRate(), 1));
    if (def_.fog.startTime.raw > 0 && t < def_.fog.startTime) {
        World& w = sim.world();
        const MapGrid& g = gs.grid;
        const Fixed r = def_.fog.startSize * (Fixed::fromInt(1) - t / def_.fog.startTime);
        const int ri = int(r.floorInt()), e = m_->rules().fog.ellipse;
        for (size_t p = 1; p < np; ++p) {
            Entity first;
            w.each<Garrison, Owner>([&](Entity en, Garrison&, Owner& o) { if (!first.valid() && o.player == p) first = en; });
            if (!first.valid()) continue;
            int cx, cy; g.cellXY(w.get<Transform>(first).pos, cx, cy);
            for (int y = std::max(0, cy - ri); y <= std::min(g.h - 1, cy + ri); ++y)
                for (int x = std::max(0, cx - ri); x <= std::min(g.w - 1, cx + ri); ++x) {
                    const int dx = x - cx, dy = (y - cy) * e;
                    if (dx * dx + dy * dy > ri * ri) continue;
                    const size_t i = size_t(g.index(x, y));
                    if (gs.vision[p][i] != 2) { gs.vision[p][i] = 2; gs.visibleCells[p].push_back(int32_t(i)); }
                }
        }
    }
    if (!def_.fog.memory)
        for (size_t p = 1; p < np; ++p) {
            for (int32_t i : st->fogSeen[p]) if (gs.vision[p][size_t(i)] == 1) gs.vision[p][size_t(i)] = 0;
            st->fogSeen[p] = gs.visibleCells[p];
        }
}

} // namespace aiverse::game
