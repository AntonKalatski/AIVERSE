// TerraWeave Arbor/Flora -> existing Renderer3D submission path.
// Pure placement transforms are CPU-only. Model GPU upload/release MUST run on
// the owning gfx::Device thread; the caller owns renderer frame/scene lifetime.
// This adapter intentionally creates ONE near model + ONE impostor per species,
// not unique models per tree.
#pragma once

#include "engine/render/TerraWeaveModelAdapter.h"
#include "engine/render/TerraWeaveImpostorAtlas.h"
#include "engine/render/Renderer3D.h"
#include "engine/world/TerraWeaveFlora.h"
#include "engine/world/TerraWeavePhenology.h"
#include "engine/world/TerraWeaveWind.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

// Render-origin rebasing in the same centimetre coordinate space as scatter.
// Renderer3D's camera and this origin must be in the same coordinate frame.
struct ForestRenderOriginCm {
    int64_t x = 0, y = 0, z = 0;
};
struct ForestCameraCm {
    int64_t x = 0, z = 0;
};

class ArborForestAdapter {
public:
    // Build a shared procedural geometry prototype off the render thread.
    // Mesh seeds are per species: an individual FoliageInstance varies only
    // in position/yaw/scale, so the GPU can instance identical geometry.
    void addPrototype(const TreeProfile& profile, uint64_t meshSeed,
                      ArborMeshSettings mesh = {8, true}) {
        if (profile.speciesId == 0 || models_.contains(profile.speciesId))
            throw std::invalid_argument("TerraWeave duplicate/zero forest species");
        BranchLodSettings settings;
        settings.mesh = mesh;
        auto model = makeArborBranchLodModel(profile, meshSeed, settings);
        // BranchLOD nodes and the skeleton use the same ordered seed. Store
        // compact branch origins once per SPECIES, never per cosmetic tree.
        const TreeSkeleton skeleton=generateTree(profile,meshSeed,TreeLOD::High);
        std::vector<render::float3> pivots;
        pivots.reserve(skeleton.branches.size());
        for(const TreeBranch& branch:skeleton.branches)
            pivots.push_back({float(branch.x0)*0.01f,
                              float(branch.y0)*0.01f,
                              float(branch.z0)*0.01f});
        if(pivots.size()!=model->nodes.size())
            throw std::logic_error("TerraWeave branch pose/skeleton mismatch");
        auto impostor = makeArborImpostorAtlas(profile, meshSeed);
        models_.emplace(profile.speciesId, std::move(model));
        billboardMaterials_.emplace(profile.speciesId, std::move(impostor.viewMaterials));
        billboards_.emplace(profile.speciesId, std::move(impostor.model));
        branchPivots_.emplace(profile.speciesId,std::move(pivots));
        evergreen_[profile.speciesId]=(profile.shape==CrownShape::Conifer);
        if(phenology_!=CanopyPhenology{})rebuildPhenologyMaterials();
    }

    // Update presentation materials once per season/environment revision,
    // never once per tree. Call before recording renderer submissions.
    // The same shared models, atlas images and per-species batching survive.
    void setPhenology(CanopyPhenology value) {
        validateCanopyPhenology(value);
        if(value==phenology_)return;
        phenology_=value;
        rebuildPhenologyMaterials();
    }
    const CanopyPhenology& phenology() const {return phenology_;}
    // PHYSIS or native gameplay supplies a bounded, presentation-only wind
    // vector. Stable tick/key controls phase; no tree ECS or GPU buffers.
    void setWind(ForestWindState value) {
        validateForestWind(value);
        wind_=value;
    }
    const ForestWindState& wind() const {return wind_;}
    void setPresentationTick(uint64_t tick) {presentationTick_=tick;}
    // Bounded per-frame temporary pose arena. 0 disables branch poses,
    // preserving the original low-cost whole-tree lean fallback.
    void setBranchPoseBudgetBytes(size_t bytes) {
        if(bytes>8u*1024u*1024u)
            throw std::invalid_argument("TerraWeave branch pose budget");
        branchPoseBudgetBytes_=bytes;
    }
    size_t branchPoseBudgetBytes() const {return branchPoseBudgetBytes_;}

