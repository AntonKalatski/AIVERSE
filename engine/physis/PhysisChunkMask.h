// PHYSIS chunk activity mask (2026-11-22): which grid chunks are simulated
// this tick. Authoritative, persistent ECS resource (part of save + hash) so
// every peer updates exactly the same cells. Absent resource or `all` = the
// whole grid runs (historic behaviour). See PhysisStreaming.h.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aiverse::physis {

struct ActiveChunks {
    std::int32_t chunkCells = 32;   // chunk edge in grid cells
    std::int32_t chunksX = 0, chunksZ = 0;
    bool all = true;                // no anchors: every chunk active
    std::uint64_t revision = 0;     // bumps when the active set changes
    std::vector<std::uint8_t> active; // chunksX * chunksZ, row-major
};

// Grid-local cell index (row-major, z * width + x) -> simulated this tick?
inline bool cellSimulated(const ActiveChunks* mask, std::int32_t width, std::size_t index) {
    if (!mask || mask->all || width <= 0 || mask->chunkCells <= 0) return true;
    const auto x = std::int32_t(index % std::size_t(width));
    const auto z = std::int32_t(index / std::size_t(width));
    const auto c = std::size_t(z / mask->chunkCells) * std::size_t(mask->chunksX) +
                   std::size_t(x / mask->chunkCells);
    return c < mask->active.size() && mask->active[c] != 0;
}

} // namespace aiverse::physis
