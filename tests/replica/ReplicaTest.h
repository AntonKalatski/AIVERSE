// Shared test harness for tests/replica (docs/replica.md): a tiny TEST registry, a sample schema and a
// loopback world of one server + N clients on a virtual clock.
#pragma once
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "engine/net/Conn.h"
#include "engine/net/replica/Replica.h"
#include "engine/net/replica/ReplicaWire.h"

namespace rt {
using namespace aiverse;
using namespace aiverse::net;
using namespace aiverse::net::replica;

extern int g_failed, g_checks;
#define CHECK(cond) do { ++::rt::g_checks; if (!(cond)) { ++::rt::g_failed; std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
std::vector<std::pair<const char*, std::function<void()>>>& registry();
struct Reg { Reg(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };
#define TEST(name) static void name(); static ::rt::Reg reg_##name(#name, name); static void name()

// Sample game schema: players (owned, predicted), props (moving boxes), a game-state object.
struct Game {
    Schema schema;
    ClassId player = 0, prop = 0, state = 0, item = 0;
    int pPos = 0, pRot = 1, pHp = 2, pName = 3;    // field indices (checked against the schema below)
    int bPos = 0, bRot = 1, bAwake = 2;
    int sClock = 0, sRound = 1, sTitle = 2;
    int iPos = 0, iKind = 1;
    Game(float radius = 0) {
        ClassDef p{"player", {}, 4.0f, radius, false, 0};
        p.fields = {{"pos", FieldKind::Vec3, -2048, 2048, 0.01, 0, false, true},
                    {"rot", FieldKind::Quat, 0, 0, 0, 0, false, true},
                    {"hp", FieldKind::Int, 0, 100},
                    {"name", FieldKind::String}};
        player = schema.add(p);
        ClassDef b{"prop", {}, 1.0f, radius, false, 0};
        b.fields = {{"pos", FieldKind::Vec3, -2048, 2048, 0.01, 0, false, true},
                    {"rot", FieldKind::Quat, 0, 0, 0, 10, false, true},
                    {"awake", FieldKind::Bool}};
        prop = schema.add(b);
        ClassDef s{"state", {}, 2.0f, 0, true, -1};
        s.fields = {{"clock", FieldKind::Float, 0, 100000, 0.05}, {"round", FieldKind::Int, 0, 1000},
                    {"title", FieldKind::String, 0, 0, 0, 0, true}};
        state = schema.add(s);
        ClassDef it{"item", {}, 1.0f, radius, false, 0};
        it.fields = {{"pos", FieldKind::Vec3, -2048, 2048, 0.05}, {"kind", FieldKind::Int, 0, 15, 0, 0, true}};
        item = schema.add(it);
        if (schema.field(player, "name") != pName || schema.field(prop, "awake") != bAwake || schema.field(state, "title") != sTitle ||
            schema.field(item, "kind") != iKind)
            std::abort();
    }
};

inline Value vec(float x, float y, float z) { Value v; v.f = {x, y, z, 0}; return v; }
inline Value quatYaw(float a) { Value v; v.f = {0, std::sin(a / 2), 0, std::cos(a / 2)}; return v; }
inline Value ival(int64_t i) { Value v; v.i = i; return v; }
inline Value fval(float f) { Value v; v.f[0] = f; return v; }
inline Value sval(const std::string& s) { Value v; v.s = s; return v; }

// What the client must end up with for a field the server holds.
inline Value expected(const Schema& s, const Server& srv, ObjectId id, ClassId cls, int f) {
    wire::Layout L(s);
    return wire::normalize(L.classes[cls].fields[size_t(f)], srv.get(id, f));
}
inline bool sameValue(const Value& a, const Value& b, float eps = 1e-4f) {
    if (a.i != b.i || a.s != b.s) return false;
    for (int i = 0; i < 4; ++i) if (std::fabs(a.f[size_t(i)] - b.f[size_t(i)]) > eps) return false;
    return true;
}

// One server + N loopback clients on a virtual clock (60 Hz).
struct LoopWorld {
    Game game;
    Server server;
    std::vector<std::unique_ptr<Client>> clients;
    uint64_t now = 1000;
    uint64_t ticks = 0;
    LoopbackOptions link;
    ClientOptions copt;
    std::function<void()> step;   // the game's tick between poll and tick/broadcast
    explicit LoopWorld(ServerOptions so = {}, LoopbackOptions lo = {}, ClientOptions co = {}, float radius = 0)
        : game(radius), server(game.schema, so), link(lo), copt(co) {}
    Client& add() {
        auto [a, b] = makeLoopbackPair(link);
        server.attach(std::move(a));
        clients.push_back(std::make_unique<Client>(game.schema, std::move(b), copt));
        link.seed++;
        return *clients.back();
    }
    void frame() {
        ++ticks;
        now = 1000 + ticks * 1000 / 60;
        server.poll(now);
        if (step) step();
        server.tick();
        server.broadcast(now);
        for (auto& c : clients) if (c) c->poll(now);
    }
    void run(int frames) { for (int i = 0; i < frames; ++i) frame(); }
    bool allReady() { for (auto& c : clients) if (c && !c->ready()) return false; return true; }
};

// Every non-reliable and reliable field of every object the client has equals the server's (quantized).
inline bool sameState(const LoopWorld& w, const Client& c, ObjectId id) {
    if (!w.server.exists(id) || !c.exists(id)) return false;
    ClassId cls = c.classOf(id);
    if (c.ownerOf(id) != w.server.ownerOf(id)) return false;
    for (size_t f = 0; f < w.game.schema.cls(cls).fields.size(); ++f)
        if (!sameValue(c.latest(id, int(f)), expected(w.game.schema, w.server, id, cls, int(f)))) return false;
    return true;
}

} // namespace rt
