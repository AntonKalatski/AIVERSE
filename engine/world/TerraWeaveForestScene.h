// TerraWeave opt-in forest scene host for every AIVERSE genre.
// CPU placement streaming is deterministic and bounded. GPU upload and
// draw submission use the existing Renderer3D/gfx::Device render thread.
// Does not create one ECS entity per cosmetic tree or mutate game state.
#pragma once

#include "engine/render/TerraWeaveForestAdapter.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
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
    FloraView view{0, 0, 3000, 9000, 12000, 24000, 650, 3000};
};

struct ForestSceneStats {
    size_t chunks = 0;
    size_t instances = 0;
    size_t species = 0;
    uint64_t revision = 0;
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
            settings_.scatter.densityPermille > 1000)
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

    // Invalidate resident foliage if generation settings / landscape are
    // explicitly changed. Game-specific persistent harvest/removal sets
    // should be applied by the caller before presentation submission.
    void invalidate() {
        patches_.clear();
        ++revision_;
        if (revision_ == 0) revision_ = 1;
    }

    // Prepare visible world chunks using globally keyed scatter.
    // Any spatial partition generates the same placements and stable IDs.
    // Select nearest candidates deterministically when the budget is full.
    void streamAt(ForestCameraCm camera) {
        if (camera.x < -kWorldLimitCm || camera.x > kWorldLimitCm ||
            camera.z < -kWorldLimitCm || camera.z > kWorldLimitCm)
            throw std::out_of_range("TerraWeave forest observer extent");
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
            if (heightSource_ || placementFilter_) {
                size_t write = 0;
                for (auto instance : instances) {
                    if (heightSource_)
                        instance.heightCm = heightSource_(instance.xCm, instance.zCm);
                    if (int64_t(instance.heightCm) < -kWorldLimitCm ||
                        int64_t(instance.heightCm) > kWorldLimitCm)
                        throw std::out_of_range("TerraWeave forest sampled height extent");
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
        return {patches_.size(), n, species_.size(), revision_};
    }

    const ForestSceneSettings& settings() const { return settings_; }
    const ArborForestAdapter& adapter() const { return adapter_; }

private:
    ForestSceneSettings settings_;
    ArborForestAdapter adapter_;
    std::vector<FoliageSpecies> species_;
    std::vector<RectCm> exclusions_;
    HeightSource heightSource_;
    PlacementFilter placementFilter_;
    std::map<std::pair<int64_t, int64_t>, std::vector<FoliageInstance>> patches_;
    ForestCameraCm observer_;
    uint64_t revision_ = 1;
    bool residentStarted_ = false;
    bool gpuUploaded_ = false;
};

} // namespace aiverse::terraweave
