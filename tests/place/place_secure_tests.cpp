// Place hosting (docs/replica.md § Place hosting): tickets over SecureUdp and the PlaceGate (WebSocket / plain UDP),
// guests, rate limits, reconnect to the kept character after a SimNet blackout, expiry of the reconnect window,
// ticket reuse by another connection, app suspend / resume, and the native WebSocket client end to end.
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include "apps/place/PlaceLink.h"
#include "apps/place/PlaceServer.h"
#include "engine/net/SecureUdp.h"
#include "engine/net/SimNet.h"
#include "engine/net/WebSocket.h"

using namespace aiverse;
using namespace aiverse::place;

namespace {
int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failed; std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
std::vector<std::pair<const char*, std::function<void()>>>& registry() { static std::vector<std::pair<const char*, std::function<void()>>> r; return r; }
struct Reg { Reg(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };
#define TEST(name) static void name(); static Reg reg_##name(#name, name); static void name()

constexpr uint64_t kUnix = 1'800'000'000'000ull;

Json flatScene() {
    return Json::parse(R"({"tickRate": 60, "substeps": 4, "place": {"spawns": [[0, 0.05, 0]]},
      "bodies": [{"id": "ground", "motion": "static", "position": [0, -0.5, 0], "size": [200, 1, 200]},
                 {"id": "crate", "position": [30, 0.35, 30], "size": [0.7, 0.7, 0.7]}]})");
}
float dist(b3Vec3 a, b3Vec3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

// A place server with a SecureListener and a PlaceGate on a SimNet, players as PlaceLinks, on a virtual 60 Hz clock.
struct World {
    net::SimNetwork net{5};
    net::Key key = net::randomKey();
    uint64_t now = 1000, ticks = 0;
    PlaceServer server;
    std::unique_ptr<net::SecureListener> listener;
    net::Address serverAddr = net::SimNetwork::address("10.0.0.1", 27300);
    PlaceGate gate;
    std::vector<std::unique_ptr<PlaceLink>> links;
    std::vector<net::Address> clientEnds;
    bool blackout = false;
    uint16_t nextPort = 40000;
    std::function<Input(size_t)> input = [](size_t) { return Input{}; };
    std::string profile = "wifi";

    explicit World(double reconnectSeconds = 30, const Json& scene = flatScene()) : server(scene), gate([this] {
        GateConfig g;
        g.key = key;
        g.clock = [this] { return kUnix + now; };
        return g;
    }()) {
        server.setReconnectSeconds(reconnectSeconds);
        net::SecureServerConfig sc;
        sc.serverKey = key;
        sc.admit = [this](const net::TicketClaims& c) { return c.matchId == kPlaceMatch && server.admits(c.playerId); };
        sc.clock = [this] { return kUnix + now; };
        listener = std::make_unique<net::SecureListener>(sc);
        listener->attach(net.open("10.0.0.1", 27300));
    }
    net::ClientTicket ticket(const std::string& player, double seconds = 3600, uint64_t match = kPlaceMatch) {
        net::TicketClaims c;
        c.matchId = match;
        c.playerId = player;
        c.slot = 1;
        c.expiresAtMs = kUnix + now + uint64_t(seconds * 1000);
        return net::issueTicket(key, c, "10.0.0.1:27300");
    }
    LinkConfig secureLink(const net::ClientTicket& t, uint64_t seed) {
        LinkConfig lc;
        lc.seed = seed;
        lc.dial = [this, t, seed] {
            std::string ip = "10.1.0." + std::to_string(seed % 250 + 1);
            uint16_t port = nextPort++;
            auto p = net.open(ip, port, net::linkPreset(profile));
            auto a = net::SimNetwork::address(ip, port);
            clientEnds.push_back(a);
            if (blackout) net.setSuspended(a, true);
            return net::connectSecure(std::move(p), serverAddr, t);
        };
        return lc;
    }
    // Gate transport over an in-process ordered pipe (what WebSocket gives).
    LinkConfig gateLink(std::optional<net::ClientTicket> t, uint64_t seed) {
        LinkConfig lc;
        lc.seed = seed;
        lc.gate = true;
        lc.ticket = t;
        lc.dial = [this, seed] {
            auto [a, b] = net::makeLoopbackPair({10, 2, seed});
            gate.add(std::move(a), now);
            return std::move(b);
        };
        return lc;
    }
    PlaceLink& add(LinkConfig lc) { links.push_back(std::make_unique<PlaceLink>(std::move(lc))); return *links.back(); }
    void setBlackout(bool on) {
        blackout = on;
        for (auto& a : clientEnds) net.setSuspended(a, on);
    }
    void frame() {
        ++ticks;
        now = 1000 + ticks * 1000 / 60;
        net.setTime(now);
        for (auto& a : listener->poll(now)) server.attach(std::move(a.conn), a.claims.playerId);
        for (auto& g : gate.poll(now)) server.attach(std::move(g.conn), g.player);
        server.update(now);
        for (size_t i = 0; i < links.size(); ++i) {
            if (!links[i]) continue;
            links[i]->update(now, input(i));
            if (auto* c = links[i]->client()) c->advanceSmoothing(1000.0f / 60);
        }
    }
    void run(double s) { for (int i = 0; i < int(s * 60 + 0.5); ++i) frame(); }
    bool runUntil(double maxSeconds, const std::function<bool()>& done) {
        for (int i = 0; i < int(maxSeconds * 60); ++i) { frame(); if (done()) return true; }
        return false;
    }
};
Input walk() { Input in; in.move.moveZ = 1; in.move.yaw = 0; return in; }
}

TEST(secure_udp_tickets_admit_and_reject) {
    World w;
    auto& good = w.add(w.secureLink(w.ticket("alice"), 1));
    // Expired, wrong place, other key: SecureListener answers nothing (no oracle); they never play.
    auto& expired = w.add(w.secureLink(w.ticket("bob", -1), 2));
    auto& wrong = w.add(w.secureLink(w.ticket("carol", 3600, 99), 3));
    World other;
    auto& forged = w.add(w.secureLink(other.ticket("mallory"), 4));
    w.run(3);
    CHECK(good.playing());
    CHECK(!expired.playing() && !wrong.playing() && !forged.playing());
    CHECK(w.server.players() == 1);
    CHECK(w.server.playerOf(good.client()->replica().slot()) == "alice");
    CHECK(w.listener->stats().rejectedTickets >= 3);
    std::printf("    playing 1 of 4, rejected tickets %llu\n", (unsigned long long)w.listener->stats().rejectedTickets);
}

TEST(gate_tickets_proof_guests_and_rate_limit) {
    World w;
    auto& good = w.add(w.gateLink(w.ticket("alice"), 1));
    auto noTicket = w.add(w.gateLink(std::nullopt, 2)).status();
    (void)noTicket;
    auto stolen = w.ticket("bob");
    stolen.claims.c2s = net::randomKey();   // the sealed part alone (keys unknown): the proof fails
    auto& thief = w.add(w.gateLink(stolen, 3));
    auto& late = w.add(w.gateLink(w.ticket("dave", -1), 4));
    w.run(2);
    CHECK(good.playing());
    CHECK(w.links[1]->status() == LinkStatus::Rejected && w.links[1]->rejectReason() == GateReject::NeedTicket);
    CHECK(thief.status() == LinkStatus::Rejected && thief.rejectReason() == GateReject::BadTicket);
    CHECK(late.status() == LinkStatus::Rejected && late.rejectReason() == GateReject::BadTicket);
    CHECK(w.server.players() == 1);

    // Guests without a key; names are cleaned, missing names numbered.
    GateConfig gc;
    PlaceGate guests(gc);
    auto [a, b] = net::makeLoopbackPair();
    auto [c, d] = net::makeLoopbackPair();
    guests.add(std::move(a), 1000);
    guests.add(std::move(c), 1000);
    GateClient ga(std::move(b), std::nullopt, "Zoe <script>");
    GateClient gb(std::move(d), std::nullopt, "");
    std::vector<GateAdmitted> in;
    for (uint64_t t = 1000; t < 1100; t += 10) {
        ga.poll(t); gb.poll(t);
        for (auto& x : guests.poll(t)) in.push_back(std::move(x));
    }
    CHECK(in.size() == 2 && ga.poll(1100) == GateClient::State::Admitted && gb.poll(1100) == GateClient::State::Admitted);
    CHECK(ga.player() == "guest-Zoescript");
    CHECK(gb.player() == "guest-1");

    // One source address hammering hellos: the burst passes, then refusals.
    GateConfig rc;
    rc.key = w.key;
    PlaceGate limited(rc);
    int refused = 0;
    std::vector<std::unique_ptr<net::Conn>> keep;
    for (int i = 0; i < 20; ++i) {
        auto [s, cl] = net::makeLoopbackPair();
        limited.add(std::move(s), 5000);
        keep.push_back(std::move(cl));
    }
    refused = int(limited.stats().rateLimited);
    CHECK(refused == 20 - int(rc.helloBurst));
    // Hostile hellos never crash and never admit.
    std::mt19937 rng(3);
    for (int i = 0; i < 200; ++i) {
        auto [s, cl] = net::makeLoopbackPair();
        GateConfig hc; hc.key = w.key; hc.helloBurst = 1000;
        PlaceGate g(hc);
        g.add(std::move(s), 1);
        net::Bytes junk(rng() % 64);
        for (auto& x : junk) x = uint8_t(rng());
        if (i % 3 == 0 && junk.size() >= 3) { junk[0] = 'P'; junk[1] = 'L'; junk[2] = 'H'; }
        cl->send(junk);
        for (uint64_t t = 1; t < 60; t += 10) { cl->update(t); CHECK(g.poll(t).empty()); }
    }
    std::printf("    gate: admitted %llu, need ticket %llu, bad ticket %llu; rate limit refused %d of 20\n", (unsigned long long)w.gate.stats().admitted,
                (unsigned long long)w.gate.stats().needTicket, (unsigned long long)w.gate.stats().badTicket, refused);
}

TEST(blackout_10s_resumes_the_same_character) {
    World w(30);
    w.input = [&](size_t) { return w.ticks < 60 * 3 ? walk() : Input{}; };
    auto& link = w.add(w.secureLink(w.ticket("alice"), 1));
    CHECK(w.runUntil(3, [&] { return link.playing(); }));
    w.run(3);
    replica::ObjectId character = w.server.characterOfPlayer("alice");
    CHECK(character && link.client()->own() == character);
    w.input = [](size_t) { return walk(); };   // the player keeps pressing W into a dead link
    w.setBlackout(true);
    w.run(10);
    CHECK(link.status() == LinkStatus::Reconnecting);
    // The server's 10 s silence timeout fires about now (away), or the client's new handshake replaces the dead
    // session first: either way the character is kept and nobody else got it.
    CHECK(w.server.players() + w.server.away() == 1);
    CHECK(w.server.characterOfPlayer("alice") == character);
    b3Vec3 kept = w.server.characterStateOf("alice")->position;
    w.setBlackout(false);
    CHECK(w.runUntil(15, [&] { return link.playing(); }));
    w.run(1);
    CHECK(w.server.players() == 1 && w.server.away() == 0);
    CHECK(w.server.stats().resumed == 1 && w.server.stats().joined == 1);
    CHECK(w.server.characterOfPlayer("alice") == character);
    CHECK(link.client() && link.client()->own() == character);
    CHECK(link.reconnects() == 1 && link.resumedCharacter());
    b3Vec3 now = w.server.characterStateOf("alice")->position;
    float moved = dist(kept, now);
    std::printf("    reconnected after %u dials; kept at (%.2f, %.2f, %.2f), %.2f m walked since the resume\n", link.attempts(), kept.x, kept.y, kept.z, moved);
    CHECK(kept.z < -2);   // it had walked 3 s along -Z before the drop and stood while away
    CHECK(moved > 0.5f);  // walking again from there, not from the spawn
    CHECK(dist(link.client()->predicted().position, now) < 0.6f);
}

TEST(character_removed_after_the_reconnect_window) {
    World w(5);
    auto& keep = w.add(w.secureLink(w.ticket("keeper"), 1));
    w.add(w.secureLink(w.ticket("leaver"), 2));
    CHECK(w.runUntil(3, [&] { return keep.playing() && w.links[1]->playing(); }));
    replica::ObjectId gone = w.server.characterOfPlayer("leaver");
    auto sees = [&] { return keep.client() && keep.client()->replica().exists(gone); };
    w.run(1);
    CHECK(sees());
    w.links[1].reset();   // the app is closed: the connection says bye
    w.run(1);
    CHECK(w.server.away() == 1 && sees());   // kept, standing
    w.run(5);
    CHECK(w.server.away() == 0 && w.server.stats().expired == 1);
    CHECK(!w.server.characterOfPlayer("leaver") && !w.server.replica().exists(gone));
    w.run(1);
    CHECK(!sees());
    // Coming back later is a fresh join at the spawn.
    auto& back = w.add(w.secureLink(w.ticket("leaver"), 3));
    CHECK(w.runUntil(3, [&] { return back.playing(); }));
    CHECK(w.server.stats().joined == 3 && w.server.stats().resumed == 0);
    CHECK(dist(w.server.characterStateOf("leaver")->position, {0, 0.05f, 0}) < 3);
}

TEST(ticket_reuse_by_another_connection_takes_over) {
    // SecureUdp: the same ticket from a second device; the newest connection gets the character.
    World w;
    auto t = w.ticket("alice");
    auto& first = w.add(w.secureLink(t, 1));
    CHECK(w.runUntil(3, [&] { return first.playing(); }));
    replica::ObjectId character = w.server.characterOfPlayer("alice");
    auto& second = w.add(w.secureLink(t, 2));
    CHECK(w.runUntil(3, [&] { return second.playing(); }));
    CHECK(w.server.players() == 1 && w.server.stats().replaced == 1 && w.server.stats().resumed == 1);
    CHECK(w.server.characterOfPlayer("alice") == character && second.client()->own() == character);
    // The listener closed the first session silently (SecureUdp answers nothing): the first device notices only by
    // the missing snapshots and dials again. Both keep doing that; after 3 takeovers in 60 s the id is contested and
    // whoever holds the character keeps it.
    w.run(30);
    CHECK(w.server.stats().replaced == 3 && w.server.stats().contested + w.listener->stats().rejectedTickets > 0);
    CHECK(w.server.players() == 1 && int(first.playing()) + int(second.playing()) == 1);
    CHECK(w.server.characterOfPlayer("alice") == character);
    std::printf("    secure: %llu takeovers, then contested (%llu refused handshakes)\n", (unsigned long long)w.server.stats().replaced,
                (unsigned long long)w.listener->stats().rejectedTickets);
    // Over the gate the displaced client hears why and stops (no reconnect fight).
    World g;
    auto gt = g.ticket("bob");
    auto& a = g.add(g.gateLink(gt, 1));
    CHECK(g.runUntil(2, [&] { return a.playing(); }));
    replica::ObjectId bob = g.server.characterOfPlayer("bob");
    auto& b = g.add(g.gateLink(gt, 2));
    CHECK(g.runUntil(2, [&] { return b.playing(); }));
    g.run(1);
    CHECK(a.status() == LinkStatus::Replaced);
    CHECK(b.client()->own() == bob && g.server.players() == 1);
    g.run(3);
    CHECK(a.status() == LinkStatus::Replaced && b.playing() && g.server.stats().replaced == 1);
}

TEST(app_suspend_and_resume_reconnects_at_once) {
    World w;
    auto& link = w.add(w.secureLink(w.ticket("phone"), 1));
    CHECK(w.runUntil(3, [&] { return link.playing(); }));
    replica::ObjectId character = w.server.characterOfPlayer("phone");
    // Short absence: the link survives.
    link.suspend(w.now);
    w.setBlackout(true);
    w.run(1.5);
    w.setBlackout(false);
    link.resume(w.now);
    w.run(1);
    CHECK(link.playing() && link.attempts() == 1);
    // Long absence (the OS killed the socket): resume dials right away, the character is the same.
    link.suspend(w.now);
    w.setBlackout(true);
    w.run(12);
    CHECK(link.status() == LinkStatus::Suspended && w.server.away() == 1);
    w.setBlackout(false);
    link.resume(w.now);
    CHECK(w.runUntil(2, [&] { return link.playing(); }));
    CHECK(link.attempts() == 2 && link.client()->own() == character && w.server.stats().resumed == 1);
}

TEST(native_websocket_client_end_to_end) {
    // Real sockets on localhost: WsListener -> PlaceGate (ticket + proof) -> PlaceServer; the client dials ws://.
    PlaceServer server(flatScene());
    server.setReconnectSeconds(30);
    net::Key key = net::randomKey();
    GateConfig gc;
    gc.key = key;
    PlaceGate gate(gc);
    net::WsListener ws;
    uint16_t port = 0;
    for (uint16_t p = 27391; p < 27399 && !port; ++p) if (ws.open(p, true)) port = p;
    CHECK(port != 0);
    if (!port) return;
    std::string url = "ws://127.0.0.1:" + std::to_string(port);
    PlaceLink link(linkConfig(url, placeTicket(key, "web-1", 600), "", false));
    PlaceLink noTicket(linkConfig(url, std::nullopt, "anon", false));
    uint64_t start = net::nowMs(), lastTick = start;
    uint64_t snapshotsAt2s = 0;
    while (net::nowMs() - start < 4000) {
        uint64_t now = net::nowMs();
        for (auto& c : ws.poll(now)) gate.add(std::move(c), now);
        for (auto& g : gate.poll(now)) server.attach(std::move(g.conn), g.player);
        if (now - lastTick >= 16) {
            lastTick = now;
            server.update(now);
            link.update(now, walk());
            noTicket.update(now, {});
        } else {
            link.poll(now);
            noTicket.poll(now);
        }
        if (!snapshotsAt2s && now - start >= 2000 && link.client()) snapshotsAt2s = link.client()->replica().stats().snapshots;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(link.playing());
    CHECK(server.playerOf(link.client() ? link.client()->replica().slot() : 0) == "web-1");
    CHECK(noTicket.status() == LinkStatus::Rejected && noTicket.rejectReason() == GateReject::NeedTicket);
    uint64_t snaps = link.client() ? link.client()->replica().stats().snapshots : 0;
    float rate = float(snaps - snapshotsAt2s) / 2.0f;
    auto* st = server.characterStateOf("web-1");
    CHECK(st && st->position.z < -1);   // its inputs arrived
    CHECK(rate > 15);
    std::printf("    ws://127.0.0.1:%u: %.1f snapshots/s, ping %u ms, walked to z %.2f\n", unsigned(port), rate, link.client() ? link.client()->stats().rttMs : 0,
                st ? st->position.z : 0.0f);
}

int main(int argc, char** argv) {
    std::string only = argc > 1 ? argv[1] : "";
    for (auto& [name, fn] : registry()) {
        if (!only.empty() && only != name) continue;
        std::printf("%s\n", name);
        int before = g_failed;
        fn();
        std::printf("  %s\n", g_failed == before ? "ok" : "FAILED");
    }
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
