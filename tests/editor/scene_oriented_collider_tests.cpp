#include "apps/editor/EditorEntityAdapter.h"
#include <cstdlib>
#include <cmath>

using namespace aiverse;
using namespace aiverse::editor;

static void require(bool ok) { if (!ok) std::abort(); }

int main() {
    MapDoc map;
    Json collider = Json::object();
    collider.set("id", Json::number(int64_t(3)));
    Json centre = Json::array(), half = Json::array();
    for (double n : {10.0, 2.0, 20.0}) centre.push(Json::number(n));
    for (double n : {2.0, 1.0, 3.0}) half.push(Json::number(n));
    collider.set("center", centre);
    collider.set("half", half);
    collider.set("yaw", Json::number(0.25, 6));
    Json colliders = Json::array();
    colliders.push(collider);
    map.data.fps = Json::object();
    map.data.fps.set("colliders", colliders);

    const auto ref = DocumentEntityRef::fpsCollider(0);
    DocumentTransform pos;
    require(readDocumentTransform(map, ref, pos));
    require(pos.x == 10 && pos.y == 2 && pos.z == 20);
    const auto components = documentComponents(map, ref);
    require(components.size() == 2);
    require(std::string(components[1].id) == "OrientedBoxCollider");

    Json transform;
    require(readDocumentComponent(map, ref, DocumentComponentKind::Transform, transform));
    require(std::abs(transform["rotationY"].toDouble() - 90.0) < 0.01);
    require(transform["scale"][0].toDouble() == 4.0);
    require(transform["scale"][1].toDouble() == 2.0);
    require(transform["scale"][2].toDouble() == 6.0);

    const auto before = map.revision;
    require(translateDocumentEntities(map, {ref, ref}, {1, 0, -2}));
    require(map.revision == before + 1);
    require(readDocumentTransform(map, ref, pos));
    require(pos.x == 11 && pos.z == 18);

    Json changes = Json::object();
    changes.set("rotationY", Json::number(180.0));
    Json scales = Json::array();
    for (double n : {8.0, 4.0, 10.0}) scales.push(Json::number(n));
    changes.set("scale", scales);
    require(writeDocumentComponent(map, ref, DocumentComponentKind::Transform, changes));
    const Json& updated = map.data.fps["colliders"][0];
    require(std::abs(updated["yaw"].toDouble() - 0.5) < 0.00001);
    require(updated["half"][0].toDouble() == 4.0);
    require(updated["half"][1].toDouble() == 2.0);
    require(updated["half"][2].toDouble() == 5.0);
    require(updated["id"].integer() == 3);

    const uint64_t stable = map.revision;
    Json invalid = Json::object();
    Json bad = Json::array();
    for (double n : {0.0, -1.0, 1.0}) bad.push(Json::number(n));
    invalid.set("scale", bad);
    require(!writeDocumentComponent(map, ref, DocumentComponentKind::Transform, invalid));
    require(map.revision == stable);
}
