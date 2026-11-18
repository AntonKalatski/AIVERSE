#include "apps/editor/EditorEntityAdapter.h"
#include "apps/editor/EditorTransformGizmo.h"
#include <cstdlib>
#include <cmath>

using namespace aiverse;
using namespace aiverse::editor;

static void require(bool ok) { if (!ok) std::abort(); }

int main() {
    MapDoc map;
    DocEntity sedan;
    sedan.id = "sedan";
    sedan.type = "sedan";
    sedan.x = 10.0f;
    sedan.z = 12.0f;
    sedan.extra.set("facing", Json::number(90.0));
    map.data.entities.push_back(sedan);
    const auto ref = DocumentEntityRef::mapEntity(0);

    Json component;
    require(readDocumentComponent(map, ref, DocumentComponentKind::Transform,
                                  component));
    require(component["rotationY"].isNumber());
    require(component["rotationY"].toDouble() == 90.0);
    require(!component["scale"].isArray()); // RTS lacks per-entity Scale

    Json change = Json::object();
    change.set("rotationY", Json::number(180.0));
    const uint64_t before = map.revision;
    require(writeDocumentComponent(map, ref, DocumentComponentKind::Transform,
                                   change));
    require(map.revision == before + 1);
    require(map.data.entities[0].extra["facing"].toDouble() == 180.0);
    require(map.data.entities[0].x == 10.0f && map.data.entities[0].z == 12.0f);

    // Objects lacking authored orientation must not invent it.
    map.data.entities[0].extra.erase("facing");
    require(!writeDocumentComponent(map, ref, DocumentComponentKind::Transform,
                                    change));

    render::RTSCamera camera;
    camera.target = {0, 0, 0};
    camera.distance = 18;
    camera.pitch = 0.9f;
    render::float2 cursor;
    constexpr float radius = 2.0f;
    require(camera.worldToScreen({radius, 0, 0}, 800, 600, cursor));
    require(pickRotateYawGizmoRing(camera, 800, 600, 0, 0,
                                   cursor.x, cursor.y,
                                   {0, 0, 0}, radius));
    require(!pickRotateYawGizmoRing(camera, 800, 600, 0, 0,
                                    -500, -500, {0, 0, 0}, radius));
}
