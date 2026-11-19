// NEREID: immutable water presentation snapshots with stable per-body GPU residency.
// No gameplay mutation, native genre dependencies, or legacy RTS ownership.
#pragma once
#include "engine/render/NereidSurfaceResident.h"
#include "engine/world/WorldAssets.h"
#include "engine/world/NereidRipples.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <future>
#include <chrono>
#include <utility>
#include <stdexcept>
#include <vector>

namespace aiverse::world {

struct NereidSceneSurface {
    // Stable nonzero id supplied by host; 0 means legacy index-based id.
    // Live ECS adapters must supply generation-safe entity id, never a pool index.
    uint64_t id=0;
    lib::nereid::WaterBodyComponent body{};
    lib::nereid::WaterGeometryComponent geometry{};
    bool useGeometry=false;
    lib::nereid::WaterSurfaceMeshOptions mesh{};
    lib::nereid::WaterSurfaceStyle style{};
    // Optional camera-distance LOD; fixed thresholds are presentation-only.
    bool autoLod=false;
    float mediumDistance=120.f, farDistance=400.f;
    float lodHysteresis=12.f;
};

// Strictly presentation-only equality; no hashing of padding or ECS mutation.
inline bool sameNereidSceneSurface(const NereidSceneSurface& a,
                                   const NereidSceneSurface& b) {
    const auto& x=a.body;const auto& y=b.body;
    const auto& m=a.mesh;const auto& n=b.mesh;
    const auto& u=a.style;const auto& v=b.style;
    if(x.kind!=y.kind || x.footprint!=y.footprint ||
       x.center.x!=y.center.x || x.center.y!=y.center.y ||
       x.center.z!=y.center.z || x.surfaceY!=y.surfaceY ||
       x.bottomY!=y.bottomY || x.halfExtents.x!=y.halfExtents.x ||
       x.halfExtents.y!=y.halfExtents.y || x.radius!=y.radius ||
       x.flow.x!=y.flow.x || x.flow.y!=y.flow.y ||
       x.flow.z!=y.flow.z || x.priority!=y.priority ||
       x.enabled!=y.enabled || a.useGeometry!=b.useGeometry ||
       m.lod!=n.lod || m.uvScale!=n.uvScale ||
       m.foamWidth!=n.foamWidth || m.oceanPatchRadius!=n.oceanPatchRadius ||
       m.maxVertices!=n.maxVertices ||
       m.rippleTileGrid!=n.rippleTileGrid ||
       a.autoLod!=b.autoLod ||
       a.mediumDistance!=b.mediumDistance || a.farDistance!=b.farDistance ||
       a.lodHysteresis!=b.lodHysteresis ||
       u.deepR!=v.deepR || u.deepG!=v.deepG || u.deepB!=v.deepB ||
       u.foamR!=v.foamR || u.foamG!=v.foamG || u.foamB!=v.foamB ||
       u.foamStrength!=v.foamStrength ||
       u.refractionStrength!=v.refractionStrength ||
       u.absorption!=v.absorption || u.shoreFoamDepth!=v.shoreFoamDepth ||
       u.depthTint!=v.depthTint ||
       u.underwaterDistortion!=v.underwaterDistortion ||
       u.underwaterScattering!=v.underwaterScattering ||
       u.opacity!=v.opacity ||
       u.specular!=v.specular)return false;
    // Camera coordinates are intentionally excluded: resident ocean patches
    // separately snap their camera anchor to a stable grid.
    if(!a.useGeometry)return true;
    if(a.geometry.enabled!=b.geometry.enabled ||
       a.geometry.kind!=b.geometry.kind ||
       a.geometry.points.size()!=b.geometry.points.size())return false;
    for(size_t i=0;i<a.geometry.points.size();++i) {
        const auto& p=a.geometry.points[i];const auto& q=b.geometry.points[i];
        if(p.x!=q.x || p.z!=q.z || p.halfWidth!=q.halfWidth)return false;
    }
    return true;
}

struct NereidSceneStats {
    size_t visible=0, resident=0, pending=0;
    uint32_t uploadsAttempted=0;
    size_t estimatedBytesAdmitted=0; // legacy metric name: now exact VB+IB array bytes
    size_t preparedBytes=0, budgetDeferred=0;
    size_t asyncPreparing=0, asyncDiscarded=0, stagingReservedBytes=0;
};

class NereidWaterScene final {
    struct Slot {
        NereidSceneSurface source;
        uint64_t revision=0;
        std::unique_ptr<render::NereidSurfaceResident> gpu=
            std::make_unique<render::NereidSurfaceResident>();
        bool visible=true;
        std::optional<render::NereidPreparedSurface> prepared;
        std::optional<lib::nereid::WaterSurfaceLod> selectedLod;
    };
public:
    static constexpr size_t kMaxSurfaces=64;
    // A single bounded worker avoids concurrent builds of large geometry.
    // Explicit opt-in; synchronous behavior is the default.
    void setAsyncPreparation(bool enabled) {asyncPrepare_=enabled;}
    void setMaxWaterStagingBytes(size_t bytes) {
        if(bytes<1024 || bytes>64u*1024u*1024u)
            throw std::invalid_argument("nereid.scene: invalid CPU staging budget");
        maxStagingBytes_=bytes;
    }
    bool asyncPreparation()const{return asyncPrepare_;}
    size_t maxWaterStagingBytes()const{return maxStagingBytes_;}
    // No GPU operations inside snapshot submission. Removed resources wait
    // in retired_ until the next device-thread update/shutdown.
    void setSurfaces(std::vector<NereidSceneSurface> incoming) {
        if(incoming.size()>kMaxSurfaces)
            throw std::invalid_argument("nereid.scene: too many water surfaces");
        std::map<uint64_t,size_t> seen;
        for(size_t i=0;i<incoming.size();++i) {
            if(incoming[i].id==0)
                incoming[i].id=(uint64_t(1)<<63)|(uint64_t(i)+1);
            if(!seen.emplace(incoming[i].id,i).second)
                throw std::invalid_argument("nereid.scene: duplicate water id");
        }
        retired_.reserve(retired_.size()+slots_.size());
        // Preallocate before moving GPU owners: invalid input never touches
        // the current snapshot, and slots keep identity across reorder.
        std::vector<Slot> next;next.reserve(incoming.size());
        std::map<uint64_t,size_t> oldIds;
        for(size_t i=0;i<slots_.size();++i)oldIds.emplace(slots_[i].source.id,i);
        for(auto& source:incoming) {
            Slot slot;
            const auto found=oldIds.find(source.id);
            if(found!=oldIds.end()) {
                auto& old=slots_[found->second];
                slot.gpu=std::move(old.gpu);
                slot.revision=old.revision;
                slot.visible=old.visible;
                slot.prepared=std::move(old.prepared);
                slot.selectedLod=old.selectedLod;
                if(!sameNereidSceneSurface(source,old.source)) {
                    ++slot.revision;
                    slot.prepared.reset();
                    slot.selectedLod.reset();
                }
            }else slot.revision=++nextRevision_;
            slot.source=std::move(source);
            next.push_back(std::move(slot));
        }
        for(auto& old:slots_)if(old.gpu)retired_.push_back(std::move(old.gpu));
        slots_=std::move(next);
        nextUpload_=0;
    }
    void setMaxUploadsPerFrame(uint32_t count) {
        if(count<1 || count>16)
            throw std::invalid_argument("nereid.scene: upload count must be 1..16");
        maxUploadsPerFrame_=count;
    }
    void setMaxEstimatedUploadBytes(size_t bytes) {
        if(bytes<1024 || bytes>64u*1024u*1024u)
            throw std::invalid_argument("nereid.scene: estimated upload budget out of range");
        maxEstimatedUploadBytes_=bytes;
    }
    // 0 disables distance culling. Uses horizontal source bounds (not
    // a camera frustum); infinite oceans are always eligible.
    void setVisibilityRange(float metres) {
        if(!std::isfinite(metres)||metres<0 || metres>1000000)
            throw std::invalid_argument("nereid.scene: invalid visibility range");
        visibilityRange_=metres;
    }
    uint32_t maxUploadsPerFrame()const{return maxUploadsPerFrame_;}
    size_t surfaceCount()const{return slots_.size();}
    // Renderer-side flow lookup by the packed WaterBody Entity stored in a
    // presentation ripple event; no ECS pool access or mutable ownership.
    Vec3 currentForInteractionWater(uint64_t packedWaterId)const {
        if(packedWaterId==UINT64_MAX)return {};
        const uint64_t stableId=packedWaterId+1;
        for(const auto& slot:slots_)if(slot.source.id==stableId &&
                                         slot.source.body.enabled)
            return slot.source.body.flow;
        return {};
    }
    // Read-only presentation visitor; callers must not retain references
    // beyond the next setSurfaces transaction.
    template<class Fn> void forEachSource(Fn&& fn)const {
        for(const auto& slot:slots_)fn(slot.source);
    }
    uint64_t surfaceRevision(uint64_t id)const {
        for(const auto& slot:slots_)if(slot.source.id==id)return slot.revision;
        return 0;
    }
    size_t maxEstimatedUploadBytes()const{return maxEstimatedUploadBytes_;}
    const NereidSceneStats& stats()const{return stats_;}

