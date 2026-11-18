// TerraWeave opt-in forest scene host for every AIVERSE genre.
// CPU placement streaming is deterministic and bounded. GPU upload and
// draw submission use the existing Renderer3D/gfx::Device render thread.
// Does not create one ECS entity per cosmetic tree or mutate game state.
#pragma once

#include "engine/render/TerraWeaveForestAdapter.h"
#include "engine/world/AsyncChunkStream.h"
#include "engine/world/TerraWeaveExclusions.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct ForestSceneSettings {
    TerrainSettings terrain;
    BiomeSettings biomes;
    ScatterSettings scatter;
    int32_t chunkSizeCm = 6400;       // signed world-space lattice tiles
    uint32_t radiusChunks = 4;        // residency extent around observer
    uint32_t maxResidentChunks = 64;  // hard CPU cache bound
    bool asynchronous = false;        // opt in; synchronous native API remains source compatible
    uint32_t maxPendingJobs = 3;      // 1..8; bounded queue and reserved bytes
    uint32_t requestsPerUpdate = 2;  // 1..8
    uint32_t publicationsPerUpdate = 2; // 1..8
    FloraView view{0, 0, 3000, 9000, 12000, 24000, 650, 3000};
};

struct ForestSceneStats {
    size_t chunks = 0;
    size_t instances = 0;
    size_t species = 0;
    uint64_t revision = 0;
    size_t preparing = 0, discarded = 0; // asynchronous streaming diagnostics
};

// Header-only native-project API. Call addSpecies before streamAt/uploadModels.
// streamAt(...) is currently synchronous and must run on a host work phase;
// run it on an owning thread, not concurrently with submit/releaseModels.
class TerraWeaveForestScene {
public:
    // Optional host-provided *immutable* terrain snapshots. Decorative only.
    // Keep callbacks alive and stable while streamAt() evaluates patches.
    using HeightSource = std::function<int32_t(int64_t, int64_t)>;
    using PlacementFilter = std::function<bool(const FoliageInstance&)>;

    explicit TerraWeaveForestScene(ForestSceneSettings settings = {})
        : settings_(std::move(settings)) {
        if (settings_.chunkSizeCm < 100 || settings_.chunkSizeCm > 100000 ||
            settings_.radiusChunks > 8 ||
            settings_.maxResidentChunks == 0 ||
            settings_.maxResidentChunks > 289 ||
            settings_.scatter.spacingCm <= 0 ||
            settings_.scatter.densityPermille > 1000 ||
            settings_.maxPendingJobs == 0 || settings_.maxPendingJobs > 8 ||
            settings_.requestsPerUpdate == 0 || settings_.requestsPerUpdate > 8 ||
            settings_.publicationsPerUpdate == 0 ||
            settings_.publicationsPerUpdate > 8)
            throw std::invalid_argument("TerraWeave forest scene settings");
        // Bound scatter candidate work across the WHOLE cache, not merely
        // each chunk. Tiny spacing and a large radius must not allocate
        // hundreds of millions of instances before budget checks.
        const int64_t cellsPerAxis =
            (int64_t(settings_.chunkSizeCm) + settings_.scatter.spacingCm - 1) /
            settings_.scatter.spacingCm + 2;
        const int64_t perChunk = cellsPerAxis * cellsPerAxis;
        if (perChunk > 65536 ||
            perChunk * settings_.maxResidentChunks > 1000000)
            throw std::invalid_argument("TerraWeave forest scatter candidate budget");
        asyncReservation_ = 4096 + static_cast<size_t>(perChunk) * sizeof(FoliageInstance);
        // Validate LOD/far transition settings before first draw.
        (void)buildFloraBatches({}, settings_.view);
    }

