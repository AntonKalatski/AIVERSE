// Immutable render-only adapter for AIVERSE WorldScene's authored terrain
// height tiles. All input/output positions are signed world centimetres.
// Interpolation follows readChunkCollision()'s (a,c,b), (a,d,c) diagonal.
// Does not modify collision physics, navmesh, ECS or game state.
#pragma once

#include "engine/world/WorldAssets.h"
#include "engine/world/TerraWeave.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {

struct AuthoredHeightTileCm {
    uint32_t resolution = 0;
    std::vector<int32_t> heightsCm; // x + z*resolution; copied from WorldScene terrain
};

class AuthoredWorldHeightSnapshot {
public:
    explicit AuthoredWorldHeightSnapshot(int32_t worldCellSizeMetres)
        : cellSizeCm_(int64_t(worldCellSizeMetres) * 100) {
        if (worldCellSizeMetres < 1 || worldCellSizeMetres > 4096)
            throw std::invalid_argument("TerraWeave authored tile cell size");
    }

    void setTile(world::Cell key, AuthoredHeightTileCm tile) {
        if (tile.resolution < 2 || tile.resolution > 129 ||
            tile.heightsCm.size() !=
                size_t(tile.resolution) * size_t(tile.resolution))
            throw std::invalid_argument("TerraWeave authored tile resolution");
        tiles_[key] = std::move(tile);
    }

    size_t tileCount() const { return tiles_.size(); }

    // A null result means that no loaded WorldScene heightfield covers this
    // point. Caller may fall back to deterministic procedural height.
    // The same two triangles as the terrain collider are interpolated in
    // integer centimetres; no bilinear rounding across a diagonal.
    std::optional<int32_t> sample(int64_t xCm, int64_t zCm) const {
        if (xCm < -kWorldLimitCm || xCm > kWorldLimitCm ||
            zCm < -kWorldLimitCm || zCm > kWorldLimitCm)
            throw std::out_of_range("TerraWeave authored terrain world extent");
        const int64_t cx = detail::floorDiv(xCm, cellSizeCm_);
        const int64_t cz = detail::floorDiv(zCm, cellSizeCm_);
        const auto it = tiles_.find(world::Cell{
            static_cast<int32_t>(cx), 0, static_cast<int32_t>(cz)});
        if (it == tiles_.end()) return std::nullopt;
        const AuthoredHeightTileCm& tile = it->second;
        const int64_t localX = xCm - cx * cellSizeCm_;
        const int64_t localZ = zCm - cz * cellSizeCm_;
        const int64_t gx = localX * (tile.resolution - 1);
        const int64_t gz = localZ * (tile.resolution - 1);
        const size_t ix = static_cast<size_t>(gx / cellSizeCm_);
        const size_t iz = static_cast<size_t>(gz / cellSizeCm_);
        const int64_t fx = gx % cellSizeCm_;
        const int64_t fz = gz % cellSizeCm_;
        // localX/localZ are half-open; ix and iz cannot exceed resolution-2.
        const auto at = [&](size_t x, size_t z) {
            return int64_t(tile.heightsCm[z * tile.resolution + x]);
        };
        const int64_t a = at(ix, iz);
        const int64_t b = at(ix + 1, iz);
        const int64_t c = at(ix + 1, iz + 1);
        const int64_t d = at(ix, iz + 1);
        const int64_t weighted = fx >= fz
            ? a * (cellSizeCm_ - fx) + b * (fx - fz) + c * fz
            : a * (cellSizeCm_ - fz) + d * (fz - fx) + c * fx;
        // Round to nearest signed centimetre, never truncate negative Y
        // toward zero differently from positive Y.
        const int64_t absNum = weighted >= 0 ? weighted : -weighted;
        const int64_t rounded = (absNum + cellSizeCm_ / 2) / cellSizeCm_;
        return static_cast<int32_t>(weighted >= 0 ? rounded : -rounded);
    }

private:
    int64_t cellSizeCm_ = 0;
    std::map<world::Cell, AuthoredHeightTileCm> tiles_;
};

} // namespace aiverse::terraweave
