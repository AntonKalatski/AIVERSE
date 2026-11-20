#include "apps/editor/EditorSceneNodes.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace aiverse;
    editor::MapDoc map;
    editor::DocEntity placed;
    placed.id = "alpha";
    placed.type = "tower";
    placed.x = 4.25f;
    placed.z = -2.5f;
    map.data.entities.push_back(placed);
    auto nodes = editor::enumerateDocumentSceneNodes(map, false);
    assert(nodes.size() == 1);
    assert(nodes[0].ref.source == editor::DocumentEntityRef::Source::MapEntity);
    assert(nodes[0].label == "tower #alpha");
    assert(nodes[0].hasPosition && std::abs(nodes[0].position.x - 4.25) < .0001);

    map.data.fps = Json::parse(R"({
        "colliders":[{"min":[0,1,2],"max":[10,11,12]}],
        "spawns":[[2,3,4]]
    })");
    nodes = editor::enumerateDocumentSceneNodes(map, true);
    assert(nodes.size() == 2);
    assert(nodes[0].ref.source == editor::DocumentEntityRef::Source::FpsCollider);
    assert(nodes[0].group == "Colliders" && nodes[0].hasPosition);
    assert(std::abs(nodes[0].position.x - 5.0) < .0001);
    assert(nodes[1].ref.source == editor::DocumentEntityRef::Source::FpsSpawn);
    assert(nodes[1].hasPosition && nodes[1].position.y == 3);
    std::cout << "Studio scene node contract PASS\n";
    return 0;
}
