// SpectraFX GPU presentation-only slot lifecycle.
// Generation-safe handles; particle positions remain GPU-owned.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "SpectraFxGpuEvolution.h"

namespace aiverse::fx {
struct SpectraFxGpuParticleHandle {
    uint32_t index=0, generation=0;
    bool valid() const { return generation!=0; }
};

class SpectraFxGpuParticleSlots {
public:
    struct Stats {
        uint64_t emitted=0,killed=0,expired=0,refused=0;
    };

    bool init(gfx::Device& device,uint32_t capacity) {
        shutdown();
        if(!evolution_.initCapacity(device,capacity)) return false;
        slots_.assign(capacity,{});
        free_.clear();
        free_.reserve(capacity);
        for(uint32_t i=capacity;i>0;--i) free_.push_back(i-1);
        active_=0;stats_={};
        return true;
    }
    void shutdown() {
        evolution_.shutdown();
        slots_.clear();free_.clear();active_=0;
    }

    SpectraFxGpuParticleHandle emit(const SpectraFxGpuSeed& seed) {
        if(free_.empty() || !std::isfinite(seed.age) ||
           !std::isfinite(seed.lifetime) || seed.lifetime<=0 ||
           seed.age<0 || seed.age>=seed.lifetime) {
            ++stats_.refused;return {};
        }
        uint32_t index=free_.back();
        if(!evolution_.writeSlot(index,seed)) {
            ++stats_.refused;return {};
        }
        free_.pop_back();
        Slot& slot=slots_[index];
        // Generation counter survives init()/shutdown() on this manager.
        // No stale handle can alias a reset/recycled slot until uint32 wrap.
        if(++nextGeneration_==0) ++nextGeneration_;
        slot.generation=nextGeneration_;
        slot.active=true;slot.age=seed.age;slot.life=seed.lifetime;
        ++active_;++stats_.emitted;
        return {index,slot.generation};
    }

    bool kill(SpectraFxGpuParticleHandle handle) {
        if(!alive(handle)||!release(handle.index)) return false;
        ++stats_.killed;return true;
    }

    // Must execute within a frame and outside rendering passes. The allocator
    // clock moves only after successful GPU dispatch; expiry is deferred if
    // the RHI cannot safely update shared buffers until another frame.
    bool step(float dt,const SpectraFxGpuEnvironment& env={}) {
        if(!evolution_.ready() || !std::isfinite(dt) ||
           dt<0 || dt>0.1f || !evolution_.step(dt,env)) return false;
        for(uint32_t i=0;i<slots_.size();++i) {
            Slot& slot=slots_[i];
            if(!slot.active) continue;
            slot.age=std::min(slot.life,slot.age+dt);
            if(slot.age>=slot.life && release(i)) ++stats_.expired;
        }
        return true;
    }

    bool alive(SpectraFxGpuParticleHandle handle) const {
        return handle.valid() && handle.index<slots_.size() &&
               slots_[handle.index].active &&
               slots_[handle.index].generation==handle.generation;
    }
    // Snapshot the generation when requesting GPU async feedback. A recycled
    // slot is rejected by alive(handle) before delivery to presentation.
    SpectraFxGpuParticleHandle handleAt(uint32_t index) const {
        if(index>=slots_.size() || !slots_[index].active) return {};
        return {index,slots_[index].generation};
    }
    uint32_t activeCount() const {return active_;}
    uint32_t capacity() const {return uint32_t(slots_.size());}
    const Stats& stats() const {return stats_;}
    SpectraFxGpuEvolution& evolution() {return evolution_;}
    const SpectraFxGpuEvolution& evolution() const {return evolution_;}

private:
    struct Slot {
        uint32_t generation=0;
        float age=0,life=0;
        bool active=false;
    };
    bool release(uint32_t index) {
        SpectraFxGpuSeed dormant{};
        dormant.lifetime=0;
        if(!evolution_.writeSlot(index,dormant)) return false;
        Slot& slot=slots_[index];
        slot.active=false;slot.age=slot.life=0;
        free_.push_back(index);
        --active_;
        return true;
    }
    SpectraFxGpuEvolution evolution_;
    std::vector<Slot> slots_;
    std::vector<uint32_t> free_;
    uint32_t active_=0;
    uint32_t nextGeneration_=0;
    Stats stats_{};
};
} // namespace aiverse::fx
