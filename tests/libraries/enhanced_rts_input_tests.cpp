#include "apps/client/ClientInputRouting.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace p = aiverse::platform;
namespace ei = aiverse::lib::gameplay::enhanced;
using aiverse::client::ClientInputRouting;
#define CHECK(x) do { if(!(x)){std::cerr<<"RTS input test line "<<__LINE__<<": " #x "\n";std::abort();} } while(false)
static bool contains(const std::vector<std::string>& actions,const char* key) {
    return std::find(actions.begin(),actions.end(),key)!=actions.end();
}
static p::Event key(p::Key keyCode,bool down=true) {
    p::Event e;
    e.type=down?p::EventType::KeyDown:p::EventType::KeyUp;
    e.key=keyCode;
    return e;
}
int main() {
    ClientInputRouting route;
    CHECK(contains(route.onEvent(key(p::Key::A),true),"AttackMove"));
    CHECK(route.system().state("IdleCameraPan")->value.x==-1.0f);
    CHECK(!contains(route.onEvent(key(p::Key::A),true),"AttackMove")); // key repeat
    CHECK(!contains(route.onEvent(key(p::Key::A,false),true),"AttackMove"));
    CHECK(contains(route.onEvent(key(p::Key::P),true),"Patrol"));
    route.onEvent(key(p::Key::P,false),true);
    CHECK(contains(route.onEvent(key(p::Key::R),true),"Repair"));
    route.onEvent(key(p::Key::R,false),true);
    CHECK(contains(route.onEvent(key(p::Key::E),true),"Reclaim"));
    route.onEvent(key(p::Key::E,false),true);
    CHECK(contains(route.onEvent(key(p::Key::S),true),"Stop"));
    route.onEvent(key(p::Key::S,false),true);
    CHECK(contains(route.onEvent(key(p::Key::H),true),"Hold"));
    route.onEvent(key(p::Key::H,false),true);
    CHECK(contains(route.onEvent(key(p::Key::Space),true),"FocusCamera"));
    route.onEvent(key(p::Key::Space,false),true);
    CHECK(route.system().registeredContexts().size()==3);
    CHECK(!route.system().state("CameraPan")->active);
    route.onEvent(key(p::Key::Up),true);
    CHECK(route.system().state("CameraPan")->value.y==1.0f);
    route.onEvent(key(p::Key::Right),true);
    CHECK(route.system().state("CameraPan")->value.x==1.0f);
    route.onEvent(key(p::Key::Up,false),true);
    route.onEvent(key(p::Key::Right,false),true);

    p::Event wheel;wheel.type=p::EventType::MouseWheel;wheel.wheelY=2.0f;
    auto zoom=route.onEvent(wheel,true);
    CHECK(contains(zoom,"CameraZoom"));
    CHECK(route.system().state("CameraZoom")->value.x==2.0f);
    CHECK(route.onEvent(wheel,false).empty()); // UI owns this wheel
    p::Event click;click.type=p::EventType::MouseDown;click.button=0;
    CHECK(contains(route.onEvent(click,true),"Select"));
    click.type=p::EventType::MouseUp;route.onEvent(click,true);
    // An active UI/modal context must prevent keys from leaking into gameplay.
    CHECK(route.onEvent(key(p::Key::A),false).empty());
    CHECK(contains(route.onEvent(key(p::Key::A),true),"AttackMove"));
    p::Event lost;lost.type=p::EventType::FocusLost;
    CHECK(route.onEvent(lost,false).empty());
    CHECK(contains(route.onEvent(key(p::Key::A),true),"AttackMove"));
    route.onEvent(key(p::Key::A,false),true);
    // Higher-priority context wins on the same control, as in Unreal Mapping Contexts.
    CHECK(route.system().defineAction({"ModalAccept",ei::ValueType::Boolean}));
    ei::MappingContext modal{"RTS.Modal"};
    modal.bind("ModalAccept",{ei::Device::Keyboard,int(p::Key::A)},{},{ei::Trigger::pressed()});
    CHECK(route.system().registerContext(modal));
    CHECK(route.system().activateContext("RTS.Modal",100));
    auto triggered=route.onEvent(key(p::Key::A),true);
    CHECK(contains(triggered,"ModalAccept"));
    CHECK(!contains(triggered,"AttackMove"));
    route.onEvent(key(p::Key::A,false),true);
    route.system().deactivateContext("RTS.Modal");
    CHECK(contains(route.onEvent(key(p::Key::A),true),"AttackMove"));
    std::cout<<"RTS semantic input / context routing passed\n";
}
