#pragma once
// Strata experimental GPU metadata staging. This is NOT a geometry allocator
// or a GPU draw path: source VB/IB and all draw commands remain Renderer3D's.
#include "StrataModelAdapter.h"
#include "../gfx/Gfx.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace aiverse::render::strata {

// Version 1, explicit 16-byte-aligned storage layout, shared with future MSL/
// GLSL compute. All offsets are BYTE offsets within ONE metadata storage buffer.
struct alignas(16) GpuMetadataHeader {
    uint32_t version = 1;
    uint32_t primitiveCount = 0;
    uint32_t nodeCount = 0;
    uint32_t rootCount = 0;
    uint32_t primitivesByteOffset = 0;
    uint32_t nodesByteOffset = 0;
    uint32_t rootsByteOffset = 0;
    uint32_t reserved = 0;
};
struct alignas(16) GpuPrimitive {
    uint32_t mesh = 0;
    uint32_t primitive = 0;
    uint32_t materialGroup = 0;
    uint32_t nodeBase = 0;
    uint32_t nodeCount = 0;
    uint32_t rootBase = 0;
    uint32_t rootCount = 0;
    uint32_t reserved = 0;
};
struct alignas(16) GpuNode {
    float boundsMin[4] = {}; // xyz, padding
    float boundsMax[4] = {}; // xyz, padding
    uint32_t firstChild = invalidNode; // global metadata node ID
    uint32_t childCount = 0;
    uint32_t sourceFirstIndex = 0; // ASSET-local, NOT a GPU/Model IB offset
    uint32_t sourceIndexCount = 0;
    uint32_t materialGroup = 0;
    uint32_t resident = 0; // CPU asset snapshot only; NOT GPU index residency
    float geometricError = 0;
    uint32_t reserved = 0;
};
static_assert(sizeof(GpuMetadataHeader) == 32);
static_assert(sizeof(GpuPrimitive) == 32);
static_assert(sizeof(GpuNode) == 64);
static_assert(alignof(GpuNode) == 16);

struct GpuMetadataStats {
    uint32_t sources = 0;
    uint32_t declined = 0;
    uint32_t failed = 0;
    uint64_t bytes = 0; // GPU metadata allocations only (not geometry/VRAM total)
};

// Pure CPU packer, preserving output on failure. Rejects inconsistent source
// ranges, topology and integer offsets; never creates GPU resources.
// Payload: [Header(32), Primitive[...](32), Node[...](64), Root[u32]...],
// with trailing padding to 16 bytes. Root/node IDs are global within payload.
// ASSET-local sourceFirstIndex is never a drawable GPU index offset.
bool packGpuMetadata(const ModelAsset& source, std::vector<uint8_t>& out,
                     std::string* error = nullptr);

// Device-thread-only, explicit resource ownership. Stage after uploadModel()
// so source.Model can be checked against the EXISTING shared geometry pool.
// A metadata upload alone does NOT make Strata index streams GPU resident.
// Source buffers, model transforms, material grouping and all RHI draws remain
// owned by the existing engine; no new geometry pool is created here.
class GpuMetadataScene {
public:
    struct Limits {
        size_t maxSourceBytes = 1024 * 1024;
        size_t maxSceneBytes = 8 * 1024 * 1024;
    };
    GpuMetadataScene() = default;
    GpuMetadataScene(const GpuMetadataScene&) = delete;
    GpuMetadataScene& operator=(const GpuMetadataScene&) = delete;
    bool stage(gfx::Device& dev, const std::string& sourceKey,
               const Model& uploadedModel, const ModelAsset& asset);
    void shutdown(gfx::Device& dev); // BEFORE source Models and Device release
    gfx::BufferHandle find(const std::string& key,
                           const Model& uploadedModel) const;
    const GpuMetadataStats& stats() const { return stats_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    void setLimits(Limits value) { limits_ = value; }
private:
    struct Entry {
        gfx::BufferHandle metadata;
        const Model* source = nullptr; // borrowed; GeometryScene owns Model
        gfx::BufferHandle sourceVb, sourceIb;
        uint64_t sourceRevision = 0;
        size_t bytes = 0;
    };
    Limits limits_{};
    GpuMetadataStats stats_{};
    std::map<std::string, Entry> entries_;
    std::vector<std::string> warnings_;
};
} // namespace aiverse::render::strata
