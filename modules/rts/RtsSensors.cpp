// Sensors (RtsSensors.h): radar / sonar contacts, jammers, stealth, underwater targets, radar targeting.
#include "RtsSensors.h"
#include <stdexcept>
#include "RtsInternal.h"

namespace aiverse::game {
using namespace detail;

namespace {
Fixed radius(const Json& j, const std::string& where) {
    if (j.isNull()) return Fixed{};
    const Fixed r = j.isObject() ? j.getFx("radius") : j.fx();
    if (r.raw <= 0) throw std::runtime_error(where + ": expected a radius > 0");
    return r;
}
} // namespace

void Sensors::setup(Simulation&, RtsModule& m, const Json& rulesJson) {
    m_ = &m;
    const Rules& R = m.rules();
    types_.assign(R.types.size(), {});
    bool any = false;
    for (const auto& [id, tj] : rulesJson["types"].members()) {
        if (!tj.isObject()) continue;
        const int ti = R.findType(id);
        if (ti < 0) continue;
        SensorTypeDef& d = types_[size_t(ti)];
        const std::string where = "type '" + id + "'";
        d.radar = radius(tj["radar"], where + " radar");
        d.sonar = radius(tj["sonar"], where + " sonar");
        d.jammer = radius(tj["jammer"], where + " jammer");
        d.stealth = tj.getBool("stealth", false);
        d.underwater = tj.getBool("underwater", false);
        const Json& wj = tj["weapons"];
        for (size_t i = 0; i < wj.size() && i < 8; ++i)
            if (wj[i].isObject() && wj[i].getBool("underwater", false)) d.underwaterWeapons |= uint8_t(1u << i);
        any = any || d.radar.raw > 0 || d.sonar.raw > 0 || d.jammer.raw > 0 || d.stealth || d.underwater || d.underwaterWeapons;
    }
    const Json& rj = rulesJson["radar"];
    if (!rj.isNull() && !rj.isObject()) throw std::runtime_error("rules radar: expected an object {\"targeting\": true}");
    targeting_ = rj.getBool("targeting", false);
    enabled_ = any || targeting_;
}

void Sensors::collect(Simulation& sim) {
    radars_.clear(); sonars_.clear(); jammers_.clear();
    World& w = sim.world();
    w.each<Unit, Owner, Transform>([&](Entity, Unit& u, Owner& o, Transform& tr) {
        if (o.player == 0 || u.hp.raw <= 0 || u.type >= types_.size()) return;
        if (u.flags & (kUnitUnderConstruction | kUnitDisabled | kUnitInCargo | kUnitOffMap)) return;   // unpowered radars go dark
        const SensorTypeDef& d = types_[u.type];
        if (d.radar.raw > 0) radars_.push_back({tr.pos, d.radar, o.player});
        if (d.sonar.raw > 0) sonars_.push_back({tr.pos, d.sonar, o.player});
        if (d.jammer.raw > 0) jammers_.push_back({tr.pos, d.jammer, o.player});
    });
}

bool Sensors::covered(Simulation& sim, const std::vector<Source>& list, Vec3 p, uint8_t side) const {
    for (const Source& s : list) {
        if (s.owner != side && (s.owner == 0 || m_->hostile(sim, s.owner, side))) continue;   // the side and its allies
        if (dist2D(s.pos, p) <= s.radius) return true;
    }
    return false;
}

bool Sensors::contact(Simulation& sim, Entity e, uint8_t viewer) const {
    World& w = sim.world();
    const auto* u = w.tryGet<Unit>(e);
    const auto* o = w.tryGet<Owner>(e);
    if (!u || !o || viewer == 0 || u->hp.raw <= 0 || u->type >= types_.size()) return false;
    if (o->player == 0 || !m_->hostile(sim, viewer, o->player)) return false;
    if (u->flags & (kUnitInCargo | kUnitOffMap)) return false;
    const SensorTypeDef& d = types_[u->type];
    if (d.stealth) return false;
    const Vec3 p = w.get<Transform>(e).pos;
    if (!covered(sim, d.underwater ? sonars_ : radars_, p, viewer)) return false;
    // jammed: a jammer of the unit's side (its owner or an ally, hostile to the viewer) covers it
    for (const Source& j : jammers_)
        if ((j.owner == o->player || !m_->hostile(sim, j.owner, o->player)) && m_->hostile(sim, j.owner, viewer) && dist2D(j.pos, p) <= j.radius)
            return false;
    return true;
}

bool Sensors::onRadar(Simulation& sim, Entity e, uint8_t viewer) {
    if (!enabled_) return false;
    collect(sim);
    return contact(sim, e, viewer);
}

std::vector<Entity> Sensors::blips(Simulation& sim, uint8_t viewer) {
    std::vector<Entity> out;
    if (!enabled_ || viewer == 0) return out;
    collect(sim);
    if (radars_.empty() && sonars_.empty()) return out;
    World& w = sim.world();
    w.each<Unit, Owner, Transform>([&](Entity e, Unit&, Owner&, Transform& tr) {
        if (!contact(sim, e, viewer)) return;
        if (m_->isVisible(sim, viewer, tr.pos) && m_->canSee(sim, e, viewer)) return;   // seen: no blip
        out.push_back(e);
    });
    return out;
}

void Sensors::beginTargeting(Simulation& sim) { if (targeting_) collect(sim); }

bool Sensors::tracked(Simulation& sim, Entity e, uint8_t viewer) {
    const Vec3 p = sim.world().get<Transform>(e).pos;
    return m_->isVisible(sim, viewer, p) || contact(sim, e, viewer);
}

bool Sensors::weaponHits(uint16_t shooter, size_t weapon, uint16_t target) const {
    if (target >= types_.size() || !types_[target].underwater) return true;
    return shooter < types_.size() && weapon < 8 && (types_[shooter].underwaterWeapons & (1u << weapon));
}

bool Sensors::anyWeaponHits(uint16_t shooter, uint16_t target) const {
    if (target >= types_.size() || !types_[target].underwater) return true;
    return shooter < types_.size() && types_[shooter].underwaterWeapons != 0;
}

} // namespace aiverse::game
