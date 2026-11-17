// Regression checks for the RTS/FPS authored-object source adapter.
#include "apps/editor/EditorSceneNodes.h"
#include <cstdlib>
#include <cmath>
#include <vector>

using namespace aiverse;
using namespace aiverse::editor;

static void require(bool condition) {
    // Active in Release builds too; assertions are compiled out by NDEBUG.
    if (!condition) std::abort();
}

int main() {
    MapDoc map;
    DocEntity sedan;
    sedan.id = "sedan_01";
    sedan.type = "sedan";
    sedan.x = 32.0f;
    sedan.z = 64.0f;
    map.data.entities.push_back(sedan);

    const auto rts = enumerateDocumentSceneNodes(map, false);
    require(rts.size() == 1);
    require(rts[0].ref.source == DocumentEntityRef::Source::MapEntity);
    require(rts[0].label == "sedan #sedan_01");
    require(rts[0].hasPosition);
    require(rts[0].position.x == 32.0 && rts[0].position.z == 64.0);
    require(translateDocumentEntity(map, rts[0].ref, {1.0, 0.0, -2.0}));
    require(map.data.entities[0].x == 33.0f && map.data.entities[0].z == 62.0f);
    // RTS map coordinates are planar; do not silently drop a Y movement.
    require(!translateDocumentEntity(map, rts[0].ref, {0.0, 1.0, 0.0}));

    Json lower = Json::array();
    lower.push(Json::number(0.0)); lower.push(Json::number(1.0)); lower.push(Json::number(2.0));
    Json upper = Json::array();
    upper.push(Json::number(4.0)); upper.push(Json::number(5.0)); upper.push(Json::number(6.0));
    Json collider = Json::object();
    collider.set("min", lower);
    collider.set("max", upper);
    Json colliders = Json::array();
    colliders.push(collider);
    Json spawn = Json::array();
    spawn.push(Json::number(7.0)); spawn.push(Json::number(8.0)); spawn.push(Json::number(9.0));
    Json spawns = Json::array();
    spawns.push(spawn);
    map.data.fps = Json::object();
    map.data.fps.set("colliders", colliders);
    map.data.fps.set("spawns", spawns);

    const auto fps = enumerateDocumentSceneNodes(map, true);
    require(fps.size() == 2);
    require(fps[0].group == "Colliders" && fps[0].label == "Box Collider #1");
    require(fps[1].group == "Spawn Points" && fps[1].label == "Spawn #1");
    require(fps[0].hasPosition && fps[1].hasPosition);
    require(fps[0].position.x == 2 && fps[0].position.y == 3 && fps[0].position.z == 4);
    require(fps[1].position.x == 7 && fps[1].position.y == 8 && fps[1].position.z == 9);

    DocumentEntityRef decoded;
    for (const auto& node : fps) {
        const uint64_t key = node.ref.fpsKey();
        require(key != 0);
        require(DocumentEntityRef::fromFpsKey(key, decoded));
        require(decoded.source == node.ref.source && decoded.index == node.ref.index);
    }
    // Batch movement must deduplicate sources, and must be atomic on invalid input.
    const auto before = map.revision;
    require(translateDocumentEntities(map, {fps[0].ref, fps[0].ref, fps[1].ref}, {1, 0, 0}));
    require(map.revision == before + 1);
    DocumentTransform moved;
    require(readDocumentTransform(map, fps[0].ref, moved));
    require(moved.x == 3.0);
    require(readDocumentTransform(map, fps[1].ref, moved));
    require(moved.x == 8.0);

    const auto stable = map.revision;
    const auto invalid = DocumentEntityRef::fpsSpawn(1000);
    require(!translateDocumentEntities(map, {fps[0].ref, invalid}, {1, 0, 0}));
    require(map.revision == stable);
    require(readDocumentTransform(map, fps[0].ref, moved));
    require(moved.x == 3.0);
}
