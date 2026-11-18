#include "engine/platform/EnhancedInputConfig.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
namespace p=aiverse::platform;
namespace ei=aiverse::lib::gameplay::enhanced;
namespace conf=aiverse::platform::enhanced_config;
#define CHECK(x) do { if(!(x)){ std::cerr<<"Enhanced Input JSON failed line "<<__LINE__<<": " #x "\n";std::abort();} } while(false)

int main() {
    const auto data=aiverse::Json::parse(R"JSON({
       "version":1,
       "actions":[
          {"name":"Move","type":"axis2D","modifiers":[{"type":"normalize"}]},
          {"name":"Aim","type":"bool"},
          {"name":"Fire","type":"bool"}
       ],
       "contexts":[{
          "name":"Gameplay","priority":10,"active":true,
          "mappings":[
             {"id":3,"action":"Move","control":{"device":"keyboard","code":"W"},
              "modifiers":[{"type":"swizzle","order":[1,0,2]}]},
             {"id":4,"action":"Move","control":{"device":"gamepadAxis","code":"LY"},
              "modifiers":[{"type":"deadzone","threshold":0.13},{"type":"negate"},{"type":"swizzle","order":[1,0,2]}]},
             {"id":5,"action":"Aim","control":{"device":"mouseButton","code":"right"}},
             {"id":6,"action":"Fire","control":{"device":"mouseButton","code":"left"},
              "triggers":[{"type":"pressed"},{"type":"chord","action":"Aim"}]}
          ]
       }]
    })JSON");
    std::string err;
    conf::Asset asset;
    CHECK(conf::readAsset(data,asset,&err));
    CHECK(asset.actions.size()==3&&asset.contexts.size()==1);
    CHECK(asset.contexts[0].context.mappings[0].control.code==int(p::Key::W));
    CHECK(asset.contexts[0].context.mappings[1].modifiers[0].threshold>0.12f);
    const auto serialized=conf::writeAsset(asset);
    conf::Asset duplicate;
    CHECK(conf::readAsset(aiverse::Json::parse(serialized.dump()),duplicate,&err));
    CHECK(duplicate.contexts[0].context.mappings[0].id==3);
    ei::InputSystem input;
    CHECK(conf::installAsset(input,duplicate,&err));
    input.setControl({ei::Device::Keyboard,int(p::Key::W)},1);
    input.update(0.016f);
    CHECK(input.state("Move")&&input.state("Move")->value.y==1);
    input.setControl({ei::Device::Keyboard,int(p::Key::W)},0);
    input.update(0.016f);
    CHECK(!input.state("Move")->active);
    input.setControl({ei::Device::MouseButton,0},1);
    input.update(0.016f);
    CHECK(!input.state("Fire")->triggered);
    input.setControl({ei::Device::MouseButton,1},1);
    input.setControl({ei::Device::MouseButton,0},0);
    input.update(0.016f);
    input.setControl({ei::Device::MouseButton,0},1);
    input.update(0.016f);
    CHECK(input.state("Fire")->triggered);

    const auto profileData=aiverse::Json::parse(R"JSON({
       "version":1,"rebindings":[
         {"context":"Gameplay","id":3,"control":{"device":"keyboard","code":"Up"}}
       ]
    })JSON");
    conf::Profile profile;
    CHECK(conf::readProfile(profileData,profile,&err));
    CHECK(conf::applyProfile(input,profile,&err));
    CHECK(input.context("Gameplay")->mappings[0].control.code==int(p::Key::Up));
    CHECK(conf::writeProfile(profile).dump()==profileData.dump());

    const auto bad=aiverse::Json::parse(R"({"version":2,"actions":[],"contexts":[]})");
    CHECK(!conf::readAsset(bad,duplicate,&err));
    CHECK(!err.empty());
    const auto dupBindings=aiverse::Json::parse(R"JSON({
        "version":1,"rebindings":[
          {"context":"Gameplay","id":1,"control":{"device":"keyboard","code":"A"}},
          {"context":"Gameplay","id":1,"control":{"device":"keyboard","code":"B"}}
        ]
    })JSON");
    CHECK(!conf::readProfile(dupBindings,profile,&err));
    const auto unknown=aiverse::Json::parse(R"JSON({
        "version":1,"rebindings":[
          {"context":"Gameplay","id":999,"control":{"device":"keyboard","code":"B"}}
        ]
    })JSON");
    CHECK(conf::readProfile(unknown,profile,&err));
    const int before=input.context("Gameplay")->mappings[0].control.code;
    CHECK(!conf::applyProfile(input,profile,&err));
    CHECK(input.context("Gameplay")->mappings[0].control.code==before);
    const auto invalidCode=aiverse::Json::parse(R"JSON({
        "version":1,"rebindings":[
          {"context":"Gameplay","id":3,"control":{"device":"gamepadAxis","code":"NotAnAxis"}}
        ]
    })JSON");
    CHECK(!conf::readProfile(invalidCode,profile,&err));
    std::cout<<"Enhanced Input config/schema/profile roundtrip passed\n";
}
