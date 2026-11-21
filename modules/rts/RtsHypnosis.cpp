// Simulation support shared by every game: sends out of a hypnotised garrison (status flag "hypnotised",
// StatusDef::sendShare / captureSent), per capture point produced / kill counters (kept when the rules' computer
// controller asks for them, AIControllerKind::pointStats) and the player-wide stat of a type without an entity.
#include "RtsInternal.h"

namespace aiverse::game {


void RtsModule::notePointProduced(Simulation& sim, Entity e, int n) {
    if (!aiKind_ || !aiKind_->pointStats) return;   // only games whose controller asks for them keep (and hash) the counters
    ModesState& ms = state(sim).modes;
    if (ms.stats.empty()) return;
    if (ms.pointProduced.size() <= e.index) ms.pointProduced.resize(e.index + 1, 0);
    ms.pointProduced[e.index] += n;
}

// kCmdSend from a structure the sender does not own: allowed while it carries a "hypnotised" status the sender
// applied. The units stay the owner's (or become the sender's with captureSent); the status ends with the send
// (BW Modificator.OnUnitsMoved -> Deactivate). Returns true when the command was such a send.
bool RtsModule::hypnotisedSend(Simulation& sim, const Command& cmd) {
    if (!rules_.advancedEffects || cmd.type != kCmdSend) return false;
    World& w = sim.world();
    bool handled = false;
    for (Entity e : cmd.units) {
        if (!w.alive(e) || !w.has<Garrison>(e) || !w.has<Owner>(e) || w.get<Owner>(e).player == cmd.player) continue;
        auto* st = w.tryGet<Statuses>(e);
        if (!st) continue;
        StatusInst* hyp = nullptr;
        for (auto& in : st->list)
            if ((rules_.statuses[in.def].flags & kStHypnotised) && in.owner == cmd.player && in.until > sim.tick()) { hyp = &in; break; }
        if (!hyp) continue;
        handled = true;
        const StatusDef& sd = rules_.statuses[hyp->def];
        if (!w.alive(cmd.target) || cmd.target == e || !w.has<Garrison>(cmd.target)) continue;
        const UnitType& t = rules_.types[w.get<Unit>(e).type];
        if (t.spawnUnit == kNoType) continue;
        if (native_.has(kHookSend) && !native_.allowSend(*this, sim, e, cmd.target)) continue;
        if (!canSend(sim, e, cmd.target)) continue;
        Garrison& g = w.get<Garrison>(e);
        const int share = int((Fixed::fromInt(g.count) * fxClamp(sd.sendShare, Fixed{}, Fixed::fromInt(1))).floorInt());
        int n = cmd.param < 0 ? int(std::min<int64_t>(share, -cmd.param)) : int(int64_t(share) * std::clamp<int64_t>(cmd.param, 1, 100) / 100);
        if (n <= 0) continue;
        const uint8_t owner = w.get<Owner>(e).player;
        hyp->until = sim.tick();   // one group per hypnosis
        if (rules_.routes.groups) {
            sendAlongRoute(sim, e, cmd.target, owner, n);
            if (sd.captureSent)
                for (auto& sg : state(sim).routes.groups)
                    if (sg.from == e && sg.to == cmd.target && sg.owner == owner) { sg.owner = cmd.player; sg.payer = owner; }
            continue;
        }
        g.count -= n;
        g.pendingOut += n;
        g.pendingTarget = cmd.target;
        g.pendingGroup = state(sim).nextGroupId++;
    }
    return handled;
}

// Player-wide stat of a type (no entity): RtsModule::stat without the per-entity parts. moraleDelta adds that many
// levels of the perUnit resources (morale), clamped to their storage.
Fixed RtsModule::typeStatWithMorale(Simulation& sim, uint8_t owner, uint16_t type, Stat s, int moraleDelta) const {
    const UnitType& t = rules_.types[type];
    const Fixed kOne = Fixed::fromInt(1);
    Fixed base = s == Stat::Speed ? t.speed : s == Stat::GarrisonCap ? Fixed::fromInt(t.garrisonMax) : s == Stat::Range ? Fixed{} : kOne;
    auto* gs = sim.world().resource<GameState>();
    Fixed add{}, mul = kOne;
    if (owner < gs->players.size() && !gs->players[owner].mods.empty()) {
        const TypeMods& m = gs->players[owner].mods[type];
        add += m.add[size_t(s)]; mul *= m.mul[size_t(s)];
    }
    if (rules_.advancedEffects && owner < gs->players.size())
        for (auto& in : gs->players[owner].statuses)
            for (auto& m : rules_.statuses[in.def].mods)
                if (m.stat == s) { add += m.add * in.stacks; for (int k = 0; k < in.stacks; ++k) mul *= m.mul; }
    if (rules_.originals) {
        add += playerModsStat(sim, owner, type, s, mul);
        if (moraleDelta != 0 && owner != 0 && owner < gs->players.size())
            for (uint16_t r : rules_.perUnitResources) {
                const auto& eco = gs->players[owner];
                const int n = int(fxMax(Fixed{}, eco.stock[r]).floorInt());
                Fixed cap = eco.cap[r].raw > 0 ? eco.cap[r] : rules_.resources[r].storage;
                int to = std::max(0, n + moraleDelta);
                if (cap.raw > 0) to = std::min(to, int(cap.floorInt()));
                const int extra = to - n;
                if (extra <= 0) continue;
                for (auto& [tags, mod] : rules_.resources[r].perUnitMods) {
                    if (mod.stat != s || (tags && !(t.tags & tags))) continue;
                    add += mod.add * extra;
                    for (int k = 0; k < extra; ++k) mul *= mod.mul;
                }
            }
    }
    return (base + add) * mul;
}

} // namespace aiverse::game
