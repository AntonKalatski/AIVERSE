#include "engine/platform/EnhancedInputAdapter.h"
#include "engine/platform/EnhancedInputUsers.h"
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
namespace p = aiverse::platform;
namespace in = aiverse::lib::gameplay::enhanced;
#define CHECK(x) do { if(!(x)) { std::cerr << "Input adapter failed " << __LINE__ << ": " #x "\n"; std::abort(); } } while(false)

namespace aiverse::platform::detail {
void pollGamepadBackend(GamepadState (&pads)[kMaxGamepads]) { (void)pads; }
}
static void fakeGamepads(p::GamepadState (&pads)[p::kMaxGamepads]) {
    pads[0].connected = true;
    pads[0].axes[int(p::PadAxis::LX)] = 0.65f;
    pads[0].buttons[int(p::PadButton::A)] = true;
}
static bool has(std::span<const in::ActionEvent> events, std::string_view name, in::Phase phase) {
    for (const auto& e : events) if (e.action == name && e.phase == phase) return true;
    return false;
}
int main() {
    in::InputSystem actions;
    actions.defineAction({"Walk",in::ValueType::Boolean});
    actions.defineAction({"Click",in::ValueType::Boolean});
    actions.defineAction({"MouseX",in::ValueType::Axis1D});
    actions.defineAction({"Wheel",in::ValueType::Axis1D});
    actions.defineAction({"Stick",in::ValueType::Axis1D});
    actions.defineAction({"PadJump",in::ValueType::Boolean});
    actions.defineAction({"Touch",in::ValueType::Boolean});
    actions.defineAction({"SwipeX",in::ValueType::Axis1D});
    actions.defineAction({"Pinch",in::ValueType::Axis1D});
    actions.defineAction({"TwoFingerPanX",in::ValueType::Axis1D});
    in::MappingContext controls{"Test"};
    controls.bind("Walk",{in::Device::Keyboard,int(p::Key::W)});
    controls.bind("Click",{in::Device::MouseButton,0});
    controls.bind("MouseX",{in::Device::MouseAxis,0});
    controls.bind("Wheel",{in::Device::MouseWheel,1});
    controls.bind("Stick",{in::Device::GamepadAxis,int(p::PadAxis::LX)});
    controls.bind("PadJump",{in::Device::GamepadButton,int(p::PadButton::A)});
    controls.bind("Touch",{in::Device::Touch,0});
    controls.bind("SwipeX",{in::Device::Touch,1});
    controls.bind("Pinch",{in::Device::Touch,3});
    controls.bind("TwoFingerPanX",{in::Device::Touch,4});
    CHECK(actions.registerContext(controls));
    CHECK(actions.activateContext("Test"));
    p::EnhancedInputAdapter adapter(actions);
    p::Event e;
    e.type=p::EventType::KeyDown; e.key=p::Key::W;
    adapter.onEvent(e);
    CHECK(has(actions.update(0.016f),"Walk",in::Phase::Started));
    e.type=p::EventType::KeyUp; adapter.onEvent(e);
    CHECK(has(actions.update(0.016f),"Walk",in::Phase::Completed));
    e={};e.type=p::EventType::MouseDown;e.button=0;adapter.onEvent(e);
    CHECK(has(actions.update(0.016f),"Click",in::Phase::Triggered));
    e={};e.type=p::EventType::MouseMove;e.deltaX=9.5f;adapter.onEvent(e);
    CHECK(has(actions.update(0.016f),"MouseX",in::Phase::Triggered));
    CHECK(actions.state("MouseX")->value.x==9.5f);
    CHECK(has(actions.update(0.016f),"MouseX",in::Phase::Completed));
    e={};e.type=p::EventType::MouseWheel;e.wheelY=-2;adapter.onEvent(e);
    CHECK(has(actions.update(0.016f),"Wheel",in::Phase::Triggered));
    CHECK(actions.state("Wheel")->value.x==-2);
    e={};e.type=p::EventType::TouchBegin;e.touchId=42;adapter.onEvent(e);
    CHECK(has(actions.update(0.016f),"Touch",in::Phase::Triggered));
    // A touch delta is independent of the held finger and resets after a frame.
    e.type=p::EventType::TouchMove;e.x=9;e.y=3;adapter.onEvent(e);
    e.x=14;e.y=3;adapter.onEvent(e);
    CHECK(has(actions.update(0.016f),"SwipeX",in::Phase::Triggered));
    CHECK(actions.state("SwipeX")->value.x==14);
    CHECK(has(actions.update(0.016f),"SwipeX",in::Phase::Completed));
    e.type=p::EventType::TouchEnd;adapter.onEvent(e);
    CHECK(has(actions.update(0.016f),"Touch",in::Phase::Completed));
    // Reuse a freed touch slot; duplicate begin must not create another slot.
    e={};e.type=p::EventType::TouchBegin;e.touchId=80;adapter.onEvent(e);adapter.onEvent(e);
    CHECK(has(actions.update(0.016f),"Touch",in::Phase::Started));
    e.type=p::EventType::TouchCancel;adapter.onEvent(e);
    CHECK(has(actions.update(0.016f),"Touch",in::Phase::Completed));

    // Pinch / two-finger pan use relative deltas and reset when fingers change.
    e={};e.type=p::EventType::TouchBegin;e.touchId=100;e.x=0;e.y=0;adapter.onEvent(e);
    e.touchId=101;e.x=10;adapter.onEvent(e);
    CHECK(!has(actions.update(0.016f),"Pinch",in::Phase::Triggered));
    e.type=p::EventType::TouchMove;e.touchId=100;e.x=-5;adapter.onEvent(e);
    CHECK(has(actions.update(0.016f),"Pinch",in::Phase::Triggered));
    CHECK(actions.state("Pinch")->value.x==5);
    CHECK(actions.state("TwoFingerPanX")->value.x==-2.5f);
    CHECK(has(actions.update(0.016f),"Pinch",in::Phase::Completed));
    e.type=p::EventType::TouchEnd;adapter.onEvent(e);
    e={};e.type=p::EventType::TouchBegin;e.touchId=102;e.x=20;adapter.onEvent(e);
    CHECK(!has(actions.update(0.016f),"Pinch",in::Phase::Triggered));
    e.type=p::EventType::FocusLost;adapter.onEvent(e);
    CHECK(!has(actions.update(0.016f),"Pinch",in::Phase::Triggered));

    p::detail::setGamepadBackend(&fakeGamepads);
    adapter.pollGamepads();
    CHECK(has(actions.update(0.016f),"Stick",in::Phase::Triggered));
    CHECK(actions.state("Stick")->value.x==0.65f);
    CHECK(has(actions.events(),"PadJump",in::Phase::Triggered));
    p::detail::setGamepadBackend(nullptr);
    // No real hardware in this unit test: use the zeroed fallback.
    e={};e.type=p::EventType::FocusLost;adapter.onEvent(e);
    CHECK(has(actions.events(),"PadJump",in::Phase::Canceled));
    CHECK(!actions.state("Stick"));
    // Local multiplayer: events and controllers belong to only one logical user.
    p::EnhancedInputUsers users;
    in::InputSystem u0,u1;
    CHECK(u0.defineAction({"Walk",in::ValueType::Boolean}));
    CHECK(u1.defineAction({"PadJump",in::ValueType::Boolean}));
    in::MappingContext kb{"Keyboard"};
    kb.bind("Walk",{in::Device::Keyboard,int(p::Key::W)});
    in::MappingContext gamepad{"Gamepad"};
    gamepad.bind("PadJump",{in::Device::GamepadButton,int(p::PadButton::A)});
    CHECK(u0.registerContext(kb)&&u0.activateContext("Keyboard"));
    CHECK(u1.registerContext(gamepad)&&u1.activateContext("Gamepad"));
    CHECK(users.addUser(0,std::move(u0)));
    CHECK(users.addUser(1,std::move(u1)));
    CHECK(users.setKeyboardOwner(0));
    CHECK(users.assignGamepad(0,1));
    e={};e.type=p::EventType::KeyDown;e.key=p::Key::W;
    users.onEvent(e);
    CHECK(has(users.update(0,0.016f),"Walk",in::Phase::Triggered));
    CHECK(users.update(1,0.016f).empty());
    p::detail::setGamepadBackend(&fakeGamepads);
    users.pollGamepads();
    CHECK(has(users.update(1,0.016f),"PadJump",in::Phase::Triggered));
    CHECK(!users.input(0)->state("PadJump"));
    CHECK(!users.assignGamepad(1,1)); // one physical pad per user
    users.unassignGamepad(0);
    CHECK(has(users.input(1)->events(),"PadJump",in::Phase::Canceled));
    e.type=p::EventType::FocusLost;
    users.onEvent(e);
    CHECK(!users.input(0)->state("Walk"));
    p::detail::setGamepadBackend(nullptr);

    std::cout << "Enhanced Input platform adapter passed\n";
}
