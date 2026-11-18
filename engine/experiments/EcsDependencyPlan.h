// Experimental dependency analysis for ECS systems.
// Scheduling order is stable and deterministic; independent systems are only
// marked as compatible. Running separate systems simultaneously remains opt-in.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::ecs_experiment {

using AccessId=uint32_t;
struct SystemAccess {
    std::string name;
    std::vector<AccessId> reads, writes;
    // Resources include RNG, event queues, global state, physics, etc.
    std::vector<AccessId> resourceReads, resourceWrites;
    bool structural=false;
    bool orderedSideEffects=false;
};
inline bool intersection(const std::vector<AccessId>& a,const std::vector<AccessId>& b) {
    for(auto x:a)if(std::find(b.begin(),b.end(),x)!=b.end())return true;
    return false;
}
inline bool conflicts(const SystemAccess& a,const SystemAccess& b) {
    if(a.structural||b.structural||a.orderedSideEffects||b.orderedSideEffects)return true;
    return intersection(a.writes,b.reads)||intersection(a.reads,b.writes)||
           intersection(a.writes,b.writes)||
           intersection(a.resourceWrites,b.resourceReads)||
           intersection(a.resourceReads,b.resourceWrites)||
           intersection(a.resourceWrites,b.resourceWrites);
}
class DependencyPlan {
    std::vector<SystemAccess> entries_;
public:
    void add(SystemAccess access) {entries_.push_back(std::move(access));}
    const std::vector<SystemAccess>& systems() const{return entries_;}
    // Preserve registration order. Each wave contains pairwise independent systems.
    // A later system cannot leap ahead of an earlier conflicting system, even
    // transitively; this is essential for stateful deterministic behavior.
    std::vector<std::vector<size_t>> waves() const {
        std::vector<std::vector<size_t>> groups;
        std::vector<size_t> waveFor(entries_.size());
        for(size_t i=0;i<entries_.size();++i) {
            size_t minWave=0;
            for(size_t j=0;j<i;++j)
                if(conflicts(entries_[j],entries_[i]))
                    minWave=std::max(minWave,waveFor[j]+1);
            waveFor[i]=minWave;
            if(groups.size()<=minWave)groups.resize(minWave+1);
            groups[minWave].push_back(i);
        }
        return groups;
    }
};
} // namespace aiverse::ecs_experiment
