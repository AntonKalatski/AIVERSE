#pragma once
// Renderer-owned CPU MDI feedback policy; opt-in and presentation-only.
//
// This is not a GPU-time estimator. Measurements cover CPU preparation and
// indirect submission API time; final grouping/material/LOD checks remain
// authoritative. A periodic probe prevents permanent fallback after changes
// in workload, backend or pipeline warmup.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <functional>

namespace aiverse::render {

// A page is an allocation/lifetime scope, while VB/IB IDs distinguish
// reuploads or arena rebinding on that same page. The renderer additionally
// holds a weak_ptr to reject pointer-address reuse after streaming unload.
struct GeometryPoolPage;
struct CpuMdiPageKey {
    const GeometryPoolPage* page = nullptr;
    uint32_t vertexBuffer = 0;
    uint32_t indexBuffer = 0;
    // Renderer-local view/host namespace, explicitly chosen by callers.
    // Zero preserves historical shared feedback behavior for other hosts.
    uint64_t scope = 0;
    bool operator==(const CpuMdiPageKey&) const = default;
};
struct CpuMdiPageKeyHash {
    size_t operator()(const CpuMdiPageKey& key) const noexcept {
        size_t h = std::hash<const GeometryPoolPage*>{}(key.page);
        const auto mix = [&h](size_t next) {
            h ^= next + size_t(0x9e3779b9) + (h << 6) + (h >> 2);
        };
        mix(std::hash<uint32_t>{}(key.vertexBuffer));
        mix(std::hash<uint32_t>{}(key.indexBuffer));
        mix(std::hash<uint64_t>{}(key.scope));
        return h;
    }
};

struct CpuMdiAdaptiveFeedback {
    double avgUsPerCommand = 0.0;
    uint32_t samples = 0;
    uint64_t opportunities = 0;
    uint64_t successfulSubmissions = 0;
    uint64_t measuredAttempts = 0;

    bool permit(double budgetUsPerCommand, uint32_t probeEvery = 32u) {
        ++opportunities;
        if (samples < 8 || avgUsPerCommand <= budgetUsPerCommand) return true;
        // Deliberately retry a sufficiently large compatible group once in
        // a while. The caller still checks GPU support and unique commands.
        return probeEvery > 0u && opportunities % probeEvery == 0u;
    }

    void observe(double elapsedUs, uint32_t distinctCommands,
                 bool submitted) {
        if (!distinctCommands || !std::isfinite(elapsedUs) ||
            elapsedUs < 0.0) return;
        ++measuredAttempts;
        if (submitted) ++successfulSubmissions;
        // Ignore the first two samples for shader/pipeline warmup, but count
        // them toward warmup so the policy cannot get stuck uninitialized.
        if (measuredAttempts <= 2) return;
        const double normalized = std::clamp(
            elapsedUs / double(distinctCommands), 0.0, 2000.0);
        if (!samples) avgUsPerCommand = normalized;
        else avgUsPerCommand += 0.15 * (normalized - avgUsPerCommand);
        if (samples < 1000000u) ++samples;
    }
};

} // namespace aiverse::render
