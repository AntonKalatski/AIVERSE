#include "apps/editor/PhysisAuthoring.h"
#include <cstdio>
#include <cmath>
#include <limits>
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
void testRadiusResizeAndSnapping() {
    MapDoc doc; doc.reset(8,8);
    std::string error;
    CHECK(apply(doc,defaultConfig(),&error));
    std::string wind,storm,gravity;
    CHECK(addField(doc,physis::AuthoredFieldKind::Wind,0,0,0,wind,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Storm,1,2,3,storm,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Gravity,4,5,6,gravity,&error));
    CHECK(snapMeters(-2.6,5.0) == -5.0);
    CHECK(snapMeters(2.6,5.0) == 5.0);
    CHECK(snapMeters(-2.5,5.0) == -5.0);
    CHECK(snapMeters(1.2,0.25) == 1.25);
    CHECK(snapMeters(1.2,0.0) == 1.2);
    CHECK(snapMeters(1000001.0,5.0) == 1000000.0);
    CHECK(moveField(doc,wind,snapMeters(-12.2,5),0,snapMeters(12.2,5),true,&error));
    std::string key; std::size_t index=0; Json field;
    CHECK(findField(read(doc),wind,key,index,&field));
    CHECK(field["center"][0].fx() == Fixed::fromInt(-10));
    const Json before=read(doc);
    CHECK(resizeField(doc,storm,250.25,true,&error));
    CHECK(resizeField(doc,storm,300.75,false,&error));
    CHECK(findField(read(doc),storm,key,index,&field));
    CHECK(field["outerRadiusMeters"].fx() == Fixed::fromRatio(1203,4));
    CHECK(field["eyeRadiusMeters"].fx() == Fixed::fromInt(20));
    doc.undo();
    CHECK(read(doc)==before); // entire resize drag is one Undo entry
    doc.redo();
    CHECK(findField(read(doc),storm,key,index,&field));
    CHECK(field["outerRadiusMeters"].fx() == Fixed::fromRatio(1203,4));
    const auto rev=doc.revision;
    CHECK(resizeField(doc,storm,300.75,true,&error)); // no-op
    CHECK(doc.revision==rev);
    CHECK(!resizeField(doc,storm,19.0,true,&error)); // eye radius violation
    CHECK(doc.revision==rev);
    CHECK(!resizeField(doc,gravity,100001.0,true,&error));
    CHECK(doc.revision==rev);
    CHECK(!resizeField(doc,wind,std::numeric_limits<double>::infinity(),true,&error));
    CHECK(doc.revision==rev);
    CHECK(resizeField(doc,gravity,10.0,true,&error));
    CHECK(findField(read(doc),gravity,key,index,&field));
    CHECK(field["radiusMeters"].fx() == Fixed::fromInt(10));
}

void testGroupOperations() {
    MapDoc doc; doc.reset(8,8);
    std::string error;
    CHECK(apply(doc,defaultConfig(),&error));
    std::string wind,storm,gravity;
    CHECK(addField(doc,physis::AuthoredFieldKind::Wind,-10,3,0,wind,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Storm,20,5,-30,storm,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Gravity,1,-2,4,gravity,&error));
    std::string collection; std::size_t index=0;
    Json source;
    CHECK(findField(read(doc),wind,collection,index,&source));
    source.set("customTag",Json::string("keep"));
    CHECK(replaceField(doc,wind,source,&error));
    const Json before = read(doc);
    const auto revBefore = doc.revision;
    CHECK(moveFields(doc,{{wind,-5,3,10},{storm,25,5,-20},{gravity,6,-2,14}},true,&error));
    CHECK(moveFields(doc,{{wind,0,3,12},{storm,30,5,-18},{gravity,11,-2,16}},false,&error));
    CHECK(doc.revision>revBefore);
    CHECK(findField(read(doc),storm,collection,index,&source));
    CHECK(source["center"][0].fx()==Fixed::fromInt(30));
    doc.undo();
    CHECK(read(doc)==before);
    doc.redo();
    CHECK(findField(read(doc),wind,collection,index,&source));
    CHECK(source["center"][0].fx()==Fixed::fromInt(0));
    CHECK(source["customTag"].str()=="keep");
    const Json moved = read(doc);
    const auto rev=doc.revision;
    CHECK(moveFields(doc,{{wind,0,3,12},{storm,30,5,-18}},true,&error));
    CHECK(doc.revision==rev);
    CHECK(!moveFields(doc,{{wind,100,0,0},{"missing",20,0,0}},true,&error));
    CHECK(read(doc)==moved && doc.revision==rev);
    CHECK(!moveFields(doc,{{wind,100,0,0},{storm,2000000,0,0}},true,&error));
    CHECK(read(doc)==moved && doc.revision==rev);
    CHECK(!moveFields(doc,{{wind,100,0,0},{wind,200,0,0}},true,&error));
    CHECK(read(doc)==moved && doc.revision==rev);

    std::vector<std::string> copies;
    CHECK(duplicateFields(doc,{gravity,storm,wind},5,0,5,copies,&error));
    CHECK(copies.size()==3);
    CHECK(read(doc)["windZones"].size()==2);
    CHECK(read(doc)["storms"].size()==2);
    CHECK(read(doc)["gravityFields"].size()==2);
    CHECK(findField(read(doc),wind+"-copy",collection,index,&source));
    CHECK(source["customTag"].str()=="keep");
    CHECK(source["center"][0].fx()==Fixed::fromInt(5));
    doc.undo();
    CHECK(read(doc)==moved);
    doc.redo();
    CHECK(read(doc)["windZones"].size()==2);
    const Json duplicated=read(doc);
    const auto dupeRev=doc.revision;
    CHECK(!duplicateFields(doc,{wind,"missing"},5,0,5,copies,&error));
    CHECK(read(doc)==duplicated && doc.revision==dupeRev);
    CHECK(!duplicateFields(doc,{wind,storm},2000000,0,0,copies,&error));
    CHECK(read(doc)==duplicated && doc.revision==dupeRev);
    std::vector<std::string> secondCopies;
    CHECK(duplicateFields(doc,{wind},0,0,0,secondCopies,&error));
    CHECK(secondCopies.size()==1);
    CHECK(secondCopies[0]==wind+"-copy-2");
    doc.undo();
    CHECK(read(doc)==duplicated);
    CHECK(removeFields(doc,{wind,storm},&error));
    CHECK(!findField(read(doc),wind,collection,index));
    CHECK(!findField(read(doc),storm,collection,index));
    CHECK(findField(read(doc),wind+"-copy",collection,index));
    doc.undo();
    CHECK(read(doc)==duplicated);
    const auto safeRev=doc.revision;
    CHECK(!removeFields(doc,{wind,"unknown"},&error));
    CHECK(read(doc)==duplicated && doc.revision==safeRev);
}
void testGroupFpsMap() {
    MapDoc fps;
    std::string error;
    CHECK(fps.loadJson(Json::parse(R"({"name":"fps","colliders":[],"spawns":[],"other":"unchanged"})"),&error));
    CHECK(apply(fps,defaultConfig(),&error));
    std::string wind,gravity;
    CHECK(addField(fps,physis::AuthoredFieldKind::Wind,0,0,0,wind,&error));
    CHECK(addField(fps,physis::AuthoredFieldKind::Gravity,10,0,10,gravity,&error));
    const Json before=read(fps);
    CHECK(moveFields(fps,{{wind,5,0,5},{gravity,15,0,15}},true,&error));
    CHECK(fps.toJson().getStr("other")=="unchanged");
    fps.undo();
    CHECK(read(fps)==before);
    std::vector<std::string> copies;
    CHECK(duplicateFields(fps,{wind,gravity},4,0,4,copies,&error));
    MapDoc roundtrip;
    CHECK(roundtrip.loadJson(fps.toJson(),&error));
    CHECK(read(roundtrip)["windZones"].size()==2);
    CHECK(read(roundtrip)["gravityFields"].size()==2);
    CHECK(removeFields(fps,copies,&error));
    CHECK(read(fps)==before);
    fps.undo();
    CHECK(read(fps)["windZones"].size()==2);
}

