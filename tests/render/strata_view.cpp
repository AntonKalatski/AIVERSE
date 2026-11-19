// Strata view-plan reference checks; no GPU encoder required.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataView.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <string>
using namespace aiverse::render;
using namespace aiverse::render::strata;

int main() {
    Asset asset;
    // Parent is a coarser rendering of two adjacent child patches.
    asset.nodes.resize(3);
    asset.bounds.resize(3);
    asset.roots = {0};
    asset.indices = {0, 1, 2, 1, 2, 3, 0, 1, 2, 1, 2, 3};
    auto& p = asset.nodes[0];
    p.firstChild = 1;
    p.childCount = 2;
    p.firstIndex = 0;
    p.indexCount = 6;
    p.geometricError = 0.1f;
    p.materialGroup = 7;
    p.resident = true;
    for (uint32_t i = 1; i <= 2; ++i) {
        auto& n = asset.nodes[i];
        n.firstIndex = i == 1 ? 6 : 9;
        n.indexCount = 3;
        n.materialGroup = 7;
        n.resident = true;
    }
    asset.bounds[0] = {{-0.7f, -0.4f, 0.2f}, {0.7f, 0.4f, 0.7f}};
    asset.bounds[1] = {{-0.7f, -0.4f, 0.2f}, {0.0f, 0.4f, 0.7f}};
    asset.bounds[2] = {{0.0f, -0.4f, 0.2f}, {0.7f, 0.4f, 0.7f}};
    assert(validate(asset.nodes, asset.roots));

    ViewConfig config;
    config.width = 1280;
    config.height = 720;
    config.errorThresholdPixels = 1.f;
    ViewPlan result;
    std::string error;
    assert(planView(asset, config, result, &error));
    assert(error.empty());
    assert(result.ranges.size() == 2);
    assert(result.triangles == 2);
    assert(result.ranges[0].node == 1 && result.ranges[1].node == 2);
    assert(result.ranges[0].materialGroup == 7);
    assert(result.culledNodes == 0);

    // Small projected error selects the resident parent.
    config.objectToClip = mat4::scale({0.001f, 0.001f, 1.f});
    assert(planView(asset, config, result, &error));
    assert(result.ranges.size() == 1 && result.ranges[0].node == 0);
    assert(result.triangles == 2);

    // A separate shadow/reflection pass can project the same geometry
    // differently; do not reuse main camera selection implicitly.
    config.objectToClip = mat4::identity();
    config.maxSelectedNodes = 1;
    assert(planView(asset, config, result, &error));
    assert(result.ranges.size() == 1 && result.budgetExceeded);
    config.maxSelectedNodes = 65536;

    // A missing child forces the coarse resident parent, without holes.
    asset.nodes[2].resident = false;
    assert(planView(asset, config, result, &error));
    assert(result.ranges.size() == 1 && result.ranges.front().node == 0);
    assert(result.missingRefinements == 1);
    asset.nodes[2].resident = true;
    asset.nodes[0].resident = false;
    const size_t oldCount = result.ranges.size();
    assert(!planView(asset, config, result, &error));
    assert(result.ranges.size() == oldCount); // transactional error
    asset.nodes[0].resident = true;

    // Frustum clipping can omit all ranges without destroying staged assets.
    config.objectToClip = mat4::translate({8.f, 0.f, 0.f});
    assert(planView(asset, config, result, &error));
    assert(result.ranges.empty());
    assert(result.culledNodes == 2);
    config.frustumCull = false;
    assert(planView(asset, config, result, &error));
    assert(result.ranges.size() == 2);
    config.frustumCull = true;

    // Malformed input cannot publish partial plans.
    const auto previous = result;
    asset.nodes[1].firstIndex = uint32_t(asset.indices.size());
    assert(!planView(asset, config, result, &error));
    assert(result.ranges.size() == previous.ranges.size());
    asset.nodes[1].firstIndex = 6;
    asset.bounds[2].max[0] = std::numeric_limits<float>::quiet_NaN();
    assert(!planView(asset, config, result, &error));
    asset.bounds[2].max[0] = 0.7f;
    config.width = 0;
    assert(!planView(asset, config, result, &error));
    return 0;
}
