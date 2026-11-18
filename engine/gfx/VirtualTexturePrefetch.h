// AIVERSE genre-agnostic virtual texture visibility and prefetch planning.
// CPU-only. No camera geometry, ECS, filesystem or gfx::Device dependency:
// a caller projects visible surfaces to normalized virtual UV rectangles.
#pragma once

#include "VirtualTexturePages.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <vector>

namespace aiverse::gfx {

struct VirtualTextureVisibleRegion {
    // Normalized atlas UV rectangle, half-open [u0,u1) x [v0,v1).
    // Clamp to [0,1]; reject empty/non-finite regions.
    double u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    int layer = 0;
    int desiredMip = 0; // 0 = highest resolution
    int priority = 0;   // larger is preferred among pages at the same mip
};

struct VirtualTexturePrefetchConfig {
    int tilesX = 0, tilesY = 0;
    int mipLevels = 0, virtualLayers = 1;
    // Strict CPU and work bounds even for viewport-wide mip-zero requests.
    size_t maxRegions = 256;
    size_t maxCandidates = 4096;
    size_t maxRequestsPerFrame = 32;
    bool pinCoarsest = true;
};
struct VirtualTexturePrefetchRequest {
    VirtualPageKey key;
    bool pin = false;
    int priority = 0;
};
struct VirtualTexturePrefetchStats {
    size_t invalidRegions = 0;
    size_t uniqueCandidates = 0;
    size_t alreadyResident = 0;
    size_t deferredByBudget = 0;
    size_t produced = 0;
    bool candidateLimitReached = false;
};

// The caller owns the mapping from 3D visibility, camera, landscape, UI etc.
// to UV coverage. This planner has no FPS/RTS or engine-world dependencies.
// Request plans are stable for identical input, independent of hash order.
class VirtualTexturePrefetchPlanner {
public:
    using ExactResidentFn = std::function<bool(VirtualPageKey)>;

    bool initialize(VirtualTexturePrefetchConfig cfg) {
        reset();
        if (cfg.tilesX < 1 || cfg.tilesY < 1 ||
            cfg.tilesX > (1 << 20) || cfg.tilesY > (1 << 20) ||
            cfg.mipLevels < 1 || cfg.mipLevels > 30 ||
            cfg.virtualLayers < 1 || cfg.virtualLayers > 65536 ||
            cfg.maxRegions == 0 || cfg.maxRegions > 4096 ||
            cfg.maxCandidates == 0 || cfg.maxCandidates > (size_t(1) << 20) ||
            cfg.maxRequestsPerFrame == 0 ||
            cfg.maxRequestsPerFrame > cfg.maxCandidates)
            return false;
        const int necessaryMipLevels = [&] {
            int n = 1;
            for (int side = std::max(cfg.tilesX,cfg.tilesY); side > 1; side >>= 1)
                ++n;
            return n;
        }();
        if (cfg.mipLevels > necessaryMipLevels) return false;
        config_ = cfg;
        initialized_ = true;
        return true;
    }

    void reset() {
        initialized_ = false;
        config_ = {};
        stats_ = {};
    }

