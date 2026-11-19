// Shared GPU geometry pages. Rigid models use the same vertex/index buffer
// without baking node transforms, allowing indexed Multi-Draw across assets.
#pragma once
#include "Model.h"
#include "GeometryPoolEligibility.h"

namespace aiverse::render {
struct GeometryPoolStats {
    size_t pages = 0;
    size_t allocatedBytes = 0;
    size_t occupiedBytes = 0;
    size_t liveModels = 0;
};
GeometryPoolStats geometryPoolStats(gfx::Device& dev);
// Read-only prediction before GPU upload: distinguishes global/backend
// restrictions from per-asset geometry/LOD rejection without allocations.
enum class GeometryPoolAdmissionReason : uint8_t {
    Eligible = 0, Disabled, UnsupportedBackend, EmptyGeometry,
    DedicatedAsset, UnsafeGeometry, Oversized
};
struct GeometryPoolAdmission {
    GeometryPoolAdmissionReason reason = GeometryPoolAdmissionReason::Eligible;
    RigidGeometryPoolBlock modelBlock = RigidGeometryPoolBlock::None;
    bool eligible() const { return reason == GeometryPoolAdmissionReason::Eligible; }
};
GeometryPoolAdmission geometryPoolAdmission(gfx::Device& dev, const Model& model);
const char* geometryPoolAdmissionName(GeometryPoolAdmissionReason reason);
bool globalGeometryPoolingEnabled();
void setGlobalGeometryPoolingEnabled(bool enabled);
// Opt-in for immutable rigid LOD meshes; changes affect subsequent uploads,
// NOT models already uploaded to dedicated or shared GPU buffers.
bool rigidLodGeometryPoolingEnabled();
void setRigidLodGeometryPoolingEnabled(bool enabled);
bool uploadModelToGeometryPool(gfx::Device& dev, Model& m);
void releaseModelGeometryPool(gfx::Device& dev, Model& m);
}
