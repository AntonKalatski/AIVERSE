// MotionWeave: cosmetic actions inferred from confirmed replicated counters.
// No input commands, hit resolution or gameplay authority are modified.
#pragma once
#include "AnimationMontage.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace aiverse::lib::animation {
struct ReplicatedAnimationState {
    uint32_t tick=0, life=0, shots=0, reloadUntil=0;
    int32_t health=0;
};
struct CosmeticAnimationActions {
    bool fire=false,reload=false,death=false,respawn=false;
};
class ReplicatedAnimationActions {
public:
    CosmeticAnimationActions observe(ReplicatedAnimationState next) {
        CosmeticAnimationActions result;
        if (!initialized_ || next.life!=last_.life) {
            result.respawn=initialized_;
            last_=next;initialized_=true;
            return result; // no phantom shot/death for first sighting
        }
        if(next.tick<last_.tick)return result; // out-of-order net update
        result.fire=next.shots>last_.shots && next.health>0;
        result.reload=next.reloadUntil>next.tick &&
            next.reloadUntil!=last_.reloadUntil && next.health>0;
        result.death=last_.health>0 && next.health<=0;
        last_=next;
        return result;
    }
    void reset(){initialized_=false;last_={};}
private:
    ReplicatedAnimationState last_{};
    bool initialized_=false;
};
// Replace an active montage in the SAME slot, allow simultaneous slots
// with different masks. The stage owner must call this before graph update.
inline bool playAnimationSlot(AnimationMontageSlotsComponent& slots,
        std::shared_ptr<const MontageDefinition> asset) {
    if(!asset)return false;
    for(auto& current:slots.players) {
        if(current.slot()==asset->slot)
            return current.play(std::move(asset));
    }
    if(slots.players.size()>=8)return false;
    slots.players.emplace_back();
    if(!slots.players.back().play(std::move(asset))) {
        slots.players.pop_back();
        return false;
    }
    return true;
}
} // namespace aiverse::lib::animation