    // Plan work on the device/streaming owner thread. Use a callback that
    // checks EXACT tile residency, not the parent-fallback sampleability.
    std::vector<VirtualTexturePrefetchRequest> plan(
        const std::vector<VirtualTextureVisibleRegion>& regions,
        const ExactResidentFn& exactResident = {}) {
        stats_ = {};
        std::vector<VirtualTexturePrefetchRequest> output;
        if (!initialized_) return output;
        const size_t limitRegions = std::min(regions.size(), config_.maxRegions);
        stats_.invalidRegions += regions.size() - limitRegions;

        struct Region {
            VirtualTextureVisibleRegion view;
            size_t ordinal = 0;
        };
        std::vector<Region> valid;
        valid.reserve(limitRegions);
        for (size_t i = 0; i < limitRegions; ++i) {
            auto v = regions[i];
            if (!std::isfinite(v.u0) || !std::isfinite(v.u1) ||
                !std::isfinite(v.v0) || !std::isfinite(v.v1) ||
                v.layer < 0 || v.layer >= config_.virtualLayers ||
                v.desiredMip < 0 || v.desiredMip >= config_.mipLevels) {
                ++stats_.invalidRegions;
                continue;
            }
            v.u0 = std::clamp(v.u0,0.0,1.0);
            v.v0 = std::clamp(v.v0,0.0,1.0);
            v.u1 = std::clamp(v.u1,0.0,1.0);
            v.v1 = std::clamp(v.v1,0.0,1.0);
            if (v.u1 <= v.u0 || v.v1 <= v.v0) {
                ++stats_.invalidRegions;
                continue;
            }
            valid.push_back({v,i});
        }
        // Priority is stable within coarse mip order and independent of
        // overlapping view submission order for equal-priority regions.
        std::stable_sort(valid.begin(),valid.end(),[](const auto& a,const auto& b) {
            return a.view.priority > b.view.priority;
        });
        struct Candidate {
            int priority = 0;
            bool pin = false;
        };
        std::map<VirtualPageKey,Candidate> unique;
        bool full = false;
        for (int mip=config_.mipLevels-1; mip>=0 && !full; --mip) {
            for (const auto& region : valid) {
                const auto& v = region.view;
                if (mip < v.desiredMip) continue;
                const int scale = 1 << mip;
                const int xCount = (config_.tilesX + scale - 1) / scale;
                const int yCount = (config_.tilesY + scale - 1) / scale;
                const auto lower = [](double t, int n) {
                    return std::clamp(int(std::floor(t*double(n))),0,n-1);
                };
                const auto upper = [](double t, int n) {
                    return std::clamp(int(std::ceil(t*double(n)))-1,0,n-1);
                };
                const int x0=lower(v.u0,xCount), x1=upper(v.u1,xCount);
                const int y0=lower(v.v0,yCount), y1=upper(v.v1,yCount);
                for(int y=y0;y<=y1 && !full;++y) {
                    for(int x=x0;x<=x1;++x) {
                        const VirtualPageKey key{mip,x,y,v.layer};
                        auto found=unique.find(key);
                        if(found!=unique.end()) {
                            found->second.priority = std::max(found->second.priority,v.priority);
                            found->second.pin |= config_.pinCoarsest &&
                                                 mip == config_.mipLevels-1;
                            continue;
                        }
                        if(unique.size()==config_.maxCandidates) {
                            full=true; break;
                        }
                        unique.emplace(key,Candidate{
                            v.priority, config_.pinCoarsest && mip==config_.mipLevels-1});
                    }
                }
                if(full) break;
            }
        }
        stats_.candidateLimitReached=full;
        stats_.uniqueCandidates=unique.size();
        output.reserve(std::min(unique.size(),config_.maxRequestsPerFrame));
        std::vector<VirtualTexturePrefetchRequest> ordered;
        ordered.reserve(unique.size());
        for(const auto& [key,candidate]:unique)
            ordered.push_back({key,candidate.pin,candidate.priority});
        std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b) {
            if(a.key.mip!=b.key.mip) return a.key.mip>b.key.mip;
            if(a.priority!=b.priority) return a.priority>b.priority;
            if(a.key.layer!=b.key.layer) return a.key.layer<b.key.layer;
            if(a.key.y!=b.key.y) return a.key.y<b.key.y;
            return a.key.x<b.key.x;
        });
        for(const auto& candidate:ordered) {
            if (exactResident && exactResident(candidate.key)) {
                ++stats_.alreadyResident;
                continue;
            }
            if (output.size() == config_.maxRequestsPerFrame) {
                ++stats_.deferredByBudget;
                continue;
            }
            output.push_back(candidate);
        }
        stats_.produced=output.size();
        return output;
    }

    const VirtualTexturePrefetchStats& stats() const { return stats_; }
    const VirtualTexturePrefetchConfig& config() const { return config_; }
    bool ready() const { return initialized_; }

private:
    bool initialized_ = false;
    VirtualTexturePrefetchConfig config_;
    VirtualTexturePrefetchStats stats_;
};

} // namespace aiverse::gfx
