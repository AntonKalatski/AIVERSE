// Shared, presentation-side semantic RTS shortcuts. Input rules are independent of ECS and Commands.
// The UI owns text/modal capture; no raw key state may enter the deterministic simulation.
#pragma once
#include <aiverselib/gameplay/EnhancedInput.h>
#include "engine/platform/Platform.h"
#include "engine/platform/EnhancedInputAdapter.h"
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
            adapter_.focusLost();
            return {};
        }
        // Route all pointer/keyboard events after UI capture so authored semantic
        // actions can rebind clicks and zoom without altering selection gestures.
        adapter_.onEvent(e);
        std::vector<std::string> actions;
        for(const auto& event:input_.update(0.0f))
            if(event.phase==ei::Phase::Triggered)actions.push_back(event.action);
        return actions;
    }
    // Top-down possession is a higher-priority, optional context.
    // A project-specific action asset can omit it; in that case legacy buttons
    // remain usable but semantic customization requires a matching Action.
    void setTopDownEnabled(bool enabled) {
        const char* context="RTS.TopDown";
        if(!input_.context(context))return;
        if(enabled && !input_.isContextActive(context))
            input_.activateContext(context,100);
        else if(!enabled && input_.isContextActive(context))
            input_.deactivateContext(context);
    }
    // The world may still need pointer coordinates/drag sequences, but the
    // initiating physical mouse button must respect project-owned Select and
    // Command Action mappings. Return -1 when an unassigned button is pressed.
    int logicalMouseButton(int physical) const {
        const auto* ctx=input_.context("RTS.Pointer");
        if(!ctx || !input_.isContextActive("RTS.Pointer"))return physical;
        bool select=false,command=false;
        for(const auto& mapping:ctx->mappings) {
            if(mapping.control.device!=ei::Device::MouseButton ||
               mapping.control.code!=physical || mapping.control.deviceIndex!=0)continue;
            if(mapping.action=="Select")select=true;
            if(mapping.action=="Command")command=true;
        }
        if(select)return 0; // explicit tie-break; conflicts() reports duplicates
        if(command)return 1;
        if(physical==2)return 2; // independent camera pan
        return -1;
    }
    void reset() {adapter_.focusLost();}
private:
    ei::InputSystem input_;
    platform::EnhancedInputAdapter adapter_{input_};
};
} // namespace aiverse::client
