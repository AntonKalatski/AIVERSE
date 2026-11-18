// Universal NEREID water presentation layer for any WorldScene/native game.
// Not an ECS simulation, liquid solver, or legacy RTS water replacement.
#pragma once
#include "engine/render/NereidSurfaceResident.h"
#include "engine/world/WorldAssets.h"
#include <memory>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <algorithm>

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
    void setMaxUploadsPerFrame(uint32_t count) {
        if(count==0 || count>16)
            throw std::invalid_argument("nereid.scene: upload count must be 1..16");
        maxUploadsPerFrame_=count;
    }
    uint32_t maxUploadsPerFrame()const{return maxUploadsPerFrame_;}
    // Round-robin scheduling caps rebuilds per frame, including failed attempts.
    // Retiring disabled/removed surfaces is immediate and does not consume budget.
    void update(gfx::Device& device,const Vec3* observer=nullptr) {
        while(resident_.size()>surfaces_.size()) {
            resident_.back()->release(device);
            resident_.pop_back();
        }
        while(resident_.size()<surfaces_.size())
            resident_.push_back(std::make_unique<render::NereidSurfaceResident>());
        if(surfaces_.empty()){nextUpload_=0;return;}
        // Remove disabled bodies immediately; they must not retain stale images.
        for(size_t i=0;i<surfaces_.size();++i)
            if(!surfaces_[i].body.enabled)resident_[i]->release(device);
        uint32_t attempts=0;
        const size_t count=surfaces_.size();
        for(size_t scanned=0;scanned<count && attempts<maxUploadsPerFrame_;++scanned) {
            const size_t i=(nextUpload_+scanned)%count;
            if(!surfaces_[i].body.enabled)continue;
            auto opts=surfaces_[i].mesh;
            if(observer) {
                opts.cameraX=float(observer->x.toDouble());
                opts.cameraZ=float(observer->z.toDouble());
            }
            const auto* geometry=surfaces_[i].useGeometry?&surfaces_[i].geometry:nullptr;
            if(!resident_[i]->needsUpdate(surfaces_[i].body,geometry,
                                          revision_,opts,surfaces_[i].style))
                continue;
            ++attempts;
            resident_[i]->update(device,surfaces_[i].body,geometry,revision_,
                                 opts,surfaces_[i].style);
            nextUpload_=(i+1)%count;
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
        nextUpload_=0;
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
    uint32_t maxUploadsPerFrame_=2;
    size_t nextUpload_=0;
};

} // namespace aiverse::world
