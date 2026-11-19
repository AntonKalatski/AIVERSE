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

namespace aiverse::render {

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
