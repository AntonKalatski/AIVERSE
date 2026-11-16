// Sensors: radar, sonar, jammers, stealth and the underwater layer (docs/game-format.md § Radar, sonar and jammers).
//
// A radar (type "radar": radius) shows enemies inside its circle to its owner and allies as contacts
// ("blips") even outside sight; sonar ("sonar": radius) does the same for underwater units ("underwater": true),
// which radar does not see. A jammer ("jammer": radius) hides its own side's units inside its circle from enemy
// radar and sonar; "stealth": true units never show on radar. With rules "radar": {"targeting": true} units
// acquire targets beyond their sight up to their weapon range when an ally sees the target or it is a contact
// (long-range artillery firing at radar dots). Underwater units are hit only by weapons with "underwater": true.
//
// No state of its own: contacts are a pure function of the units, recomputed when asked, so saves, replays and
// rollback need nothing extra. Games without any of these keys keep their checksums.
#pragma once
#include <cstdint>
#include <vector>
#include "../../engine/core/ECS.h"
#include "../../engine/core/Fixed.h"

namespace aiverse { class Simulation; class Json; }

namespace aiverse::game {

class RtsModule;

struct SensorTypeDef {
    Fixed radar{}, sonar{}, jammer{};      // radii (cells); 0 = none
    bool stealth = false;                  // never a radar / sonar contact
    bool underwater = false;               // only "underwater" weapons hit it; sonar (not radar) tracks it
    uint8_t underwaterWeapons = 0;         // bit per weapon (0..7): hits underwater units
};

class Sensors {
public:
    bool active() const { return enabled_; }
    void setup(Simulation& sim, RtsModule& m, const Json& rulesJson);
    const SensorTypeDef* def(uint16_t type) const { return type < types_.size() ? &types_[type] : nullptr; }

    // Radar / sonar contact: an enemy of `viewer` inside the viewer team's radar (sonar for underwater units),
    // not covered by a jammer of its own side and not stealthy. Says nothing about sight.
    bool onRadar(Simulation& sim, Entity e, uint8_t viewer);
    // Contacts the viewer does not see (for the view and scripts): enemies on radar outside the viewer's sight.
    std::vector<Entity> blips(Simulation& sim, uint8_t viewer);

    // targeting hooks (RtsCombat.cpp)
    bool radarTargeting() const { return targeting_; }
    void beginTargeting(Simulation& sim);                          // refreshes the source lists for this pass
    bool tracked(Simulation& sim, Entity e, uint8_t viewer);       // seen by the viewer's side or a contact
    bool underwater(uint16_t type) const { return type < types_.size() && types_[type].underwater; }
    bool weaponHits(uint16_t shooter, size_t weapon, uint16_t target) const;   // false: underwater target, weapon can't
    bool anyWeaponHits(uint16_t shooter, uint16_t target) const;

private:
    struct Source { Vec3 pos; Fixed radius; uint8_t owner; };
    void collect(Simulation& sim);
    bool covered(Simulation& sim, const std::vector<Source>& list, Vec3 p, uint8_t side) const;
    bool contact(Simulation& sim, Entity e, uint8_t viewer) const;   // with the current source lists

    RtsModule* m_ = nullptr;
    bool enabled_ = false, targeting_ = false;
    std::vector<SensorTypeDef> types_;
    std::vector<Source> radars_, sonars_, jammers_;
};

} // namespace aiverse::game
