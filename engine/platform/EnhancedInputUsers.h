// Multi-local-player presentation routing for AIVERSE Enhanced Input.
// Each local player owns an independent InputSystem and input lifecycles.
// One platform event or gamepad sample is delivered only to its assigned owner.
// No game genre, simulation state, network command or ECS dependencies.
#pragma once

#include "EnhancedInputAdapter.h"
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace aiverse::platform {

class EnhancedInputUsers {
public:
    using UserId = std::uint32_t;
    using Core = lib::gameplay::enhanced::InputSystem;

    bool addUser(UserId id, Core scheme = {}) {
        if(users_.contains(id))return false;
        auto user=std::make_unique<User>();
        user->input=std::move(scheme);
        users_.emplace(id,std::move(user));
        return true;
    }
    bool removeUser(UserId id) {
        if(!users_.contains(id))return false;
        users_.at(id)->adapter.focusLost();
        for(auto it=gamepadOwner_.begin();it!=gamepadOwner_.end();)
            if(it->second==id)it=gamepadOwner_.erase(it);
            else ++it;
        if(keyboardOwner_==id)keyboardOwner_=kUnassigned;
        if(pointerOwner_==id)pointerOwner_=kUnassigned;
        if(touchOwner_==id)touchOwner_=kUnassigned;
        users_.erase(id);
        return true;
    }

    Core* input(UserId id) {
        auto it=users_.find(id);
        return it==users_.end()?nullptr:&it->second->input;
    }
    const Core* input(UserId id) const {
        auto it=users_.find(id);
        return it==users_.end()?nullptr:&it->second->input;
    }

    bool setKeyboardOwner(UserId id) { return moveOwner(keyboardOwner_,id); }
    bool setPointerOwner(UserId id) { return moveOwner(pointerOwner_,id); }
    bool setTouchOwner(UserId id) { return moveOwner(touchOwner_,id); }
    UserId keyboardOwner() const { return keyboardOwner_; }
    UserId pointerOwner() const { return pointerOwner_; }
    UserId touchOwner() const { return touchOwner_; }

    // Each physical gamepad may belong to one local user; each user accepts one
    // gamepad, mapped to logical deviceIndex 0 in that user's project scheme.
    // This makes standard FPS/RTS presets reusable in split-screen games.
    bool assignGamepad(int pad,UserId id) {
        if(pad<0 || pad>=kMaxGamepads || !users_.contains(id))return false;
        for(const auto& [assigned,owner]:gamepadOwner_)
            if(owner==id && assigned!=pad)return false;
        auto previous=gamepadOwner_.find(pad);
        if(previous!=gamepadOwner_.end()){
            if(previous->second==id)return true;
            users_.at(previous->second)->adapter.focusLost();
        }
        users_.at(id)->adapter.focusLost();
        gamepadOwner_[pad]=id;
        return true;
    }
    void unassignGamepad(int pad) {
        auto it=gamepadOwner_.find(pad);
        if(it==gamepadOwner_.end())return;
        if(auto player=users_.find(it->second);player!=users_.end())
            player->second->adapter.focusLost();
        gamepadOwner_.erase(it);
    }
    UserId gamepadOwner(int pad) const {
        const auto it=gamepadOwner_.find(pad);
        return it==gamepadOwner_.end()?kUnassigned:it->second;
    }

    // Call for OS events after UI capture resolution. The target owner's held
    // controls are cancelled when a modal UI captures input, avoiding stuck keys.
    void onEvent(const Event& e,bool gameplayAllowed=true) {
        if(e.type==EventType::FocusLost){cancelAll();return;}
        UserId owner=kUnassigned;
        switch(e.type) {
        case EventType::KeyDown:case EventType::KeyUp:owner=keyboardOwner_;break;
        case EventType::MouseDown:case EventType::MouseUp:
        case EventType::MouseMove:case EventType::MouseWheel:owner=pointerOwner_;break;
        case EventType::TouchBegin:case EventType::TouchMove:
        case EventType::TouchEnd:case EventType::TouchCancel:owner=touchOwner_;break;
        default:return;
        }
        const auto it=users_.find(owner);
        if(it==users_.end())return;
        if(gameplayAllowed)it->second->adapter.onEvent(e);
        else it->second->adapter.focusLost();
    }

    // Poll the physical backend ONCE per frame; unlike one adapter per user,
    // this cannot accidentally broadcast every controller to every player.
    void pollGamepads() {
        using namespace lib::gameplay::enhanced;
        ::aiverse::platform::pollGamepads();
        for(const auto& [pad,owner]:gamepadOwner_) {
            auto user=users_.find(owner);
            if(user==users_.end())continue;
            const auto& state=::aiverse::platform::gamepad(pad);
            auto& input=user->second->input;
            for(int button=0;button<int(PadButton::Count);++button)
                input.setControl({Device::GamepadButton,button,0},
                    state.connected && state.down(PadButton(button))?1.0f:0.0f);
            for(int axis=0;axis<int(PadAxis::Count);++axis)
                input.setControl({Device::GamepadAxis,axis,0},
                    state.connected?state.axis(PadAxis(axis)):0.0f);
        }
    }

    // Returned events are owned by each user's InputSystem until that user
    // receives another update/cancel. Call after onEvent/pollGamepads.
    std::span<const lib::gameplay::enhanced::ActionEvent> update(UserId id,float dt) {
        auto* system=input(id);
        return system?system->update(dt):std::span<const lib::gameplay::enhanced::ActionEvent>{};
    }
    void cancelAll() {
        for(auto& [id,user]:users_){(void)id;user->adapter.focusLost();}
    }

    static constexpr UserId kUnassigned=~UserId{0};
private:
    struct User {
        Core input;
        EnhancedInputAdapter adapter{input};
    };
    bool moveOwner(UserId& target,UserId id) {
        if(!users_.contains(id))return false;
        if(target==id)return true;
        if(auto old=users_.find(target);old!=users_.end())old->second->adapter.focusLost();
        users_.at(id)->adapter.focusLost();
        target=id;
        return true;
    }

    std::map<UserId,std::unique_ptr<User>> users_;
    std::map<int,UserId> gamepadOwner_;
    UserId keyboardOwner_=kUnassigned, pointerOwner_=kUnassigned, touchOwner_=kUnassigned;
};

} // namespace aiverse::platform
