// NEREID stage 9: opt-in water surface GPU residency for any native scene.
// Presentation only: caller explicitly owns the Device and releases before
// device teardown. Never called from deterministic ECS simulation.
#pragma once
#include "NereidWaterRenderer.h"
#include <cmath>
#include <cstdint>
#include <limits>

namespace aiverse::render {

struct NereidSurfaceKey {
    uint64_t sourceRevision=0; // host increments after body/geometry edits or rollback
    lib::nereid::WaterSurfaceLod lod=lib::nereid::WaterSurfaceLod::Medium;
    float uvScale=0, foamWidth=0, oceanPatchRadius=0;
    uint32_t maxVertices=0;
    float patchX=0,patchZ=0; // relevant for infinite ocean only
    lib::nereid::WaterSurfaceStyle style{};
    bool infinite=false;
};

// Center a camera-relative ocean patch on a stable lattice: moving the
// camera within a cell does not re-upload an identical water mesh.
inline float nereidPatchAnchor(float camera,float radius) {
    if(!std::isfinite(camera) || !std::isfinite(radius) || radius<=0)
        return std::numeric_limits<float>::quiet_NaN();
    const double step=double(radius); // patch is 2*radius wide
    const double snapped=std::floor(double(camera)/step)*step;
    if(snapped < -double(std::numeric_limits<float>::max()) ||
       snapped > double(std::numeric_limits<float>::max()))
        return std::numeric_limits<float>::quiet_NaN();
    return float(snapped);
}

inline NereidSurfaceKey makeNereidSurfaceKey(uint64_t revision,bool infinite,
        lib::nereid::WaterSurfaceMeshOptions opts,
        lib::nereid::WaterSurfaceStyle style={}) {
    NereidSurfaceKey key;
    key.sourceRevision=revision;
    key.infinite=infinite;
    key.lod=opts.lod;
    key.uvScale=opts.uvScale;
    key.foamWidth=opts.foamWidth;
    key.maxVertices=opts.maxVertices;
    key.style=style;
    if(infinite) {
        key.oceanPatchRadius=opts.oceanPatchRadius;
        key.patchX=nereidPatchAnchor(opts.cameraX,opts.oceanPatchRadius);
        key.patchZ=nereidPatchAnchor(opts.cameraZ,opts.oceanPatchRadius);
    }
    return key;
}

inline bool sameNereidSurfaceKey(const NereidSurfaceKey& a,
                                const NereidSurfaceKey& b) {
    const auto& x=a.style;const auto& y=b.style;
    return a.sourceRevision==b.sourceRevision &&
        a.infinite==b.infinite && a.lod==b.lod &&
        a.uvScale==b.uvScale && a.foamWidth==b.foamWidth &&
        a.maxVertices==b.maxVertices &&
        a.oceanPatchRadius==b.oceanPatchRadius &&
        a.patchX==b.patchX && a.patchZ==b.patchZ &&
        x.deepR==y.deepR && x.deepG==y.deepG && x.deepB==y.deepB &&
        x.foamR==y.foamR && x.foamG==y.foamG && x.foamB==y.foamB &&
        x.foamStrength==y.foamStrength && x.opacity==y.opacity &&
        x.specular==y.specular;
}

enum class NereidSurfaceUpdate { Unchanged, Uploaded, Failed };

class NereidSurfaceResident final {
public:
    NereidSurfaceResident()=default;
    NereidSurfaceResident(const NereidSurfaceResident&)=delete;
    NereidSurfaceResident& operator=(const NereidSurfaceResident&)=delete;
    NereidSurfaceResident(NereidSurfaceResident&&)=delete;
    NereidSurfaceResident& operator=(NereidSurfaceResident&&)=delete;

    bool needsUpdate(const lib::nereid::WaterBodyComponent& body,
            const lib::nereid::WaterGeometryComponent* geometry,
            uint64_t sourceRevision,
            lib::nereid::WaterSurfaceMeshOptions opts={},
            lib::nereid::WaterSurfaceStyle style={}) const {
        if(!body.enabled)return false;
        const bool infinite=(!geometry || !geometry->enabled) &&
            body.footprint==lib::nereid::WaterFootprint::Infinite;
        const auto key=makeNereidSurfaceKey(sourceRevision,infinite,opts,style);
        return !resident_ || !sameNereidSurfaceKey(key,key_);
    }
    // The device must remain valid until release(). Same device required for
    // successive updates; call release(oldDevice) before device recreation.
    NereidSurfaceUpdate update(gfx::Device& device,
            const lib::nereid::WaterBodyComponent& body,
            const lib::nereid::WaterGeometryComponent* geometry,
            uint64_t sourceRevision,
            lib::nereid::WaterSurfaceMeshOptions opts={},
            lib::nereid::WaterSurfaceStyle style={}) {
        if(!body.enabled || !validNereidWaterStyle(style))
            return NereidSurfaceUpdate::Failed;
        // Enabled custom geometry replaces the footprint; infinite means
        // the canonical body footprint with no enabled custom geometry.
        const bool infinite=(!geometry || !geometry->enabled) &&
            body.footprint==lib::nereid::WaterFootprint::Infinite;
        const auto key=makeNereidSurfaceKey(sourceRevision,infinite,opts,style);
        if(resident_ && sameNereidSurfaceKey(key,key_))
            return NereidSurfaceUpdate::Unchanged;
        if(infinite) {
            opts.cameraX=key.patchX;
            opts.cameraZ=key.patchZ;
        }
        const auto cpu=lib::nereid::buildWaterSurfaceMesh(body,geometry,opts);
        if(cpu.empty())return NereidSurfaceUpdate::Failed;
        Mesh replacement=uploadNereidWaterSurface(device,cpu,style);
        if(!replacement.vb.valid() || !replacement.ib.valid() ||
           replacement.indexCount==0) {
            releaseNereidWaterSurface(device,replacement);
            return NereidSurfaceUpdate::Failed;
        }
        // Only discard the previous surface AFTER a complete upload.
        releaseNereidWaterSurface(device,mesh_);
        mesh_=replacement;
        key_=key;
        resident_=true;
        return NereidSurfaceUpdate::Uploaded;
    }

    void draw(Renderer3D& renderer,const mat4& transform=mat4::identity()) const {
        if(resident_)drawNereidWaterSurface(renderer,mesh_,key_.style,transform);
    }
    void release(gfx::Device& device) {
        releaseNereidWaterSurface(device,mesh_);
        resident_=false;
        key_={};
    }
    bool resident() const {return resident_;}
    uint32_t indexCount() const {return resident_?mesh_.indexCount:0;}
    const NereidSurfaceKey& key() const {return key_;}

private:
    Mesh mesh_{};
    NereidSurfaceKey key_{};
    bool resident_=false;
};

} // namespace aiverse::render
