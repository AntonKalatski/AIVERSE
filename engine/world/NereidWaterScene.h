// Universal NEREID water presentation layer for any WorldScene/native game.
// Not an ECS simulation, liquid solver, or legacy RTS water replacement.
#pragma once
#include "engine/render/NereidSurfaceResident.h"
#include "engine/world/WorldAssets.h"
#include <memory>
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace aiverse::world {

struct NereidSceneSurface {
    lib::nereid::WaterBodyComponent body{};
    // Optional geometry copied from the authoritative scene snapshot.
    lib::nereid::WaterGeometryComponent geometry{};
    bool useGeometry=false;
    lib::nereid::WaterSurfaceMeshOptions mesh{};
    lib::nereid::WaterSurfaceStyle style{};
};

class NereidWaterScene final {
public:
    static constexpr size_t kMaxSurfaces=64;
    // Snapshot/copy before calling update; no dangling ECS World references.
    // Each replacement bumps the revision and rebuilds changed GPU geometry.
    void setSurfaces(std::vector<NereidSceneSurface> surfaces) {
        if(surfaces.size()>kMaxSurfaces)
            throw std::invalid_argument("nereid.scene: too many water surfaces");
        surfaces_=std::move(surfaces);
        ++revision_;
        // Preserve prior GPU meshes until update commits the replacements.
    }
    void update(gfx::Device& device,const Vec3* observer=nullptr) {
        while(resident_.size()>surfaces_.size()) {
            resident_.back()->release(device);
            resident_.pop_back();
        }
        while(resident_.size()<surfaces_.size())
            resident_.push_back(std::make_unique<render::NereidSurfaceResident>());
        for(size_t i=0;i<surfaces_.size();++i) {
            auto opts=surfaces_[i].mesh;
            if(observer) {
                opts.cameraX=float(observer->x.toDouble());
                opts.cameraZ=float(observer->z.toDouble());
            }
            const auto* geometry=surfaces_[i].useGeometry?&surfaces_[i].geometry:nullptr;
            if(!surfaces_[i].body.enabled) {
                resident_[i]->release(device);
                continue;
            }
            // Geometry generation/upload can fail: preserve the last valid GPU surface.
            resident_[i]->update(device,surfaces_[i].body,geometry,revision_,
                                 opts,surfaces_[i].style);
        }
    }
    void draw(render::Renderer3D& renderer,Cell origin,int cellSize)const {
        const auto transform=render::mat4::translate({
            float(-double(origin.x)*double(cellSize)),0.f,
            float(-double(origin.z)*double(cellSize))});
        for(const auto& surface:resident_)surface->draw(renderer,transform);
    }
    void shutdown(gfx::Device& device) {
        for(auto& surface:resident_)surface->release(device);
        resident_.clear();
    }
    size_t visibleSurfaceCount() const {
        size_t count=0;
        for(const auto& surface:resident_)if(surface->resident())++count;
        return count;
    }
private:
    std::vector<NereidSceneSurface> surfaces_;
    std::vector<std::unique_ptr<render::NereidSurfaceResident>> resident_;
    uint64_t revision_=0;
};

} // namespace aiverse::world