    // Conservative upper bound based on maxVertices; the existing mesh
    // builder uses 32-bit indices and 3 vertices per triangle at most.
    // This caps scheduling demand, NOT actual graphics driver allocations.
    static size_t estimatedUploadBytes(const NereidSceneSurface& s) {
        const size_t count=s.mesh.maxVertices;
        return count*(sizeof(render::MeshVertex)+6*sizeof(uint32_t));
    }
    static bool visibleFrom(const NereidSceneSurface& s,const Vec3* observer,
                            float distance) {
        if(!s.body.enabled)return false;
        if(!observer || distance<=0)return true;
        if((!s.useGeometry || !s.geometry.enabled) &&
           s.body.footprint==lib::nereid::WaterFootprint::Infinite)
            return true;
        double minX=0,maxX=0,minZ=0,maxZ=0;
        if(s.useGeometry && s.geometry.enabled) {
            if(s.geometry.points.empty())return false;
            minX=maxX=s.geometry.points.front().x.toDouble();
            minZ=maxZ=s.geometry.points.front().z.toDouble();
            for(const auto& p:s.geometry.points) {
                const double width=s.geometry.kind==lib::nereid::WaterGeometryKind::RiverSpline
                    ?p.halfWidth.toDouble():0.0;
                const double x=p.x.toDouble(),z=p.z.toDouble();
                minX=std::min(minX,x-width);maxX=std::max(maxX,x+width);
                minZ=std::min(minZ,z-width);maxZ=std::max(maxZ,z+width);
            }
        }else{
            const double x=s.body.center.x.toDouble(),z=s.body.center.z.toDouble();
            const double hx=s.body.footprint==lib::nereid::WaterFootprint::Box
                ?s.body.halfExtents.x.toDouble():s.body.radius.toDouble();
            const double hz=s.body.footprint==lib::nereid::WaterFootprint::Box
                ?s.body.halfExtents.y.toDouble():s.body.radius.toDouble();
            minX=x-hx;maxX=x+hx;minZ=z-hz;maxZ=z+hz;
        }
        const double x=observer->x.toDouble(),z=observer->z.toDouble();
        const double dx=std::max({minX-x,0.0,x-maxX});
        const double dz=std::max({minZ-z,0.0,z-maxZ});
        return dx*dx+dz*dz<=double(distance)*double(distance);
    }
    static lib::nereid::WaterSurfaceLod selectedLod(
            const NereidSceneSurface& s,const Vec3* observer,
            std::optional<lib::nereid::WaterSurfaceLod> previous=std::nullopt) {
        using L=lib::nereid::WaterSurfaceLod;
        if(!s.autoLod || !observer)return s.mesh.lod;
        if(!s.useGeometry && s.body.footprint==lib::nereid::WaterFootprint::Infinite)
            return s.mesh.lod;
        // For authored polygon/rivers the body center is not necessarily
        // meaningful; use the geometry's X/Z bounding rectangle instead.
        double cx=s.body.center.x.toDouble(),cz=s.body.center.z.toDouble();
        if(s.useGeometry && s.geometry.enabled && !s.geometry.points.empty()) {
            double loX=s.geometry.points.front().x.toDouble();
            double hiX=loX;
            double loZ=s.geometry.points.front().z.toDouble();
            double hiZ=loZ;
            for(const auto& p:s.geometry.points) {
                const double width=s.geometry.kind==lib::nereid::WaterGeometryKind::RiverSpline
                    ?p.halfWidth.toDouble():0.0;
                loX=std::min(loX,p.x.toDouble()-width);
                hiX=std::max(hiX,p.x.toDouble()+width);
                loZ=std::min(loZ,p.z.toDouble()-width);
                hiZ=std::max(hiZ,p.z.toDouble()+width);
            }
            cx=std::clamp(observer->x.toDouble(),loX,hiX);
            cz=std::clamp(observer->z.toDouble(),loZ,hiZ);
        }
        const double dx=cx-observer->x.toDouble();
        const double dz=cz-observer->z.toDouble();
        const double sq=dx*dx+dz*dz;
        // Schmitt trigger around each boundary avoids uploads while the
        // camera jitters within a narrow range. From Far, require crossing
        // the lower threshold; from Near, require crossing the upper one.
        const double h=s.lodHysteresis;
        const double mid=double(s.mediumDistance),far=double(s.farDistance);
        const double d=std::sqrt(sq);
        if(previous==L::Near) {
            if(d<=mid+h)return L::Near;
            return d>far+h?L::Far:L::Medium;
        }
        if(previous==L::Far) {
            if(d>=far-h)return L::Far;
            return d<mid-h?L::Near:L::Medium;
        }
        if(previous==L::Medium) {
            if(d<mid-h)return L::Near;
            if(d>far+h)return L::Far;
            return L::Medium;
        }
        if(d>far)return L::Far;
        if(d>mid)return L::Medium;
        return L::Near;
    }
    struct PendingCpuJob {
        uint64_t id=0;
        render::NereidSurfaceKey key{};
        size_t reservation=0;
        std::future<std::optional<render::NereidPreparedSurface>> future;
    };
    // Poll without waiting. A result is adopted only when identity + full
    // presentation key still match; rollback/reorder/LOD changes discard it.
    size_t retainedPreparedBytes()const {
        size_t total=0;
        for(const auto& slot:slots_)
            if(slot.prepared) {
                const auto bytes=slot.prepared->bufferBytes;
                if(bytes>SIZE_MAX-total)return SIZE_MAX;
                total+=bytes;
            }
        return total;
    }
    void pollPreparation() {
        if(!pendingJob_)return;
        if(pendingJob_->future.wait_for(std::chrono::seconds(0))!=
           std::future_status::ready)return;
        std::optional<render::NereidPreparedSurface> prepared;
        try{prepared=pendingJob_->future.get();}
        catch(...) {prepared.reset();}
        if(prepared) {
            bool accepted=false;
            for(auto& slot:slots_)if(slot.source.id==pendingJob_->id) {
                if(slot.revision==prepared->key.sourceRevision &&
                   render::sameNereidSurfaceKey(prepared->key,pendingJob_->key) &&
                   prepared->bufferBytes<=maxStagingBytes_ &&
                   retainedPreparedBytes()<=maxStagingBytes_-prepared->bufferBytes) {
                    slot.prepared=std::move(prepared);
                    accepted=true;
                }
                break;
            }
            if(!accepted)++asyncDiscarded_;
        }else ++asyncDiscarded_;
        pendingJob_.reset();
    }
    static size_t maxCpuReservation(const NereidSceneSurface& source) {
        // Safe cap under NereidWaterSurfaceMesh maxVertices and its
        // subdivided triangle topology, including retained CPU geometry.
        const size_t n=source.mesh.maxVertices;
        return n*(sizeof(lib::nereid::WaterSurfaceVertex)+
                  6*sizeof(uint32_t));
    }
    static void applyWaterMeshBasis(
            lib::nereid::WaterSurfaceMeshOptions& opts,
            const NereidSceneSurface& source) {
        opts.originY=source.body.surfaceY.toDouble();
        // Use a stable near-body world anchor, not an absolute position cast
        // to float. Read-only presentation geometry, no gameplay changes.
        if(source.useGeometry && source.geometry.enabled &&
           !source.geometry.points.empty()) {
            opts.originX=source.geometry.points.front().x.toDouble();
            opts.originZ=source.geometry.points.front().z.toDouble();
        }else if(source.body.footprint==lib::nereid::WaterFootprint::Infinite) {
            opts.originX=render::nereidPatchAnchor64(
                opts.preciseCamera?opts.cameraX64:double(opts.cameraX),
                double(opts.oceanPatchRadius));
            opts.originZ=render::nereidPatchAnchor64(
                opts.preciseCamera?opts.cameraZ64:double(opts.cameraZ),
                double(opts.oceanPatchRadius));
        }else{
            opts.originX=source.body.center.x.toDouble();
            opts.originZ=source.body.center.z.toDouble();
        }
    }
    void update(gfx::Device& device,const Vec3* observer=nullptr) {
        observer_=observer?std::optional<Vec3>(*observer):std::nullopt;
        for(auto& old:retired_)old->release(device);
        retired_.clear();
        pollPreparation();
        stats_={};
        stats_.asyncDiscarded=asyncDiscarded_;
        stats_.asyncPreparing=pendingJob_?1:0;
        stats_.stagingReservedBytes=pendingJob_?pendingJob_->reservation:0;
        if(slots_.empty()){nextUpload_=0;return;}
        for(auto& slot:slots_) {
            slot.visible=visibleFrom(slot.source,observer,visibilityRange_);
            if(slot.visible)++stats_.visible;
            if(!slot.source.body.enabled) {
                slot.gpu->release(device);
                slot.prepared.reset();
            }
        }
        const size_t start=nextUpload_%slots_.size();
        for(size_t scanned=0;scanned<slots_.size();++scanned) {
            const size_t i=(start+scanned)%slots_.size();
            auto& slot=slots_[i];
            if(!slot.visible)continue;
            auto opts=slot.source.mesh;
            opts.lod=selectedLod(slot.source,observer,slot.selectedLod);
            slot.selectedLod=opts.lod;
            if(observer) {
                opts.cameraX64=observer->x.toDouble();
                opts.cameraZ64=observer->z.toDouble();
                opts.preciseCamera=true;
                opts.cameraX=float(opts.cameraX64);
                opts.cameraZ=float(opts.cameraZ64);
            }
            applyWaterMeshBasis(opts,slot.source);
            const auto* geometry=slot.source.useGeometry?&slot.source.geometry:nullptr;
            if(!slot.gpu->needsUpdate(slot.source.body,geometry,
                                      slot.revision,opts,slot.source.style)) {
                slot.prepared.reset();continue;
            }
            ++stats_.pending;
            // A changed camera-relative patch/LOD invalidates staged CPU data.
            const bool infinite=(!geometry || !geometry->enabled) &&
                slot.source.body.footprint==lib::nereid::WaterFootprint::Infinite;
            const auto expected=render::makeNereidSurfaceKey(
                slot.revision,infinite,opts,slot.source.style);
            if(slot.prepared &&
               !render::sameNereidSurfaceKey(slot.prepared->key,expected))
                slot.prepared.reset();
            if(stats_.uploadsAttempted>=maxUploadsPerFrame_)continue;
            if(!slot.prepared) {
#ifndef __EMSCRIPTEN__
                if(asyncPrepare_) {
                    if(pendingJob_)continue;
                    const size_t reservation=maxCpuReservation(slot.source);
                    const size_t retained=retainedPreparedBytes();
                    if(reservation>maxStagingBytes_ ||
                       retained>maxStagingBytes_-reservation) {
                        ++stats_.budgetDeferred;continue;
                    }
                    // Capture immutable copies; worker must never touch
                    // live ECS/WorldScene/GPU pointers.
                    const auto bodyCopy=slot.source.body;
                    const auto geometryCopy=slot.source.geometry;
                    const bool useGeometry=slot.source.useGeometry;
                    const auto styleCopy=slot.source.style;
                    const auto revision=slot.revision;
                    auto future=std::async(std::launch::async,
                        [bodyCopy,geometryCopy,useGeometry,revision,opts,styleCopy] {
                            return render::NereidSurfaceResident::prepare(
                                bodyCopy,useGeometry?&geometryCopy:nullptr,
                                revision,opts,styleCopy);
                        });
                    pendingJob_.emplace(PendingCpuJob{
                        slot.source.id,expected,reservation,std::move(future)});
                    stats_.asyncPreparing=1;
                    stats_.stagingReservedBytes=reservation;
                    nextUpload_=(i+1)%slots_.size();
                    continue;
                }
#endif
                const size_t retained=retainedPreparedBytes();
                const size_t reservation=maxCpuReservation(slot.source);
                if(reservation>maxStagingBytes_ ||
                   retained>maxStagingBytes_-reservation) {
                    ++stats_.budgetDeferred;continue;
                }
                slot.prepared=render::NereidSurfaceResident::prepare(
                    slot.source.body,geometry,slot.revision,opts,slot.source.style);
                if(slot.prepared && slot.prepared->bufferBytes>
                    maxStagingBytes_-retained)slot.prepared.reset();
                if(!slot.prepared) {
                    ++stats_.uploadsAttempted; // failed geometry still counts as an attempt
                    nextUpload_=(i+1)%slots_.size();
                    continue;
                }
            }
            const size_t bytes=slot.prepared->bufferBytes;
            stats_.preparedBytes+=bytes;
            // Strict per-frame byte budget. Oversize surfaces remain deferred:
            // caller must raise the budget or choose a lower LOD.
            if(bytes>maxEstimatedUploadBytes_ ||
               bytes>maxEstimatedUploadBytes_-stats_.estimatedBytesAdmitted) {
                ++stats_.budgetDeferred;
                continue;
            }
            ++stats_.uploadsAttempted;
            stats_.estimatedBytesAdmitted+=bytes;
            const auto result=slot.gpu->uploadPrepared(device,*slot.prepared);
            if(result!=render::NereidSurfaceUpdate::Failed)
                slot.prepared.reset();
            nextUpload_=(i+1)%slots_.size();
        }
        for(const auto& slot:slots_)if(slot.gpu->resident())++stats_.resident;
        stats_.stagingReservedBytes=retainedPreparedBytes()+
            (pendingJob_?pendingJob_->reservation:0);
    }
    void draw(render::Renderer3D& renderer,Cell origin,int cellSize,
              const NereidRippleField* ripples=nullptr)const {
        const double worldOriginX=double(origin.x)*double(cellSize);
        const double worldOriginZ=double(origin.z)*double(cellSize);
        const double worldOriginY=double(origin.y)*double(cellSize);
        for(const auto& slot:slots_) {
            if(!slot.visible || !slot.gpu->resident())continue;
            const auto& basis=slot.gpu->key();
            const auto transform=render::mat4::translate({
                float(basis.originX-worldOriginX),
                float(basis.originY-worldOriginY),
                float(basis.originZ-worldOriginZ)});
            if(!slot.gpu->tiles().empty()) {
                // One shared VB/IB, multiple disjoint index ranges; each tile
                // can see up to 16 waves near its own portion of the ocean.
                for(const auto& tile:slot.gpu->tiles()) {
                    std::array<render::float4,16> waves{};
                    if(ripples)waves=ripples->shaderRipplesForTile(
                        slot.source.id,worldOriginX,worldOriginZ,
                        tile.minX,tile.maxX,tile.minZ,tile.maxZ);
                    slot.gpu->drawTile(renderer,transform,tile,waves);
                }
            }else{
                std::array<render::float4,16> waves{};
                if(ripples) {
                    const double targetX=observer_?observer_->x.toDouble():basis.originX;
                    const double targetZ=observer_?observer_->z.toDouble():basis.originZ;
                    waves=ripples->shaderRipples(
                        slot.source.id,worldOriginX,worldOriginZ,targetX,targetZ);
                }
                slot.gpu->draw(renderer,transform,waves);
            }
        }
    }
    void shutdown(gfx::Device& device) {
        // Shutdown is an explicit synchronization barrier; never abandon a
        // worker that owns copied inputs before device/scene destruction.
        if(pendingJob_) {
            pendingJob_->future.wait();
            pendingJob_.reset();
        }
        for(auto& slot:slots_)slot.gpu->release(device);
        for(auto& slot:retired_)slot->release(device);
        retired_.clear();
        stats_={};
        nextUpload_=0;
    }
    size_t visibleSurfaceCount()const {
        size_t count=0;
        for(const auto& slot:slots_)
            if(slot.visible && slot.gpu->resident())++count;
        return count;
    }
private:
    std::vector<Slot> slots_;
    std::vector<std::unique_ptr<render::NereidSurfaceResident>> retired_;
    uint64_t nextRevision_=0;
    uint32_t maxUploadsPerFrame_=2;
    size_t maxEstimatedUploadBytes_=8u*1024u*1024u;
    float visibilityRange_=0;
    size_t nextUpload_=0;
    NereidSceneStats stats_{};
    bool asyncPrepare_=false;
    size_t maxStagingBytes_=16u*1024u*1024u;
    std::optional<PendingCpuJob> pendingJob_;
    size_t asyncDiscarded_=0;
    std::optional<Vec3> observer_;
};
} // namespace aiverse::world
