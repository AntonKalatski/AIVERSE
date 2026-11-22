#pragma once
// ECS state of the traffic services (RtsTrafficCity.cpp): custody (traffic.services.<id>.custody), carry
// (traffic.services.<id>.carry) and people a script or native module has taken over (ServicePassenger).
// Registered as persistent components (saved and hashed) by Traffic::setup when the game has traffic.
// Scripts use them through the `services` Lua table (RtsServicesApi.cpp); native modules use them directly.
#include "../../engine/core/Archive.h"
#include "../../engine/core/ECS.h"
#include "../../engine/core/Fixed.h"
#include <cstdint>
#include <utility>
#include <vector>

namespace aiverse::game {

// Custody phases of a vehicle (CustodyCar::phase).
enum CustodyPhase : uint8_t { kCustodyNone = 0, kCustodyCuffing = 1, kCustodyToDoor = 2, kCustodyBoarding = 3,
                              kCustodyDriving = 5, kCustodyToStation = 6, kCustodyHandover = 7 };
// What the held player sees (CustodyPlayer::phase).
enum CustodyPlayerPhase : uint8_t { kHeldNone = 0, kHeldCuffed = 1, kHeldWalked = 2, kHeldBoarding = 3, kHeldDriven = 4,
                                    kHeldDelivered = 5 };

// On a custody vehicle that has taken an offender.
struct CustodyCar {
    Entity prisoner = kNullEntity;
    bool owned = false;                      // the prisoner is this vehicle's (claimed with InCustody)
    uint8_t phase = kCustodyNone;
    uint32_t since = 0;                      // tick the phase began
    bool wasDisabled = false, wasPossessed = false;
    bool doorOpen = false;                   // presentation: the rear door is open
    Fixed doorX{}, doorZ{}, entryX{}, entryZ{};
    std::vector<std::pair<Entity, uint8_t>> crewHeld;   // crew it froze: 1 = was free, 2 = was already disabled
    friend void io(Archive& a, CustodyCar& c) {
        using aiverse::io;
        io(a, c.prisoner); io(a, c.owned); io(a, c.phase); io(a, c.since); io(a, c.wasDisabled); io(a, c.wasPossessed); io(a, c.doorOpen);
        io(a, c.doorX); io(a, c.doorZ); io(a, c.entryX); io(a, c.entryZ); io(a, c.crewHeld);
    }
};
// On an offender (or a stand-in body) claimed by a custody vehicle.
struct InCustody {
    Entity car = kNullEntity;
    friend void io(Archive& a, InCustody& c) { using aiverse::io; io(a, c.car); }
};
// On the body a fallen hero / casualty leaves for the officers to take away.
struct StandIn {
    Entity victim = kNullEntity;             // gone by now; kept as an id
    uint16_t original = 0;                   // unit type of the victim (a hero comes back as it)
    friend void io(Archive& a, StandIn& s) { using aiverse::io; io(a, s.victim); io(a, s.original); }
};
// A person taken over by a service, a script or a native module: the pedestrian system leaves him alone.
// claim: who holds him (non-zero; custody uses kClaimCustody); aboard: inside a vehicle (carried as cargo).
// A walk request (walkRequest > 0, to walkX / walkZ) is answered by the pedestrian system:
// walkResult = +walkRequest (on his way) or -walkRequest (no way there); walkDone = walkRequest once he stands there.
enum : uint8_t { kClaimCustody = 6 };
struct ServicePassenger {
    uint8_t claim = 0;
    bool aboard = false;
    bool seen = false;                       // the pedestrian system saw him claimed last tick
    int32_t walkRequest = 0, walkResult = 0, walkDone = 0;
    Fixed walkX{}, walkZ{};
    friend void io(Archive& a, ServicePassenger& p) {
        using aiverse::io;
        io(a, p.claim); io(a, p.aboard); io(a, p.seen); io(a, p.walkRequest); io(a, p.walkResult); io(a, p.walkDone);
        io(a, p.walkX); io(a, p.walkZ);
    }
};
// Carry phases (CarryCar::phase).
enum CarryPhase : uint8_t { kCarryNone = 0, kCarryToPatient = 1, kCarryTreating = 2, kCarryToVehicle = 3, kCarryLoading = 4,
                            kCarryDriving = 5, kCarryToWard = 6, kCarryHandover = 7 };
// On a carry vehicle (traffic.services.<id>.carry) that has taken a patient.
struct CarryCar {
    Entity patient = kNullEntity;
    bool owned = false;                      // the patient is this vehicle's until delivered
    uint8_t phase = kCarryNone;
    uint32_t since = 0;
    Fixed carriedX{}, carriedZ{}, facingX{}, facingZ{};   // presentation: where the stretcher is, which way it faces
    Fixed entranceX{}, entranceZ{}, destinationX{}, destinationZ{};
    bool wardFound = false;                  // a ward (traffic.services.<id>.wards) was found to drive to
    friend void io(Archive& a, CarryCar& c) {
        using aiverse::io;
        io(a, c.patient); io(a, c.owned); io(a, c.phase); io(a, c.since); io(a, c.carriedX); io(a, c.carriedZ);
        io(a, c.facingX); io(a, c.facingZ); io(a, c.entranceX); io(a, c.entranceZ); io(a, c.destinationX); io(a, c.destinationZ);
        io(a, c.wardFound);
    }
};
// How a carry ended (ServiceLedger::carried): kept for a minute for scripts / native modules that follow a patient.
struct CarryResult {
    Entity car = kNullEntity, patient = kNullEntity;
    bool delivered = false;                  // false: given up (patient lost, crew short); exit = where he lies
    Fixed exitX{}, exitZ{};
    uint32_t tick = 0;
    friend void io(Archive& a, CarryResult& r) {
        using aiverse::io; io(a, r.car); io(a, r.patient); io(a, r.delivered); io(a, r.exitX); io(a, r.exitZ); io(a, r.tick);
    }
};
// Beacon / siren of a vehicle set by a script or native module: >0 on, <0 off, 0 automatic (on while answering a call).
struct VehicleSignals {
    int8_t beacon = 0, siren = 0;
    friend void io(Archive& a, VehicleSignals& v) { using aiverse::io; io(a, v.beacon); io(a, v.siren); }
};
// On a service vehicle whose crew work a script / native module drives: the vehicle does not finish getting there
// (kTaskCrewOut) or getting its crew back (kTaskCrewBack) on its own; the owner moves the task on or removes it.
struct ServiceHold {
    uint8_t by = 1;                          // free for the owner
    friend void io(Archive& a, ServiceHold& h) { using aiverse::io; io(a, h.by); }
};
// On a crew member a service vehicle let out.
struct ServiceCrew {
    Entity car = kNullEntity;
    uint32_t since = 0;                      // tick he got out
    friend void io(Archive& a, ServiceCrew& c) { using aiverse::io; io(a, c.car); io(a, c.since); }
};
// A vehicle's door a script / native module opened (presentation; services close it when they drive off):
// 0 closed, -1 / +1 open on the left / right.
struct VehicleDoor {
    int8_t open = 0;
    friend void io(Archive& a, VehicleDoor& d) { using aiverse::io; io(a, d.open); }
};
// World resource: per player (index = player slot) and totals.
struct CustodyPlayer {
    uint8_t phase = kHeldNone;
    Entity car = kNullEntity;
    uint32_t deliveries = 0;
    friend void io(Archive& a, CustodyPlayer& p) { using aiverse::io; io(a, p.phase); io(a, p.car); io(a, p.deliveries); }
};
struct ServiceLedger {
    std::vector<CustodyPlayer> players;
    uint32_t custodyDelivered = 0, carryDelivered = 0;
    uint32_t dispatchReused = 0;             // calls answered by a vehicle already out (not from a station)
    std::vector<CarryResult> carried;        // the last minute's ends of carries, oldest first
    CustodyPlayer& player(size_t slot) { if (players.size() <= slot) players.resize(slot + 1); return players[slot]; }
    friend void io(Archive& a, ServiceLedger& l) { using aiverse::io; io(a, l.players); io(a, l.custodyDelivered); io(a, l.carryDelivered); io(a, l.dispatchReused); io(a, l.carried); }
};
inline ServiceLedger& serviceLedger(World& w) {
    if (auto* l = w.resource<ServiceLedger>()) return *l;
    return w.setResource<ServiceLedger>();
}

} // namespace aiverse::game
