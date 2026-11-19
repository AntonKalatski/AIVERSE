// Optional composition of SceneLoadCoordinator with a budgeted resource owner.
// Not an ECS system: all calls must be on the same host/owner thread.
#pragma once
#include "SceneResourceBackend.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::scene {

class SceneResourcePipeline {
public:
    SceneResourcePipeline(SceneLoadCoordinator& scenes, SceneResourceBackend& resources)
        : scenes_(scenes), resources_(resources) {}
    SceneResourcePipeline(const SceneResourcePipeline&) = delete;
    SceneResourcePipeline& operator=(const SceneResourcePipeline&) = delete;
    ~SceneResourcePipeline() {
        for(auto& [serial,pending] : pending_) {
            (void)serial;
            unpin(pending);
        }
    }

    SceneTicket request(std::string asset, LoadMode mode = LoadMode::Additive,
                        bool persistent = false) {
        const SceneTicket ticket = scenes_.request(std::move(asset),mode,persistent);
        if(ticket.valid()) pending_.emplace(ticket.serial, Pending{ticket});
        return ticket;
    }

    // Two independent budgets:
    // startBudget bounds NEW worker tasks; publishOps/publishBytes bound
    // owner-thread publication. Never commits authoritative scene entities.
    void poll(size_t startBudget, size_t publishOps, size_t publishBytes) {
        scenes_.poll();
        for(auto it = pending_.begin(); it != pending_.end();) {
            Pending& pending = it->second;
            const auto status = scenes_.status(pending.ticket);
            if(status == SceneLoadStatus::Unknown ||
               status == SceneLoadStatus::Cancelled ||
               status == SceneLoadStatus::Unloaded) {
                unpin(pending);
                it = pending_.erase(it);
                continue;
            }
            if(status == SceneLoadStatus::Ready && !pending.started && !pending.failed) {
                const SceneAsset* manifest = scenes_.prepared(pending.ticket);
                if(!manifest) {
                    pending.failed = true;
                    pending.error = "missing prepared scene manifest";
                } else {
                    // SceneAsset normalizes dependencies by lexical order.
                    pending.dependencies = manifest->dependencies;
                    for(const std::string& dependency : pending.dependencies) {
                        std::string error;
                        if(!resources_.pin(dependency,&error)) {
                            pending.failed = true;
                            pending.error = std::move(error);
                            unpin(pending);
                            break;
                        }
                        pending.pinned.push_back(dependency);
                    }
                    if(!pending.failed) pending.started = true;
                }
            }
            ++it;
        }
        resources_.update(startBudget,publishOps,publishBytes);
    }

    bool ready(SceneTicket ticket) const {
        const Pending* p = find(ticket);
        return p && p->started && !p->failed &&
               scenes_.ready(ticket) && resources_.ready(p->dependencies);
    }

    // Call only for an agreed deterministic transition command / tick.
    // A non-ready ticket returns false WITHOUT destroying the old scene.
    bool commit(SceneTicket ticket) {
        if(!ready(ticket)) return false;
        const bool success = scenes_.commit(ticket);
        auto it = pending_.find(ticket.serial);
        if(it != pending_.end() && it->second.ticket == ticket &&
           (success || scenes_.status(ticket) == SceneLoadStatus::Failed ||
            scenes_.status(ticket) == SceneLoadStatus::Unknown)) {
            unpin(it->second);
            pending_.erase(it);
        }
        return success;
    }

    bool cancel(SceneTicket ticket) {
        const bool result = scenes_.cancel(ticket);
        auto it = pending_.find(ticket.serial);
        if(it != pending_.end() && it->second.ticket == ticket) {
            unpin(it->second);
            pending_.erase(it);
        }
        return result;
    }
    bool unload(SceneId id) {
        for(auto it = pending_.begin(); it != pending_.end();) {
            if(it->second.ticket.scene == id) {
                unpin(it->second);
                it = pending_.erase(it);
            } else ++it;
        }
        return scenes_.unload(id);
    }

    // Invoke after restoring authoritative World and SceneHierarchy. Old
    // tickets lose their epochs; resource leases are released by coordinator.
    // Run queueRestoredResidency() for Active scenes after this call.
    void invalidateAfterRestore() {
        for(auto& [serial,pending] : pending_) {
            (void)serial;
            unpin(pending);
        }
        pending_.clear();
        scenes_.invalidateAfterRestore();
    }

    std::vector<SceneTicket> queueRestoredResidency() {
        std::vector<SceneTicket> queued = scenes_.queueRestoredResidency();
        for(const SceneTicket& ticket : queued)
            pending_.emplace(ticket.serial,Pending{ticket});
        return queued;
    }
    std::string error(SceneTicket ticket) const {
        if(const Pending* p = find(ticket)) {
            if(p->failed) return p->error;
            std::string resourceError;
            if(resources_.errorFor(p->dependencies,resourceError))
                return resourceError;
        }
        return scenes_.error(ticket);
    }
    SceneLoadStatus status(SceneTicket ticket) const {
        return scenes_.status(ticket);
    }
    size_t pendingCount() const { return pending_.size(); }

private:
    struct Pending {
        SceneTicket ticket;
        std::vector<std::string> dependencies;
        std::vector<std::string> pinned;
        bool started = false;
        bool failed = false;
        std::string error;
    };
    const Pending* find(SceneTicket ticket) const {
        auto it = pending_.find(ticket.serial);
        return it != pending_.end() && it->second.ticket == ticket
            ? &it->second : nullptr;
    }
    void unpin(Pending& pending) {
        for(auto it = pending.pinned.rbegin(); it != pending.pinned.rend(); ++it)
            resources_.unpin(*it);
        pending.pinned.clear();
    }
    SceneLoadCoordinator& scenes_;
    SceneResourceBackend& resources_;
    std::map<uint64_t,Pending> pending_;
};

} // namespace aiverse::scene
