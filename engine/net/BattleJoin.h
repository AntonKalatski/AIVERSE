// Joining a hosted battle from a game client with the sealed ticket the game's matchmaker handed out
// (docs/battle-net.md § Flow). The ticket text is ClientTicket::encode(); it carries the battle server's address.
//
//   std::string why;
//   auto net = net::joinBattle(ticketText, {"Alex", "com.example.game", "1.2|protocol:3|map:square"}, &why);
//   if (!net) show(why);              // "ticket", "expired", "compat", "unsupported", "connect"
//   ... the usual NetClient match flow: matchReady() -> build the sim (applyBattlePayload on the map) -> attach()
//   ... battleCompatible(*net->matchPayload(), compatKey) before building it
//
// The connection is the secure transport (SecureUdp.h); after a drop the client reconnects with the same ticket.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include "BattleProtocol.h"
#include "MatchTicket.h"
#include "NetClient.h"

namespace aiverse { class Json; }

namespace aiverse::net {

struct BattleJoinOptions {
    std::string name;           // shown in the match (the server takes the allocated name when it has one)
    std::string gameId;         // game.json "id"
    std::string compatKey;      // this build's compatibilityKey(); empty: not checked on the client
    uint64_t clientKey = 0;
    uint32_t reconnectWindowMs = 120000;
    std::function<uint64_t()> clock = unixMs;   // ticket expiry check
    // Opens the transport for the ticket (default: connectSecure(ticket) over a real UDP socket); tests pass SimNet.
    std::function<std::unique_ptr<Conn>(const ClientTicket&)> connect;
};

// nullptr and *reason set when the ticket does not decode ("ticket"), has expired ("expired"), was issued for
// another build than compatKey ("compat"), the platform has no UDP ("unsupported") or the socket fails ("connect").
std::unique_ptr<NetClient> joinBattle(const std::string& ticketText, const BattleJoinOptions& opt, std::string* reason = nullptr);

// The hosted match is for this build (payload compatibility key equals compatKey; an empty compatKey accepts any).
bool battleCompatible(const MatchPayloadMsg& payload, const std::string& compatKey);

// Per-seat start data into the map the simulation is built from: a player's loadout that is a JSON object is merged
// key by key into map players[slot - 1] (for example {"deck": {...}}, docs/progression.md); match-wide gameData that
// is a JSON object goes to the map's "battle" key. Other bytes are left to the game (NetClient::matchPayload()).
// Every client applies the same payload, so the simulations stay identical.
void applyBattlePayload(Json& map, const MatchPayloadMsg& payload);

} // namespace aiverse::net
