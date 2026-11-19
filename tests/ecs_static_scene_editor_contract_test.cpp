// Contract tests for typed JSON prefab authoring and read-only editor
// NativeModule inspector snapshots. No editor GPU/ImGui linkage required.
#include "engine/scene/PrefabStaticAuthoring.h"
#include "engine/scene/PrefabStaticInspector.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace test {
struct Weapon {
    uint32_t damage=0;
    uint32_t range=0;
    std::array<int32_t,28> modifiers{};
};
struct Brain {
    int32_t aggression=0;
};
inline void io(aiverse::Archive& a,Brain& value) {
    a(value.aggression);
}
inline void io(aiverse::Archive& a,Weapon& w) {
    a(w.damage,w.range);
    for(auto& v:w.modifiers)a(v);
}
}
using namespace aiverse;
namespace sc=aiverse::scene;
namespace st=aiverse::storage;
using Catalog=st::PrefabStaticCatalog<test::Weapon>;

static bool decode(const Json& obj,test::Weapon& out,std::string& error) {
    if(!obj.isObject() || !obj.has("damage") || !obj.has("range") ||
       obj.size()!=2) {
        error="weapon expects exactly damage/range";
        return false;
    }
    uint64_t damage=0,range=0;
    if(!sc::SceneAsset::decodeId(obj["damage"],damage) ||
       !sc::SceneAsset::decodeId(obj["range"],range,true) ||
       damage>100000 || range>100000) {
        error="weapon damage/range must be exact bounded integers";
        return false;
    }
    out.damage=uint32_t(damage);
    out.range=uint32_t(range);
    for(size_t i=0;i<out.modifiers.size();++i)
        out.modifiers[i]=int32_t(damage)+int32_t(i);
    return true;
}
static const char* kA=R"json({
 "version":1,"domain":"combat/weapon","prefabs":[
 {"id":"prefabs/soldier","config":{"damage":60,"range":5}},
 {"id":"prefabs/turret","config":{"damage":60,"range":5}},
 {"id":"prefabs/archer","config":{"damage":30,"range":9}}
 ]})json";
static const char* kOrderSwapped=R"json({
 "version":1,"domain":"combat/weapon","prefabs":[
 {"id":"prefabs/archer","config":{"damage":30,"range":9}},
 {"id":"prefabs/turret","config":{"damage":60,"range":5}},
 {"id":"prefabs/soldier","config":{"damage":60,"range":5}}
 ]})json";
