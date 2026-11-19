#pragma once
// GPU-page compatibility for opt-in static CPU MDI batching.
//
// Each model keeps its own primitive, transform, culling, LOD selection and
// source material. Only *after* view selection can compatible rigid primitives
// from different models sharing a GPU geometry arena join one MDI submission.
// Material/pipeline equivalence is checked separately by Renderer3D.
#include "Model.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

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
    uint32_t vertexBuffer = 0;
    uint32_t indexBuffer = 0;
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
        combine(std::hash<uint32_t>{}(key.vertexBuffer));
        combine(std::hash<uint32_t>{}(key.indexBuffer));
        return value;
    }
};

struct StaticCrossModelPreflightGroup {
    const Model* firstModel = nullptr;
    bool mixedModels = false;
    bool ready = false; // evaluated once after all visible candidates are known
    size_t items = 0;
    // The supported backend command threshold is at most 32 (Metal env
    // tuning), or 4 on other platforms. Fixed storage avoids one heap
    // allocation per material/page group in dense scenes.
    std::array<const Primitive*, 32> distinctPrimitives{};
    size_t distinctCount = 0;

    void observe(const Model* model, const Primitive* primitive, size_t threshold) {
        if (!model || !primitive || !threshold) return;
        if (!firstModel) firstModel = model;
        else if (model != firstModel) mixedModels = true;
        ++items;
        const size_t limit = std::min(threshold, distinctPrimitives.size());
        if (distinctCount < limit &&
            std::find(distinctPrimitives.begin(),
                      distinctPrimitives.begin() + distinctCount,
                      primitive) == distinctPrimitives.begin() + distinctCount)
            distinctPrimitives[distinctCount++] = primitive;
    }

    bool opportunity(size_t threshold, size_t maxItemsPerSubmission) const {
        return mixedModels && items >= threshold &&
            maxItemsPerSubmission >= threshold &&
            distinctCount >= threshold;
    }
};

} // namespace aiverse::render
