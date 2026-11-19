// AIVERSE render: non-owning, genre-neutral resource owner aggregation.
// Count resource allocations once by owner, Model identity, and shared
// geometry page identity across multiple native project hosts.
//
// Caller guarantees device-thread access and that every host belongs to
// the supplied gfx::Device. This class DOES NOT claim driver VRAM bytes,
// destroy GPU resources, wait on GPU fences, or own the registered caches.
#pragma once
#include "GlobalGeometryPool.h"
#include "ModelResourceCache.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <span>
#include <vector>

namespace aiverse::render {

enum class RenderResourceHealth : uint8_t {
    Ready, PendingGpuRetirement, DeviceUnhealthy
};

struct RenderResourceGroupStats {
    size_t uniqueOwners=0;
    size_t residentModels=0,retiredModels=0;
    size_t uniqueTextureOwners=0;
    size_t uniqueTextures=0,compressedTextures=0;
    uint64_t residentModelKnownBytes=0,retiredModelKnownBytes=0;
    uint64_t textureOwnerRgba8EquivalentBytes=0;
    // Device page capacity from the exact set of referenced pages. These
    // bytes must NOT be counted a second time for every Model or host.
    GeometryPoolReferencedStats geometryPages;
    GeometryPoolStats deviceGeometryPool;
    uint64_t pendingGpuPolls=0,asyncTokenFallbacks=0;
    uint64_t idleFallbackFailures=0;
    RenderResourceHealth health=RenderResourceHealth::Ready;
    // A source/format/layout estimate of owned allocations, not a VRAM
    // budget or total GPU heap consumption.
    uint64_t estimatedKnownBytes() const noexcept {
        const auto max=std::numeric_limits<uint64_t>::max();
        auto add=[max](uint64_t lhs,uint64_t rhs) {
            return rhs>max-lhs?max:lhs+rhs;
        };
        return add(add(add(residentModelKnownBytes,retiredModelKnownBytes),
                       textureOwnerRgba8EquivalentBytes),
                   uint64_t(geometryPages.allocatedBytes));
    }
};

class RenderResourceOwnerGroup final {
public:
    // Register borrowed caches. Clearing or destroying the group does not
    // shut down owners; owners must outlive snapshot().
    void add(const ModelResourceCache& cache) {
        for(const auto* existing:owners_)
            if(existing==&cache)return;
        owners_.push_back(&cache);
    }
    void clear() noexcept {owners_.clear();}
    size_t size() const noexcept {return owners_.size();}

    void setEstimatedBudgetBytes(uint64_t bytes) noexcept {budgetBytes_=bytes;}
    uint64_t estimatedBudgetBytes() const noexcept {return budgetBytes_;}
    bool mayAdmitNewSources(const RenderResourceGroupStats& stats) const noexcept {
        return stats.health!=RenderResourceHealth::DeviceUnhealthy &&
               (budgetBytes_==0 || stats.estimatedKnownBytes()<budgetBytes_);
    }

    // One device-owner-thread snapshot; no GPU work is submitted. Models
    // pending retirement are included until their own fence completes.
    RenderResourceGroupStats snapshot(gfx::Device& device) const {
        RenderResourceGroupStats stats;
        stats.uniqueOwners=owners_.size();
        stats.health=device.gpuHealthy() ?
            RenderResourceHealth::Ready:
            RenderResourceHealth::DeviceUnhealthy;
        std::set<const Model*> visitedModels;
        std::set<const ModelTextureCache*> visitedTextureOwners;
        std::vector<const Model*> modelsForPool;
        const auto max=std::numeric_limits<uint64_t>::max();
        auto add=[max](uint64_t& dst,uint64_t amount) {
            dst=amount>max-dst?max:dst+amount;
        };
        for(const ModelResourceCache* owner:owners_) {
            if(!owner)continue;
            const auto retired=owner->retirementStats();
            if(retired.pendingResources &&
               stats.health==RenderResourceHealth::Ready)
                stats.health=RenderResourceHealth::PendingGpuRetirement;
            add(stats.pendingGpuPolls,retired.pendingPolls);
            add(stats.asyncTokenFallbacks,retired.tokenFailures);
            add(stats.idleFallbackFailures,retired.idleFallbackFailures);
            owner->forEachOwnedModel([&](const Model& model,bool isRetired) {
                if(!visitedModels.insert(&model).second)return;
                const auto estimate=estimateModelResident(model);
                if(isRetired) {
                    ++stats.retiredModels;
                    add(stats.retiredModelKnownBytes,estimate.knownBytes());
                } else {
                    ++stats.residentModels;
                    add(stats.residentModelKnownBytes,estimate.knownBytes());
                }
                if(model.geometryPoolPage)modelsForPool.push_back(&model);
            });
            const auto textures=owner->textures();
            if(textures && visitedTextureOwners.insert(textures.get()).second) {
                ++stats.uniqueTextureOwners;
                const auto estimate=textures->gpuOwnerEstimate();
                stats.uniqueTextures+=estimate.uniqueTextures;
                stats.compressedTextures+=estimate.compressedTextures;
                add(stats.textureOwnerRgba8EquivalentBytes,
                    estimate.rgba8BaseMipEquivalentBytes);
            }
        }
        stats.geometryPages=geometryPoolReferencedStats(
            device,std::span<const Model* const>(
                modelsForPool.data(),modelsForPool.size()));
        stats.deviceGeometryPool=geometryPoolStats(device);
        return stats;
    }
private:
    std::vector<const ModelResourceCache*> owners_;
    uint64_t budgetBytes_=0;
};

} // namespace aiverse::render
