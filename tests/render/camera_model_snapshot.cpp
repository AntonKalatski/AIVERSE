// LENS final-ModelDraw snapshot: borrowed transient data must be owned.
#include "engine/render/CameraModelSnapshot.h"
#include <cassert>
using namespace aiverse::render;
int main(){
    CameraModelSnapshot snapshot(1);
    Model model;
    std::vector<mat4> nodes{mat4{}};
    std::vector<NodePose> pose(1);
    std::vector<Material> materials(1);
    std::vector<int> nodeMaterials{3};
    BakedSample baked{};
    ModelDraw draw;
    draw.model=&model;
    draw.persistentId=73;
    draw.nodeWorld=&nodes;
    draw.pose=&pose;
    draw.materials=&materials;
    draw.nodeMaterial=&nodeMaterials;
    draw.baked=&baked;
    snapshot.record(draw);
    // Source containers can be destroyed immediately after primary draw.
    nodes.clear();pose.clear();materials.clear();nodeMaterials.clear();
    draw.persistentId=999;
    size_t visits=0;
    snapshot.forEach([&](const ModelDraw& captured){
        ++visits;
        assert(captured.persistentId==73);
        assert(captured.model==&model);
        assert(captured.nodeWorld && captured.nodeWorld->size()==1);
        assert(captured.pose && captured.pose->size()==1);
        assert(captured.materials && captured.materials->size()==1);
        assert(captured.nodeMaterial && captured.nodeMaterial->at(0)==3);
        assert(captured.baked);
        assert(!captured.staticCache);
    });
    snapshot.forEach([&](const ModelDraw& captured){
        ++visits;
        assert(captured.persistentId==73);
    });
    assert(visits==2 && snapshot.size()==1);
    snapshot.record(draw);
    assert(snapshot.dropped()==1 && snapshot.size()==1);
    snapshot.clear();
    assert(snapshot.size()==0 && snapshot.dropped()==0);

    // A pre-LOD packet replaces only its matching finalized base draw;
    // unrelated attachments and legacy draws still replay exactly once.
    CameraModelSnapshot mixed(8);
    aiverse::view::View visual(0);
    aiverse::view::Object prepared;
    prepared.id=43;prepared.generation=2;
    prepared.draw.model=&model;
    prepared.draw.persistentId=73;
    std::vector<Material> preparedMaterial(1);
    prepared.draw.materials=&preparedMaterial;
    mixed.recordPrepared(visual,prepared);
    mixed.record(prepared.draw);
    ModelDraw attachment;
    attachment.model=&model;
    attachment.persistentId=0;
    mixed.record(attachment);
    preparedMaterial.clear();
    assert(mixed.size()==2 && mixed.preparedCount()==1);
    assert(mixed.isPreparedDraw(73) && !mixed.isPreparedDraw(74));
    int fallbacks=0,preparedCount=0;
    mixed.forEachFallback([&](const ModelDraw& retained){
        ++fallbacks;
        assert(retained.persistentId==0);
    });
    mixed.forEachPrepared([&](const aiverse::view::View& source,
                              const aiverse::view::Object& retained){
        ++preparedCount;
        assert(&source==&visual);
        assert(retained.id==43 && retained.draw.persistentId==73);
        assert(retained.draw.materials && retained.draw.materials->size()==1);
    });
    assert(fallbacks==1 && preparedCount==1);
    mixed.clear();
    assert(mixed.size()==0 && mixed.preparedCount()==0);
}
