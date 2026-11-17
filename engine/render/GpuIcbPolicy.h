#pragma once
// CPU-side selection policy for optional GPU-encoded Metal ICB batches.
// A conservative, testable heuristic; this is not a learned GPU cost model.
#include <algorithm>
#include <array>
#include <cmath>
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

// Select from fixed-camera balanced Scene CPU measurements. Small differences
// are treated as ties in favor of the cheaper/default earlier mode.
// 0=Instancing, 1=CPU MDI, 2=GPU-authored. This does not assume that full
// editor GPU timestamps measure the isolated Scene renderer.
inline int chooseSceneBatchMode(const std::array<double, 3>& cpuAverages,
                                int modeCount, double minimumGain = 0.005) {
    modeCount = std::clamp(modeCount, 1, 3);
    int best = 0;
    for (int mode = 1; mode < modeCount; ++mode)
        if (std::isfinite(cpuAverages[mode]) && cpuAverages[mode] > 0.0 &&
            cpuAverages[mode] < cpuAverages[best] * (1.0 - minimumGain))
            best = mode;
    return best;
}

} // namespace aiverse::render
