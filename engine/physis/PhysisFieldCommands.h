// PHYSIS Stage 25: host-owned, deterministic tick-boundary field command queue.
// No editor, genre, network, or renderer dependencies. Queue is transient;
// authoritative host must journal/replay commands after rollback.
#pragma once
#include "PhysisRuntimeFields.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::physis {

struct FieldCommand {
    std::uint64_t tick = 0;
    std::uint64_t sequence = 0; // strictly increasing per authoritative source
    std::vector<AuthoredField> fields; // complete validated desired field set
};

class FieldCommandQueue {
public:
    // Replaces an entire authored-field set at a specified simulation tick.
    // Commands are never applied immediately by enqueue().
    void enqueue(FieldCommand command) {
        if (command.sequence <= lastEnqueuedSequence_ && hasEnqueued_)
            throw std::invalid_argument("PHYSIS command sequence must increase");
        if (hasApplied_ && (command.tick < lastAppliedTick_ ||
            (command.tick == lastAppliedTick_ &&
             command.sequence <= lastAppliedSequence_)))
            throw std::invalid_argument("PHYSIS command precedes committed history");
        if (command.fields.size() > 2048)
            throw std::invalid_argument("PHYSIS command exceeds field cap");
        std::vector<std::string> ids;
        ids.reserve(command.fields.size());
        for (const auto& f : command.fields) {
            if (f.id.empty() || f.id.size() > 64)
                throw std::invalid_argument("PHYSIS command field ID invalid");
            ids.push_back(f.id);
        }
        std::sort(ids.begin(), ids.end());
        if (std::adjacent_find(ids.begin(), ids.end()) != ids.end())
            throw std::invalid_argument("PHYSIS command contains duplicate IDs");
        const auto sequence = command.sequence;
        pending_.push_back(std::move(command));
        lastEnqueuedSequence_ = sequence;
        hasEnqueued_ = true;
    }

    // Call once at the authoritative barrier BEFORE advancing PHYSIS systems.
    // The host supplies the tick from its deterministic simulation clock.
    // Reject late commands instead of silently applying them at a wrong tick.
    // A failed reconciliation retains the command for retry/diagnostics.
    std::size_t applyAtTick(World& world, std::uint64_t tick) {
        if (!pending_.empty() && pending_.front().tick < tick)
            throw std::invalid_argument("PHYSIS late field command");
        std::size_t applied = 0;
        while (!pending_.empty() && pending_.front().tick == tick) {
            const auto& command = pending_.front();
            reconcileTrackedRuntimeFields(world, command.fields);
            hasApplied_ = true;
            lastAppliedTick_ = tick;
            lastAppliedSequence_ = command.sequence;
            pending_.erase(pending_.begin());
            ++applied;
        }
        return applied;
    }

    bool empty() const { return pending_.empty(); }
    std::size_t size() const { return pending_.size(); }
    std::uint64_t lastAppliedSequence() const { return lastAppliedSequence_; }
    // Host rollback: reconstruct a fresh queue by replaying the authoritative
    // journal after restoring the World snapshot. Do not reuse a future queue.
    void reset() {
        pending_.clear();
        hasEnqueued_ = false;
        hasApplied_ = false;
        lastEnqueuedSequence_ = lastAppliedSequence_ = lastAppliedTick_ = 0;
    }
private:
    std::vector<FieldCommand> pending_;
    std::uint64_t lastEnqueuedSequence_ = 0;
    std::uint64_t lastAppliedTick_ = 0;
    std::uint64_t lastAppliedSequence_ = 0;
    bool hasEnqueued_ = false;
    bool hasApplied_ = false;
};

} // namespace aiverse::physis
