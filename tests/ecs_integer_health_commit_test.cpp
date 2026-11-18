// The integer HP commit used by generic ECS damage and live FPS must preserve
// the previous valid FPS result, including zero, death and excessive damage.
// This test is deliberately independent of modules/fps and modules/rts.
#include <aiverselib/genre/EcsDamageSystems.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace aiverse;
namespace lg=aiverse::lib::genre;

static void check(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
int main() {
    for(int health=0;health<=100;++health) {
        for(int incoming=0;incoming<=1000;++incoming) {
            int32_t value=health;
            const int32_t applied=lg::applyIntegerHitPointDamage(value,incoming);
            const int32_t expected=std::max(0,health-incoming);
            check(value==expected,"FPS-compatible health result changed");
            check(applied==health-expected,"damage report differs from health delta");
        }
    }
    int32_t empty=0;
    check(lg::applyIntegerHitPointDamage(empty,25)==0 && empty==0,
          "dead target received damage");
    int32_t negative=-5;
    check(lg::applyIntegerHitPointDamage(negative,12)==0 && negative==-5,
          "invalid negative health was silently rewritten");
    int32_t healthy=90;
    check(lg::applyIntegerHitPointDamage(healthy,-5)==0 && healthy==90,
          "negative damage healed a target");
    check(lg::applyIntegerHitPointDamage(healthy,0)==0 && healthy==90,
          "zero damage changed target state");

    int32_t maximum=std::numeric_limits<int32_t>::max();
    check(lg::applyIntegerHitPointDamage(
              maximum,std::numeric_limits<int32_t>::max())==
              std::numeric_limits<int32_t>::max() && maximum==0,
          "maximal damage overflowed");
    int32_t partial=std::numeric_limits<int32_t>::max();
    check(lg::applyIntegerHitPointDamage(partial,5)==5 &&
          partial==std::numeric_limits<int32_t>::max()-5,
          "large remaining health was not updated exactly");

    std::cout<<"PASS generic ECS and FPS integer health commit parity\n";
}
