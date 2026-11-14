#include "BattleJoin.h"
#include <exception>
#include "../data/Json.h"
#ifndef __EMSCRIPTEN__
#include "SecureUdp.h"
#endif

namespace aiverse::net {

std::unique_ptr<NetClient> joinBattle(const std::string& ticketText, const BattleJoinOptions& opt, std::string* reason) {
    auto fail = [reason](const char* why) { if (reason) *reason = why; return std::unique_ptr<NetClient>(); };
    std::optional<ClientTicket> t = ClientTicket::decode(ticketText);
    if (!t || t->claims.slot == 0) return fail("ticket");
    if (t->claims.expiresAtMs && opt.clock && t->claims.expiresAtMs <= opt.clock()) return fail("expired");
    if (!opt.compatKey.empty() && t->claims.compatHash != compatibilityHash(opt.compatKey)) return fail("compat");
    std::function<std::unique_ptr<Conn>()> open;
    const ClientTicket ticket = *t;
    if (opt.connect) {
        auto connect = opt.connect;
        open = [connect, ticket] { return connect(ticket); };
    } else {
#ifdef __EMSCRIPTEN__
        return fail("unsupported");
#else
        open = [ticket] { return connectSecure(ticket); };
#endif
    }
    std::unique_ptr<Conn> conn = open();
    if (!conn) return fail("connect");
    auto client = std::make_unique<NetClient>(std::move(conn), ClientIdentity{opt.name, opt.gameId, opt.clientKey});
    client->setReconnect(open, opt.reconnectWindowMs);   // same ticket: the server moves the seat to the new link
    if (reason) reason->clear();
    return client;
}

bool battleCompatible(const MatchPayloadMsg& payload, const std::string& compatKey) {
    return compatKey.empty() || payload.compatKey == compatKey;
}

namespace {
bool jsonObject(const Bytes& b, Json& out) {
    if (b.empty() || b[0] != '{') return false;
    try { out = Json::parse(std::string(b.begin(), b.end())); } catch (const std::exception&) { return false; }
    return out.isObject();
}
} // namespace

void applyBattlePayload(Json& map, const MatchPayloadMsg& payload) {
    if (!map.isObject()) return;
    Json gd;
    if (jsonObject(payload.gameData, gd)) map.set("battle", gd);
    Json* players = map.find("players");
    if (!players || !players->isArray()) return;
    for (const BattlePlayer& p : payload.players) {
        Json lo;
        if (p.slot == 0 || size_t(p.slot) > players->size() || !jsonObject(p.loadout, lo)) continue;
        Json& entry = players->at(size_t(p.slot - 1));
        if (!entry.isObject()) entry = Json::object();
        for (auto& [k, v] : lo.members()) entry.set(k, v);
    }
}

} // namespace aiverse::net
