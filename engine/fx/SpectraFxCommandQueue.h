// SpectraFX: presentation-only command bridge for arbitrary native game modules.
// Enqueue from gameplay adapters after the authoritative tick; flush on the
// renderer thread. The queue neither reads nor mutates gameplay/ECS state.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>

#include "ParticleSystem.h"

namespace aiverse::fx {

// Stable keys are supplied by the host (e.g. generation-safe ECS entity + cue ID).
// A zero key is a one-shot; nonzero keys identify attached/looping instances.
// The caller MUST invalidate keys on entity despawn and across match resets.
class SpectraFxCommandQueue {
public:
    enum class Kind : uint8_t { Play, Move, Stop, Kill };
    struct Command {
        Kind kind = Kind::Play;
        uint64_t key = 0;
        std::string effect;
        float3 pos{};
        float3 dir{0, 1, 0};
        float scale = 1;
        float4 tint{1, 1, 1, 1};
    };
    struct Stats {
        uint64_t submitted = 0;
        uint64_t dropped = 0;
        uint64_t played = 0;
        uint64_t failed = 0;
        uint64_t stopped = 0;
    };

    explicit SpectraFxCommandQueue(size_t capacity = 4096)
        : capacity_(capacity) {}

    // Bounded, FIFO submission. This object is not thread-safe: merge producer
    // streams in a deterministic caller-defined order before enqueueing.
    bool enqueue(Command cmd) {
        ++stats_.submitted;
        if (pending_.size() >= capacity_) {
            ++stats_.dropped;
            return false;
        }
        pending_.push_back(std::move(cmd));
        return true;
    }

    bool play(std::string effect, float3 pos, uint64_t key = 0,
              float3 dir = {0, 1, 0}, float scale = 1,
              float4 tint = {1, 1, 1, 1}) {
        Command cmd;
        cmd.kind = Kind::Play;
        cmd.effect = std::move(effect);
        cmd.key = key;
        cmd.pos = pos;
        cmd.dir = dir;
        cmd.scale = scale;
        cmd.tint = tint;
        return enqueue(std::move(cmd));
    }
    bool move(uint64_t key, float3 pos, float3 dir = {0, 1, 0}) {
        if (!key) return false;
        Command cmd;
        cmd.kind = Kind::Move;
        cmd.key = key;
        cmd.pos = pos;
        cmd.dir = dir;
        return enqueue(std::move(cmd));
    }
    bool stop(uint64_t key, bool immediate = false) {
        if (!key) return false;
        Command cmd;
        cmd.kind = immediate ? Kind::Kill : Kind::Stop;
        cmd.key = key;
        return enqueue(std::move(cmd));
    }

    // Flush after the host's ECS event extraction, before ParticleSystem::update.
    // A keyed Play replaces a prior keyed instance, including its live particles.
    // Unknown keyed Move/Stop are no-ops, permitting despawn and frame coalescing.
    void flush(ParticleSystem& particles) {
        while (!pending_.empty()) {
            Command cmd = std::move(pending_.front());
            pending_.pop_front();
            if (cmd.kind == Kind::Play) {
                if (cmd.key) {
                    const auto previous = active_.find(cmd.key);
                    if (previous != active_.end()) {
                        particles.kill(previous->second);
                        active_.erase(previous);
                    }
                }
                const FxHandle handle = particles.play(cmd.effect, cmd.pos,
                                                       cmd.dir, cmd.scale, cmd.tint);
                if (handle.valid()) {
                    ++stats_.played;
                    if (cmd.key) active_[cmd.key] = handle;
                } else {
                    ++stats_.failed;
                }
                continue;
            }
            const auto it = active_.find(cmd.key);
            if (it == active_.end()) continue;
            if (cmd.kind == Kind::Move) {
                particles.move(it->second, cmd.pos, cmd.dir);
            } else {
                if (cmd.kind == Kind::Kill) particles.kill(it->second);
                else particles.stop(it->second);
                active_.erase(it);
                ++stats_.stopped;
            }
        }
    }

    // On a new game/match, kill all owned FX and discard pending commands.
    // Does not clear unrelated effects in the shared ParticleSystem.
    void reset(ParticleSystem& particles) {
        pending_.clear();
        for (const auto& entry : active_) particles.kill(entry.second);
        active_.clear();
    }

    // Non-render-thread reset for an abandoned queue. Active handles require
    // reset(ParticleSystem&) before the renderer or effects are torn down.
    void discardPending() { pending_.clear(); }
    size_t pending() const { return pending_.size(); }
    size_t attached() const { return active_.size(); }
    size_t capacity() const { return capacity_; }
    const Stats& stats() const { return stats_; }
    void resetStats() { stats_ = {}; }

private:
    size_t capacity_ = 4096;
    std::deque<Command> pending_;
    std::unordered_map<uint64_t, FxHandle> active_;
    Stats stats_{};
};

} // namespace aiverse::fx
