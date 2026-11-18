#include "apps/editor/PhysisAuthoring.h"
#include <cstdio>
using namespace aiverse;
using namespace aiverse::editor;
using namespace aiverse::editor::physis_authoring;
namespace {
int failures=0;
#define CHECK(v) do { if(!(v)){std::printf("PHYSIS FIELD EDIT FAIL %s:%d %s\n",__FILE__,__LINE__,#v);++failures;} } while(0)
void testOneDragOneUndo() {
    MapDoc doc;doc.reset(8,8);
    std::string error;
    CHECK(apply(doc,defaultConfig(),&error));
    std::string wind,storm,gravity;
    CHECK(addField(doc,physis::AuthoredFieldKind::Wind,0,0,0,wind,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Storm,10,0,-10,storm,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Gravity,-5,1,5,gravity,&error));
    CHECK(!wind.empty() && !storm.empty() && !gravity.empty());
    const Json before=read(doc);
    const auto rev=doc.revision;
    CHECK(moveField(doc,wind,5.5,2,7,true,&error));  // first actual drag
    CHECK(moveField(doc,wind,7.5,3,8,false,&error)); // same drag no snapshot
    CHECK(doc.revision>rev);
    std::string key;
    std::size_t index=0;
    Json field;
    CHECK(findField(read(doc),wind,key,index,&field));
    CHECK(field["center"][0].fx()==Fixed::fromRatio(15,2));
    doc.undo();
    CHECK(read(doc)==before); // one Undo must revert the WHOLE drag
    doc.redo();
    CHECK(findField(read(doc),wind,key,index,&field));
    CHECK(field["center"][0].fx()==Fixed::fromRatio(15,2));
    const auto stableRev=doc.revision;
    CHECK(!moveField(doc,wind,2000000,0,0,true,&error));
    CHECK(doc.revision==stableRev);
    CHECK(!error.empty());

    Json copy=field;
    copy.set("customField",Json::string("preserved"));
    CHECK(replaceField(doc,wind,copy,&error));
    CHECK(read(doc)["windZones"][0]["customField"].str()=="preserved");
    CHECK(removeField(doc,storm,&error));
    CHECK(!findField(read(doc),storm,key,index));
    doc.undo();
    CHECK(findField(read(doc),storm,key,index));
}
void testFpsAndDuplicateIds() {
    MapDoc fps;
    std::string error;
    CHECK(fps.loadJson(Json::parse(R"({"name":"fps","colliders":[],"spawns":[],"mod":"preserve"})"),&error));
    CHECK(apply(fps,defaultConfig(),&error));
    std::string id;
    CHECK(addField(fps,physis::AuthoredFieldKind::Storm,0,0,0,id,&error));
    CHECK(fps.toJson()["mod"].str()=="preserve");
    CHECK(fps.toJson()["physis"]["storms"].size()==1);
    MapDoc reread;
    CHECK(reread.loadJson(fps.toJson(),&error));
    CHECK(read(reread)["storms"].size()==1);
    fps.undo();
    CHECK(read(fps)["storms"].isNull());
    fps.redo();
    CHECK(read(fps)["storms"].size()==1);

    Json invalid=read(fps);
    auto storms=invalid["storms"];
    auto clone=storms[0];
    storms.push(clone);
    invalid.set("storms",storms);
    const auto revision=fps.revision;
    CHECK(!apply(fps,invalid,&error));
    CHECK(fps.revision==revision);
}
}
int main(){testOneDragOneUndo();testFpsAndDuplicateIds();
std::printf("PHYSIS Field Authoring: %s\n",failures?"FAIL":"PASS");return failures?1:0;}
