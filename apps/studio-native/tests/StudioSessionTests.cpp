#include "apps/studio-native/StudioSession.h"
#include "tests/TestCheck.h"
#include "apps/studio-native/StudioGameLaunch.h"
#include "apps/studio-native/StudioGameInput.h"
#include "apps/studio-native/StudioGameDevice.h"
#include "apps/studio-native/StudioRuntimeInspect.h"
#include "apps/studio-native/StudioMoveJournal.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace {
void projectFileRecoveryWorkflow() {
    using namespace aiverse;
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("studio-open-recovery-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project=root/"templates"/"fixture";
    fs::create_directories(project/"content"/"A");
    fs::create_directories(project/"content"/"B");
    fs::create_directories(project/".aiverse-cache");
    std::ofstream(project/"game.json") << R"({"name":"Recovery fixture","module":"rts","rules":"rules.json"})";
    std::ofstream(project/"rules.json") << R"({"types":{"unit":{"health":{"max":100}}}})";
    std::ofstream(project/"Fixture.aiverse") << R"({"format":1,"name":"Recovery fixture","manifest":"game.json"})";
    const auto original=project/"content"/"A"/"sample.txt";
    const auto moved=project/"content"/"B"/"sample.txt";
    std::ofstream(original) << "persistent bytes";
    auto simulateCrash=[&] {
        studio::StudioMoveJournal journal(project);
        journal.begin({{"A/sample.txt","B/sample.txt"}});
        fs::rename(original,moved);
    };
    const std::vector<std::string> inputs={project.string(),(project/"game.json").string(),
        (project/"Fixture.aiverse").string(),"fixture"};
    for (const auto& input:inputs) {
        simulateCrash();
        studio::StudioSession session(root.string(),input);
        AIVERSE_TEST_CHECK(fs::exists(original) && !fs::exists(moved));
        AIVERSE_TEST_CHECK(!fs::exists(project/".aiverse-cache"/"studio-move-journal"));
        simulateCrash();
        session.openProject(input);
        AIVERSE_TEST_CHECK(fs::exists(original) && !fs::exists(moved));
        AIVERSE_TEST_CHECK(!fs::exists(project/".aiverse-cache"/"studio-move-journal"));
    }
    fs::remove_all(root);
}
void authoredEntityWorkflow() {
   using namespace aiverse;namespace fs=std::filesystem;
   const auto root=fs::temp_directory_path()/("studio-authored-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
   const auto project=root/"fixture";fs::create_directories(project/"maps");
   std::ofstream(project/"game.json")<<R"({"module":"rts","rules":"rules.json","maps":[{"file":"maps/scene.json"}]})";
   std::ofstream(project/"rules.json")<<R"({"types":{"tree":{"health":{"max":100}}}})";
   editor::MapDoc map;map.reset(16,16);editor::DocEntity e;e.id="first";e.type="tree";e.x=2;e.z=3;
   e.extra=Json::parse(R"({"components":{"Transform":{"y":4,"yaw":0,"scale":1},"StaticModel":{"asset":"content/tree.gltf"}}})");map.data.entities.push_back(e);e.id="second";map.data.entities.push_back(e);
   AIVERSE_TEST_CHECK(map.save((project/"maps/scene.json").string(),nullptr));
   studio::StudioSession session(root.string(),project.string());
   auto call=[&](const char* method,const char* params="{}") {auto request=Json::object();request.set("id",Json::number(int64_t(1)));request.set("method",Json::string(method));request.set("params",Json::parse(params));return session.dispatch(request);};
   AIVERSE_TEST_CHECK(!call("studio.select",R"({"id":"entity:@second"})").has("error"));
   AIVERSE_TEST_CHECK(!call("studio.setProperty",R"({"entityId":"entity:@second","key":"y","value":9})").has("error"));
   AIVERSE_TEST_CHECK(session.workspace().map.data.entities[1].extra["components"]["Transform"]["y"].integer()==9);
   AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));AIVERSE_TEST_CHECK(session.workspace().map.data.entities[1].extra["components"]["Transform"]["y"].integer()==4);
   AIVERSE_TEST_CHECK(!call("studio.select",R"({"id":"entity:@first"})").has("error"));
   AIVERSE_TEST_CHECK(!call("studio.entityDelete",R"({"entityId":"entity:@first"})").has("error"));
   AIVERSE_TEST_CHECK(!call("studio.select",R"({"id":"entity:@second"})").has("error"));AIVERSE_TEST_CHECK(session.selectedEntityIndex()==0);
   AIVERSE_TEST_CHECK(!call("studio.entityDuplicate",R"({"entityId":"entity:@second"})").has("error"));
   AIVERSE_TEST_CHECK(session.selectedObjectId().rfind("entity:@",0)==0 && session.selectedObjectId()!="entity:@second");
   AIVERSE_TEST_CHECK(!call("studio.save").has("error"));session.openProject(project.string());
   AIVERSE_TEST_CHECK(session.workspace().map.data.entities[1].extra["components"]["StaticModel"].getStr("asset")=="content/tree.gltf");
   fs::remove_all(root);
}
void entityWorkflow() {
    using namespace aiverse;
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("studio-entities-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project=root/"templates"/"fixture";
    fs::create_directories(project/"maps");
    std::ofstream(project/"game.json") << R"({"name":"Studio fixture","module":"rts","rules":"rules.json","maps":[{"file":"maps/scene.json"}]})";
    std::ofstream(project/"rules.json") << R"({"types":{"unit":{"health":{"max":100}}}})";
    editor::MapDoc map;
    map.reset(16,16);
    editor::DocEntity entity; entity.type="unit";entity.id="original";entity.x=2;entity.z=3;
    entity.extra=Json::parse(R"({"inventory":{"wood":20}})");
    map.data.extra.set("nereidWater",Json::parse(R"({"surfaces":[{"id":7,"kind":"river","shape":"river","surfaceY":2,"bottomY":0,"points":[[0,0,3],[0,16,3]]}]})"));
    map.data.entities.push_back(entity);
    entity.id="other";entity.x=5;map.data.entities.push_back(entity);
    map.data.roads.emplace_back(0,1);
    AIVERSE_TEST_CHECK(map.save((project/"maps/scene.json").string(),nullptr));
    {
        studio::StudioSession session(root.string(),project.string());
        auto call=[&](const char* method,Json params=Json::object()) {
            Json request=Json::object();request.set("id",Json::number(int64_t(1)));
            request.set("method",Json::string(method));request.set("params",std::move(params));
            return session.dispatch(request);
        };
        {
            auto state=call("studio.nereidFlow");AIVERSE_TEST_CHECK(!state.has("error")&&state["result"]["surfaces"].size()==1);
            auto input=Json::parse(R"({"id":7,"mode":"automatic","slopeDegrees":1,"downhillHeadingDegrees":180})");
            AIVERSE_TEST_CHECK(!call("studio.nereidFlowSet",input).has("error"));
            AIVERSE_TEST_CHECK(session.workspace().map.data.extra["nereidWater"]["surfaces"][0]["grade"].has("slopeDegrees"));
            AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
            AIVERSE_TEST_CHECK(!session.workspace().map.data.extra["nereidWater"]["surfaces"][0].has("grade"));
            AIVERSE_TEST_CHECK(!call("studio.redo").has("error"));
            const auto previous=session.workspace().map.toJson();input.set("slopeDegrees",Json::number(90.0));
            AIVERSE_TEST_CHECK(call("studio.nereidFlowSet",input).has("error"));AIVERSE_TEST_CHECK(session.workspace().map.toJson()==previous);
            AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
            AIVERSE_TEST_CHECK(!call("studio.save").has("error"));
            session.openProject(project.string());
        }
        {
            Simulation live({1,20,0},std::make_unique<game::RtsModule>());
            live.setup(session.workspace().project.rulesJson,session.workspace().map.toJson());
            studio::StudioRuntimeInspect inspector;
            const auto before=live.checksum();
            auto page=inspector.entities(&live,"run-1",0,1,"");
            AIVERSE_TEST_CHECK(page["active"].boolean() && page["rows"].size()==1 && page["total"].integer()>=2);
            const auto ref=page["rows"][0].getStr("ref");
            const auto detail=inspector.inspect(&live,"run-1","run-1",ref);
            AIVERSE_TEST_CHECK(detail["components"].has("Transform"));
            AIVERSE_TEST_CHECK(live.checksum()==before);
            bool stale=false;try {inspector.inspect(&live,"run-2","run-1",ref);}catch(...){stale=true;}
            AIVERSE_TEST_CHECK(stale);
            AIVERSE_TEST_CHECK(inspector.entities(nullptr,"run-2",0,100,"")["rows"].size()==0);
        }
        const auto plan=studio::studioGameLaunch(session.workspace(),true);
        AIVERSE_TEST_CHECK(plan.map && *plan.map==0);
        AIVERSE_TEST_CHECK(plan.project==session.workspace().project.dir);
        AIVERSE_TEST_CHECK(!studio::studioGameLaunch(session.workspace(),false).map);
        const auto catalog=call("studio.entityCatalog");
        AIVERSE_TEST_CHECK(catalog["result"].getBool("mapEntities"));
        AIVERSE_TEST_CHECK(catalog["result"]["types"].size()==1);
        const auto before=session.workspace().map.toJson();
        AIVERSE_TEST_CHECK(call("studio.entityCreate",Json::parse(R"({"type":"missing","x":1,"z":2,"owner":0})")).has("error"));
        AIVERSE_TEST_CHECK(call("studio.entityCreate",Json::parse(R"({"type":"unit","x":1,"z":2,"owner":0.5})")).has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.toJson()==before);
        AIVERSE_TEST_CHECK(!session.workspace().map.canUndo());
        const auto created=call("studio.entityCreate",Json::parse(R"({"type":"unit","x":4,"z":6,"owner":0,"count":100})"));
        AIVERSE_TEST_CHECK(!created.has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.data.entities.size()==3); // unallowlisted count ignored
        AIVERSE_TEST_CHECK(created["result"].getStr("selectedId")=="entity:2");
        AIVERSE_TEST_CHECK(call("studio.entityDelete",Json::parse(R"({"entityId":"entity:0"})")).has("error"));
        AIVERSE_TEST_CHECK(!call("studio.select",Json::parse(R"({"id":"entity:0"})")).has("error"));
        AIVERSE_TEST_CHECK(!call("studio.entityDuplicate",Json::parse(R"({"entityId":"entity:0"})")).has("error"));
        const auto& copy=session.workspace().map.data.entities.back();
        AIVERSE_TEST_CHECK(copy.id!="original" && copy.extra==entity.extra && copy.x==3 && copy.z==4);
        AIVERSE_TEST_CHECK(!call("studio.entityDelete",Json::parse(R"({"entityId":"entity:3"})")).has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.data.entities.size()==3);
        AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.data.entities.size()==4 && session.selectedObjectId().empty());
        AIVERSE_TEST_CHECK(!call("studio.redo").has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.data.entities.size()==3);
        AIVERSE_TEST_CHECK(!call("studio.save").has("error"));
        AIVERSE_TEST_CHECK(!session.workspace().map.dirty);
        session.openProject(project.string());
        AIVERSE_TEST_CHECK(session.workspace().map.data.entities.size()==3);
        AIVERSE_TEST_CHECK(session.workspace().map.data.entities[2].x==4);
        AIVERSE_TEST_CHECK(!call("studio.select",Json::parse(R"({"id":"entity:0"})")).has("error"));
        AIVERSE_TEST_CHECK(!call("studio.entityDelete",Json::parse(R"({"entityId":"entity:0"})")).has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.data.roads.empty());
        AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.data.roads.size()==1);
        const auto projectState=call("studio.project")["result"];
        AIVERSE_TEST_CHECK(projectState["maps"].size()==1 && projectState.getStr("currentMap")=="maps/scene.json");
        const auto prior=session.workspace().map.toJson();
        AIVERSE_TEST_CHECK(call("studio.newMap",Json::parse(R"({"name":"new","width":0,"height":32})")).has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.toJson()==prior);
        AIVERSE_TEST_CHECK(call("studio.openMap",Json::parse(R"({"file":"../rules.json"})")).has("error"));
        const auto revision=session.workspace().map.revision;
        AIVERSE_TEST_CHECK(!call("studio.newMap",Json::parse(R"({"name":"New document","width":32,"height":24})")).has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.data.w==32 && session.workspace().map.dirty);
        AIVERSE_TEST_CHECK(session.workspace().map.path.empty());
        AIVERSE_TEST_CHECK(session.workspace().map.revision>revision);
        bool rejected=false;
        try { session.saveMapAs((project/"game.json").string()); } catch(const std::exception&) { rejected=true; }
        AIVERSE_TEST_CHECK(rejected);
        bool dirtyPlayRejected=false;
        try { (void)studio::studioGameLaunch(session.workspace(),true); }
        catch(const std::exception&) {dirtyPlayRejected=true;}
        AIVERSE_TEST_CHECK(dirtyPlayRejected);
        session.saveMapAs((project/"maps"/"new.json").string());
        AIVERSE_TEST_CHECK(fs::exists(project/"maps"/"new.json") && !session.workspace().map.dirty);
        AIVERSE_TEST_CHECK(call("studio.project")["result"]["maps"].size()==2);
        AIVERSE_TEST_CHECK(!call("studio.openMap",Json::parse(R"({"file":"maps/scene.json"})")).has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.data.entities.size()==3);
        const auto restored=session.workspace().map.toJson();
        std::ofstream(project/"maps"/"new.json") << "invalid map JSON";
        AIVERSE_TEST_CHECK(call("studio.openMap",Json::parse(R"({"file":"maps/new.json"})")).has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.toJson()==restored);

    }
    fs::remove_all(root);
}
}

