// TerraWeave Arbor/Flora -> existing Renderer3D submission path.
// Pure placement transforms are CPU-only. Model GPU upload/release MUST run on
// the owning gfx::Device thread; the caller owns renderer frame/scene lifetime.
// This adapter intentionally creates ONE Model per species, not per tree.
#pragma once

#include "engine/render/TerraWeaveModelAdapter.h"
#include "engine/render/Renderer3D.h"
#include "engine/world/TerraWeaveFlora.h"
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

// Render-origin rebasing in the same centimetre coordinate space as scatter.
// Renderer3D's camera and this origin must be in the same coordinate frame.
struct ForestRenderOriginCm {
    int64_t x = 0, y = 0, z = 0;
};

class ArborForestAdapter {
public:
    // Build a shared procedural geometry prototype off the render thread.
    // Mesh seeds are per species: an individual FoliageInstance varies only
    // in position/yaw/scale, so the GPU can instance identical geometry.
    void addPrototype(const TreeProfile& profile, uint64_t meshSeed,
                      ArborMeshSettings mesh = {}) {
        if (profile.speciesId == 0 || models_.contains(profile.speciesId))
            throw std::invalid_argument("TerraWeave duplicate/zero forest species");
        auto model = makeArborModel(profile, meshSeed, mesh);
        models_.emplace(profile.speciesId, std::move(model));
    }

    size_t prototypeCount() const { return models_.size(); }
    render::Model* prototype(uint32_t speciesId) const {
        const auto it = models_.find(speciesId);
        return it == models_.end() ? nullptr : it->second.get();
    }

    // Prepare/upload once on the render thread. Keep the same adapter alive
    // for every scene frame and releaseModels before destroying the gfx device.
    bool uploadModels(gfx::Device& device) {
        for (auto& [id, model] : models_) {
            (void)id;
            if (model->vb.valid() && model->ib.valid()) continue;
            render::prepareModelUpload(*model);
            if (!render::uploadModel(device, *model)) return false;
        }
        return true;
    }

    void releaseModels(gfx::Device& device) {
        for (auto& [id, model] : models_) {
            (void)id;
            render::releaseModel(device, *model);
        }
    }

    // Does not allocate a per-instance model or a second render pipeline.
    // Use buildFloraBatches(...) for stable filtering, thinning and ordering;
    // Renderer3D then chooses the 3 geometric LODs by projected pixel size.
    // An Impostor batch currently falls back to that model's lowest mesh,
    // until an actual billboard texture/LOD is implemented.
    template<class Emit>
    void forEachDraw(const std::vector<FloraBatch>& batches,
                     ForestRenderOriginCm origin, Emit&& emit) const {
        const auto inside = [](int64_t v) {
            return v >= -kWorldLimitCm && v <= kWorldLimitCm;
        };
        if (!inside(origin.x) || !inside(origin.y) || !inside(origin.z))
            throw std::out_of_range("TerraWeave forest render origin");
        for (const FloraBatch& batch : batches) {
            const render::Model* model = prototype(batch.speciesId);
            if (!model) throw std::invalid_argument("TerraWeave unregistered forest species");
            for (const FoliageInstance& instance : batch.instances) {
                if (instance.speciesId != batch.speciesId ||
                    instance.scalePermille == 0 || instance.scalePermille > 10000 ||
                    instance.yawMilliDegrees >= 360000)
                    throw std::invalid_argument("TerraWeave invalid forest instance");
                if (!inside(instance.xCm) || !inside(instance.zCm) ||
                    !inside(instance.heightCm))
                    throw std::out_of_range("TerraWeave forest instance extent");

                // Subtract in int64 BEFORE converting to float: a floating
                // origin prevents visible jitter in large streamed worlds.
                const render::float3 relative{
                    float(instance.xCm - origin.x) * 0.01f,
                    float(int64_t(instance.heightCm) - origin.y) * 0.01f,
                    float(instance.zCm - origin.z) * 0.01f
                };
                const float yaw = float(instance.yawMilliDegrees) *
                    0.000017453292519943295f;
                const float scale = float(instance.scalePermille) * 0.001f;
                render::ModelDraw draw;
                draw.model = model;
                draw.world = render::mat4::translate(relative) *
                             render::mat4::rotateY(yaw) *
                             render::mat4::scale({scale, scale, scale});
                // Namespaced stable per-tree identity; never derive it from
                // the transient index in the GPU instance buffer.
                draw.persistentId = detail::mix64(
                    instance.key ^ (uint64_t(instance.speciesId) << 32) ^
                    0x54eaa75eedc0de11ULL);
                if (draw.persistentId == 0) draw.persistentId = 1;
                draw.flags = render::kDrawStatic;
                emit(draw);
            }
        }
    }

    // Call after Renderer3D::beginScene and before Renderer3D::render.
    // drawModel uses the engine's normal RHI, instancing, shadow and LOD paths.
    void submit(render::Renderer3D& renderer, const std::vector<FloraBatch>& batches,
                ForestRenderOriginCm origin = {}) const {
        forEachDraw(batches, origin, [&](const render::ModelDraw& draw) {
            renderer.drawModel(draw);
        });
    }

private:
    std::map<uint32_t, std::unique_ptr<render::Model>> models_;
};

} // namespace aiverse::terraweave
