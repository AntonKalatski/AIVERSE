// game.start(i, {players = ..., deck = ...}) (UI Lua, ClientScript.cpp): per-seat start data of a local match. Each
// entry is merged key by key into the map's players[slot - 1] before the simulation is set up, the same place a
// hosted battle puts a seat's loadout (net::applyBattlePayload): e.g. players[i].deck for in-match progression
// (docs/progression.md). `players` is an array (index = slot) or an object keyed by slot ("2": {...});
// `deck` is shorthand for players[humanSlot].deck.
#pragma once
#include <cstdlib>
#include <string>
#include "engine/data/Json.h"

namespace aiverse::client {

// "" when merged, else what is wrong (the match does not start).
inline std::string mergeStartPlayers(Json& map, const Json& players, int humanSlot, const Json& deck) {
    if (players.isNull() && deck.isNull()) return {};
    Json* list = map.find("players");
    if (!list || !list->isArray()) return "game.start: the map has no players";
    auto merge = [&](long slot, const Json& entry) -> std::string {
        if (slot < 1 || size_t(slot) > list->size()) return "game.start: players[" + std::to_string(slot) + "]: the map has " + std::to_string(list->size()) + " players";
        if (!entry.isObject()) return "game.start: players[" + std::to_string(slot) + "] must be a table";
        Json& p = list->at(size_t(slot - 1));
        if (!p.isObject()) p = Json::object();
        for (auto& [k, v] : entry.members()) p.set(k, v);
        return {};
    };
    if (players.isArray()) {
        for (size_t i = 0; i < players.size(); ++i)
            if (!players[i].isNull()) if (std::string e = merge(long(i + 1), players[i]); !e.empty()) return e;
    } else if (players.isObject()) {
        for (auto& [k, v] : players.members()) {
            char* end = nullptr;
            const long slot = std::strtol(k.c_str(), &end, 10);
            if (k.empty() || *end) return "game.start: players keys are player slots (1, 2, ...), not '" + k + "'";
            if (std::string e = merge(slot, v); !e.empty()) return e;
        }
    } else if (!players.isNull()) return "game.start: players must be a table";
    if (!deck.isNull()) {
        Json d = Json::object();
        d.set("deck", deck);
        if (std::string e = merge(humanSlot, d); !e.empty()) return e;
    }
    return {};
}

} // namespace aiverse::client
