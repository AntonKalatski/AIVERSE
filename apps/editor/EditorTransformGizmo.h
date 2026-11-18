// Shared screen-space hit-testing for editor Move gizmo handles.
// Geometry/persistence stays in genre-specific adapters; the interaction
// policy and pick mathematics are identical across RTS/FPS.
#pragma once
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aiverse::editor {

inline int pickMoveGizmoAxis(
    const render::RTSCamera& camera,
    float viewportWidth, float viewportHeight,
    float viewportOriginX, float viewportOriginY,
    float cursorX, float cursorY,
    render::float3 pivot, float extent,
    uint8_t enabledAxes = 0x7) {
    if (!(extent > 0) || !std::isfinite(extent)) return -1;
    render::float2 projected;
    if (!camera.worldToScreen(pivot, viewportWidth, viewportHeight, projected))
        return -1;
    const float ax = viewportOriginX + projected.x;
    const float ay = viewportOriginY + projected.y;
    int nearest = -1;
    float bestDist2 = 90.0f; // within ~9.5 screen points of the handle
    for (int axis = 0; axis < 3; ++axis) {
        if ((enabledAxes & uint8_t(1u << axis)) == 0) continue;
        render::float3 point = pivot;
        if (axis == 0) point.x += extent;
        if (axis == 1) point.y += extent;
        if (axis == 2) point.z += extent;
        render::float2 end;
        if (!camera.worldToScreen(point, viewportWidth, viewportHeight, end))
            continue;
        const float vx = viewportOriginX + end.x - ax;
        const float vy = viewportOriginY + end.y - ay;
        const float length2 = vx * vx + vy * vy;
        if (!std::isfinite(length2) || length2 < 64.0f) continue;
        const float t = std::clamp(((cursorX - ax) * vx +
                                    (cursorY - ay) * vy) / length2,
                                   0.25f, 1.1f);
        const float dx = cursorX - (ax + t * vx);
        const float dy = cursorY - (ay + t * vy);
        const float distance2 = dx * dx + dy * dy;
        if (distance2 < bestDist2) {
            bestDist2 = distance2;
            nearest = axis;
        }
    }
    return nearest;
}


inline bool pickRotateYawGizmoRing(
    const render::RTSCamera& camera,
    float viewportWidth, float viewportHeight,
    float viewportOriginX, float viewportOriginY,
    float cursorX, float cursorY,
    render::float3 pivot, float radius) {
    if (!(radius > 0) || !std::isfinite(radius)) return false;
    constexpr int segments = 64;
    constexpr float tau = 6.2831853071795864769f;
    render::float2 previous{};
    bool previousValid = false;
    for (int i = 0; i <= segments; ++i) {
        const float angle = tau * float(i) / float(segments);
        render::float2 current;
        const bool valid = camera.worldToScreen(
            pivot + render::float3{std::cos(angle) * radius, 0,
                                   std::sin(angle) * radius},
            viewportWidth, viewportHeight, current);
        if (valid && previousValid) {
            const float ax = previous.x + viewportOriginX;
            const float ay = previous.y + viewportOriginY;
            const float bx = current.x + viewportOriginX;
            const float by = current.y + viewportOriginY;
            const float vx = bx - ax, vy = by - ay;
            const float len2 = vx * vx + vy * vy;
            if (len2 > 1e-6f) {
                const float t = std::clamp(
                    ((cursorX - ax) * vx + (cursorY - ay) * vy) / len2,
                    0.0f, 1.0f);
                const float dx = cursorX - (ax + vx * t);
                const float dy = cursorY - (ay + vy * t);
                if (dx * dx + dy * dy < 100.0f) return true;
            }
        }
        previous = current;
        previousValid = valid;
    }
    return false;
}

} // namespace aiverse::editor
