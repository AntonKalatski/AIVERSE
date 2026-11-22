// LENS camera body "sphere orbit": an orbit camera over the surface of a sphere (a planet), zooming out to the whole
// sphere and to a system of spheres, with smooth focus changes. Presentation only; one of the camera bodies of
// CameraBehavior.h (docs/camera-behaviors.md). The host supplies the sphere (centre, radius, ground height under the
// focus) and the system (centre, extent) each frame; the game planet camera (engine/view/game/GamePlanetCamera.h)
// is such a host.
//
// The renderer works in a "view world": world space moved and rotated so that the focus point on the surface is the
// origin and its local up is +Y (SphereViewFrame). Then the classic Camera, shadow fitting, light map and overlays
// work unchanged near the focus; far away the sphere simply curves down. Zoom regimes by camera distance d (from the
// focus point): surface (target on the ground, local frame) -> sphere (target slides to the centre) -> system (target
// slides to the system centre, frame turns to the orbital plane).
#pragma once
#include <algorithm>
#include <cmath>
#include "Math3D.h"

namespace aiverse::render {

struct SphereViewFrame {
    float3 origin;               // world point mapped to the view-world origin
    float3 right{1, 0, 0}, up{0, 1, 0}, back{0, 0, 1};   // world directions of view X / Y / Z
    float3 toView(float3 w) const { float3 d = w - origin; return {dot(d, right), dot(d, up), dot(d, back)}; }
    float3 dirToView(float3 d) const { return {dot(d, right), dot(d, up), dot(d, back)}; }
    float3 toWorld(float3 v) const { return origin + right * v.x + up * v.y + back * v.z; }
    float3 dirToWorld(float3 v) const { return right * v.x + up * v.y + back * v.z; }
    // world -> view world as a matrix (for chunk / model transforms)
    mat4 matrix() const {
        mat4 m;
        const float3 t = {-dot(origin, right), -dot(origin, up), -dot(origin, back)};
        m.at(0, 0) = right.x; m.at(0, 1) = right.y; m.at(0, 2) = right.z; m.at(0, 3) = t.x;
        m.at(1, 0) = up.x;    m.at(1, 1) = up.y;    m.at(1, 2) = up.z;    m.at(1, 3) = t.y;
        m.at(2, 0) = back.x;  m.at(2, 1) = back.y;  m.at(2, 2) = back.z;  m.at(2, 3) = t.z;
        return m;
    }
};

// The sphere and its system this frame.
struct SphereOrbitInput {
    float3 center{};              // sphere centre (world)
    float radius = 1;
    float ground = 0;             // height of the surface under the focus direction above the radius
    float3 systemCenter{};        // centre of the system of spheres
    float systemExtent = 4;       // radius of the system (at least a few sphere radii)
};

class CameraSphereOrbit {
public:
    float pitch = 0.95f;          // radians above the local horizon
    float distance = 24;          // from the focus point
    float minDistance = 6;
    float fovY = 0.75f;

    // Focus direction `dir` (from the centre), looking along `forwardHint` projected on the tangent plane.
    void place(float3 dir, float3 forwardHint) {
        dir_ = dot(dir, dir) > 0 ? normalize(dir) : float3{0, 1, 0};
        fwd_ = tangent(forwardHint);
        snap_ = true; init_ = false; transition_ = false;
    }
    float3 dir() const { return dir_; }
    float3 forward() const { return fwd_; }
    void setDir(float3 d) { transport(normalize(d)); }
    // A new sphere is focused: frame and target move there smoothly.
    void beginTransition() { transition_ = true; }
    bool inTransition() const { return transition_; }

    // Screen-space drag in pixels (viewport height h): the ground follows the cursor near the surface; at sphere zoom
    // the globe spins.
    void pan(float dx, float dy, float h) {
        if (h <= 0) return;
        const float worldPerPx = 2.0f * std::tan(fovY * 0.5f) * std::max(distance * (1.0f - sphereW_) + radius_ * sphereW_ * 1.2f, 1.0f) / h;
        move(-dx * worldPerPx, dy * worldPerPx / std::max(std::sin(pitch), 0.35f) * (1.0f - sphereW_) + dy * worldPerPx * sphereW_);
    }
    // Move the focus over the surface by (right, forward) world units at the surface radius. The forward vector is
    // parallel-transported: no singular poles or seams.
    void move(float right, float forward) {
        const float R = std::max(radius_, 1.0f);
        const float3 r = cross(fwd_, dir_);
        transport(normalize(dir_ + r * (right / R) + fwd_ * (forward / R)));
    }
    void zoom(float factor) { distance = std::clamp(distance * factor, minDistance, maxDistance_); }
    void rotate(float dHeading) {   // around the local up
        const float c = std::cos(dHeading), sn = std::sin(dHeading);
        fwd_ = normalize(fwd_ * c + cross(dir_, fwd_) * sn);
    }
    float maxDistance() const { return maxDistance_; }
    float sphereBlend() const { return sphereW_; }   // 0 surface .. 1 whole sphere
    float systemBlend() const { return systemW_; }   // 0 sphere .. 1 system

