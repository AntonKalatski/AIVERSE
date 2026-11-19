// SpectraFX emitter-event router. Presentation-only orchestration between
// effect instances: an impact, completion or authored marker may trigger a
// secondary effect. No gameplay event publication or ECS state mutation.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "SpectraFxCommandQueue.h"

namespace aiverse::fx {

enum class SpectraFxEmitterEvent : uint8_t {
    Spawned, Completed, Impact, Marker, Manual
};

struct SpectraFxEmitterEventRecord {
    uint64_t emitterKey = 0;
    SpectraFxEmitterEvent type = SpectraFxEmitterEvent::Manual;
    uint32_t marker = 0;
    float3 position{};
    float3 direction{0,1,0};
    float scale = 1;
    float4 tint{1,1,1,1};
    uint8_t depth = 0;
};

struct SpectraFxEmitterEventLink {
    uint32_t sourceMarker = 0; // 0 for any; used only for Marker events
    std::string effect;
    float scale = 1;
    bool enabled = true;
};

class SpectraFxEmitterEventRouter {
public:
    static constexpr size_t kMaxPending = 2048;
    static constexpr uint8_t kMaxDepth = 3;
    struct Stats {
        uint64_t submitted = 0, processed = 0, spawned = 0;
        uint64_t rejected = 0, depthLimited = 0;
    };

    // One source instance key and event type can fan out to multiple effects.
    // Zero emitterKey is a global/manual source with no owned instance.
    void link(uint64_t emitterKey, SpectraFxEmitterEvent event,
              SpectraFxEmitterEventLink link) {
        links_[{emitterKey,event}].push_back(std::move(link));
    }
    void unlink(uint64_t emitterKey) {
        for (auto it=links_.begin();it!=links_.end();) {
            if (it->first.key == emitterKey) it=links_.erase(it);
            else ++it;
        }
    }
    void clear() { pending_.clear(); links_.clear(); }

    bool publish(SpectraFxEmitterEventRecord event) {
        ++stats_.submitted;
        if (event.depth > kMaxDepth) {
            ++stats_.depthLimited;
            return false;
        }
        if (pending_.size() >= kMaxPending) {
            ++stats_.rejected;
            return false;
        }
        pending_.push_back(std::move(event));
        return true;
    }

    // Bounded per-frame process. Never recursively handles newly published
    // events in the same drain: callback-produced events run next frame.
    // Links call the existing SpectraFX queue and ParticleSystem backend.
    void drain(SpectraFxCommandQueue& queue,size_t maxEvents = 512) {
        const size_t count = pending_.size() < maxEvents ? pending_.size() : maxEvents;
        for (size_t i=0;i<count;++i) {
            auto record=std::move(pending_.front());
            pending_.pop_front();
            ++stats_.processed;
            if (record.depth >= kMaxDepth) {
                ++stats_.depthLimited;
                continue;
            }
            auto it=links_.find({record.emitterKey,record.type});
            if (it==links_.end()) continue;
            for (const auto& link : it->second) {
                if (!link.enabled || link.effect.empty()) continue;
                if (queue.play(link.effect,record.position,0,record.direction,
                               record.scale*link.scale,record.tint))
                    ++stats_.spawned;
                else ++stats_.rejected;
            }
        }
    }

    size_t pending() const { return pending_.size(); }
    const Stats& stats() const { return stats_; }
    void resetStats() { stats_ = {}; }

private:
    struct Source {
        uint64_t key;
        SpectraFxEmitterEvent event;
        bool operator==(const Source&) const = default;
    };
    struct SourceHash {
        size_t operator()(const Source& s) const {
            const uint64_t v=s.key ^ (uint64_t(static_cast<uint8_t>(s.event)) << 56);
            return size_t(v ^ (v >> 33));
        }
    };
    std::unordered_map<Source,std::vector<SpectraFxEmitterEventLink>,SourceHash> links_;
    std::deque<SpectraFxEmitterEventRecord> pending_;
    Stats stats_{};
};

} // namespace aiverse::fx
