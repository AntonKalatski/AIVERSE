// SpectraFX asynchronous GPU impact feedback.
// No synchronous particle readback, no writes to authoritative ECS state.
// On backends without async buffer readback this is optional/unavailable.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>
#include "SpectraFxGpuParticleSlots.h"

namespace aiverse::fx {

struct SpectraFxGpuImpact {
    SpectraFxGpuParticleHandle particle;
    render::float3 position{};
};

class SpectraFxGpuImpactFeedback {
public:
    static constexpr uint32_t kMaxReadbackParticles=4096; // RHI max 64 KiB
    static constexpr size_t kMaxBufferedEvents=512;
    struct Stats {
        uint64_t requested=0,delivered=0,discarded=0,unsupported=0;
    };

    // The callback may arrive in a later RHI frame; weak ownership and an
    // epoch prevent stale callbacks from publishing after reset/destruction.
    bool request(gfx::Device& device,const SpectraFxGpuParticleSlots& slots) {
        const auto& gpu=slots.evolution();
        if(!gpu.impactReadbackEligible() ||
           gpu.count()>kMaxReadbackParticles) {
            ++state_->stats.unsupported;
            return false;
        }
        if(state_->inFlight) return false;
        const uint32_t count=gpu.count();
        std::vector<SpectraFxGpuParticleHandle> generations;
        generations.reserve(count);
        for(uint32_t i=0;i<count;++i) generations.push_back(slots.handleAt(i));
        const auto epoch=state_->epoch;
        const std::weak_ptr<State> weak=state_;
        state_->inFlight=true;
        const bool scheduled=device.readBufferAsync(
            gpu.impactBuffer(),size_t(count)*sizeof(render::float4),
            [weak,epoch,generations=std::move(generations)](
                    std::vector<uint8_t> bytes) {
                auto state=weak.lock();
                if(!state || state->epoch!=epoch) return;
                state->inFlight=false;
                if(bytes.size()!=generations.size()*sizeof(render::float4)) {
                    ++state->stats.discarded;
                    return;
                }
                for(size_t i=0;i<generations.size();++i) {
                    if(!generations[i].valid()) continue;
                    render::float4 event{};
                    std::memcpy(&event,bytes.data()+i*sizeof(event),sizeof(event));
                    if(event.w<0.5f) continue;
                    if(!std::isfinite(event.x)||!std::isfinite(event.y)||
                       !std::isfinite(event.z)) {
                        ++state->stats.discarded;
                        continue;
                    }
                    if(state->pending.size()>=kMaxBufferedEvents) {
                        ++state->stats.discarded;
                        continue;
                    }
                    state->pending.push_back({generations[i],
                                              {event.x,event.y,event.z}});
                }
            });
        if(!scheduled) {
            state_->inFlight=false;
            ++state_->stats.unsupported;
            return false;
        }
        ++state_->stats.requested;
        return true;
    }

    // Render/presentation thread only. Generation check avoids dispatching
    // callbacks for recycled slots; no event enters SimEvent or gameplay.
    template<class Callback>
    void drain(const SpectraFxGpuParticleSlots& slots,Callback&& callback,
               size_t maxEvents=128) {
        for(size_t i=0;i<maxEvents&&!state_->pending.empty();++i) {
            auto event=state_->pending.front();
            state_->pending.pop_front();
            if(!slots.alive(event.particle)) {
                ++state_->stats.discarded;
                continue;
            }
            callback(event);
            ++state_->stats.delivered;
        }
    }

    void reset() {
        ++state_->epoch;
        state_->inFlight=false;
        state_->pending.clear();
    }
    const Stats& stats() const {return state_->stats;}
    size_t pending() const {return state_->pending.size();}

private:
    struct State {
        uint64_t epoch=1;
        bool inFlight=false;
        std::deque<SpectraFxGpuImpact> pending;
        Stats stats{};
    };
    std::shared_ptr<State> state_=std::make_shared<State>();
};

} // namespace aiverse::fx
