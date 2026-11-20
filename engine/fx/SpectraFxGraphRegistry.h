// SpectraFX graph routing and per-instance parameter overrides.
// Presentation only; authored graph contents can be replaced in a live editor.
// No genre-specific event IDs or gameplay authority state.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

#include "SpectraFxGraph.h"

namespace aiverse::fx {

// A graph asset is referenced by an opaque, host-defined stable ID. Native
// games can register effects without requiring a particular game genre.
class SpectraFxGraphRegistry {
public:
    struct Entry {
        SpectraFxGraph graph;
        SpectraFxGraphParameters defaults;
        uint64_t revision = 1;
    };
    struct Stats {
        uint64_t received = 0;
        uint64_t graphExecuted = 0;
        uint64_t directDispatched = 0;
        uint64_t missingGraph = 0;
        uint64_t rejected = 0;
    };

    // Replaces an asset atomically after validation; invalid edits never
    // corrupt the running graph. revision increments for editor cache invalidation.
    bool define(std::string name, SpectraFxGraph graph,
                SpectraFxGraphParameters defaults = {},
                std::string* error = nullptr) {
        if (name.empty() || name.size() > 128) {
            if (error) *error = "SpectraFX graph name must have 1..128 bytes";
            return false;
        }
        if (!graph.validate(error)) return false;
        auto it = graphs_.find(name);
        const uint64_t nextRevision = it == graphs_.end() ? 1 : it->second.revision + 1;
        graphs_[std::move(name)] = {std::move(graph), std::move(defaults), nextRevision};
        if (error) error->clear();
        return true;
    }

    bool erase(const std::string& name) {
        const auto count = graphs_.erase(name);
        if (count) {
            for (auto it = routes_.begin(); it != routes_.end();) {
                if (it->second == name) it = routes_.erase(it);
                else ++it;
            }
        }
        return count != 0;
    }
    const Entry* find(const std::string& name) const {
        const auto it = graphs_.find(name);
        return it == graphs_.end() ? nullptr : &it->second;
    }

    // Routes an input cue ID to a graph name. Setting a route does not require
    // an asset to exist yet: content may arrive after level streaming.
    void route(uint32_t cueId, std::string graphName) {
        if (graphName.empty()) routes_.erase(cueId);
        else routes_[cueId] = std::move(graphName);
    }
    void unroute(uint32_t cueId) { routes_.erase(cueId); }
    const std::string* routeFor(uint32_t cueId) const {
        const auto it = routes_.find(cueId);
        return it == routes_.end() ? nullptr : &it->second;
    }

    // Per-call overrides take priority over asset defaults, and never mutate
    // the shared asset. Persistent actor parameters remain owned by the host,
    // not by this registry, and must be resubmitted after rollback.
    static SpectraFxGraphParameters mergeParameters(
            const SpectraFxGraphParameters& defaults,
            const SpectraFxGraphParameters& overrides) {
        SpectraFxGraphParameters merged = defaults;
        for (const auto& e : overrides.scalars) merged.scalars[e.first] = e.second;
        for (const auto& e : overrides.colors) merged.colors[e.first] = e.second;
        for (const auto& e : overrides.flags) merged.flags[e.first] = e.second;
        return merged;
    }

    // A routed input must use the configured graph. Missing assets do NOT
    // silently dispatch a different effect. Unrouted input retains legacy
    // direct cue mapping for compatibility with existing particle effects.
    SpectraFxGraph::Result dispatch(
            const SpectraFxCue& cue, SpectraFxCueBridge& bridge,
            SpectraFxCommandQueue& queue,
            const SpectraFxGraphParameters& overrides = {}) {
        ++stats_.received;
        const std::string* name = routeFor(cue.id);
        if (!name) {
            const bool ok = bridge.dispatch(cue, queue);
            if (ok) ++stats_.directDispatched;
            else ++stats_.rejected;
            return {ok ? size_t(1) : size_t(0), ok ? size_t(0) : size_t(1), true};
        }
        const Entry* entry = find(*name);
        if (!entry) {
            ++stats_.missingGraph;
            return {0, 1, false};
        }
        // Explicit params are ephemeral. The graph only changes cues and
        // queues FX commands, never authoritative ECS components.
        auto merged = mergeParameters(entry->defaults, overrides);
        auto result = entry->graph.execute(cue, merged, bridge, queue);
        if (result.valid) ++stats_.graphExecuted;
        stats_.rejected += result.rejected;
        if (!result.valid) ++stats_.rejected;
        return result;
    }

    const Stats& stats() const { return stats_; }
    void resetStats() { stats_ = {}; }
    size_t graphCount() const { return graphs_.size(); }
    size_t routeCount() const { return routes_.size(); }

private:
    std::unordered_map<std::string, Entry> graphs_;
    std::unordered_map<uint32_t, std::string> routes_;
    Stats stats_{};
};

} // namespace aiverse::fx
