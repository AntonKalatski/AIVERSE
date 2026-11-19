// Experimental GPU metadata contract, pure CPU checks (no graphics device).
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataGpuScene.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace aiverse::render::strata;

template <class T> T read(const std::vector<uint8_t>& v, size_t offset) {
    assert(offset <= v.size() && sizeof(T) <= v.size() - offset);
    T item;
    std::memcpy(&item, v.data() + offset, sizeof(T));
    return item;
}
static ModelPrimitiveAsset primitive(uint32_t mesh, uint32_t material) {
    ModelPrimitiveAsset p;
    p.mesh = mesh;
    p.primitive = 0;
    p.material = material;
    auto& a = p.geometry;
    a.indices = {0, 1, 2, 0, 1, 2, 2, 3, 0};
    a.roots = {0};
    a.nodes.resize(3);
    a.bounds.resize(3);
    for (uint32_t i = 0; i < 3; ++i) {
        auto& node = a.nodes[i];
        node.materialGroup = material;
        node.resident = true;
        node.indexCount = 3;
        node.firstIndex = i * 3;
        a.bounds[i] = {{float(i), 0, -1}, {float(i + 1), 1, 1}};
    }
    a.nodes[0].firstChild = 1;
    a.nodes[0].childCount = 2;
    a.nodes[0].geometricError = 0.5f;
    a.nodes[2].resident = false;
    return p;
}
int main() {
    ModelAsset asset;
    asset.primitives.push_back(primitive(2, 7));
    asset.primitives.push_back(primitive(5, 9));
    std::vector<uint8_t> packed;
    std::string error;
    assert(packGpuMetadata(asset, packed, &error));
    assert(error.empty() && !packed.empty());
    assert(packed.size() % 16 == 0);
    const GpuMetadataHeader header = read<GpuMetadataHeader>(packed, 0);
    assert(header.version == 1 && header.primitiveCount == 2);
    assert(header.nodeCount == 6 && header.rootCount == 2);
    assert(header.primitivesByteOffset == sizeof(GpuMetadataHeader));
    assert(header.nodesByteOffset == 32 + 2 * sizeof(GpuPrimitive));
    assert(header.rootsByteOffset == header.nodesByteOffset + 6 * sizeof(GpuNode));
    const GpuPrimitive first = read<GpuPrimitive>(packed, header.primitivesByteOffset);
    const GpuPrimitive second = read<GpuPrimitive>(packed,
        header.primitivesByteOffset + sizeof(GpuPrimitive));
    assert(first.mesh == 2 && first.materialGroup == 7);
    assert(first.nodeBase == 0 && first.nodeCount == 3);
    assert(second.mesh == 5 && second.materialGroup == 9);
    assert(second.nodeBase == 3 && second.nodeCount == 3);
    assert(first.rootBase == 0 && second.rootBase == 1);
    const uint32_t firstRoot = read<uint32_t>(packed, header.rootsByteOffset);
    const uint32_t secondRoot = read<uint32_t>(packed, header.rootsByteOffset + 4);
    assert(firstRoot == 0 && secondRoot == 3);
    const GpuNode parent = read<GpuNode>(packed, header.nodesByteOffset);
    const GpuNode child = read<GpuNode>(packed, header.nodesByteOffset + 5 * sizeof(GpuNode));
    assert(parent.firstChild == 1 && parent.childCount == 2);
    assert(child.firstChild == invalidNode && child.resident == 0);
    assert(child.sourceFirstIndex == 6 && child.sourceIndexCount == 3);
    assert(child.materialGroup == 9);
    assert(child.boundsMin[0] == 2.f && child.boundsMax[0] == 3.f);

    // Repeated primitive IDs and malformed local geometry must preserve the
    // previous payload. No GPU resource allocation occurs on pack failure.
    auto old = packed;
    asset.primitives.push_back(asset.primitives.front());
    assert(!packGpuMetadata(asset, packed, &error));
    assert(packed == old && !error.empty());
    asset.primitives.pop_back();
    asset.primitives[1].geometry.nodes[0].firstChild = 0;
    assert(!packGpuMetadata(asset, packed, &error));
    assert(packed == old);
    asset.primitives[1].geometry.nodes[0].firstChild = 1;
    asset.primitives[1].geometry.nodes[2].firstIndex = 0xfffffffeu;
    assert(!packGpuMetadata(asset, packed, &error));
    assert(packed == old);
    asset.primitives[1].geometry.nodes[2].firstIndex = 6;
    asset.primitives[0].geometry.bounds[2].max[0] =
        std::numeric_limits<float>::infinity();
    assert(!packGpuMetadata(asset, packed, &error));
    assert(packed == old);
    asset.primitives[0].geometry.bounds[2].max[0] = 3.f;
    assert(packGpuMetadata(asset, packed, &error));
    return 0;
}