int main() {
    using namespace aiverse;
    aiverse::studio::StudioSession session(".");
    bool playRejected=false;
    try { (void)studio::studioGameLaunch(session.workspace(),false); }
    catch(const std::exception&) {playRejected=true;}
    AIVERSE_TEST_CHECK(playRejected);
    auto phone=studio::StudioGameDevice::parse(Json::parse(R"({"id":"iphone-x","landscape":false,"safeArea":true})"));
    AIVERSE_TEST_CHECK(phone.size(800,600)==std::make_pair(375.f,812.f));
    AIVERSE_TEST_CHECK(phone.insets().top==44 && phone.insets().bottom==34);
    phone.landscape=true;
    AIVERSE_TEST_CHECK(phone.size(800,600)==std::make_pair(812.f,375.f));
    AIVERSE_TEST_CHECK(phone.insets().left==44 && phone.insets().bottom==21);
    phone.safeArea=false;AIVERSE_TEST_CHECK(phone.insets().left==0);
    phone.id="none";AIVERSE_TEST_CHECK(phone.size(800,600)==std::make_pair(800.f,600.f));
    bool badProfile=false;try {studio::StudioGameDevice::parse(Json::parse(R"({"id":"unknown","landscape":true,"safeArea":true})"));}catch(...){badProfile=true;}
    AIVERSE_TEST_CHECK(badProfile);
    AIVERSE_TEST_CHECK(studio::studioGameKey("w")==platform::Key::W);
    AIVERSE_TEST_CHECK(studio::studioGameKey("Escape")==platform::Key::Escape);
    AIVERSE_TEST_CHECK(studio::studioGameKey("ArrowDown")==platform::Key::Down);
    AIVERSE_TEST_CHECK(studio::studioGameKey("not-a-key")==platform::Key::Unknown);
    auto call = [&session](const char* json) { return session.dispatch(Json::parse(json)); };
    const Json snapshot = call(R"({"id":1,"method":"studio.snapshot","params":{}})");
    AIVERSE_TEST_CHECK(snapshot["result"].isObject());
    AIVERSE_TEST_CHECK(snapshot["result"]["scene"]["entities"].isArray());
    AIVERSE_TEST_CHECK(snapshot["result"]["selectedId"].isNull());
    AIVERSE_TEST_CHECK(call(R"({"id":2,"method":"studio.select","params":{"id":"entity:0"}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":3,"method":"workspace.call","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":4,"method":"studio.setProperty","params":{"entityId":"entity:0","key":"x","value":1}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":5,"method":"studio.select","params":{"id":null}})")["result"]["selectedId"].isNull());
    AIVERSE_TEST_CHECK(call(R"({"id":6,"method":"studio.save","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":7,"method":"studio.undo","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":8,"method":"studio.redo","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(session.selectedEntityIndex() == -1);
    AIVERSE_TEST_CHECK(call(R"({"id":21,"method":"studio.materials","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":22,"method":"studio.materialPatch","params":{"name":"steel","patch":{"metallic":0.5}}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":23,"method":"studio.materialCreate","params":{"name":"new_material","preset":"metal"}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":24,"method":"studio.materialPreview","params":{"name":"steel"}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":25,"method":"studio.shaderGraphs","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":26,"method":"studio.shaderGraphGet","params":{"name":"../escape"}})").has("error"));
    AIVERSE_TEST_CHECK(session.previewMaterialName().empty());
    AIVERSE_TEST_CHECK(call(R"({"id":29,"method":"studio.shaderGraphCatalog","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":27,"method":"studio.shaderGraphValidate","params":{"graph":{"domain":"material","nodes":[]}}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":28,"method":"studio.shaderGraphSave","params":{"name":"test","graph":{"domain":"material","nodes":[]}}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":12,"method":"studio.lighting","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":13,"method":"studio.lightAdd","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":14,"method":"studio.lightUpdate","params":{"index":0,"key":"intensity","value":-5}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":15,"method":"studio.lightingSet","params":{"key":"storm","value":2}})").has("error"));
    // Asset listing must never expose a filesystem root without an open project.
    AIVERSE_TEST_CHECK(call(R"({"id":9,"method":"studio.assets","params":{"dir":"../../"}})").has("error"));
    bool openRejected = false;
    try { session.openProject("/nonexistent/aiverse-project"); }
    catch (const std::exception&) { openRejected = true; }
    AIVERSE_TEST_CHECK(openRejected);
    authoredEntityWorkflow();
    entityWorkflow();
    projectFileRecoveryWorkflow();
    std::cout << "studio session contract PASS\n";
}