    size_t prototypeCount() const { return models_.size(); }
    render::Model* prototype(uint32_t speciesId) const {
        const auto it = models_.find(speciesId);
        return it == models_.end() ? nullptr : it->second.get();
    }

    render::Model* billboardPrototype(uint32_t speciesId) const {
        const auto it = billboards_.find(speciesId);
        return it == billboards_.end() ? nullptr : it->second.get();
    }

    uint32_t billboardAzimuthCount(uint32_t speciesId) const {
        const auto it = billboardMaterials_.find(speciesId);
        return it == billboardMaterials_.end() ? 0u :
               static_cast<uint32_t>(it->second.size());
    }

    // Prepare/upload once on the render thread. Keep the same adapter alive
    // for every scene frame and releaseModels before destroying the gfx device.
    bool uploadModels(gfx::Device& device) {
        const auto upload = [&](auto& registry) {
            for (auto& [id, model] : registry) {
                (void)id;
                if (model->vb.valid() && model->ib.valid()) continue;
                render::prepareModelUpload(*model);
                if (!render::uploadModel(device, *model)) return false;
            }
            return true;
        };
        return upload(models_) && upload(billboards_);
    }

    void releaseModels(gfx::Device& device) {
        for (auto& [id, model] : models_) {
            (void)id;
            render::releaseModel(device, *model);
        }
        for (auto& [id, model] : billboards_) {
            (void)id;
            render::releaseModel(device, *model);
        }
    }

