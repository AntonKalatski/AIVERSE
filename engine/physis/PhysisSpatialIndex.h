// PHYSIS Stage 16 — transient, read-only broadphase for environmental volumes.
// Never store this in authoritative ECS or reuse it across World mutations /
// rollback / load / committed ticks. Index owns COPIES of the sampled volumes.
// No Box3D, TerraWeave, NEREID, genres, render backend or wall-clock dependencies.
#pragma once
#include "PhysisPresentation.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace aiverse::physis {

struct SpatialBucket {
    std::int64_t x = 0, y = 0, z = 0;
    constexpr auto operator<=>(const SpatialBucket&) const = default;
};

// Copy of a single ECS volume. Never retain raw component pointers that may
// become dangling on World COW/structural edits.
template<class T>
struct SpatialVolume {
    Entity entity = kNullEntity;
    T value{};
};

template<class T>
class SpatialVolumeIndex {
public:
    static constexpr std::int64_t kBucketMeters = 256;
    static constexpr std::size_t kMaxCellsPerVolume = 256;
    static constexpr std::size_t kMaxTotalAssignments = 65536;

    void add(Entity entity, const T& value, Vec3 center,
             Fixed radiusXY, Fixed radiusZ) {
        const auto ordinal = static_cast<std::uint32_t>(volumes_.size());
        volumes_.push_back(SpatialVolume<T>{entity, value});

        // A global or oversized volume falls back to a single global entry.
        // It must never disappear because the index budget was exceeded.
        if (radiusXY <= Fixed{} || radiusZ <= Fixed{} ||
            radiusXY.raw >= std::numeric_limits<std::int64_t>::max() / 4 ||
            radiusZ.raw >= std::numeric_limits<std::int64_t>::max() / 4) {
            globals_.push_back(ordinal);
            return;
        }
        const auto x0 = bucketOf(satSub(center.x.raw, radiusXY.raw));
        const auto x1 = bucketOf(satAdd(center.x.raw, radiusXY.raw));
        const auto y0 = bucketOf(satSub(center.y.raw, radiusZ.raw));
        const auto y1 = bucketOf(satAdd(center.y.raw, radiusZ.raw));
        const auto z0 = bucketOf(satSub(center.z.raw, radiusXY.raw));
        const auto z1 = bucketOf(satAdd(center.z.raw, radiusXY.raw));
        const auto dx = x1 - x0 + 1;
        const auto dy = y1 - y0 + 1;
        const auto dz = z1 - z0 + 1;
        // Check before multiplying to avoid pathological allocations/overflow.
        if (dx <= 0 || dy <= 0 || dz <= 0 ||
            dx > static_cast<std::int64_t>(kMaxCellsPerVolume) ||
            dy > static_cast<std::int64_t>(kMaxCellsPerVolume) ||
            dz > static_cast<std::int64_t>(kMaxCellsPerVolume) ||
            dx * dy > static_cast<std::int64_t>(kMaxCellsPerVolume) ||
            dx * dy * dz > static_cast<std::int64_t>(kMaxCellsPerVolume) ||
            assignments_ + static_cast<std::size_t>(dx * dy * dz) >
                kMaxTotalAssignments) {
            globals_.push_back(ordinal);
            return;
        }
        for (std::int64_t x = x0; x <= x1; ++x)
            for (std::int64_t y = y0; y <= y1; ++y)
                for (std::int64_t z = z0; z <= z1; ++z)
                    buckets_[{x, y, z}].push_back(ordinal);
        assignments_ += static_cast<std::size_t>(dx * dy * dz);
    }

    template<class Callback>
    void visit(Vec3 position, Callback&& callback) const {
        const SpatialBucket key{bucketOf(position.x.raw),
                                bucketOf(position.y.raw),
                                bucketOf(position.z.raw)};
        const auto bucket = buckets_.find(key);
        const std::vector<std::uint32_t>* local =
            bucket == buckets_.end() ? nullptr : &bucket->second;
        const std::size_t count = local ? local->size() : 0;
        // Stable merge by original ECS traversal order: critical for Fixed
        // addition/rounding and shelter sequencing. No query allocation.
        std::size_t i = 0, j = 0;
        while (i < globals_.size() || j < count) {
            const bool useGlobal = j == count ||
                (i < globals_.size() && globals_[i] < (*local)[j]);
            const auto ordinal = useGlobal ? globals_[i++] : (*local)[j++];
            const auto& entry = volumes_[ordinal];
            callback(entry.entity, entry.value);
        }
    }

    std::size_t volumeCount() const { return volumes_.size(); }
    std::size_t bucketCount() const { return buckets_.size(); }
    std::size_t assignmentCount() const { return assignments_; }
    std::size_t globalCount() const { return globals_.size(); }

private:
    static std::int64_t satAdd(std::int64_t a, std::int64_t b) {
        constexpr auto max = std::numeric_limits<std::int64_t>::max();
        return b > 0 && a > max - b ? max : a + b;
    }
    static std::int64_t satSub(std::int64_t a, std::int64_t b) {
        constexpr auto min = std::numeric_limits<std::int64_t>::min();
        return b > 0 && a < min + b ? min : a - b;
    }
    static std::int64_t bucketOf(std::int64_t raw) {
        return floorDiv(raw, kBucketMeters * Fixed::kOne);
    }

