// What the game systems ask of the scripting layer (the Lua VM implements it; a game without scripts has none).
#pragma once
#include <cstdint>
#include <string>
#include "../../engine/core/ECS.h"
#include "../../engine/data/Json.h"

namespace aiverse { class Simulation; }

namespace aiverse::game {

class ScriptHooks {
public:
    virtual ~ScriptHooks() = default;
    virtual bool hasFunction(const std::string& name) const = 0;
    // A script veto: false when the named function exists and returns false (missing function: allowed).
    virtual bool allowNamed(Simulation& sim, const std::string& fn, uint8_t player, const std::string& arg) = 0;
    // Runs a script action function with arguments.
    virtual void callAction(Simulation& sim, const std::string& fn, const Json& args, uint8_t player, Entity unit, Vec3 at,
                            const std::string& trigger) = 0;
};

} // namespace aiverse::game
