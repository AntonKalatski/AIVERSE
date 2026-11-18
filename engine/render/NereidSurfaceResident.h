// NEREID stage 9: opt-in water surface GPU residency for any native scene.
// Presentation only: caller explicitly owns the Device and releases before
// device teardown. Never called from deterministic ECS simulation.
#pragma once
#include "NereidWaterRenderer.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <cstddef>

namespace aiverse::render {

struct NereidSurfaceKey {
    uint64_t sourceRevision=0; // host increments after body/geometry edits or rollback
    lib::nereid::WaterSurfaceLod lod=lib::nereid::WaterSurfaceLod::Medium;
    float uvScale=0, foamWidth=0, oceanPatchRadius=0;
    uint32_t maxVertices=0;
    double patchX=0,patchZ=0; // relevant for infinite ocean only
    double originX=0,originZ=0; // local vertex basis, retained with GPU mesh
    double originY=0;
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

inline double nereidPatchAnchor64(double camera,double radius) {
    if(!std::isfinite(camera) || !std::isfinite(radius) || radius<=0)
        return std::numeric_limits<double>::quiet_NaN();
    return std::floor(camera/radius)*radius;
}

inline NereidSurfaceKey makeNereidSurfaceKey(uint64_t revision,bool infinite,
        lib::nereid::WaterSurfaceMeshOptions opts,
        lib::nereid::WaterSurfaceStyle style={}) {
    NereidSurfaceKey key;
    key.sourceRevision=revision;
    key.originX=opts.originX;
    key.originZ=opts.originZ;
    key.originY=opts.originY;
    key.infinite=infinite;
    key.lod=opts.lod;
    key.uvScale=opts.uvScale;
    key.foamWidth=opts.foamWidth;
    key.maxVertices=opts.maxVertices;
    key.style=style;
    if(infinite) {
        key.oceanPatchRadius=opts.oceanPatchRadius;
        key.patchX=nereidPatchAnchor64(
            opts.preciseCamera?opts.cameraX64:double(opts.cameraX),
            double(opts.oceanPatchRadius));
        key.patchZ=nereidPatchAnchor64(
            opts.preciseCamera?opts.cameraZ64:double(opts.cameraZ),
            double(opts.oceanPatchRadius));
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
        a.originX==b.originX && a.originZ==b.originZ &&
        a.originY==b.originY &&
        x.deepR==y.deepR && x.deepG==y.deepG && x.deepB==y.deepB &&
        x.foamR==y.foamR && x.foamG==y.foamG && x.foamB==y.foamB &&
        x.foamStrength==y.foamStrength &&
        x.refractionStrength==y.refractionStrength &&
        x.absorption==y.absorption &&
        x.shoreFoamDepth==y.shoreFoamDepth &&
        x.depthTint==y.depthTint && x.opacity==y.opacity &&
        x.specular==y.specular;
}

enum class NereidSurfaceUpdate { Unchanged, Uploaded, Failed };

struct NereidPreparedSurface {
    lib::nereid::WaterSurfaceMesh cpu;
    NereidSurfaceKey key{};
    // Exact bytes of the vertex/index arrays uploaded by MeshBuilder.
    // Driver allocation/stride overhead and temporary staging are excluded.
    size_t bufferBytes=0;
};
inline size_t nereidMeshBufferBytes(const lib::nereid::WaterSurfaceMesh& mesh) {
    if(mesh.vertices.size()>SIZE_MAX/sizeof(MeshVertex) ||
       mesh.indices.size()>SIZE_MAX/sizeof(uint32_t))return SIZE_MAX;
    const size_t vb=mesh.vertices.size()*sizeof(MeshVertex);
    const size_t ib=mesh.indices.size()*sizeof(uint32_t);
    return vb>SIZE_MAX-ib?SIZE_MAX:vb+ib;
}

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
    // Pure CPU work, safe outside the render/device update phase as long as
    // the copied body/geometry input remains immutable during preparation.
    static std::optional<NereidPreparedSurface> prepare(
            const lib::nereid::WaterBodyComponent& body,
            const lib::nereid::WaterGeometryComponent* geometry,
            uint64_t sourceRevision,
            lib::nereid::WaterSurfaceMeshOptions opts={},
            lib::nereid::WaterSurfaceStyle style={}) {
        if(!body.enabled || !validNereidWaterStyle(style))return std::nullopt;
        const bool infinite=(!geometry || !geometry->enabled) &&
            body.footprint==lib::nereid::WaterFootprint::Infinite;
        NereidPreparedSurface out;
        out.key=makeNereidSurfaceKey(sourceRevision,infinite,opts,style);
        if(infinite) {
            opts.preciseCamera=true;
            opts.cameraX64=out.key.patchX;
            opts.cameraZ64=out.key.patchZ;
        }
        out.cpu=lib::nereid::buildWaterSurfaceMesh(body,geometry,opts);
        if(out.cpu.empty())return std::nullopt;
        out.bufferBytes=nereidMeshBufferBytes(out.cpu);
        if(out.bufferBytes==SIZE_MAX)return std::nullopt;
        return out;
    }

    NereidSurfaceUpdate uploadPrepared(gfx::Device& device,
                                      const NereidPreparedSurface& prepared) {
        if(prepared.cpu.empty() || !validNereidWaterStyle(prepared.key.style))
            return NereidSurfaceUpdate::Failed;
        if(resident_ && sameNereidSurfaceKey(prepared.key,key_))
            return NereidSurfaceUpdate::Unchanged;
        Mesh replacement=uploadNereidWaterSurface(device,prepared.cpu,
                                                   prepared.key.style);
        if(!replacement.vb.valid() || !replacement.ib.valid() ||
           replacement.indexCount==0) {
            releaseNereidWaterSurface(device,replacement);
            return NereidSurfaceUpdate::Failed;
        }
        releaseNereidWaterSurface(device,mesh_);
        mesh_=replacement;
        key_=prepared.key;
        resident_=true;
        return NereidSurfaceUpdate::Uploaded;
    }

    // The device must remain valid until release(). Same device required for
    // successive updates; call release(oldDevice) before device recreation.
    NereidSurfaceUpdate update(gfx::Device& device,
            const lib::nereid::WaterBodyComponent& body,
            const lib::nereid::WaterGeometryComponent* geometry,
            uint64_t sourceRevision,
            lib::nereid::WaterSurfaceMeshOptions opts={},
            lib::nereid::WaterSurfaceStyle style={}) {
        auto prepared=prepare(body,geometry,sourceRevision,opts,style);
        return prepared?uploadPrepared(device,*prepared):NereidSurfaceUpdate::Failed;
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