    void addSpecies(const TreeProfile& profile,
                    const FoliageSpecies& placement,
                    uint64_t prototypeSeed) {
        if (residentStarted_ || gpuUploaded_)
            throw std::logic_error("TerraWeave species registry already in use");
        if (placement.id != profile.speciesId || placement.id == 0)
            throw std::invalid_argument("TerraWeave forest species ids");
        // Validates profile before insertion and prepares shared geometry/
        // impostor atlas once per species, rather than once per tree.
        adapter_.addPrototype(profile, prototypeSeed);
        species_.push_back(placement);
    }

    // Scars from buildings, roads and authored clearings can be supplied as
    // world-space half-open centimetre rectangles. Changing them invalidates
    // the CPU cache without changing the deterministic generator settings.
    void setExclusions(std::vector<RectCm> rectangles) {
        for (const RectCm& r : rectangles) {
            if (r.maxX < r.minX || r.maxZ < r.minZ ||
                r.minX < -kWorldLimitCm || r.minZ < -kWorldLimitCm ||
                r.maxX > kWorldLimitCm || r.maxZ > kWorldLimitCm)
                throw std::invalid_argument("TerraWeave forest exclusion");
        }
        exclusions_ = std::move(rectangles);
        invalidate();
    }

    // Adapt procedural scatter to a real authored/physics terrain. Changing
    // a provider invalidates cached chunks; provider functions are evaluated
    // by streamAt(), not on GPU or from the authoritative ECS simulation.
    void setHeightSource(HeightSource source) {
        heightSource_ = std::move(source);
        invalidate();
    }
    void setPlacementFilter(PlacementFilter filter) {
        placementFilter_ = std::move(filter);
        invalidate();
    }
    // Geometry prepared by any project/terrain provider. Immutable while
    // workers are running; a new shared snapshot invalidates old results.
    void setPlacementMask(std::shared_ptr<const FoliageExclusionMask> mask) {
        placementMask_ = std::move(mask);
        invalidate();
    }

    // Invalidate resident foliage if generation settings / landscape are
    // explicitly changed. Game-specific persistent harvest/removal sets
    // should be applied by the caller before presentation submission.
    void invalidate() {
        patches_.clear();
        ++revision_;
        if (revision_ == 0) revision_ = 1;
        // Live async jobs capture immutable configuration snapshots. Removing
        // a catalog key cancels its publication while its staging reservation
        // stays held until the worker completes and update() drains it.
        if (async_) {
            for (const world::Cell& cell : asyncCatalog_)
                async_->remove(cell, [](world::Cell) {});
            asyncCatalog_.clear();
            asyncSnapshot_.store(captureSnapshot(), std::memory_order_release);
        }
    }

