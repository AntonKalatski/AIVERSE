#include "apps/editor/EditorTransformGizmo.h"
#include <cstdlib>

using namespace aiverse::render;
using namespace aiverse::editor;

static void require(bool condition) {
    // Active in Release builds too; assertions are compiled out by NDEBUG.
    if (!condition) std::abort();
}

int main() {
    RTSCamera camera;
    camera.target = {0, 0, 0};
    camera.distance = 15.0f;
    camera.pitch = 0.9f;
    constexpr float width = 800.0f, height = 600.0f;
    constexpr float ox = 100.0f, oy = 40.0f;
    constexpr float extent = 2.5f;
    float2 start, end;
    require(camera.worldToScreen({0, 0, 0}, width, height, start));
    require(camera.worldToScreen({extent, 0, 0}, width, height, end));
    const float x = ox + start.x * 0.4f + end.x * 0.6f;
    const float y = oy + start.y * 0.4f + end.y * 0.6f;
    require(pickMoveGizmoAxis(camera, width, height, ox, oy, x, y,
                             {0, 0, 0}, extent, 0x1) == 0);
    // RTS planar maps cannot expose their unavailable Y handle.
    require(pickMoveGizmoAxis(camera, width, height, ox, oy, x, y,
                             {0, 0, 0}, extent, 0x0) == -1);
    require(pickMoveGizmoAxis(camera, width, height, ox, oy, -100, -100,
                             {0, 0, 0}, extent, 0x7) == -1);
    require(pickMoveGizmoAxis(camera, width, height, ox, oy, x, y,
                             {0, 0, 0}, 0, 0x7) == -1);
}
