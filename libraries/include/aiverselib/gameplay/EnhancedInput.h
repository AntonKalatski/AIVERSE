// AIVERSE Enhanced Input: presentation-side action mapping, independent of game genres.
// See docs/enhanced-input-agent-handoff.md. No simulation or platform dependencies.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace aiverse::lib::gameplay::enhanced {

enum class ValueType : std::uint8_t { Boolean, Axis1D, Axis2D, Axis3D };
enum class Device : std::uint8_t {
    Keyboard, MouseButton, MouseAxis, MouseWheel, GamepadButton, GamepadAxis, Touch
};

// Code is the platform::Key, mouse button/axis or platform::PadButton/PadAxis ordinal.
// Device index is normally 0; 0..3 identify gamepads.
struct Control {
    Device device = Device::Keyboard;
    int code = 0;
    int deviceIndex = 0;
    auto operator<=>(const Control&) const = default;
};

struct Value {
    float x = 0, y = 0, z = 0;
    float length() const { return std::sqrt(x*x + y*y + z*z); }
    bool actuated(float epsilon = 0.00001f) const { return length() > epsilon; }
    Value& operator+=(const Value& other) {
        x += other.x; y += other.y; z += other.z; return *this;
    }
};

enum class ModifierKind : std::uint8_t { Scale, Negate, DeadZone, Swizzle, Normalize };
struct Modifier {
    ModifierKind kind = ModifierKind::Scale;
    Value scalar{1, 1, 1};
    float threshold = 0.1f;
    std::array<int, 3> swizzle{0, 1, 2};

    static Modifier scale(float x, float y = 1, float z = 1) {
        Modifier m; m.scalar = {x,y,z}; return m;
    }
    static Modifier negate() { Modifier m; m.kind = ModifierKind::Negate; return m; }
    static Modifier deadZone(float threshold) {
        Modifier m; m.kind = ModifierKind::DeadZone; m.threshold = threshold; return m;
    }
    // For a 1D key feeding Axis2D Y: swizzle({1,0,2}).
    static Modifier swapXY() {
        Modifier m; m.kind = ModifierKind::Swizzle; m.swizzle = {1,0,2}; return m;
    }
    static Modifier normalize() { Modifier m; m.kind = ModifierKind::Normalize; return m; }
};

inline Value applyModifiers(Value v, std::span<const Modifier> modifiers) {
    for (const auto& m : modifiers) {
        switch (m.kind) {
        case ModifierKind::Scale:
            v.x *= m.scalar.x; v.y *= m.scalar.y; v.z *= m.scalar.z; break;
        case ModifierKind::Negate:
            v.x = -v.x; v.y = -v.y; v.z = -v.z; break;
        case ModifierKind::DeadZone: {
            const float len = v.length(), threshold = std::clamp(m.threshold, 0.0f, 0.9999f);
            if (len <= threshold) v = {};
            else if (len > 0) {
                const float scaled = std::clamp((len - threshold) / (1.0f - threshold), 0.0f, 1.0f);
                const float multiplier = scaled / len;
                v.x *= multiplier; v.y *= multiplier; v.z *= multiplier;
            }
            break;
        }
        case ModifierKind::Swizzle: {
            const std::array<float,3> a{v.x,v.y,v.z};
            // Unreal-style swizzle: destination component i receives source component swizzle[i].
            // For 1D -> Y use {1,0,2}: {0,x,0}.
            v = {a[std::clamp(m.swizzle[0],0,2)],
                 a[std::clamp(m.swizzle[1],0,2)],
                 a[std::clamp(m.swizzle[2],0,2)]};
            break;
        }
        case ModifierKind::Normalize: {
            const float len = v.length();
            if (len > 1.0f) { v.x /= len; v.y /= len; v.z /= len; }
            break;
        }
        }
    }
    return v;
}

enum class TriggerKind : std::uint8_t { Down, Pressed, Released, Hold, Tap, Chord };
struct Trigger {
    TriggerKind kind = TriggerKind::Down;
    float seconds = 0;
    std::string chordAction;

    static Trigger down() { return {}; }
    static Trigger pressed() { return {TriggerKind::Pressed}; }
    static Trigger released() { return {TriggerKind::Released}; }
    static Trigger hold(float seconds) { return {TriggerKind::Hold, seconds}; }
    static Trigger tap(float seconds) { return {TriggerKind::Tap, seconds}; }
    static Trigger chord(std::string action) { return {TriggerKind::Chord, 0, std::move(action)}; }
};

