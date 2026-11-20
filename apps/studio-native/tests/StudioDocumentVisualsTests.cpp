#include "apps/studio-native/StudioDocumentVisuals.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace aiverse;

int main() {
    editor::MapDoc doc;
    doc.data.w=8; doc.data.h=8;
    editor::DocEntity entity;
    entity.type="generic object"; entity.owner=1;
    entity.x=3; entity.z=4;
    doc.data.entities.push_back(entity);
    const auto map=studio::studioDocumentProxies(doc,false);
    assert(map.size()==1);
    assert(map[0].id=="entity:0");
    assert(map[0].center.x==3 && map[0].center.z==4);
    assert(map[0].boundsMin.x<map[0].center.x);
    doc.data.fps=Json::parse(R"({
      "colliders":[
        {"min":[0,0,0],"max":[4,6,8]},
        {"center":[10,2,5],"half":[3,1,2],"yaw":0.125}
      ],
      "spawns":[[5,3,2]]
    })");
    const auto visual=studio::studioDocumentProxies(doc,true);
    assert(visual.size()==3);
    assert(visual[0].id=="collider:0");
    assert(visual[0].center.x==2 && visual[0].center.y==3);
    assert(visual[0].half.x==2 && visual[0].half.z==4);
    assert(visual[1].id=="collider:1");
    assert(visual[1].boundsMax.x>visual[1].center.x+3);
    assert(visual[2].id=="spawn:0" && visual[2].center.y==3);
    assert(studio::studioDocumentId(editor::DocumentEntityRef::fpsSpawn(0))=="spawn:0");
    std::cout << "StudioDocumentVisuals PASS\n";
    return 0;
}
