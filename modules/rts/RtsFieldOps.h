// Field operations (docs/game-format.md § Field operations): what builders and long-range weapons do in
// flow-economy wars, as optional blocks.
//
//  - rules "repair": {"cost": 0, "units": true}: repair price (share of the build cost for a full repair,
//    default 0.3) and whether mobile units can be repaired; kCmdRepair orders it explicitly.
//  - rules "reclaim": {"units": true, "refund": 1}: builders take units (own or enemy) apart for that share of
//    their cost (kCmdReclaim; on a resource node it is a gather order). Reclaimed units leave no wreck.
//  - type "assist": true: a builder guarding (kCmdGuard) an own factory adds its build power to the factory.
//  - type "patrolWork": true: a builder on patrol repairs damaged allies and reclaims resource nodes it passes.
//  - rules "selfDestruct": {"time": 5}: kCmdSelfDestruct arms a countdown (again: disarms); at zero the unit
//    dies with its death effects.
//  - type "stockpile": {"max": 5, "time": 60, "cost": {...}, "abilities": ["nuke"]}: builds charges, paying the
//    cost while it builds (stalls when short); the listed abilities (default: all of the type) need and use one.
//  - type "intercept": {"radius": 40, "tags": ["nuke"]}: destroys hostile projectiles shot by types with those
//    tags inside the radius, one stockpile charge each when the type has a stockpile.
//  - rules / map "ambient": {"wind": {"min": 0, "max": 25, "every": [15, 45]}, "tidal": 18} and type
//    "ambientIncome": {"wind": {"energy": 1}}: income that follows a changing wind or the map's tide.
//  - type "attackRun": {"overshoot": 4}: an aircraft attacks in passes over its target instead of hovering.
//  - type "repairPad": {"heal": 40, "radius": 1.5} heals units resting on it; type "seekRepair": 0.3 sends an idle
//    unit below that health share to the nearest own pad.
//
// State lives in the FieldOpsStore world resource (hashed, saved, copied by rollback snapshots). Games without
// any of these blocks have no store, no extra system and keep their checksums.
#pragma once
#include <cstdint>
#include <vector>
#include "../../engine/core/ECS.h"
#include "../../engine/core/Fixed.h"
#include "RtsData.h"

namespace aiverse { class Simulation; class Hasher; class Archive; class Json; struct Command; }

namespace aiverse::game {

class RtsModule;
struct Order;

struct StockpileDef { int max = 0; Fixed time = Fixed::fromInt(60); std::vector<Amount> cost; uint8_t abilities = 0xFF; };
struct InterceptDef { Fixed radius{}; uint64_t tags = 0; };
struct AmbientIncome { uint8_t source = 0; uint16_t res = 0; Fixed amount{}; };   // source: 0 wind, 1 tidal

struct FieldOpsTypeDef {
    bool assist = false, patrolWork = false;
    StockpileDef stockpile;
    InterceptDef intercept;
    std::vector<AmbientIncome> ambient;
    Fixed overshoot{};                     // attackRun
    Fixed padHeal{}, padRadius = Fixed::fromRatio(3, 2);
    Fixed seekRepair{};
};

struct AmbientDef {
    bool wind = false;
    Fixed windMin{}, windMax = Fixed::fromInt(20), everyMin = Fixed::fromInt(15), everyMax = Fixed::fromInt(45);
    Fixed tidal{};
};

struct FieldOpsStore {
    struct Countdown { Entity e; uint32_t at = 0; };
    struct Stock { Entity e; int count = 0; Fixed progress{}; };
    struct Run { Entity e; Entity target; Vec3 goal{}; Vec3 dir{}; };
    std::vector<Countdown> selfDestruct;   // sorted by entity index
    std::vector<Stock> stocks;             // sorted by entity index
    std::vector<Run> runs;                 // sorted by entity index
    Fixed wind{};
    uint32_t nextWind = 0;
};

// kEvFieldOp values (event value = kind | data << 8)
enum FieldOpEvent : int64_t { kFieldOpArmed = 1, kFieldOpDisarmed = 2, kFieldOpStockpiled = 3, kFieldOpIntercepted = 4, kFieldOpWind = 5 };

class FieldOps {
public:
    bool active() const { return enabled_; }
    void setup(Simulation& sim, RtsModule& m, const Json& rulesJson, const Json& map);
    bool command(Simulation& sim, const Command& cmd);   // kCmdRepair / kCmdReclaim / kCmdSelfDestruct
    void tick(Simulation& sim);                          // after targeting: countdowns, stockpiles, interceptors, runs, pads
    void economy(Simulation& sim);                       // sysEconomy: ambient income
    void hash(Simulation& sim, Hasher& h) const;
    void io(Simulation& sim, Archive& a);
    FieldOpsStore* store(Simulation& sim) const;
    const FieldOpsTypeDef* def(uint16_t type) const { return type < types_.size() ? &types_[type] : nullptr; }

    // hooks into the generic code
    Fixed repairCost() const { return repairCost_; }               // RtsUnits.cpp OrderType::Repair
    bool repairsUnits() const { return repairUnits_; }             // smart click on a damaged own unit
    Fixed assistMult(Simulation& sim, Entity producer);            // sysProduction: 1 + assisting build power
    bool castReady(Simulation& sim, Entity caster, int ability) const;   // canCast: a stockpile charge is there
    void onCast(Simulation& sim, Entity caster, int ability);      // castAbility: uses the charge
    bool runReclaim(Simulation& sim, Entity e, Order& o);          // OrderType::Reclaim
    int stockpile(Simulation& sim, Entity e) const;                // charges ready (-1: no stockpile)
    uint32_t selfDestructAt(Simulation& sim, Entity e) const;      // 0: not armed
    Fixed wind(Simulation& sim) const;
    Fixed tidal() const { return ambient_.tidal; }
    bool selfDestructEnabled() const { return selfDestructOn_; }

private:
    bool builder(uint16_t type) const;
    Fixed payStep(Simulation& sim, uint8_t player, const std::vector<Amount>& cost, Fixed step);
    void patrolWork(Simulation& sim);
    void interceptors(Simulation& sim, FieldOpsStore& st);
    void attackRuns(Simulation& sim, FieldOpsStore& st);
    void pads(Simulation& sim);

    RtsModule* m_ = nullptr;
    bool enabled_ = false, anyStock_ = false, anyIntercept_ = false, anyRun_ = false, anyPad_ = false, anyPatrol_ = false, anyAssist_ = false;
    bool selfDestructOn_ = false, reclaimUnits_ = false, repairUnits_ = false;
    Fixed selfDestructTime_ = Fixed::fromInt(5), repairCost_ = Fixed::fromRatio(3, 10), reclaimRefund_ = Fixed::fromInt(1);
    AmbientDef ambient_;
    std::vector<FieldOpsTypeDef> types_;
};

} // namespace aiverse::game
