// Shared GPU geometry pages. Rigid models use the same vertex/index buffer
// without baking node transforms, allowing indexed Multi-Draw across assets.
#pragma once
#include "Model.h"
#include "GeometryPoolEligibility.h"
#include <span>

namespace aiverse::render {
struct GeometryPoolStats {
    size_t pages = 0;
    size_t allocatedBytes = 0;
    size_t occupiedBytes = 0;
    size_t liveModels = 0;
    size_t retiredPairs = 0;       // bounded, quarantined until GPU completes
    size_t reusablePairs = 0;      // subset with completed GPU serial
    uint64_t recycledUploads = 0; // successful upload into an old pair
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
// The device's shared pages are counted ONCE by page identity across the
// entire supplied model set, which may combine independent host caches.
// Do not add these values per-host: the same page can span many owners.
// Only pages registered for this device are counted. This is allocator
// capacity/occupied accounting, NOT exact driver VRAM usage.
struct GeometryPoolReferencedStats {
    size_t uniquePages=0,referenceModels=0;
    size_t allocatedBytes=0,occupiedBytes=0;
};
GeometryPoolReferencedStats geometryPoolReferencedStats(
    gfx::Device& dev,std::span<const Model* const> models);
bool globalGeometryPoolingEnabled();
void setGlobalGeometryPoolingEnabled(bool enabled);
// Opt-in for immutable rigid LOD meshes; changes affect subsequent uploads,
// NOT models already uploaded to dedicated or shared GPU buffers.
bool rigidLodGeometryPoolingEnabled();
void setRigidLodGeometryPoolingEnabled(bool enabled);
bool uploadModelToGeometryPool(gfx::Device& dev, Model& m);
void releaseModelGeometryPool(gfx::Device& dev, Model& m);
}
