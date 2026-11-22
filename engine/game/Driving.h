// Driving on a grid map (rules "driving", docs/driving.md): the grid-map host of the one vehicle system
// (engine/vehicles/Vehicles.h). Vehicles are ordinary units of the types listed in driving.vehicles with the shared
// VehicleDrive component; whoever drives carries VehicleOccupant. This file is only what the grid map knows:
// unit types <-> vehicle types, cells <-> metres, the sweep of the body (three circles along the car) over the map
// grid, the units and the static colliders, decks (bridges / tunnels: map "decks" and spline-road spans), the hero's
// sticky top-down input as the driver's controls, kCmdTopDownEnter wiring, damage through the RTS combat code, and the
// race seats (engine/vehicles/VehicleRace.h runs the race).
//
// kCmdTopDownEnter toggles: on foot the hero gets into the nearest free vehicle in range, in a car it gets out (slow
// enough, next to a door, on a passable spot). While driving the hero is off the map (kUnitInCargo) and follows the
// car; the hero's kCmdTopDown input steers it ("tank" / "stick", mechanics/VehicleDrive.h), the fire bit is the handbrake.
//
// State: VehicleDrive / VehicleOccupant components (saved with the RTS pools), the DrivingDecks and RaceState world
// resources. Games without the block keep their checksums and save layout.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../core/ECS.h"
#include "../core/Fixed.h"
#include "../data/Json.h"
#include "../vehicles/Vehicles.h"
#include "../vehicles/VehicleRace.h"

namespace aiverse { class Simulation; class Hasher; class Archive; struct Command; }

namespace aiverse::game {

// A walker waiting at the kerb for vehicles (PedestrianVehicles.inc; kept only in games with traffic, which
// registers it).
// reason: 0 walking, 1 a vehicle is coming, 2 peeking past a parked one.
struct CrossingWait {
    uint8_t reason = 0;
    bool peeked = false;
    uint32_t clearUntil = 0, peekUntil = 0;
    friend void io(Archive& a, CrossingWait& c) { using aiverse::io; io(a, c.reason); io(a, c.peeked); io(a, c.clearUntil); io(a, c.peekUntil); }
};

class GameHost;

// A deck is a second level of the map: a bridge over a road, a tunnel under houses (map "decks", docs/driving.md).
// A car gets on at either end and stays between the walls until it leaves through an end; while it is on the
// deck the map grid under it does not stop it and it does not touch what is on the ground or on another deck.
struct DrivingDeck {
    std::string id;
    Fixed width = Fixed::fromInt(2);               // cells, wall to wall
    std::vector<Vec3> pts;                         // centre line: x, z in cells; y is the roadway height in metres
    std::vector<Fixed> at;                         // distance along the centre line at each point, cells
    Fixed length() const { return at.empty() ? Fixed{} : at.back(); }
};
struct DrivingDecks { std::vector<DrivingDeck> decks; };   // world resource, map order (VehicleDrive::deck indexes it)

class Driving {
public:
    bool active() const { return rules_.enabled; }
    const vehicles::DriveRules& rules() const { return rules_; }
    void setup(Simulation& sim, GameHost& m, const Json& rulesJson);
    // map "decks" and the bridges / tunnels of its spline roads ("roadNetwork", rules "roadTypes"); throws readable errors
    void loadMap(Simulation& sim, const Json& map, const Json& roadTypes = Json());
    bool command(Simulation& sim, const Command& cmd);   // true: kCmdTopDownEnter handled
    void tick(Simulation& sim);                          // after the top-down heroes moved
    void hash(Simulation& sim, Hasher& h) const;
    void io(Simulation& sim, Archive& a);
    const std::vector<DrivingDeck>* decks(Simulation& sim) const;                 // null without the block
    const vehicles::VehicleDrive* carOf(Simulation& sim, Entity driver) const;   // the car this unit drives, or null
    Entity vehicleOf(Simulation& sim, Entity driver) const;                       // its entity, or kNullEntity
    const vehicles::VehicleDrive* car(Simulation& sim, Entity vehicle) const;    // the state of a vehicle unit, or null
    bool isDriving(Simulation& sim, Entity hero) const { return carOf(sim, hero) != nullptr; }
    Fixed speed(Simulation& sim, Entity vehicle) const;                           // forward speed, m/s (0: not a vehicle)
    // Computer drivers (traffic, scripts) and locks: the shared API (vehicles::setAutopilot / setLocked), simulation code only.
    bool setAutopilot(Simulation& sim, Entity vehicle, const lib::mechanics::VehicleInput& input);
    bool clearAutopilot(Simulation& sim, Entity vehicle);
    bool setLocked(Simulation& sim, Entity vehicle, bool locked);
    bool locked(Simulation& sim, Entity vehicle) const;
    // The vehicle the enter key of this hero would take now (the nearest free one in reach, locked or not), or null.
    Entity enterTarget(Simulation& sim, Entity hero) const;
    // Levels. deckOf: index of the deck the vehicle is on, -1 on the ground. deckIndex: by map id, -1 unknown.
    // height: metres above the ground at the vehicle (0 on the ground), for the view. setDeck puts a vehicle on a
    // deck it already stands inside (traffic spawning a car in a tunnel) or back on the ground (-1); simulation code only.
    int deckOf(Simulation& sim, Entity vehicle) const;
    int deckIndex(Simulation& sim, const std::string& id) const;
    Fixed height(Simulation& sim, Entity vehicle) const;
    bool setDeck(Simulation& sim, Entity vehicle, int deck);
    // Races (rules "race" + map "race", docs/races.md): racer i + 1 is the i-th car of the map (entity order) and
    // belongs to player i + 1 (bots and players without a hero race with the race bot). racing(): this map has a race.
    bool racing(Simulation& sim) const;
    void loadRace(Simulation& sim, const Json& rules, const Json& map);   // after loadMap; throws readable errors
    lib::mechanics::RaceView raceView(Simulation& sim, uint8_t player) const;
    // takes a just spawned vehicle unit now (instead of at its next tick), rolling forward at `speed` m/s (traffic)
    bool adopt(Simulation& sim, Entity vehicle, Fixed speed);
    // the police pull the hero out of a slow car (false: too fast)
    bool policeExit(Simulation& sim, Entity hero);

private:
    class Host;
    class Seats;
    friend class Host;
    friend class Seats;
    const vehicles::VehicleType* typeOf(uint16_t unitType) const { return rules_.type(unitType); }
    void adoptAndDrop(Simulation& sim);
    void adoptOne(Simulation& sim, Entity e);
    void enter(Simulation& sim, uint8_t player, Entity hero);
    void seat(Simulation& sim, Entity car, uint8_t player, Entity hero);   // put the hero at the wheel (no checks)
    bool leave(Simulation& sim, Entity car, Entity hero, bool forced);
    bool collide(Simulation& sim, Entity car, Vec3& posCells, vehicles::VehicleImpacts& impacts);
    lib::mechanics::VehicleInput heroPedals(Simulation& sim, const vehicles::VehicleDrive& c, bool& ok) const;

    GameHost* m_ = nullptr;
    vehicles::DriveRules rules_;
    std::vector<Fixed> reach_;                      // per unit type: the farthest its body gets from its centre (derived)
    std::shared_ptr<const vehicles::VehicleRace> race_;   // rules + map course (derived); state: RaceState resource
};

} // namespace aiverse::game
