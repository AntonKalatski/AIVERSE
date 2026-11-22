// The `services` table of the scripting API: state of the traffic services (RtsServiceState.h) for mission and
// view scripts, and the handles scripts use to take a person, a vehicle or a hero over for a while.
#include "RtsScriptInternal.h"
#include "RtsInternal.h"
#include "RtsServiceState.h"
#include "RtsTraffic.h"
#include "RtsTopDown.h"
#include "RtsDriving.h"

namespace aiverse::game::script {

namespace {

bool aliveArg(lua_State* L, Vm& vm, int idx, Entity& e) {
    return argEntity(L, idx, e) && vm.sim->world().alive(e);
}
bool trafficOn(Vm& vm) { Traffic* t = vm.module ? vm.module->traffic() : nullptr; return t && t->active() && vm.sim; }
void pushOpt(lua_State* L, Entity e) { if (e.valid()) pushEntity(L, e); else lua_pushnil(L); }

// services.custody(player) -> phase, car | nil, deliveries   (phase: 0 free, 1 cuffed, 2 walked, 3 boarding, 4 driven, 5 delivered)
int sCustody(lua_State* L, Vm& vm) {
    int64_t p;
    if (!argInt(L, vm, 1, p, "player")) return -1;
    const ServiceLedger* l = trafficOn(vm) ? vm.sim->world().resource<ServiceLedger>() : nullptr;
    const CustodyPlayer* c = l && p >= 0 && size_t(p) < l->players.size() ? &l->players[size_t(p)] : nullptr;
    lua_pushinteger(L, c ? c->phase : 0); pushOpt(L, c ? c->car : kNullEntity); lua_pushinteger(L, c ? c->deliveries : 0);
    return 3;
}
// services.held_by(e) -> the custody vehicle that holds e, or nil
int sHeldBy(lua_State* L, Vm& vm) {
    Entity e;
    const InCustody* c = trafficOn(vm) && aliveArg(L, vm, 1, e) ? vm.sim->world().tryGet<InCustody>(e) : nullptr;
    pushOpt(L, c ? c->car : kNullEntity);
    return 1;
}
// services.stand_in(body) -> victim handle, victim type id   (nil: not a stand-in body)
int sStandIn(lua_State* L, Vm& vm) {
    Entity e;
    const StandIn* s = trafficOn(vm) && aliveArg(L, vm, 1, e) ? vm.sim->world().tryGet<StandIn>(e) : nullptr;
    if (!s) { lua_pushnil(L); return 1; }
    pushEntity(L, s->victim);
    const auto& types = vm.module->rules().types;
    lua_pushstring(L, s->original < types.size() ? types[s->original].id.c_str() : "");
    return 2;
}
// services.custody_car(car) -> phase, prisoner | nil   (phase 0: none; 1 cuffing ... 7 handing over: RtsServiceState.h)
int sCustodyCar(lua_State* L, Vm& vm) {
    Entity e;
    const CustodyCar* c = trafficOn(vm) && aliveArg(L, vm, 1, e) ? vm.sim->world().tryGet<CustodyCar>(e) : nullptr;
    lua_pushinteger(L, c ? c->phase : 0); pushOpt(L, c && c->owned ? c->prisoner : kNullEntity);
    return 2;
}
// services.carry(car) -> phase, patient | nil   (phase 0: none; 1 to the patient ... 7 handing over)
int sCarry(lua_State* L, Vm& vm) {
    Entity e;
    const CarryCar* c = trafficOn(vm) && aliveArg(L, vm, 1, e) ? vm.sim->world().tryGet<CarryCar>(e) : nullptr;
    lua_pushinteger(L, c ? c->phase : 0); pushOpt(L, c ? c->patient : kNullEntity);
    return 2;
}
// services.carried(car) -> delivered, x, z of the last carry this vehicle ended within a minute (nil: none)
int sCarried(lua_State* L, Vm& vm) {
    Entity e;
    if (!trafficOn(vm) || !argEntity(L, 1, e)) { lua_pushnil(L); return 1; }
    const ServiceLedger* l = vm.sim->world().resource<ServiceLedger>();
    const CarryResult* r = nullptr;
    if (l) for (const auto& x : l->carried) if (x.car == e) r = &x;
    if (!r) { lua_pushnil(L); return 1; }
    lua_pushboolean(L, r->delivered); pushFx(L, r->exitX); pushFx(L, r->exitZ);
    return 3;
}
// services.totals() -> custody deliveries, carry deliveries, calls answered by a vehicle already out
int sTotals(lua_State* L, Vm& vm) {
    const ServiceLedger* l = trafficOn(vm) ? vm.sim->world().resource<ServiceLedger>() : nullptr;
    lua_pushinteger(L, l ? l->custodyDelivered : 0); lua_pushinteger(L, l ? l->carryDelivered : 0); lua_pushinteger(L, l ? l->dispatchReused : 0);
    return 3;
}
// services.crossing(e) -> 0 walking, 1 waiting for a vehicle, 2 peeking past a parked one
int sCrossing(lua_State* L, Vm& vm) {
    Entity e;
    const CrossingWait* c = trafficOn(vm) && aliveArg(L, vm, 1, e) ? vm.sim->world().tryGet<CrossingWait>(e) : nullptr;
    lua_pushinteger(L, c ? c->reason : 0);
    return 1;
}
// services.passenger(e) -> claim, aboard, walk result, walk done   (claim 0: nobody holds him)
int sPassenger(lua_State* L, Vm& vm) {
    Entity e;
    const ServicePassenger* p = trafficOn(vm) && aliveArg(L, vm, 1, e) ? vm.sim->world().tryGet<ServicePassenger>(e) : nullptr;
    lua_pushinteger(L, p ? p->claim : 0); lua_pushboolean(L, p && p->aboard);
    lua_pushinteger(L, p ? p->walkResult : 0); lua_pushinteger(L, p ? p->walkDone : 0);
    return 4;
}
// services.citizen(id) -> held, off, journey   (nil: no such citizen)
int sCitizen(lua_State* L, Vm& vm) {
    int64_t id;
    if (!argInt(L, vm, 1, id, "citizen")) return -1;
    TrafficStore* st = trafficOn(vm) ? vm.module->traffic()->store(*vm.sim) : nullptr;
    if (!st || id < 0 || size_t(id) >= st->citizens.size()) { lua_pushnil(L); return 1; }
    const TrafficCitizen& c = st->citizens[size_t(id)];
    lua_pushboolean(L, c.held); lua_pushboolean(L, c.off); lua_pushinteger(L, c.journey);
    return 3;
}

// ---- actions (simulation scripts)
ServicePassenger* passengerOf(lua_State* L, Vm& vm, Entity& e) {
    if (!trafficOn(vm) || !aliveArg(L, vm, 1, e)) return nullptr;
    World& w = vm.sim->world();
    if (!w.has<ServicePassenger>(e)) w.add<ServicePassenger>(e);
    return &w.get<ServicePassenger>(e);
}
// services.claim(e, claim): take a person over (claim > 0; 0 gives him back to the pedestrians)
int sClaim(lua_State* L, Vm& vm) {
    Entity e; int64_t c;
    if (!argInt(L, vm, 2, c, "claim")) return -1;
    if (auto* p = passengerOf(L, vm, e)) { p->claim = uint8_t(std::clamp<int64_t>(c, 0, 255)); if (!p->claim) p->aboard = false; }
    return 0;
}
// services.board(e, aboard): a claimed person rides inside a vehicle (carried as cargo) or not
int sBoard(lua_State* L, Vm& vm) {
    Entity e;
    if (auto* p = passengerOf(L, vm, e)) p->aboard = lua_toboolean(L, 2);
    return 0;
}
// services.walk(e, request, x, z): a claimed person walks the sidewalks to (x, z); request: a new positive number per walk
int sWalk(lua_State* L, Vm& vm) {
    Entity e; int64_t r; Fixed x, z;
    if (!argInt(L, vm, 2, r, "request") || !argFx(L, vm, 3, x, "x") || !argFx(L, vm, 4, z, "z")) return -1;
    if (auto* p = passengerOf(L, vm, e)) { p->walkRequest = int32_t(std::clamp<int64_t>(r, 0, INT32_MAX)); p->walkX = x; p->walkZ = z; }
    return 0;
}
// services.signals(vehicle, beacon, siren): >0 on, <0 off, 0 automatic
int sSignals(lua_State* L, Vm& vm) {
    Entity e; int64_t b, s;
    if (!argInt(L, vm, 2, b, "beacon") || !argInt(L, vm, 3, s, "siren")) return -1;
    if (!trafficOn(vm) || !aliveArg(L, vm, 1, e)) return 0;
    World& w = vm.sim->world();
    if (!b && !s) { w.remove<VehicleSignals>(e); return 0; }
    if (!w.has<VehicleSignals>(e)) w.add<VehicleSignals>(e);
    auto& v = w.get<VehicleSignals>(e);
    v.beacon = int8_t(b > 0 ? 1 : b < 0 ? -1 : 0); v.siren = int8_t(s > 0 ? 1 : s < 0 ? -1 : 0);
    return 0;
}
// services.door(vehicle, open): 0 closed, -1 / 1 open on the left / right (presentation)
int sDoor(lua_State* L, Vm& vm) {
    Entity e; int64_t o;
    if (!argInt(L, vm, 2, o, "open")) return -1;
    if (!trafficOn(vm) || !aliveArg(L, vm, 1, e)) return 0;
    World& w = vm.sim->world();
    if (!w.has<VehicleDoor>(e)) w.add<VehicleDoor>(e);
    w.get<VehicleDoor>(e).open = int8_t(o > 0 ? 1 : o < 0 ? -1 : 0);
    return 0;
}
// services.hold(car, held): the script runs the crew's work at the scene / their way back in (ServiceHold)
int sHold(lua_State* L, Vm& vm) {
    Entity e;
    if (!trafficOn(vm) || !aliveArg(L, vm, 1, e)) return 0;
    World& w = vm.sim->world();
    if (lua_toboolean(L, 2)) { if (!w.has<ServiceHold>(e)) w.add<ServiceHold>(e); }
    else w.remove<ServiceHold>(e);
    return 0;
}
// services.set_citizen(id, held, off, journey)
int sSetCitizen(lua_State* L, Vm& vm) {
    int64_t id, j;
    if (!argInt(L, vm, 1, id, "citizen") || !argInt(L, vm, 4, j, "journey")) return -1;
    TrafficStore* st = trafficOn(vm) ? vm.module->traffic()->store(*vm.sim) : nullptr;
    if (!st || id < 0 || size_t(id) >= st->citizens.size()) return 0;
    TrafficCitizen& c = st->citizens[size_t(id)];
    c.held = lua_toboolean(L, 2); c.off = lua_toboolean(L, 3); c.journey = uint8_t(std::clamp<int64_t>(j, 0, 2));
    return 0;
}
// services.control(player, held): the player's stick moves his hero (false) or not (true: a script moves him)
int sControl(lua_State* L, Vm& vm) {
    int64_t p;
    if (!argInt(L, vm, 1, p, "player")) return -1;
    TopDown* td = vm.module ? vm.module->topDown() : nullptr;
    TopDownStore* st = td && td->active() ? td->store(*vm.sim) : nullptr;
    if (st && p > 0 && size_t(p) < st->pilots.size()) st->pilots[size_t(p)].controlHeld = lua_toboolean(L, 2);
    return 0;
}

} // namespace

void openServicesApi(Vm& vm) {
    lua_State* L = vm.L;
    static const luaL_Reg queries[] = {
        {"custody", bindQ<sCustody>}, {"held_by", bindQ<sHeldBy>}, {"stand_in", bindQ<sStandIn>}, {"custody_car", bindQ<sCustodyCar>},
        {"carry", bindQ<sCarry>}, {"carried", bindQ<sCarried>}, {"totals", bindQ<sTotals>}, {"crossing", bindQ<sCrossing>},
        {"passenger", bindQ<sPassenger>}, {"citizen", bindQ<sCitizen>}, {nullptr, nullptr}};
    static const luaL_Reg actions[] = {
        {"claim", bind<sClaim>}, {"board", bind<sBoard>}, {"walk", bind<sWalk>}, {"signals", bind<sSignals>}, {"door", bind<sDoor>},
        {"hold", bind<sHold>}, {"set_citizen", bind<sSetCitizen>}, {"control", bind<sControl>}, {nullptr, nullptr}};
    luaL_newlib(L, queries);
    if (vm.mode == Mode::Sim) luaL_setfuncs(L, actions, 0);
    lua_setglobal(L, "services");
}

} // namespace aiverse::game::script
