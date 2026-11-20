#include "apps/studio-native/StudioSession.h"
#include "engine/render/RenderLightingSettings.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace aiverse;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char** argv) {
    check(argc==2,"external fixture directory required");
    const std::filesystem::path root=argv[1];
    for(const char* genre:{"rts","fps"}) {
        const auto dir=root/"templates"/genre;
        std::filesystem::create_directories(dir);
        std::ofstream(dir/"game.json")<<"{\"name\":\"Lighting fixture\",\"module\":\""<<genre<<"\",\"maps\":[{\"file\":\"map.json\"}]}";
        std::ofstream(dir/"rules.json")<<R"({"fps":{},"types":{"unit":{"health":{"max":100}}}})";
        std::ofstream(dir/"map.json")<<(std::string(genre)=="fps"?R"({"name":"Fixture","colliders":[],"spawns":[[0,1,0]],"players":[{"name":"P1","team":1}]})":R"({"name":"Fixture","size":[8,8]})");
        studio::StudioSession session(root.string(),dir.string());
        auto call=[&](const char* method,Json params=Json::object()){
            Json req=Json::object();req.set("id",Json::number(int64_t(1)));req.set("method",Json::string(method));req.set("params",params);
            return session.dispatch(req);
        };
        auto get=[&](){auto r=call("studio.renderLighting");check(!r.has("error"),"get rejected");return r["result"];};
        const auto before=get();check(before.getStr("indirect")=="off","legacy default");
        const auto changed=call("studio.renderLightingSet",Json::parse(R"({"patch":{"indirect":"auto","direct":"luma","probes":6}})"));
        if(changed.has("error"))throw std::runtime_error(changed.dump());
        const auto after=get();check(after.getStr("direct")=="luma","set missing");
        const auto revision=session.workspace().map.revision;
        for(const char* patch:{R"({"patch":{"probes":3}})",R"({"patch":{"indirect":"bad"}})",R"({"patch":{"unknown":1}})",R"({"patch":{"samples":8.5}})",R"({"patch":{"version":2}})"}) {
            check(call("studio.renderLightingSet",Json::parse(patch)).has("error"),"invalid patch accepted");
            check(get()==after && session.workspace().map.revision==revision,"invalid mutation");
        }
        check(!call("studio.undo").has("error") && get()==before,"undo");
        check(!call("studio.redo").has("error") && get()==after,"redo");
        check(!call("studio.save").has("error"),"save");
        studio::StudioSession reopened(root.string(),dir.string());
        Json req=Json::parse(R"({"id":1,"method":"studio.renderLighting","params":{}})");
        check(reopened.dispatch(req)["result"]==after,"reopen");
        check(Json::parseFile((dir/"map.json").string())["renderLighting"]==after,"disk serialization");
    }
    std::cout<<"Studio render lighting persistence PASS\n";
}