    std::vector<SpatialVolume<T>> volumes_;
    std::vector<std::uint32_t> globals_;
    std::map<SpatialBucket, std::vector<std::uint32_t>> buckets_;
    std::size_t assignments_ = 0;
};

struct SpatialFieldIndex {
    std::uint64_t tick = 0;
    bool initialized = false;
    SpatialVolumeIndex<WindZone> wind;
    SpatialVolumeIndex<WindShelter> shelters;
    SpatialVolumeIndex<GravityField> gravity;
    SpatialVolumeIndex<StormEvent> storms;

    // Snapshot build, no ECS writes or persistence. A caller must rebuild
    // after ANY field edit, World structural change, restore/load or tick.
    bool rebuild(const World& world) {
        const auto* state = world.resourceConst<State>();
        if (!state) {
            *this = SpatialFieldIndex{};
            return false;
        }
        SpatialFieldIndex next;
        next.tick = state->tick;
        next.initialized = true;
        world.eachExistingConst<WindZone>([&](Entity e, const WindZone& v) {
            next.wind.add(e, v, v.center, v.radiusMeters, v.radiusMeters);
        });
        world.eachExistingConst<WindShelter>([&](Entity e, const WindShelter& v) {
            next.shelters.add(e, v, v.center, v.radiusMeters, v.radiusMeters);
        });
        world.eachExistingConst<GravityField>([&](Entity e, const GravityField& v) {
            // Uniform radius zero is intentionally global. Invalid radius
            // also remains visible to the exact reference evaluator.
            next.gravity.add(e, v, v.center, v.radiusMeters, v.radiusMeters);
        });
        world.eachExistingConst<StormEvent>([&](Entity e, const StormEvent& v) {
            // Cylinder: horizontal radius and separate vertical height.
            next.storms.add(e, v, v.center, v.outerRadiusMeters, v.heightMeters);
        });
        *this = std::move(next);
        return true;
    }

    bool sameTick(const World& world) const {
        const auto* state = world.resourceConst<State>();
        return initialized && state && state->tick == tick;
    }
};

inline Vec3 indexedWindAt(const World& world, const SpatialFieldIndex& index,
                         Vec3 position) {
    // A stale index cannot be used after a committed tick.
    if (!index.sameTick(world)) return windAt(world, position);
    return windAtWith(world, position,
        [&](auto&& callback) { index.wind.visit(position, callback); },
        [&](auto&& callback) { index.shelters.visit(position, callback); });
}

inline Vec3 indexedGravityAt(const World& world, const SpatialFieldIndex& index,
                            Vec3 position) {
    if (!index.sameTick(world)) return gravityAt(world, position);
    return gravityAtWith(world, position,
        [&](auto&& callback) { index.gravity.visit(position, callback); });
}

inline StormInfluence indexedStormsAt(const World& world,
                                      const SpatialFieldIndex& index,
                                      Vec3 position, std::uint64_t tick) {
    if (!index.sameTick(world) || tick != index.tick)
        return stormsAt(world, position, tick);
    return stormsAtWith(world, position, tick,
        [&](auto&& callback) { index.storms.visit(position, callback); });
}

inline Vec3 indexedWindWithStormsAt(const World& world,
                                    const SpatialFieldIndex& index,
                                    Vec3 position) {
    const auto* state = world.resourceConst<State>();
    if (!state) return {};
    const Vec3 base = indexedWindAt(world, index, position);
    const Vec3 storm = indexedStormsAt(world, index, position, state->tick).wind;
    const Fixed cap = Fixed::fromInt(200);
    return {fxClamp(base.x + storm.x, -cap, cap),
            fxClamp(base.y + storm.y, -cap, cap),
            fxClamp(base.z + storm.z, -cap, cap)};
}

// Indexed read-only 3D snapshot. It retains precisely the same output schema
// and climate/hazard logic as sampleNatureField, but visits only nearby
// candidate volumes. It never mutates World or any component.
inline bool sampleNatureFieldIndexed(const World& world,
                                      const SpatialFieldIndex& index,
                                      Vec3 position, NatureFieldSample& output) {
    const auto* state = world.resourceConst<State>();
    if (!state) return false;
    NatureFieldSample result;
    result.position = position;
    result.tick = state->tick;
    result.windVelocity = indexedWindWithStormsAt(world, index, position);
    result.gravityAcceleration = indexedGravityAt(world, index, position);
    result.storms = indexedStormsAt(world, index, position, state->tick);
    const Entity cellEntity = cellAtPosition(world, position);
    if (const auto* cell = world.tryGetConst<Cell>(cellEntity)) {
        result.hasAtmosphere =
            environmentAt(world, cell->x, cell->z, result.environment);
        if (const auto* atmosphere = world.tryGetConst<Atmosphere>(cellEntity))
            result.pressureHpa = atmosphere->pressure;
        if (const auto* hazard = world.tryGetConst<HazardCell>(cellEntity)) {
            result.hasHazards = true;
            result.hazards = *hazard;
        }
    }
    output = result;
    return true;
}

} // namespace aiverse::physis