    // Prepare visible world chunks using globally keyed scatter.
    // Any spatial partition generates the same placements and stable IDs.
    // Select nearest candidates deterministically when the budget is full.
    void streamAt(ForestCameraCm camera) {
        if (camera.x < -kWorldLimitCm || camera.x > kWorldLimitCm ||
            camera.z < -kWorldLimitCm || camera.z > kWorldLimitCm)
            throw std::out_of_range("TerraWeave forest observer extent");
        if (settings_.asynchronous) {
            streamAsyncAt(camera);
            return;
        }
        residentStarted_ = true;
        observer_ = camera;
        const int64_t side = settings_.chunkSizeCm;
        const int64_t cx = detail::floorDiv(camera.x, side);
        const int64_t cz = detail::floorDiv(camera.z, side);
        using Key = std::pair<int64_t, int64_t>;
        std::vector<Key> candidates;
        const int64_t radius = settings_.radiusChunks;
        for (int64_t dz = -radius; dz <= radius; ++dz) {
            for (int64_t dx = -radius; dx <= radius; ++dx) {
                const int64_t x = (cx + dx) * side;
                const int64_t z = (cz + dz) * side;
                // Each scatter candidate samples +/-100cm for slope.
                if (x < -kWorldLimitCm + 100 ||
                    z < -kWorldLimitCm + 100 ||
                    x + side > kWorldLimitCm - 100 ||
                    z + side > kWorldLimitCm - 100)
                    continue;
                candidates.push_back({cx + dx, cz + dz});
            }
        }
        std::sort(candidates.begin(), candidates.end(),
                  [cx, cz](Key a, Key b) {
                      const int64_t ax = a.first - cx, az = a.second - cz;
                      const int64_t bx = b.first - cx, bz = b.second - cz;
                      const int64_t da = ax * ax + az * az;
                      const int64_t db = bx * bx + bz * bz;
                      return da == db ? a < b : da < db;
                  });
        if (candidates.size() > settings_.maxResidentChunks)
            candidates.resize(settings_.maxResidentChunks);
        const std::set<Key> desired(candidates.begin(), candidates.end());
        for (auto it = patches_.begin(); it != patches_.end();) {
            if (!desired.contains(it->first)) it = patches_.erase(it);
            else ++it;
        }
        for (const Key& key : candidates) {
            if (patches_.contains(key)) continue;
            const int64_t minX = key.first * side;
            const int64_t minZ = key.second * side;
            auto instances = scatter(
                settings_.terrain, settings_.biomes, settings_.scatter,
                {minX, minZ, minX + side, minZ + side},
                species_, exclusions_);
            if (heightSource_ || placementFilter_ || placementMask_) {
                size_t write = 0;
                for (auto instance : instances) {
                    if (heightSource_)
                        instance.heightCm = heightSource_(instance.xCm, instance.zCm);
                    if (int64_t(instance.heightCm) < -kWorldLimitCm ||
                        int64_t(instance.heightCm) > kWorldLimitCm)
                        throw std::out_of_range("TerraWeave forest sampled height extent");
                    if (placementMask_ &&
                        placementMask_->excludes(instance.xCm, instance.zCm)) continue;
                    if (placementFilter_ && !placementFilter_(instance)) continue;
                    instances[write++] = instance;
                }
                instances.resize(write);
            }
            patches_.emplace(key, std::move(instances));
        }
    }

    std::vector<FloraBatch> visibleBatches() const {
        if (!residentStarted_) return {};
        size_t count = 0;
        for (const auto& [key, patch] : patches_) {
            (void)key;
            count += patch.size();
        }
        std::vector<FoliageInstance> placements;
        placements.reserve(count);
        for (const auto& [key, patch] : patches_) {
            (void)key;
            placements.insert(placements.end(), patch.begin(), patch.end());
        }
        FloraView view = settings_.view;
        view.cameraXcm = observer_.x;
        view.cameraZcm = observer_.z;
        return buildFloraBatches(placements, view);
    }

    // Call on GPU-owning render thread AFTER all species are registered.
    bool uploadModels(gfx::Device& device) {
        if (!adapter_.uploadModels(device)) {
            // Upload can fail midway through prototype iteration; free
            // successfully uploaded buffers/textures before retry or abort.
            adapter_.releaseModels(device);
            gpuUploaded_ = false;
            return false;
        }
        gpuUploaded_ = true;
        return true;
    }

    // Submit after renderer.beginScene, before renderer.render.
    // No own shaders, passes, buffers or engine-global ECS scheduler.
    void submit(render::Renderer3D& renderer,
                ForestRenderOriginCm origin = {}) const {
        if (!gpuUploaded_) throw std::logic_error(
            "TerraWeave forest models must be uploaded before drawing");
        const auto batches = visibleBatches();
        adapter_.submit(renderer, batches, origin);
    }

    void releaseModels(gfx::Device& device) {
        adapter_.releaseModels(device);
        gpuUploaded_ = false;
    }

    ForestSceneStats stats() const {
        size_t n = 0;
        for (const auto& [key, patch] : patches_) {
            (void)key;
            n += patch.size();
        }
        const auto state = async_ ? async_->status() : world::AsyncChunkStream::Status{};
        return {patches_.size(), n, species_.size(), revision_,
                state.preparing, state.discarded};
    }

