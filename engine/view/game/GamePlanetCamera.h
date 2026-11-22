// Planet maps: the LENS sphere-orbit camera body (engine/render/CameraSphereOrbit.h) hosted on the planets of a map
// world. This adapter only knows which body is focused and feeds its centre, radius, ground height and the system
// (centre, extent) to the body each frame.
#pragma once
#include "../../render/CameraSphereOrbit.h"
#include "GamePlanet.h"

namespace aiverse::game {

class PlanetCamera : public render::CameraSphereOrbit {
public:
    // Start over `body` at the centre of face 2 (or at `dir`), looking along the face's -v ("north").
    void reset(const PlanetWorld& w, int body, const PlanetSurface* s, double seconds, render::float3 dir = {0, 0, 0}) {
        using namespace render;
        body_ = body;
        const float3 d = dot(dir, dir) > 0 ? dir : s ? s->dirFromFace(2, s->faceSize() * 0.5f, s->faceSize() * 0.5f) : float3{0, 1, 0};
        place(d, float3{0, 0, -1});
        distance = 24; pitch = 0.95f;
        update(w, s, seconds, 0);
    }
    int body() const { return body_; }
    // Focus another body (smooth transition of the frame).
    void focus(int body) { if (body != body_) { body_ = body; beginTransition(); } }
    float planetBlend() const { return sphereBlend(); }   // 0 surface .. 1 whole planet

    void update(const PlanetWorld& w, const PlanetSurface* s, double seconds, float dt) {
        using namespace render;
        if (body_ < 0 || body_ >= int(w.bodies.size())) body_ = w.start;
        if (body_ < 0 || body_ >= int(w.bodies.size())) return;
        const PlanetBody& b = w.bodies[size_t(body_)];
        int root = 0;   // system centre: the first body without an orbit
        for (size_t i = 0; i < w.bodies.size(); ++i) if (w.bodies[i].parent < 0) { root = int(i); break; }
        SphereOrbitInput in;
        in.center = w.center(body_, seconds);
        in.radius = b.radius;
        in.ground = s ? std::max(s->heightAt(dir()), b.seaLevel > -1e8f ? b.seaLevel : -1e9f) : 0.0f;
        in.systemCenter = w.center(root, seconds);
        in.systemExtent = b.radius * 4;
        for (size_t i = 0; i < w.bodies.size(); ++i)
            in.systemExtent = std::max(in.systemExtent, length(w.center(int(i), seconds) - in.systemCenter) + w.bodies[i].radius);
        CameraSphereOrbit::update(in, dt);
    }

private:
    int body_ = 0;
};

} // namespace aiverse::game