    // Per frame: smooth focus changes, build the Camera (view world) and the frame.
    void update(const SphereOrbitInput& in, float dt) {
        radius_ = in.radius;
        maxDistance_ = in.systemExtent * 2.6f;
        distance = std::clamp(distance, minDistance, maxDistance_);
        const float3 surf = in.center + dir_ * (in.radius + in.ground);
        auto smooth = [](float e0, float e1, float x) { float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3 - 2 * t); };
        sphereW_ = smooth(in.radius * 0.3f, in.radius * 2.2f, distance);
        systemW_ = smooth(in.radius * 5.0f, std::max(in.radius * 6.0f, in.systemExtent * 0.9f), distance);
        // desired frame: local (up = dir, back = -forward), turned into the orbital plane at system zoom
        float3 U = dir_, B = -fwd_;
        if (systemW_ > 0) {
            float3 gb = float3{-fwd_.x, 0, -fwd_.z};
            gb = dot(gb, gb) > 1e-6f ? normalize(gb) : float3{0, 0, 1};
            U = normalize(lerp(U, float3{0, 1, 0}, systemW_));
            B = normalize(lerp(B, gb, systemW_));
        }
        B = normalize(B - U * dot(B, U));
        const float3 targetW = lerp(lerp(surf, in.center, sphereW_), in.systemCenter, systemW_);
        // smoothing: frame and target follow exponentially (focus / regime changes); position snaps while panning on
        // the same sphere so the ground stays under the cursor
        const float k = snap_ || dt <= 0 ? 1.0f : 1.0f - std::exp(-dt * 7.0f);
        if (!init_) { cur_.origin = targetW; cur_.up = U; cur_.back = B; init_ = true; }
        const bool follow = !transition_;   // panning / zooming on one sphere: no lag
        cur_.origin = follow ? targetW : lerp(cur_.origin, targetW, k);
        cur_.up = follow ? U : normalize(lerp(cur_.up, U, k));
        cur_.back = follow ? B : normalize(lerp(cur_.back, B, k));
        if (transition_ && length(cur_.origin - targetW) < std::max(in.radius * 0.01f, 0.05f) && dot(cur_.up, U) > 0.9999f) transition_ = false;
        cur_.back = normalize(cur_.back - cur_.up * dot(cur_.back, cur_.up));
        cur_.right = cross(cur_.up, cur_.back);
        snap_ = false;
        frame_ = cur_;
        cam_.target = {0, 0, 0};
        cam_.distance = distance;
        cam_.pitch = std::clamp(pitch, 0.05f, 1.5607f);
        cam_.yaw = 0;
        cam_.fovY = fovY;
        cam_.ortho = false;
        cam_.minDistance = minDistance; cam_.maxDistance = maxDistance_;
        const float3 eyeW = frame_.toWorld(cam_.eye());
        const float alt = std::max(length(eyeW - in.center) - in.radius - std::max(0.0f, in.ground), 0.5f);
        cam_.zNear = std::clamp(alt * 0.08f, 0.25f, 200.0f);
        cam_.zFar = std::max(1000.0f, distance + in.systemExtent * 2.5f);
    }
    const Camera& camera() const { return cam_; }
    const SphereViewFrame& frame() const { return frame_; }

private:
    float3 tangent(float3 hint) const {
        float3 t = hint - dir_ * dot(hint, dir_);
        if (dot(t, t) < 1e-6f) { t = float3{1, 0, 0} - dir_ * dir_.x; }
        return normalize(t);
    }
    // move the focus to `n`, rotating the forward vector with the same rotation (parallel transport)
    void transport(float3 n) {
        const float3 axis = cross(dir_, n);
        const float sn = length(axis), cs = dot(dir_, n);
        if (sn > 1e-7f) {
            const float3 k = axis * (1.0f / sn);
            fwd_ = fwd_ * cs + cross(k, fwd_) * sn + k * (dot(k, fwd_) * (1.0f - cs));
        }
        dir_ = n;
        fwd_ = tangent(fwd_);
    }
    float3 dir_{0, 1, 0}, fwd_{0, 0, -1};
    float radius_ = 1, maxDistance_ = 1000, sphereW_ = 0, systemW_ = 0;
    bool snap_ = true, init_ = false, transition_ = false;
    SphereViewFrame cur_, frame_;
    Camera cam_;
};

} // namespace aiverse::render