static std::shared_ptr<const Catalog> load(const std::string& text) {
    std::string error;
    auto catalog=sc::PrefabStaticAuthoring<test::Weapon>::parseText(
        text,"combat/weapon",decode,error);
    AIVERSE_TEST_CHECK(catalog!=nullptr);
    AIVERSE_TEST_CHECK(error.empty());
    return catalog;
}
static void bad(const std::string& text,const std::string& expectedError) {
    std::string error;
    auto catalog=sc::PrefabStaticAuthoring<test::Weapon>::parseText(
        text,"combat/weapon",decode,error);
    AIVERSE_TEST_CHECK(catalog==nullptr);
    AIVERSE_TEST_CHECK(error.find(expectedError)!=std::string::npos);
}
static void authoringAndInspector() {
    auto catalog=load(kA);
    auto permuted=load(kOrderSwapped);
    AIVERSE_TEST_CHECK(catalog->fingerprint()==permuted->fingerprint());
    AIVERSE_TEST_CHECK(catalog->table().fingerprint()==
                      permuted->table().fingerprint());
    AIVERSE_TEST_CHECK(catalog->uniqueConfigCount()==2);
    AIVERSE_TEST_CHECK(catalog->prefabCount()==3);
    AIVERSE_TEST_CHECK(catalog->find("prefabs/soldier")==
                      catalog->find("prefabs/turret"));
    AIVERSE_TEST_CHECK(catalog->find("prefabs/archer")!=
                      catalog->find("prefabs/turret"));
    bad(R"({"version":2,"domain":"combat/weapon","prefabs":[]})",
        "invalid prefab static v1");
    bad(R"({"version":1,"domain":"other","prefabs":[]})",
        "invalid prefab static v1");
    bad(R"({"version":1,"domain":"combat/weapon","prefabs":[
        {"id":"prefabs/soldier","config":{"damage":1.5,"range":3}}
    ]})", "exact bounded integers");
    bad(R"({"version":1,"domain":"combat/weapon","prefabs":[
        {"id":"prefabs/soldier","config":{"damage":50,"range":3}},
        {"id":"prefabs/soldier","config":{"damage":50,"range":3}}
    ]})","duplicate");
    bad(R"({"version":1,"domain":"combat/weapon","prefabs":[
        {"id":"../escape","config":{"damage":50,"range":3}}
    ]})","invalid or duplicate");

    World world,preview;
    AIVERSE_TEST_CHECK(catalog->bindWorld(world));
    AIVERSE_TEST_CHECK(catalog->bindWorld(preview));
    Entity e=world.create(), p=preview.create();
    AIVERSE_TEST_CHECK(catalog->attach(world,e,"prefabs/soldier"));
    AIVERSE_TEST_CHECK(catalog->attach(preview,p,"prefabs/turret"));
    AIVERSE_TEST_CHECK(catalog->read(world,e)==catalog->read(preview,p));
    AIVERSE_TEST_CHECK(catalog->read(world,e)->damage==60);

    sc::PrefabStaticInspector<test::Weapon> inspector(
        catalog,"StaticWeapon","Weapon configuration (read-only)",
        [](const test::Weapon& weapon) {
            Json obj=Json::object();
            obj.set("damage",Json::number(int64_t(weapon.damage)));
            obj.set("range",Json::number(int64_t(weapon.range)));
            return obj;
        });
    Json components=Json::object(),schemas=Json::object();
    AIVERSE_TEST_CHECK(inspector.append(world,e,components,schemas));
    AIVERSE_TEST_CHECK(components.has("StaticWeapon"));
    const auto& values=components["StaticWeapon"];
    AIVERSE_TEST_CHECK(values.getInt("slot")>0);
    AIVERSE_TEST_CHECK(values["config"].getInt("damage")==60);
    AIVERSE_TEST_CHECK(values["config"].getInt("range")==5);
    AIVERSE_TEST_CHECK(values["catalogRevision"].isString());
    AIVERSE_TEST_CHECK(values["catalogRevision"].str().size()==16);
    AIVERSE_TEST_CHECK(schemas["StaticWeapon"].getStr("title")==
                      "Weapon configuration (read-only)");
    const auto& fields=schemas["StaticWeapon"]["fields"];
    for(const auto& [name,meta]:fields.members()){
        (void)name;
        AIVERSE_TEST_CHECK(meta.isObject());
        AIVERSE_TEST_CHECK(!meta.getBool("editable",true));
    }
    // A single NativeModule hook can now expose several typed immutable
    // components without knowing C++ types inside the editor host.
    st::PrefabStaticCatalogBuilder<test::Brain> brainBuilder;
    AIVERSE_TEST_CHECK(brainBuilder.add("prefabs/soldier",test::Brain{33}));
    auto brainCatalog=brainBuilder.seal();
    AIVERSE_TEST_CHECK(brainCatalog->bindWorld(world));
    AIVERSE_TEST_CHECK(brainCatalog->attach(world,e,"prefabs/soldier"));
    sc::PrefabStaticInspectorRegistry inspectors;
    AIVERSE_TEST_CHECK(inspectors.add<test::Brain>(
        brainCatalog,"StaticBrain","AI config (read-only)",
        [](const test::Brain& v) {
            Json j=Json::object();
            j.set("aggression",Json::number(int64_t(v.aggression)));
            return j;
        }));
    AIVERSE_TEST_CHECK(inspectors.add<test::Weapon>(
        catalog,"StaticWeapon","Weapon config (read-only)",
        [](const test::Weapon& v) {
            Json j=Json::object();
            j.set("damage",Json::number(int64_t(v.damage)));
            return j;
        }));
    AIVERSE_TEST_CHECK(!inspectors.add<test::Weapon>(
        catalog,"StaticWeapon","Duplicate",{}));
    AIVERSE_TEST_CHECK(inspectors.seal());
    AIVERSE_TEST_CHECK(!inspectors.seal());
    AIVERSE_TEST_CHECK(!inspectors.add<test::Brain>(
        brainCatalog,"Extra","Late",{}));
    AIVERSE_TEST_CHECK(inspectors.size()==2);
    Json allComponents=Json::object(),allSchemas=Json::object();
    inspectors.append(world,e,allComponents,allSchemas);
    AIVERSE_TEST_CHECK(allComponents.size()==2);
    AIVERSE_TEST_CHECK(allComponents["StaticBrain"]["config"].getInt(
        "aggression")==33);
    AIVERSE_TEST_CHECK(allComponents["StaticWeapon"]["config"].getInt(
        "damage")==60);
    for(const auto& [componentId,schema]:allSchemas.members()) {
        (void)componentId;
        for(const auto& [fieldId,meta]:schema["fields"].members()) {
            (void)fieldId;
            AIVERSE_TEST_CHECK(!meta.getBool("editable",true));
        }
    }

    // The existing editor accepts this JSON shape from
    // NativeModule::inspectComponents. It must never expose a writeback.
    World saved=world;
    AIVERSE_TEST_CHECK(!inspector.append(world,world.create(),components,schemas));
    AIVERSE_TEST_CHECK(catalog->read(world,e)==catalog->read(saved,e));
    AIVERSE_TEST_CHECK(catalog->validateWorld(world));
    AIVERSE_TEST_CHECK(catalog->validateWorld(preview));
}
int main() {
    authoringAndInspector();
    std::cout<<"PASS prefab editor JSON authoring, deterministic catalog and read-only inspector\n";
}
