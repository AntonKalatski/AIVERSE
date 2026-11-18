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
            const int slot = nextTouchSlot_++;
            touches_[event.touchId] = slot;
            input_.setControl({Device::Touch, 0, slot}, 1);
            break;
        }
        case EventType::TouchEnd:
        case EventType::TouchCancel: {
            const auto it = touches_.find(event.touchId);
            if (it != touches_.end()) {
                input_.setControl({Device::Touch, 0, it->second}, 0);
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
        platform::pollGamepads();
        for (int pad = 0; pad < kMaxGamepads; ++pad) {
            const auto& state = platform::gamepad(pad);
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
        nextTouchSlot_ = 0;
    }
private:
    lib::gameplay::enhanced::InputSystem& input_;
    std::map<std::int64_t, int> touches_;
    int nextTouchSlot_ = 0;
};
} // namespace aiverse::platform