    // Does not allocate a per-instance model or a second render pipeline.
    // Use buildFloraBatches(...) for stable filtering, thinning and ordering;
    // Renderer3D selects each branch's geometry LOD by its projected size.
    // Far Impostor batches use one camera-facing alpha-masked full-tree quad
    // with the nearest 4/8/16-view silhouette atlas slice as material override.
    template<class Emit>
    void forEachDraw(const std::vector<FloraBatch>& batches,
                     ForestRenderOriginCm origin, Emit&& emit,
                     std::optional<ForestCameraCm> camera = std::nullopt) const {
        // Renderer3D::drawModel consumes nodeWorld synchronously and copies
        // node transforms into render items. For callbacks collecting draws,
        // pointers stay valid until the next forEachDraw() invocation.
        branchPoseArena_.clear();
        size_t branchPoseBytes=0;
        const auto inside = [](int64_t v) {
            return v >= -kWorldLimitCm && v <= kWorldLimitCm;
        };
        if (!inside(origin.x) || !inside(origin.y) || !inside(origin.z))
            throw std::out_of_range("TerraWeave forest render origin");
        if (camera && (!inside(camera->x) || !inside(camera->z)))
            throw std::out_of_range("TerraWeave forest camera extent");
        for (const FloraBatch& batch : batches) {
            const render::Model* model = batch.lod == TreeLOD::Impostor
                ? billboardPrototype(batch.speciesId)
                : prototype(batch.speciesId);
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
                const float yaw = batch.lod == TreeLOD::Impostor
                    ? std::atan2(float((camera ? camera->x : origin.x) - instance.xCm),
                                 float((camera ? camera->z : origin.z) - instance.zCm))
                    : float(instance.yawMilliDegrees) * 0.000017453292519943295f;
                const float scale = float(instance.scalePermille) * 0.001f;
                render::ModelDraw draw;
                draw.model = model;
                const uint32_t canopyCover=canopyCoverPermille(
                    phenology_,evergreen_.at(batch.speciesId));
                // Both the branch model and the impostor use bark material 0
                // and foliage material 1. Bare deciduous trees submit ONLY
                // bark, eliminating invisible leaf draws in winter.
                if(canopyCover==0u)draw.primitiveMaterialFilter=0;
                if (batch.lod == TreeLOD::Impostor) {
                    const auto& variants = phenology_==CanopyPhenology{} ?
                        billboardMaterials_.at(batch.speciesId) :
                        seasonalBillboardMaterials_.at(batch.speciesId);
                    const uint32_t azimuth = camera
                        ? selectArborImpostorAzimuth(
                            instance.yawMilliDegrees, instance.xCm, instance.zCm,
                            camera->x, camera->z,
                            static_cast<uint32_t>(variants.size()))
                        : 0u;
                    draw.materials = &variants[azimuth]; // stable per-species override
                } else if(phenology_!=CanopyPhenology{}) {
                    draw.materials=&seasonalNearMaterials_.at(batch.speciesId);
                }
                draw.world = render::mat4::translate(relative);
                if(wind_.maxLeanMilliDegrees &&
                   (wind_.xCmPerSec || wind_.zCmPerSec)) {
                    const auto lean=forestTreeLean(
                        wind_,presentationTick_,
                        instance.key ^ (uint64_t(instance.speciesId)<<32));
                    constexpr float millidegToRadians=0.000017453292519943295f;
                    // Apply lean in WORLD coordinates, before the tree's
                    // local yaw. Rotating around the trunk root avoids
                    // sliding the root point across terrain during sway.
                    draw.world = draw.world *
                        render::mat4::rotateZ(
                            -float(lean.towardX)*millidegToRadians) *
                        render::mat4::rotateX(
                             float(lean.towardZ)*millidegToRadians);
                }
                draw.world = draw.world * render::mat4::rotateY(yaw) *
                             render::mat4::scale({scale, scale, scale});
                // Nearby BranchLOD has independent rigid nodes for trunk
                // and branches. Bend each child around its own stable
                // branch pivot while preserving parent hierarchy. More
                // distant impostors remain on the cheap whole-tree sway.
                if(batch.lod!=TreeLOD::Impostor &&
                   wind_.branchFlexPermille>0 &&
                   wind_.maxLeanMilliDegrees>0 &&
                   (wind_.xCmPerSec||wind_.zCmPerSec)) {
                    const auto& pivots=branchPivots_.at(batch.speciesId);
                    const size_t bytes=model->nodes.size()*
                        sizeof(render::mat4);
                    if(model->nodes.size()>1 &&
                       bytes<=branchPoseBudgetBytes_ &&
                       branchPoseBytes<=branchPoseBudgetBytes_-bytes) {
                        branchPoseArena_.emplace_back();
                        buildBranchWindPose(*model,pivots,instance,
                                            branchPoseArena_.back());
                        draw.nodeWorld=&branchPoseArena_.back();
                        branchPoseBytes+=bytes;
                    }
                }
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
        const auto camera = renderer.cameraPos();
        const double relativeXcm = double(camera.x) * 100.0;
        const double relativeZcm = double(camera.z) * 100.0;
        if (!std::isfinite(relativeXcm) || !std::isfinite(relativeZcm) ||
            std::fabs(relativeXcm) > double(kWorldLimitCm) * 2.0 ||
            std::fabs(relativeZcm) > double(kWorldLimitCm) * 2.0)
            throw std::out_of_range("TerraWeave forest camera extent");
        const ForestCameraCm cameraCm{
            origin.x + static_cast<int64_t>(std::llround(relativeXcm)),
            origin.z + static_cast<int64_t>(std::llround(relativeZcm))
        };
        forEachDraw(batches, origin, [&](const render::ModelDraw& draw) {
            renderer.drawModel(draw);
        }, cameraCm);
    }

private:
    // All Arbor branch geometry is authored in model-space centimetres
    // converted to metres, not re-centered around a local branch origin.
    // Therefore compose cumulative model-space transforms around each
    // branch's ORIGINAL start pivot; children inherit their parent's pose.
    void buildBranchWindPose(
            const render::Model& model,
            const std::vector<render::float3>& pivots,
            const FoliageInstance& instance,
            std::vector<render::mat4>& pose) const {
        pose.resize(model.nodes.size());
        constexpr float milliRadians=0.000017453292519943295f;
        const float flex=float(wind_.branchFlexPermille)*0.001f;
        const uint64_t treeKey=instance.key ^
            (uint64_t(instance.speciesId)<<32);
        for(int nodeIndex:model.order){
            const size_t n=static_cast<size_t>(nodeIndex);
            const int parent=model.nodes[n].parent;
            const render::mat4 parentPose=parent>=0 ?
                pose[size_t(parent)] : render::mat4::identity();
            if(n==0 || n>=pivots.size()) {
                pose[n]=parentPose; // keep trunk stable under root sway
                continue;
            }
            const uint64_t phaseKey=detail::mix64(treeKey ^
                detail::mix64(uint64_t(n)+0x4252414e43485744ULL));
            const auto lean=forestTreeLean(
                wind_,presentationTick_,phaseKey);
            const auto pivot=pivots[n];
            const render::float3 neg{-pivot.x,-pivot.y,-pivot.z};
            const render::mat4 bend=
                render::mat4::translate(pivot) *
                render::mat4::rotateZ(
                    -float(lean.towardX)*milliRadians*flex) *
                render::mat4::rotateX(
                     float(lean.towardZ)*milliRadians*flex) *
                render::mat4::translate(neg);
            pose[n]=parentPose*bend;
        }
    }
    void rebuildPhenologyMaterials() {
        seasonalNearMaterials_.clear();
        seasonalBillboardMaterials_.clear();
        if(phenology_==CanopyPhenology{})return;
        const auto tint=[this](render::Material& m,uint32_t coverage) {
            m.baseColor.x=std::clamp(m.baseColor.x*
                (float(phenology_.tintRPermille)*0.001f),0.0f,1.0f);
            m.baseColor.y=std::clamp(m.baseColor.y*
                (float(phenology_.tintGPermille)*0.001f),0.0f,1.0f);
            m.baseColor.z=std::clamp(m.baseColor.z*
                (float(phenology_.tintBPermille)*0.001f),0.0f,1.0f);
            if(coverage<1000u){
                m.baseColor.w*=float(coverage)*0.001f;
                m.alpha=render::AlphaMode::Blend;
                m.transparent=true;
                m.castShadow=false; // do not cast fully opaque summer leaf shadows
            }
        };
        for(const auto& [id,model]:models_) {
            auto materials=model->materials;
            if(materials.size()>1)
                tint(materials[1],canopyCoverPermille(
                    phenology_,evergreen_.at(id)));
            seasonalNearMaterials_.emplace(id,std::move(materials));
        }
        // The impostor atlas now has two disjoint pages in ONE GPU image:
        // bark material 0 is never tinted/faded, foliage material 1 is
        // seasonally adjusted. Per-azimuth override vectors remain stable
        // across frame submissions, with no per-instance materials.
        for(const auto& [id,variants]:billboardMaterials_) {
            auto seasonal=variants;
            const uint32_t coverage=canopyCoverPermille(
                phenology_,evergreen_.at(id));
            for(auto& materials:seasonal) {
                if(materials.size()!=2)
                    throw std::logic_error("TerraWeave impostor material slots");
                tint(materials[1],coverage);
            }
            seasonalBillboardMaterials_.emplace(id,std::move(seasonal));
        }
    }
    CanopyPhenology phenology_{};
    ForestWindState wind_{};
    uint64_t presentationTick_=0;
    size_t branchPoseBudgetBytes_=4u*1024u*1024u;
    // Stable deque element addresses for ModelDraw::nodeWorld borrowed
    // pointers, valid until the next forEachDraw call on this adapter.
    mutable std::deque<std::vector<render::mat4>> branchPoseArena_;
    std::map<uint32_t,std::vector<render::float3>> branchPivots_;
    std::map<uint32_t,bool> evergreen_;
    std::map<uint32_t,std::vector<render::Material>> seasonalNearMaterials_;
    std::map<uint32_t,std::vector<std::vector<render::Material>>> seasonalBillboardMaterials_;
    std::map<uint32_t, std::unique_ptr<render::Model>> models_;
    std::map<uint32_t, std::unique_ptr<render::Model>> billboards_;
    // Lifetime of these vectors covers all queued ModelDraws until render().
    // Distinct views have distinct UV transforms; GPU texture stays shared.
    std::map<uint32_t, std::vector<std::vector<render::Material>>> billboardMaterials_;
};

} // namespace aiverse::terraweave