enum class Phase : std::uint8_t { Started, Ongoing, Triggered, Completed, Canceled };
struct ActionEvent {
    std::string action;
    Phase phase = Phase::Started;
    Value value;
};
struct ActionState {
    Value value;
    bool active = false;
    bool triggered = false;
};

struct Action {
    std::string name;
    ValueType type = ValueType::Boolean;
    std::vector<Modifier> modifiers;
    std::vector<Trigger> triggers;
};

struct Mapping {
    std::uint64_t id = 0;   // Unique inside its context; 0 = assign at registration.
    std::string action;
    Control control;
    std::vector<Modifier> modifiers;
    std::vector<Trigger> triggers;
    bool consume = true;
};
struct MappingContext {
    std::string name;
    std::vector<Mapping> mappings;

    void bind(std::string action, Control control,
              std::vector<Modifier> modifiers = {}, std::vector<Trigger> triggers = {},
              bool consume = true) {
        mappings.push_back({0, std::move(action), control,
                            std::move(modifiers), std::move(triggers), consume});
    }
};

class InputSystem {
public:
    // Defining an action or context with an existing name replaces it and resets its runtime state.
    bool defineAction(Action action) {
        if (action.name.empty()) return false;
        const std::string name = action.name;
        actions_[name] = std::move(action);
        actionRuntime_.erase(name);
        states_.erase(name);
        return true;
    }
    bool registerContext(MappingContext context) {
        if (context.name.empty()) return false;
        std::uint64_t next = 1;
        for (auto& mapping : context.mappings) {
            if (mapping.id >= next) next = mapping.id + 1;
        }
        for (auto& mapping : context.mappings) {
            if (!mapping.id) mapping.id = next++;
            if (mapping.action.empty()) return false;
        }
        const std::string name = context.name;
        contexts_[name] = std::move(context);
        eraseContextRuntime(name);
        return true;
    }
    bool activateContext(const std::string& name, int priority = 0) {
        if (!contexts_.contains(name)) return false;
        active_[name] = priority;
        return true;
    }
    void deactivateContext(const std::string& name) {
        active_.erase(name);
        eraseContextRuntime(name);
    }
    void clearContexts() {
        active_.clear();
        mappingRuntime_.clear();
    }
    bool isContextActive(const std::string& name) const { return active_.contains(name); }
    const MappingContext* context(const std::string& name) const {
        const auto it = contexts_.find(name);
        return it == contexts_.end() ? nullptr : &it->second;
    }
    // Runtime rebinding is scoped to a mapping id and leaves all other contexts untouched.
    bool rebind(const std::string& contextName, std::uint64_t id, Control control) {
        auto it = contexts_.find(contextName);
        if (it == contexts_.end()) return false;
        for (auto& mapping : it->second.mappings) if (mapping.id == id) {
            mapping.control = control;
            mappingRuntime_.erase({contextName, id});
            return true;
        }
        return false;
    }

    void setControl(Control control, float value) {
        if (!std::isfinite(value)) value = 0;
        auto& sample = controls_[control];
        const bool before = std::abs(sample.value) > kEpsilon;
        const bool after = std::abs(value) > kEpsilon;
        sample.pressed |= !before && after;
        sample.released |= before && !after;
        sample.value = value;
        sample.transient = false;
    }
    // Relative mouse axes / wheel: deltas accumulate and are automatically cleared after update.
    void addDelta(Control control, float amount) {
        if (!std::isfinite(amount)) return;
        auto& sample = controls_[control];
        const bool before = std::abs(sample.value) > kEpsilon;
        sample.value += amount;
        sample.pressed |= !before && std::abs(sample.value) > kEpsilon;
        sample.transient = true;
    }

    // Use on focus loss / modal transitions. Emits Canceled, drops held controls immediately.
    void cancelAll() {
        events_.clear();
        for (const auto& [name, state] : states_) {
            if (state.active) events_.push_back({name, Phase::Canceled, {}});
        }
        controls_.clear();
        mappingRuntime_.clear();
        actionRuntime_.clear();
        states_.clear();
    }

