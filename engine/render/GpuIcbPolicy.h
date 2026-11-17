#pragma once
// CPU-side selection policy for optional GPU-encoded Metal ICB batches.
// A conservative, testable heuristic; this is not a learned GPU cost model.
#include <algorithm>
#include <cstdint>

namespace aiverse::render {

inline uint32_t gpuIcbMinimumCommands(uint32_t eligible, uint32_t requested,
                                      bool adaptive) {
    const uint32_t floor = adaptive ? (eligible < 256u ? 16u : 8u) : 2u;
    return std::max(std::max(requested, 2u), floor);
}

inline bool gpuIcbShouldFallbackToInstancing(bool adaptive,
                                            uint32_t suitableGroups) {
    return adaptive && suitableGroups == 0;
}

} // namespace aiverse::render
