// Cross-genre picking regression tests for the shared ray/AABB implementation.
#include "apps/editor/SceneRenderable.h"
#include <cstdlib>
#include <limits>
#include <vector>

using namespace aiverse::editor;
using namespace aiverse::render;

static void require(bool condition) {
    // Active in Release builds too; assertions are compiled out by NDEBUG.
    if (!condition) std::abort();
}

int main() {
    SceneRenderable first;
    first.entity = 1;
    first.pickable = true;
    first.boundsMin = {-1, -1, 2};
    first.boundsMax = { 1,  1, 4};

    SceneRenderable second;
    second.entity = 2;
    second.pickable = true;
    second.boundsMin = {-1, -1, 6};
    second.boundsMax = { 1,  1, 8};

    const std::vector<SceneRenderable> scene{second, first};
    // Closest surface, not insertion order.
    require(pickSceneRenderable(scene, {0, 0, 0}, {0, 0, 1}) == 1);
    require(pickSceneRenderable(scene, {0, 0, 10}, {0, 0, -1}) == 0);
    // Rays parallel to an AABB slab must not create false hits.
    require(pickSceneRenderable(scene, {3, 0, 0}, {0, 0, 1}) == -1);
    require(pickSceneRenderable(scene, {0, 3, 0}, {0, 0, 1}) == -1);
    require(pickSceneRenderable(scene, {0, 0, 0}, {0, 0, 0}) == -1);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    require(pickSceneRenderable(scene, {nan, 0, 0}, {0, 0, 1}) == -1);
    require(pickSceneRenderable(scene, {0, 0, 0}, {nan, 0, 1}) == -1);

    SceneRenderable hidden = first;
    hidden.pickable = false;
    require(pickSceneRenderable({hidden}, {0, 0, 0}, {0, 0, 1}) == -1);
    SceneRenderable malformed = first;
    malformed.boundsMin.x = 5;
    require(pickSceneRenderable({malformed}, {0, 0, 0}, {0, 0, 1}) == -1);
}
