#include "engine/render/StrataBuilder.h"
#include <algorithm>
// Keep CTest assertions active under Release (-DNDEBUG) builds.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace aiverse::render::strata;
int main() {
    // A contiguous open surface with shared vertices: 32x32 quads.
    constexpr uint32_t side = 33;
    std::vector<float> positions;
    std::vector<uint32_t> source;
    for (uint32_t y = 0; y < side; ++y)
        for (uint32_t x = 0; x < side; ++x) {
            positions.push_back(float(x));
            positions.push_back(float(y));
            positions.push_back(0.15f * std::sin(float(x) * .4f));
        }
    for (uint32_t y = 0; y + 1 < side; ++y)
        for (uint32_t x = 0; x + 1 < side; ++x) {
            uint32_t a = y * side + x, b = a + 1, c = a + side, d = c + 1;
            source.insert(source.end(), {a, b, c, b, d, c});
        }
    BuildInput input;
    input.indices = source.data();
    input.indexCount = source.size();
    input.positions = positions.data();
    input.vertexCount = positions.size() / 3;
    input.positionStride = sizeof(float) * 3;
    input.materialGroup = 7;
    Asset asset;
    std::string error;
    assert(build(input, {}, asset, &error));
    assert(error.empty());
    assert(validate(asset.nodes, asset.roots));
    assert(asset.bounds.size() == asset.nodes.size());
    assert(!asset.meshlets.empty());
    size_t leafIndices = 0;
    for (const Meshlet& m : asset.meshlets) {
        assert(m.node < asset.nodes.size());
        assert(asset.nodes[m.node].childCount == 0);
        assert(asset.nodes[m.node].materialGroup == 7);
        assert(m.vertexCount <= 64 && m.triangleCount <= 124);
        assert(m.vertexCount >= 3 && m.triangleCount >= 1);
        assert(size_t(m.vertexOffset) + m.vertexCount <= asset.meshletVertices.size());
        assert(size_t(m.triangleOffset) + m.triangleCount * 3 <= asset.meshletTriangles.size());
        leafIndices += asset.nodes[m.node].indexCount;
        for (size_t i = 0; i < m.triangleCount * 3; ++i)
            assert(asset.meshletTriangles[m.triangleOffset + i] < m.vertexCount);
    }
    assert(leafIndices == source.size());
    for (const auto& node : asset.nodes)
        assert(size_t(node.firstIndex) + node.indexCount <= asset.indices.size());

    std::vector<float> pixels(asset.nodes.size(), 2.f);
    SelectionInput selectInput;
    selectInput.errorPixels = pixels.data();
    selectInput.errorCount = uint32_t(pixels.size());
    SelectionOutput result;
    assert(select(asset.nodes, asset.roots, selectInput, result));
    assert(result.nodes.size() == asset.meshlets.size());

    selectInput.thresholdPixels = 100;
    assert(select(asset.nodes, asset.roots, selectInput, result));
    assert(result.nodes.size() == asset.roots.size());
    selectInput.thresholdPixels = 1;
    selectInput.maxVisibleNodes = uint32_t(asset.roots.size());
    assert(select(asset.nodes, asset.roots, selectInput, result));
    assert(result.nodes.size() == asset.roots.size());

    if (asset.nodes[asset.roots.front()].childCount) {
        const uint32_t child = asset.nodes[asset.roots.front()].firstChild;
        asset.nodes[child].resident = false;
        selectInput.maxVisibleNodes = 65536;
        assert(select(asset.nodes, asset.roots, selectInput, result));
        assert(result.missingRefinements > 0);
        asset.nodes[child].resident = true;
    }

    auto broken = asset.nodes;
    broken[asset.roots.front()].firstChild = 0; // invalid back edge
    broken[asset.roots.front()].childCount = 1;
    assert(!validate(broken, asset.roots));
    pixels[asset.roots.front()] = std::numeric_limits<float>::infinity();
    assert(!select(asset.nodes, asset.roots, selectInput, result));

    Asset unchanged = asset;
    const uint32_t invalid[] = {0, 1, UINT32_MAX};
    input.indices = invalid;
    input.indexCount = 3;
    assert(!build(input, {}, asset, &error));
    assert(!error.empty() && asset.indices == unchanged.indices);
    return 0;
}
