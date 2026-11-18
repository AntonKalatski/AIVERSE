// Translates AIVERSE platform events/gamepad polls to the presentation-side Enhanced Input core.
// A hosted view may feed its own forwarded Event stream. Do not feed both host and guest events.
#pragma once
#include <aiverselib/gameplay/EnhancedInput.h>
#include "Platform.h"
#include "Gamepad.h"
#include <cstdint>
#include <map>

namespace aiverse::platform {

class EnhancedInputAdapter {
public:
    explicit EnhancedInputAdapter(lib::gameplay::enhanced::InputSystem& input) : input_(input) {}

    void onEvent(const Event& event) {
        using namespace lib::gameplay::enhanced;
        switch (event.type) {
        case EventType::KeyDown:
            if (event.key != Key::Unknown)
                input_.setControl({Device::Keyboard, int(event.key)}, 1);
            break;
        case EventType::KeyUp:
            if (event.key != Key::Unknown)
                input_.setControl({Device::Keyboard, int(event.key)}, 0);
            break;
        case EventType::MouseDown:
            input_.setControl({Device::MouseButton, event.button}, 1);
            break;
        case EventType::MouseUp:
            input_.setControl({Device::MouseButton, event.button}, 0);
            break;
        case EventType::MouseMove:
            if (event.deltaX != 0) input_.addDelta({Device::MouseAxis, 0}, event.deltaX);
            if (event.deltaY != 0) input_.addDelta({Device::MouseAxis, 1}, event.deltaY);
            break;
        case EventType::MouseWheel:
            if (event.wheelX != 0) input_.addDelta({Device::MouseWheel, 0}, event.wheelX);
            if (event.wheelY != 0) input_.addDelta({Device::MouseWheel, 1}, event.wheelY);
            break;
        case EventType::TouchBegin: {
            if (touches_.contains(event.touchId)) break; // repeated touch event is not another finger
            int slot = 0;
            for (; slot < 32; ++slot) {
                bool used = false;
                for (const auto& [id, touch] : touches_) {
                    (void)id;
                    if (touch.slot == slot) { used = true; break; }
                }
                if (!used) break;
            }
            if (slot == 32) break; // ignore excess touches; profile indices are 0..31
            touches_[event.touchId] = {slot,event.x,event.y};
            input_.setControl({Device::Touch, 0, slot}, 1);
            break;
        }
        case EventType::TouchMove: {
            const auto it = touches_.find(event.touchId);
            if (it != touches_.end()) touchMotion(it->second,event);
            break;
        }
        case EventType::TouchEnd:
        case EventType::TouchCancel: {
            const auto it = touches_.find(event.touchId);
            if (it != touches_.end()) {
                if (event.type == EventType::TouchEnd) touchMotion(it->second,event);
                input_.setControl({Device::Touch, 0, it->second.slot}, 0);
                touches_.erase(it);
            }
            break;
        }
        case EventType::FocusLost:
            focusLost();
            break;
        default:
            break; // IME/Char/text, UI and OS lifecycle events are not gameplay keys.
        }
    }

    // Call once per presentation frame. Dead gamepads send zero levels to release held actions.
    // Do not poll a gamepad backend twice in a single frame when another host owns polling.
    void pollGamepads() {
        using namespace lib::gameplay::enhanced;
        ::aiverse::platform::pollGamepads();
        for (int pad = 0; pad < kMaxGamepads; ++pad) {
            const auto& state = ::aiverse::platform::gamepad(pad);
            for (int button = 0; button < int(PadButton::Count); ++button) {
                const float value = state.connected && state.down(PadButton(button)) ? 1.0f : 0.0f;
                input_.setControl({Device::GamepadButton, button, pad}, value);
            }
            for (int axis = 0; axis < int(PadAxis::Count); ++axis) {
                const float value = state.connected ? state.axis(PadAxis(axis)) : 0.0f;
                input_.setControl({Device::GamepadAxis, axis, pad}, value);
            }
        }
    }

    void focusLost() {
        input_.cancelAll();
        touches_.clear();
    }
private:
    struct TouchEntry { int slot=0; float x=0,y=0; };
    void touchMotion(TouchEntry& touch,const Event& event) {
        using namespace lib::gameplay::enhanced;
        // Touch code 0 is finger-down; code 1/2 are per-frame relative motion.
        // Coalesced TouchMove events accumulate until InputSystem::update().
        const float dx=event.x-touch.x, dy=event.y-touch.y;
        if(dx!=0) input_.addDelta({Device::Touch,1,touch.slot},dx);
        if(dy!=0) input_.addDelta({Device::Touch,2,touch.slot},dy);
        touch.x=event.x;touch.y=event.y;
    }
    lib::gameplay::enhanced::InputSystem& input_;
    std::map<std::int64_t, TouchEntry> touches_;
};
} // namespace aiverse::platform
