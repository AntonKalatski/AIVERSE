// The `backend` Lua table of the client UI (agent-bw-meta): a game's meta screens talk to a remote
// meta backend named in game.json "backend" (today: Bunker Wars, engine/meta/bw) — login, profile
// view, every service request, matchmaking with the handoff into an engine battle, spectating, chat
// and the meta tutorial runner. See docs/meta-bunkerwars.md § Client screens.
#pragma once
#include <functional>
#include <memory>
#include <string>

#include "engine/data/Json.h"

struct lua_State;

namespace aiverse::client {

class ClientMeta {
public:
    ClientMeta();
    ~ClientMeta();
    // config = game.json "backend". emit = game.on(name, fn) relay. joinBattle(address, queueId, spectator):
    // the match handoff (connect the engine NetClient and join the ticket queue).
    void attach(lua_State* L, std::shared_ptr<bool> alive, const Json& config, std::function<void(const char*, const Json&)> emit,
                std::function<void(const std::string&, const std::string&, bool)> joinBattle);
    void detach();
    void update(double dt);
    struct Impl;   // public for the Lua C functions (ClientMeta.cpp)

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace aiverse::client