    const ForestSceneSettings& settings() const { return settings_; }
    // Must be called on the owning update thread, before destroying captured
    // terrain snapshots or callback contexts. Also called by the destructor.
    void shutdownAsync() {
        if (async_) {
            async_->shutdown([this](world::Cell cell) {
                patches_.erase({cell.x, cell.z});
            });
            async_.reset();
            asyncCatalog_.clear();
        }
        asyncSnapshot_.store({}, std::memory_order_release);
    }
    ~TerraWeaveForestScene() { shutdownAsync(); }
    const ArborForestAdapter& adapter() const { return adapter_; }

private:
    struct GenerationSnapshot {
        TerrainSettings terrain;
        BiomeSettings biomes;
        ScatterSettings scatter;
        std::vector<FoliageSpecies> species;
        std::vector<RectCm> exclusions;
        HeightSource heightSource;
        PlacementFilter placementFilter;
        std::shared_ptr<const FoliageExclusionMask> placementMask;
        int32_t side = 0;
        uint64_t revision = 0;
    };
    struct PreparedPatch final : world::AsyncChunkStream::Resource {
        uint64_t revision = 0;
        std::vector<FoliageInstance> instances;
    };
    std::shared_ptr<const GenerationSnapshot> captureSnapshot() const {
        auto snap = std::make_shared<GenerationSnapshot>();
        snap->terrain = settings_.terrain;
        snap->biomes = settings_.biomes;
        snap->scatter = settings_.scatter;
        snap->species = species_;
        snap->exclusions = exclusions_;
        snap->heightSource = heightSource_;
        snap->placementFilter = placementFilter_;
        snap->placementMask = placementMask_;
        snap->side = settings_.chunkSizeCm;
        snap->revision = revision_;
        return snap;
    }
    static std::vector<FoliageInstance> preparePatch(
            const GenerationSnapshot& s, int64_t cx, int64_t cz) {
        const int64_t minX = cx * s.side, minZ = cz * s.side;
        auto instances = scatter(s.terrain, s.biomes, s.scatter,
            {minX, minZ, minX + s.side, minZ + s.side},
            s.species, s.exclusions);
        if (s.heightSource || s.placementFilter || s.placementMask) {
            size_t write = 0;
            for (auto instance : instances) {
                if (s.heightSource)
                    instance.heightCm = s.heightSource(instance.xCm, instance.zCm);
                if (int64_t(instance.heightCm) < -kWorldLimitCm ||
                    int64_t(instance.heightCm) > kWorldLimitCm)
                    throw std::out_of_range("TerraWeave forest sampled height extent");
                if (s.placementMask &&
                    s.placementMask->excludes(instance.xCm, instance.zCm)) continue;
                if (s.placementFilter && !s.placementFilter(instance)) continue;
                instances[write++] = instance;
            }
            instances.resize(write);
        }
        return instances;
    }
    void streamAsyncAt(ForestCameraCm camera) {
        // All catalog mutation/publication remains on the caller's update
        // thread. Only pure preparePatch(snapshot) runs on the worker thread.
        residentStarted_ = true;
        observer_ = camera;
        if (!async_) {
            asyncSnapshot_.store(captureSnapshot(), std::memory_order_release);
            world::StreamLimits limits;
            limits.residentChunks = settings_.maxResidentChunks;
            limits.residentBytes = asyncReservation_ * settings_.maxResidentChunks;
            limits.retryUpdates = 2;
            async_ = std::make_unique<world::AsyncChunkStream>(
                limits, asyncReservation_ * (settings_.maxPendingJobs + 1u),
                settings_.maxPendingJobs,
                [this](world::Cell cell) -> world::AsyncChunkStream::Payload {
                    const auto snap = asyncSnapshot_.load(std::memory_order_acquire);
                    if (!snap) return {};
                    auto result = std::make_shared<PreparedPatch>();
                    result->revision = snap->revision;
                    result->instances = preparePatch(*snap, cell.x, cell.z);
                    return result;
                });
        }
        const int64_t side = settings_.chunkSizeCm;
        const int64_t cx = detail::floorDiv(camera.x, side);
        const int64_t cz = detail::floorDiv(camera.z, side);
        using Key = std::pair<int64_t, int64_t>;
        std::vector<Key> candidates;
        for (int64_t dz = -int64_t(settings_.radiusChunks);
                dz <= int64_t(settings_.radiusChunks); ++dz)
            for (int64_t dx = -int64_t(settings_.radiusChunks);
                    dx <= int64_t(settings_.radiusChunks); ++dx) {
                const int64_t minX = (cx + dx) * side;
                const int64_t minZ = (cz + dz) * side;
                if (minX < -kWorldLimitCm + 100 ||
                    minZ < -kWorldLimitCm + 100 ||
                    minX + side > kWorldLimitCm - 100 ||
                    minZ + side > kWorldLimitCm - 100) continue;
                candidates.push_back({cx + dx, cz + dz});
            }
        std::sort(candidates.begin(), candidates.end(), [cx,cz](Key a,Key b) {
            const int64_t ax=a.first-cx, az=a.second-cz;
            const int64_t bx=b.first-cx, bz=b.second-cz;
            const int64_t da=ax*ax+az*az, db=bx*bx+bz*bz;
            return da==db?a<b:da<db;
        });
        if (candidates.size() > settings_.maxResidentChunks)
            candidates.resize(settings_.maxResidentChunks);
        std::set<world::Cell> wanted;
        std::vector<world::Cell> pins;
        pins.reserve(candidates.size());
        for (const Key& key : candidates) {
            // Forest key range is limited to +/-1e9 cm, so uint32 Cell
            // coordinates cannot overflow for any supported chunk size.
            world::Cell cell{static_cast<int32_t>(key.first), 0,
                             static_cast<int32_t>(key.second)};
            wanted.insert(cell);
            pins.push_back(cell);
        }
        for (auto it = asyncCatalog_.begin(); it != asyncCatalog_.end();) {
            if (wanted.contains(*it)) { ++it; continue; }
            const world::Cell cell=*it;
            async_->remove(cell, [this](world::Cell old) {
                patches_.erase({old.x, old.z});
            });
            patches_.erase({cell.x, cell.z});
            it=asyncCatalog_.erase(it);
        }
        for (const world::Cell& cell : pins) {
            if (asyncCatalog_.contains(cell)) continue;
            if (!async_->add(cell, asyncReservation_))
                throw std::runtime_error("TerraWeave async chunk reservation failed");
            asyncCatalog_.insert(cell);
        }
        async_->update({}, std::span<const world::Cell>(pins.data(),pins.size()),
            settings_.requestsPerUpdate, settings_.publicationsPerUpdate,
            [this](world::Cell cell,const world::AsyncChunkStream::Payload& payload) {
                const auto& patch = static_cast<const PreparedPatch&>(*payload);
                if (patch.revision != revision_ || !asyncCatalog_.contains(cell))
                    return false;
                patches_[{cell.x,cell.z}] = patch.instances;
                return true;
            },
            [this](world::Cell cell) {
                patches_.erase({cell.x,cell.z});
            });
    }

    ForestSceneSettings settings_;
    ArborForestAdapter adapter_;
    std::vector<FoliageSpecies> species_;
    std::vector<RectCm> exclusions_;
    HeightSource heightSource_;
    PlacementFilter placementFilter_;
    std::shared_ptr<const FoliageExclusionMask> placementMask_;
    std::map<std::pair<int64_t, int64_t>, std::vector<FoliageInstance>> patches_;
    ForestCameraCm observer_;
    uint64_t revision_ = 1;
    bool residentStarted_ = false;
    bool gpuUploaded_ = false;
    size_t asyncReservation_ = 0;
    std::atomic<std::shared_ptr<const GenerationSnapshot>> asyncSnapshot_{};
    std::unique_ptr<world::AsyncChunkStream> async_;
    std::set<world::Cell> asyncCatalog_;
};

} // namespace aiverse::terraweave
