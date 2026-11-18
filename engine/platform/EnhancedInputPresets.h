// Built-in, editable Enhanced Input examples. Do not duplicate the default key layout in clients.
// Use this as a starting project asset; gameplay rules remain in Commands/ECS systems.
#pragma once
#include "EnhancedInputConfig.h"
#include <cstdint>
#include <utility>

namespace aiverse::platform::enhanced_presets {
namespace ei = aiverse::lib::gameplay::enhanced;
namespace ecfg = aiverse::platform::enhanced_config;

inline ecfg::Asset fpsReference() {
    ecfg::Asset result;
        using namespace ei;
        using K=platform::Key;
        auto key=[](K k){return Control{Device::Keyboard,int(k)};};
        auto pad=[](platform::PadButton b){return Control{Device::GamepadButton,int(b)};};
        auto axis=[](platform::PadAxis a){return Control{Device::GamepadAxis,int(a)};};
        auto define=[&](const char* name,ValueType type=ValueType::Boolean) {
            result.actions.push_back({name,type});
        };
        define("Move",ValueType::Axis2D);
        define("Look",ValueType::Axis2D);
        define("LookStick",ValueType::Axis2D);
        for(const char* name : {"Fire","Aim","Jump","Sprint","Crouch","Reload","Melee","Block",
                                 "LockOn","Prone","ShowInventory","Frag","Smoke","Flash","Heal",
                                 "Interact","Drop","Plant","Vault","Buy","Weapon1","Weapon2",
                                 "Weapon3","Weapon4","Attach1","Attach2","Attach3"}) define(name);
        MappingContext fpsContext{"FPS"};
        fpsContext.bind("Move",key(K::W),{Modifier::swapXY()});
        fpsContext.bind("Move",key(K::S),{Modifier::swapXY(),Modifier::negate()});
        fpsContext.bind("Move",key(K::A),{Modifier::negate()});
        fpsContext.bind("Move",key(K::D));
        fpsContext.bind("Move",axis(platform::PadAxis::LX),{Modifier::deadZone(0.13f)});
        fpsContext.bind("Move",axis(platform::PadAxis::LY),{Modifier::deadZone(0.13f),Modifier::swapXY(),Modifier::negate()});
        fpsContext.bind("Look",{Device::MouseAxis,0});
        fpsContext.bind("Look",{Device::MouseAxis,1},{Modifier::swapXY()});
        fpsContext.bind("LookStick",axis(platform::PadAxis::RX),{Modifier::deadZone(0.13f)});
        fpsContext.bind("LookStick",axis(platform::PadAxis::RY),{Modifier::deadZone(0.13f),Modifier::swapXY()});
        fpsContext.bind("Fire",{Device::MouseButton,0});
        fpsContext.bind("Fire",axis(platform::PadAxis::RT),{Modifier::deadZone(0.1f)});
        fpsContext.bind("Aim",{Device::MouseButton,1});
        fpsContext.bind("Aim",axis(platform::PadAxis::LT),{Modifier::deadZone(0.1f)});
        fpsContext.bind("Jump",key(K::Space),{},{Trigger::pressed()});
        fpsContext.bind("Jump",pad(platform::PadButton::A),{},{Trigger::pressed()});
        fpsContext.bind("Sprint",key(K::LeftShift));
        fpsContext.bind("Sprint",pad(platform::PadButton::LStick));
        fpsContext.bind("Crouch",key(K::LeftCtrl));
        fpsContext.bind("Crouch",pad(platform::PadButton::B));
        fpsContext.bind("Reload",key(K::R));
        fpsContext.bind("Reload",pad(platform::PadButton::X));
        fpsContext.bind("Melee",key(K::Z));
        fpsContext.bind("Block",key(K::LeftAlt));
        fpsContext.bind("LockOn",key(K::T));
        fpsContext.bind("ShowInventory",key(K::Tab));
        const std::pair<const char*,K> oneShots[]={
            {"Prone",K::V},{"Frag",K::G},{"Smoke",K::X},{"Flash",K::C},{"Heal",K::H},
            {"Interact",K::E},{"Drop",K::Q},{"Plant",K::P},{"Vault",K::F},{"Buy",K::B},
            {"Weapon1",K::Num1},{"Weapon2",K::Num2},{"Weapon3",K::Num3},{"Weapon4",K::Num4},
            {"Attach1",K::F1},{"Attach2",K::F2},{"Attach3",K::F3}
        };
        for(const auto& [name,button]:oneShots)
            fpsContext.bind(name,key(button),{},{Trigger::pressed()});
        fpsContext.bind("Interact",pad(platform::PadButton::Y),{},{Trigger::pressed()});
        std::uint64_t id=1;
        for(auto& mapping:fpsContext.mappings) mapping.id=id++;
        result.contexts.push_back({std::move(fpsContext),0,true});
    return result;
}

inline ecfg::Asset rtsReference() {
    ecfg::Asset result;
    for(const char* action: {"AttackMove","Patrol","Repair","Reclaim","Stop","Hold","FocusCamera"})
        result.actions.push_back({action,ei::ValueType::Boolean});
    ei::MappingContext gameplay{"RTS.Gameplay"};
    const auto bind=[&](const char* action,Key key) {
        gameplay.bind(action,{ei::Device::Keyboard,int(key)},{},{ei::Trigger::pressed()});
    };
    bind("AttackMove",Key::A);
    bind("Patrol",Key::P);
    bind("Repair",Key::R);
    bind("Reclaim",Key::E);
    bind("Stop",Key::S);
    bind("Hold",Key::H);
    bind("FocusCamera",Key::Space);
    std::uint64_t id=1;
    for(auto& mapping:gameplay.mappings) mapping.id=id++;
    result.contexts.push_back({std::move(gameplay),0,true});
    // Keep published RTS.Gameplay mapping IDs unchanged. Non-consuming camera
    // bindings live in their own higher-ordered context so A/P/R/E/S/H hotkeys
    // still fire, while WASD/arrows can be rebound independently.
    result.actions.push_back({"CameraPan",ei::ValueType::Axis2D});
    result.actions.push_back({"IdleCameraPan",ei::ValueType::Axis2D});
    ei::MappingContext camera{"RTS.Camera"};
    const auto cameraBind=[&](const char* action,Key key,
                              std::vector<ei::Modifier> mods={}) {
        camera.bind(action,{ei::Device::Keyboard,int(key)},std::move(mods),{},false);
    };
    cameraBind("CameraPan",Key::Up,{ei::Modifier::swapXY()});
    cameraBind("CameraPan",Key::Down,{ei::Modifier::swapXY(),ei::Modifier::negate()});
    cameraBind("CameraPan",Key::Left,{ei::Modifier::negate()});
    cameraBind("CameraPan",Key::Right);
    cameraBind("IdleCameraPan",Key::W,{ei::Modifier::swapXY()});
    cameraBind("IdleCameraPan",Key::S,{ei::Modifier::swapXY(),ei::Modifier::negate()});
    cameraBind("IdleCameraPan",Key::A,{ei::Modifier::negate()});
    cameraBind("IdleCameraPan",Key::D);
    id=1;
    for(auto& mapping:camera.mappings)mapping.id=id++;
    result.contexts.push_back({std::move(camera),0,true});
    return result;
}
} // namespace aiverse::platform::enhanced_presets
