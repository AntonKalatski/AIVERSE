// TerraWeave trees and rocks on cube-sphere bodies (presentation only, nothing here touches the simulation).
// One site per surface cell: TerraWeave picks the plant from the sphere climate (engine/world/TerraWeaveSphere.h),
// Arbor and the rock generator make the shared prototypes; the renderer instances them.
// Body visual keys: "flora": {"density": 0..1 (0.22), "scale": world units per metre (0.06)} or "flora": false.
#pragma once
#include "GamePlanet.h"
#include "engine/render/Model.h"
#include <memory>
#include <vector>

namespace aiverse::game {

class PlanetFlora {
public:
    struct Instance {
        int body = 0;
        int proto = 0;
        render::float3 local;   // body-local position on the ground
        render::float3 up;      // surface direction (unit)
        float yaw = 0, scale = 1;
    };
    void shutdown(gfx::Device& dev);
    // Rebuilds the sites of every surface body; prototypes are made and uploaded once per device.
    void setWorld(gfx::Device& dev, const PlanetWorld& w, const std::vector<const PlanetSurface*>& surfaces);
    const std::vector<Instance>& instances() const { return instances_; }
    const render::Model* proto(int i) const { return i >= 0 && size_t(i) < protos_.size() ? protos_[size_t(i)].get() : nullptr; }

private:
    bool makePrototypes(gfx::Device& dev);
    std::vector<std::unique_ptr<render::Model>> protos_;
    std::vector<Instance> instances_;
};

} // namespace aiverse::game