    std::span<const ActionEvent> update(float deltaSeconds) {
        events_.clear();
        const float dt = std::isfinite(deltaSeconds) ? std::max(0.0f, deltaSeconds) : 0.0f;

        // Stable processing: higher context priority first; equal priorities lexicographic by name.
        std::vector<std::pair<std::string,int>> ordered(active_.begin(), active_.end());
        std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
        });

        // Pass 1: raw action levels allow same-frame simultaneous chords (no one-frame lag).
        std::map<std::string,bool> rawLevels;
        for (const auto& [name,priority] : ordered) {
            (void)priority;
            for (const auto& mapping : contexts_.at(name).mappings) {
                if (!actions_.contains(mapping.action)) continue;
                const float raw = sample(mapping.control).value;
                const Value v = applyModifiers({raw,0,0}, mapping.modifiers);
                if (v.actuated()) rawLevels[mapping.action] = true;
            }
        }

        std::map<Control,bool> consumed;
        struct Aggregate { Value value; bool active = false, triggered = false, began = false; };
        std::map<std::string,Aggregate> result;

        for (const auto& [name,priority] : ordered) {
            (void)priority;
            for (const auto& mapping : contexts_.at(name).mappings) {
                const auto actionIt = actions_.find(mapping.action);
                if (actionIt == actions_.end()) continue;
                auto& runtime = mappingRuntime_[{name,mapping.id}];
                const Raw raw = sample(mapping.control);
                const bool blocked = consumed[mapping.control];
                const Value converted = blocked ? Value{} : applyModifiers({raw.value,0,0}, mapping.modifiers);
                const bool down = converted.actuated();
                const bool pressed = !blocked && (raw.pressed || (down && !runtime.wasDown));
                const bool released = !blocked && (raw.released || (!down && runtime.wasDown));
                const bool tapWithinFrame = pressed && released;
                const bool chordOK = chordSatisfied(mapping.triggers, rawLevels);
                // Track the unblocked source even if a chord isn't yet available.
                const bool fired = !blocked && chordOK &&
                    evaluateTriggers(mapping.triggers, runtime, down, pressed, released,
                                     tapWithinFrame, dt, rawLevels);
                if (blocked || !chordOK) {
                    runtime.wasDown = down;
                    if (!down) { runtime.seconds = 0; runtime.holdFired = false; }
                }
                if (down && chordOK) {
                    auto& aggregate = result[mapping.action];
                    aggregate.active = true;
                    aggregate.value += converted;
                }
                if (pressed && chordOK) result[mapping.action].began = true;
                if (fired) result[mapping.action].triggered = true;
                if (mapping.consume && chordOK && (down || pressed || released))
                    consumed[mapping.control] = true;
            }
        }

        for (const auto& [name, action] : actions_) {
            const Aggregate aggregate = result[name];
            auto& runtime = actionRuntime_[name];
            const bool press = (aggregate.active && !runtime.wasDown) || aggregate.began;
            const bool release = !aggregate.active && runtime.wasDown;
            bool triggered = aggregate.triggered;
            if (!action.triggers.empty()) {
                triggered = triggered && chordSatisfied(action.triggers, rawLevels) &&
                    evaluateTriggers(action.triggers, runtime, aggregate.active, press, release,
                                     press && release, dt, rawLevels);
            } else {
                // Keep action lifetime tracking even without explicit action triggers.
                runtime.wasDown = aggregate.active;
                if (!aggregate.active) { runtime.seconds = 0; runtime.holdFired = false; }
            }

            Value value = applyModifiers(aggregate.value, action.modifiers);
            value = constrain(value, action.type);
            const auto previous = states_[name];
            const bool quickPulse = !aggregate.active && aggregate.began && triggered;
            if ((aggregate.active && !previous.active) || quickPulse)
                events_.push_back({name, Phase::Started, value});
            if (triggered) events_.push_back({name, Phase::Triggered, value});
            else if (aggregate.active) events_.push_back({name, Phase::Ongoing, value});
            if ((!aggregate.active && previous.active) || quickPulse)
                events_.push_back({name, previous.triggered || triggered ? Phase::Completed : Phase::Canceled, {}});
            states_[name] = {value, aggregate.active, triggered || (aggregate.active && previous.triggered)};
        }

        for (auto& [key, raw] : controls_) {
            (void)key;
            raw.pressed = raw.released = false;
            if (raw.transient) { raw.value = 0; raw.transient = false; }
        }
        return events_;
    }

    const ActionState* state(const std::string& name) const {
        const auto it = states_.find(name);
        return it == states_.end() ? nullptr : &it->second;
    }
    std::span<const ActionEvent> events() const { return events_; }
    std::vector<std::string> activeContexts() const {
        std::vector<std::string> out;
        out.reserve(active_.size());
        for (const auto& entry : active_) out.push_back(entry.first);
        return out;
    }

