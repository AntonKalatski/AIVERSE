#include "engine/platform/EnhancedInputConfig.h"
#include "engine/platform/EnhancedInputFileIO.h"
#include "engine/platform/EnhancedInputPresets.h"
#include "engine/platform/EnhancedInputProfileIO.h"
#include <chrono>
#include <filesystem>
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
    // Both built-in presets can be authored, serialized and installed with stable mapping IDs.
    for(const auto preset:{p::enhanced_presets::fpsReference(),p::enhanced_presets::rtsReference()}){
        CHECK(!preset.actions.empty()&&preset.contexts.size()==1);
        conf::Asset decoded;
        CHECK(conf::readAsset(conf::writeAsset(preset),decoded,&err));
        CHECK(decoded.contexts[0].context.mappings.size()==preset.contexts[0].context.mappings.size());
        for(std::size_t i=0;i<decoded.contexts[0].context.mappings.size();++i)
            CHECK(decoded.contexts[0].context.mappings[i].id==i+1);
        ei::InputSystem runtime;
        CHECK(conf::installAsset(runtime,decoded,&err));
        CHECK(runtime.registeredContexts().size()==1);
    }
    // Host profile persistence: disk roundtrip, invalid writes do not replace prior state.
    const auto unique=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto file=std::filesystem::temp_directory_path()/
        ("aiverse-input-profile-"+std::to_string(unique)+".json");
    conf::Profile profileOnDisk;
    CHECK(conf::saveProfileFile(file,conf::Profile{{{"Gameplay",3,{ei::Device::Keyboard,int(p::Key::Up)}}}},&err));
    CHECK(conf::loadProfileFile(file,profileOnDisk,&err));
    CHECK(profileOnDisk.rebindings.size()==1);
    CHECK(profileOnDisk.rebindings[0].control.code==int(p::Key::Up));
    conf::Profile invalidProfile=profileOnDisk;
    invalidProfile.rebindings[0].control.code=999999; // unknown keyboard
    CHECK(!conf::saveProfileFile(file,invalidProfile,&err));
    conf::Profile afterFailure;
    CHECK(conf::loadProfileFile(file,afterFailure,&err));
    CHECK(afterFailure.rebindings[0].control.code==int(p::Key::Up));
    // Project-authored assets use the same non-destructive persistence primitive.
    const auto assetFile=std::filesystem::path(file.string()+".actions");
    CHECK(conf::saveAssetFile(assetFile,asset,&err));
    conf::Asset diskAsset;
    CHECK(conf::loadAssetFile(assetFile,diskAsset,&err));
    CHECK(diskAsset.actions.size()==3 && diskAsset.contexts.size()==1);
    conf::Asset invalidAsset=diskAsset;
    invalidAsset.contexts[0].context.mappings[0].action="MissingAction";
    CHECK(!conf::saveAssetFile(assetFile,invalidAsset,&err));
    conf::Asset afterAssetFailure;
    CHECK(conf::loadAssetFile(assetFile,afterAssetFailure,&err));
    CHECK(afterAssetFailure.contexts[0].context.mappings[0].action=="Move");
    std::error_code cleanup;
    std::filesystem::remove(assetFile,cleanup);
    std::filesystem::remove(file,cleanup);
    std::cout<<"Enhanced Input config/schema/profile roundtrip passed\n";
}
