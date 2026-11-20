// Stage 108 deferred: generic material inheritance and animated graph
// source policy are not defined in RTS or FPS gameplay code.
#include "engine/view/AppearanceAuthoringService.h"
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main() {
    using namespace aiverse;
    using view::AppearanceAuthoringService;
    const Json library=Json::parse(R"({
        "base":{"color":[0.2,0.3,0.4,1],"metallic":0.4,
                "roughness":0.9,"alpha":"opaque"},
        "paint":{"base":"base","roughness":0.25,"team":"tint"}
    })");
    std::vector<std::string> warnings;
    auto warn=[&](const std::string& message){warnings.push_back(message);};
    Json variant=Json::object();
    variant.set("use",Json::string("paint"));
    variant.set("roughness",Json::number(0.5));
    variant.set("texture",Json::string("textures/paint.png"));
    const auto flat=AppearanceAuthoringService::resolveMaterial(
        library,variant,warn);
    assert(flat.getStr("team")=="tint");
    assert(flat["metallic"].toDouble()==0.4);
    assert(flat["roughness"].toDouble()==0.5);
    render::Material material;
    material.name="body";
    int textureCalls=0;
    AppearanceAuthoringService::applyMaterial(
        material,library,variant,
        [&](const std::string& path) {
            assert(path=="textures/paint.png");
            ++textureCalls;
            return 7;
        },warn);
    assert(material.name=="body");
    assert(material.baseTex==7);
    assert(textureCalls==1);
    assert(material.roughness==0.5f);
    assert(material.team==render::TeamMode::Tint);
    assert(warnings.empty());
    const auto missing=AppearanceAuthoringService::resolveMaterial(
        library,Json::string("not_found"),warn);
    assert(missing.isObject());
    assert(!warnings.empty());

    render::Model model;
    bool failed=false;
    try {
        AppearanceAuthoringService::compileGraph(model,
            Json::string("graphs/missing.json"),
            [](const std::string&,std::string&) {return false;});
    }catch(const std::runtime_error&) {failed=true;}
    assert(failed);
}
