// Shared, presentation-side semantic RTS shortcuts. Input rules are independent of ECS and Commands.
// The UI owns text/modal capture; no raw key state may enter the deterministic simulation.
#pragma once
#include <aiverselib/gameplay/EnhancedInput.h>
#include "engine/platform/Platform.h"
#include "engine/platform/EnhancedInputPresets.h"
#include <string>
#include <vector>

namespace aiverse::client {
namespace ei = aiverse::lib::gameplay::enhanced;

class ClientInputRouting {
public:
    ClientInputRouting() {
        const auto preset=platform::enhanced_presets::rtsReference();
        for(const auto& action:preset.actions) input_.defineAction(action);
        for(const auto& item:preset.contexts){
            input_.registerContext(item.context);
            if(item.active) input_.activateContext(item.context.name,item.priority);
        }
    }

    ei::InputSystem& system() { return input_; }
    const ei::InputSystem& system() const { return input_; }

    // Call for every platform Event *after* UI capture is resolved; pass false for menus,
    // modal UI and text fields. A captured key is never left held when the UI takes focus.
    std::vector<std::string> onEvent(const platform::Event& e,bool gameplayAllowed) {
        if(e.type==platform::EventType::FocusLost || !gameplayAllowed) {
            input_.cancelAll();
            return {};
        }
        if(e.type!=platform::EventType::KeyDown && e.type!=platform::EventType::KeyUp)
            return {};
        if(e.key==platform::Key::Unknown)return {};
        input_.setControl({ei::Device::Keyboard,int(e.key)},
                          e.type==platform::EventType::KeyDown?1.0f:0.0f);
        std::vector<std::string> actions;
        for(const auto& event:input_.update(0.0f))
            if(event.phase==ei::Phase::Triggered)actions.push_back(event.action);
        return actions;
    }
    void reset() {input_.cancelAll();}
private:
    ei::InputSystem input_;
};
} // namespace aiverse::client