void testGroupRotationAndScale() {
    MapDoc doc;doc.reset(8,8);
    std::string error;
    CHECK(apply(doc,defaultConfig(),&error));
    std::string wind,storm,gravity;
    CHECK(addField(doc,physis::AuthoredFieldKind::Wind,0,0,0,wind,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Storm,10,0,0,storm,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Gravity,-10,0,0,gravity,&error));
    const Json before=read(doc);
    const auto rev=doc.revision;
    CHECK(transformFields(doc,{gravity,wind,storm},0,0,0,90.0,2.0,&error));
    CHECK(doc.revision>rev);
    std::string key;std::size_t index=0;Json field;
    CHECK(findField(read(doc),wind,key,index,&field));
    CHECK(field["center"][0].fx()==Fixed{});
    CHECK(field["radiusMeters"].fx()==Fixed::fromInt(200));
    CHECK(findField(read(doc),storm,key,index,&field));
    CHECK(field["center"][0].fx()==Fixed{});
    CHECK(field["center"][2].fx()==Fixed::fromInt(20));
    CHECK(field["eyeRadiusMeters"].fx()==Fixed::fromInt(40));
    CHECK(field["outerRadiusMeters"].fx()==Fixed::fromInt(400));
    CHECK(field["heightMeters"].fx()==Fixed::fromInt(800));
    CHECK(findField(read(doc),gravity,key,index,&field));
    CHECK(field["center"][2].fx()==Fixed::fromInt(-20));
    CHECK(field["radiusMeters"].fx()==Fixed::fromInt(240));
    doc.undo();
    CHECK(read(doc)==before); // rotation + scale = one Undo
    doc.redo();
    CHECK(findField(read(doc),storm,key,index,&field));
    CHECK(field["center"][2].fx()==Fixed::fromInt(20));
    const Json after=read(doc);
    const auto stableRev=doc.revision;
    CHECK(!transformFields(doc,{wind,"missing"},0,0,0,20,1.0,&error));
    CHECK(doc.revision==stableRev && read(doc)==after);
    CHECK(!transformFields(doc,{wind,storm},0,0,0,0,-1,&error));
    CHECK(doc.revision==stableRev && read(doc)==after);
    CHECK(!transformFields(doc,{wind,storm},0,0,0,0,100,&error));
    CHECK(doc.revision==stableRev && read(doc)==after);
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
    const Json fpsBeforeResize=read(fps);
    CHECK(resizeField(fps,id,350.5,true,&error));
    CHECK(read(fps)["storms"][0]["outerRadiusMeters"].fx()==Fixed::fromRatio(701,2));
    fps.undo();
    CHECK(read(fps)==fpsBeforeResize);
    fps.redo();
    CHECK(read(fps)["storms"][0]["outerRadiusMeters"].fx()==Fixed::fromRatio(701,2));

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
int main(){testOneDragOneUndo();testRadiusResizeAndSnapping();testGroupOperations();testGroupFpsMap();testGroupRotationAndScale();testFpsAndDuplicateIds();
std::printf("PHYSIS Field Authoring: %s\n",failures?"FAIL":"PASS");return failures?1:0;}