private:
    static constexpr float kEpsilon = 0.00001f;
    struct Raw { float value = 0; bool pressed = false, released = false, transient = false; };
    struct Runtime { bool wasDown = false, holdFired = false; float seconds = 0; };

    Raw sample(Control key) const {
        const auto it = controls_.find(key);
        return it == controls_.end() ? Raw{} : it->second;
    }
    static bool chordSatisfied(std::span<const Trigger> triggers, const std::map<std::string,bool>& levels) {
        for (const auto& trigger : triggers)
            if (trigger.kind == TriggerKind::Chord &&
                (!levels.contains(trigger.chordAction) || !levels.at(trigger.chordAction)))
                return false;
        return true;
    }
    static bool evaluateTriggers(std::span<const Trigger> triggers, Runtime& runtime,
                                 bool down, bool pressed, bool released, bool quick,
                                 float dt, const std::map<std::string,bool>& levels) {
        const float elapsedBeforeRelease = runtime.seconds;
        if (pressed && !runtime.wasDown) { runtime.seconds = 0; runtime.holdFired = false; }
        if (down) runtime.seconds += dt;
        const float held = quick ? dt : (down ? runtime.seconds : elapsedBeforeRelease);

        bool matched = triggers.empty() ? down : true;
        for (const auto& trigger : triggers) {
            bool ok = false;
            switch (trigger.kind) {
            case TriggerKind::Down: ok = down; break;
            case TriggerKind::Pressed: ok = pressed; break;
            case TriggerKind::Released: ok = released; break;
            case TriggerKind::Hold:
                ok = down && !runtime.holdFired && runtime.seconds >= std::max(0.0f,trigger.seconds);
                break;
            case TriggerKind::Tap:
                ok = released && (runtime.wasDown || quick) &&
                     held <= std::max(0.0f,trigger.seconds);
                break;
            case TriggerKind::Chord:
                ok = levels.contains(trigger.chordAction) && levels.at(trigger.chordAction);
                break;
            }
            matched = matched && ok;
        }
        if (matched) for (const auto& trigger : triggers)
            if (trigger.kind == TriggerKind::Hold) runtime.holdFired = true;
        runtime.wasDown = down;
        if (!down) { runtime.seconds = 0; runtime.holdFired = false; }
        return matched;
    }
    static Value constrain(Value value, ValueType type) {
        switch (type) {
        case ValueType::Boolean:
            return {value.actuated() ? 1.0f : 0.0f, 0, 0};
        case ValueType::Axis1D:
            return {std::clamp(value.x,-1.0f,1.0f), 0, 0};
        case ValueType::Axis2D:
            return {std::clamp(value.x,-1.0f,1.0f), std::clamp(value.y,-1.0f,1.0f),0};
        case ValueType::Axis3D:
            return {std::clamp(value.x,-1.0f,1.0f), std::clamp(value.y,-1.0f,1.0f),
                    std::clamp(value.z,-1.0f,1.0f)};
        }
        return {};
    }
    void eraseContextRuntime(const std::string& name) {
        for (auto it = mappingRuntime_.begin(); it != mappingRuntime_.end(); )
            if (it->first.first == name) it = mappingRuntime_.erase(it);
            else ++it;
    }

    std::map<std::string,Action> actions_;
    std::map<std::string,MappingContext> contexts_;
    std::map<std::string,int> active_;
    std::map<Control,Raw> controls_;
    std::map<std::pair<std::string,std::uint64_t>,Runtime> mappingRuntime_;
    std::map<std::string,Runtime> actionRuntime_;
    std::map<std::string,ActionState> states_;
    std::vector<ActionEvent> events_;
};

} // namespace aiverse::lib::gameplay::enhanced
