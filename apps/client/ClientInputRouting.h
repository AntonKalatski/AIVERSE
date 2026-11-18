// Shared, presentation-side semantic RTS shortcuts. Input rules are independent of ECS and Commands.
// The UI owns text/modal capture; no raw key state may enter the deterministic simulation.
#pragma once
#include <aiverselib/gameplay/EnhancedInput.h>
#include "engine/platform/Platform.h"
#include <string>
#include <vector>

namespace aiverse::client {
namespace ei = aiverse::lib::gameplay::enhanced;

class ClientInputRouting {
public:
    ClientInputRouting() {
        for (const char* name : {"AttackMove","Patrol","Repair","Reclaim","Stop","Hold","FocusCamera"})
            input_.defineAction({name,ei::ValueType::Boolean});
        ei::MappingContext gameplay{"RTS.Gameplay"};
        const auto bind=[&](const char* action,platform::Key key) {
            gameplay.bind(action,{ei::Device::Keyboard,int(key)},{},{ei::Trigger::pressed()});
        };
        bind("AttackMove",platform::Key::A);
        bind("Patrol",platform::Key::P);
        bind("Repair",platform::Key::R);
        bind("Reclaim",platform::Key::E);
        bind("Stop",platform::Key::S);
        bind("Hold",platform::Key::H);
        bind("FocusCamera",platform::Key::Space);
        input_.registerContext(std::move(gameplay));
        input_.activateContext("RTS.Gameplay",0);
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
