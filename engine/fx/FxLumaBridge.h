// Particle lights ("light" module) -> LumaWeave sources with stable identity.
// A host that shades direct light with Luma publishes these instead of adding
// them to the legacy LightSystem: never both (that would double the light).
#pragma once
#include "ParticleSystem.h"
#include "../render/LumaWeaveLights.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace aiverse::fx {

// Keys are (sourceNamespace << 32) | particle uid: stable while the particle
// lives, unique within one system and above every key < 2^32 the host uses
// for its own (static) lights. `sourceNamespace` >= 1 picks the system.
// Colours are converted to linear like the legacy-light path of LumaHost.
// `sourceRadius` > 0 turns each light into a small sphere source.
inline void appendLumaFxLights(std::vector<render::LumaLight>& out,
                               const std::vector<FxPointLight>& lights,
                               uint32_t sourceNamespace, float sourceRadius = 0) {
    if (!sourceNamespace) return;
    const auto linear = [](float v) {
        v = std::clamp(v, 0.f, 10000.f);
        return v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f);
    };
    for (const auto& l : lights) {
        render::LumaLight light;
        light.key = (uint64_t(sourceNamespace) << 32) | uint64_t(l.uid);
        light.position = l.pos;
        light.color = {linear(l.color.x), linear(l.color.y), linear(l.color.z)};
        light.radius = std::clamp(l.radius, .05f, 1000.f);
        light.intensity = std::clamp(l.intensity, 0.f, 10000.f);
        light.castsShadows = false;   // short-lived; shadow rays are the costly part
        if (sourceRadius > 0 && sourceRadius < light.radius) {
            light.kind = render::LumaLightKind::Sphere;
            light.sourceRadius = sourceRadius;
        }
        if (render::lumaValidLight(light)) out.push_back(light);
    }
}

} // namespace aiverse::fx
