#pragma once
// GPU-page compatibility for opt-in static CPU MDI batching.
//
// Each model keeps its own primitive, transform, culling, LOD selection and
// source material. Only *after* view selection can compatible rigid primitives
// from different models sharing a GPU geometry arena join one MDI submission.
// Material/pipeline equivalence is checked separately by Renderer3D.
#include "Model.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace aiverse::render {

inline bool staticCrossModelPageCompatible(const Model& a, const Model& b) {
    return &a != &b &&
        a.geometryPoolPage && b.geometryPoolPage &&
        a.geometryPoolPage == b.geometryPoolPage &&
        a.vb.valid() && a.ib.valid() &&
        a.vb == b.vb && a.ib == b.ib &&
        !a.geometry && !b.geometry;
}

// Preflight is a *necessary-condition* filter only. The renderer still
// performs full shader/material/buffer checks and enforces the backend's
// indexed-command threshold before every actual MDI submission.
// A group combines one geometry page, exact material-state group and
// draw flags. No camera/view/LOD data is cached across frames.
struct StaticCrossModelPreflightKey {
    const GeometryPoolPage* page = nullptr;
    uint32_t materialGroup = 0;
    uint32_t flags = 0;
    constexpr bool operator==(const StaticCrossModelPreflightKey&) const = default;
};

struct StaticCrossModelPreflightKeyHash {
    size_t operator()(const StaticCrossModelPreflightKey& key) const noexcept {
        size_t value = std::hash<const GeometryPoolPage*>{}(key.page);
        const auto combine = [&value](size_t next) {
            value ^= next + size_t(0x9e3779b9) + (value << 6) + (value >> 2);
        };
        combine(std::hash<uint32_t>{}(key.materialGroup));
        combine(std::hash<uint32_t>{}(key.flags));
        return value;
    }
};

struct StaticCrossModelPreflightGroup {
    const Model* firstModel = nullptr;
    bool mixedModels = false;
    bool ready = false; // evaluated once after all visible candidates are known
    size_t items = 0;
    // Keep only the first threshold distinct primitives, not every instance
    // in a city. The backend still does exact adjacent primitive-run counting.
    std::vector<const Primitive*> distinctPrimitives;

    void observe(const Model* model, const Primitive* primitive, size_t threshold) {
        if (!model || !primitive || !threshold) return;
        if (!firstModel) firstModel = model;
        else if (model != firstModel) mixedModels = true;
        ++items;
        if (distinctPrimitives.size() < threshold &&
            std::find(distinctPrimitives.begin(), distinctPrimitives.end(),
                      primitive) == distinctPrimitives.end())
            distinctPrimitives.push_back(primitive);
    }

    bool opportunity(size_t threshold, size_t maxItemsPerSubmission) const {
        return mixedModels && items >= threshold &&
            maxItemsPerSubmission >= threshold &&
            distinctPrimitives.size() >= threshold;
    }
};

} // namespace aiverse::render
