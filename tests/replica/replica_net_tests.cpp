// Replica over the secure UDP transport on the simulated mobile network (SimNet presets "4g", "bad"):
// input ordering and redundancy, convergence under loss (docs/replica.md, docs/battle-net.md).
#include <cstring>
#include <map>
#include "engine/net/MatchTicket.h"
#include "engine/net/SecureUdp.h"
#include "engine/net/SimNet.h"
#include "tests/replica/ReplicaTest.h"

namespace rt {

namespace {
constexpr uint64_t kUnix = 1'800'000'000'000ull;

struct SimWorld {
    SimNetwork simnet;
    Key key = randomKey();
    Game game;
    Server server;
    std::unique_ptr<SecureListener> listener;
    std::vector<std::unique_ptr<Client>> clients;
    Address serverAddr = SimNetwork::address("10.0.0.1", 27017);
    uint64_t now = 1000, ticks = 0;
    std::function<void()> step;

    SimWorld(uint64_t seed, ServerOptions so = {}) : simnet(seed), server(game.schema, so) {
        SecureServerConfig sc;
        sc.serverKey = key;
        sc.admit = [](const TicketClaims& c) { return c.matchId == 42; };
        sc.clock = [this] { return kUnix + now; };
        listener = std::make_unique<SecureListener>(sc);
        listener->attach(simnet.open("10.0.0.1", 27017));
    }
    Client& add(const std::string& preset, ClientOptions co = {}) {
        TicketClaims c;
        c.matchId = 42; c.playerId = "p" + std::to_string(clients.size()); c.slot = uint8_t(clients.size() + 1);
        c.expiresAtMs = kUnix + 3'600'000;
        auto port = simnet.open("10.1.0." + std::to_string(clients.size() + 1), uint16_t(40000 + clients.size()), linkPreset(preset));
        clients.push_back(std::make_unique<Client>(game.schema, connectSecure(std::move(port), serverAddr, issueTicket(key, c, "x:1")), co));
        return *clients.back();
    }
    void frame() {
        ++ticks;
        now = 1000 + ticks * 1000 / 60;
        simnet.setTime(now);
        for (auto& a : listener->poll(now)) server.attach(std::move(a.conn));
        server.poll(now);
        if (step) step();
        server.tick();
        server.broadcast(now);
        for (auto& c : clients) c->poll(now);
    }
    void run(int n) { for (int i = 0; i < n; ++i) frame(); }
};

struct InputRun { double delivered = 0; bool ordered = true; uint32_t sent = 0, lastAck = 0; uint64_t rejected = 0; };

InputRun inputsOver(const std::string& preset, int redundancy, uint64_t seed) {
    SimWorld w(seed);
    ClientOptions co; co.inputRedundancy = redundancy;
    Client& c = w.add(preset, co);
    std::map<uint32_t, int> got;
    uint32_t last = 0;
    InputRun r;
    w.server.onInput = [&](uint8_t, uint32_t s, const Bytes& b) {
        uint32_t v = 0;
        if (b.size() == 4) std::memcpy(&v, b.data(), 4);
        r.ordered &= v == s && s > last;
        last = s;
        ++got[s];
    };
    for (int i = 0; i < 600 && !c.ready(); ++i) w.frame();
    CHECK(c.ready());
    uint32_t next = 1;
    for (int f = 0; f < 1800; ++f) {        // 30 s of 60 Hz input
        Bytes b(4); std::memcpy(b.data(), &next, 4);
        c.sendInput(b);
        ++next;
        w.frame();
    }
    w.run(60);
    r.sent = next - 1;
    r.delivered = double(got.size()) / r.sent;
    r.lastAck = c.lastAppliedInput();
    r.rejected = w.server.stats().rejected;
    for (auto& [s, n] : got) r.ordered &= n == 1;
    return r;
}
} // namespace

TEST(inputs_in_order_with_redundancy_over_4g_and_bad) {
    const int def = ClientOptions{}.inputRedundancy;
    for (const char* preset : {"4g", "bad"}) {
        std::printf("    %s delivered:", preset);
        double prev = 0;
        for (int red : {1, 3, def, 8}) {
            double sum = 0;
            for (uint64_t seed : {21u, 22u, 23u}) {
                InputRun r = inputsOver(preset, red, seed);
                CHECK(r.ordered);
                CHECK(r.lastAck + 30 > r.sent);
                CHECK(r.rejected == 0);
                sum += r.delivered;
            }
            std::printf(" %.2f%% (redundancy %d)", sum / 3 * 100, red);
            CHECK(sum / 3 >= prev - 0.002);
            prev = sum / 3;
            if (red == def) CHECK(sum / 3 > (std::string(preset) == "4g" ? 0.995 : 0.97));
        }
        std::printf("\n");
    }
}

TEST(clients_converge_over_lossy_links) {
    ServerOptions so; so.defaultRadius = 300;
    SimWorld w(9, so);
    std::map<uint8_t, ObjectId> avatar;
    std::vector<ObjectId> props;
    for (int i = 0; i < 150; ++i) props.push_back(w.server.create(w.game.prop));
    ObjectId st = w.server.create(w.game.state);
    w.server.onJoin = [&](uint8_t slot) {
        ObjectId id = w.server.create(w.game.player);
        w.server.setOwner(id, slot);
        w.server.set(id, w.game.pName, sval("p" + std::to_string(slot)));
        avatar[slot] = id;
        w.server.setViewers(slot, {{0, 0, 0}});
    };
    bool moving = true;
    w.step = [&] {
        if (!moving) return;
        for (size_t i = 0; i < props.size(); ++i) {
            float a = float(w.ticks) * 0.02f + float(i);
            w.server.set(props[i], 0, vec(std::cos(a) * 100, 0, std::sin(a) * 100));
            w.server.set(props[i], 1, quatYaw(a));
        }
        w.server.set(st, w.game.sClock, fval(float(w.ticks) / 60));
        if (w.ticks % 120 == 0) w.server.set(st, w.game.sTitle, sval("t" + std::to_string(w.ticks)));
        if (w.ticks == 600) { w.server.destroy(props.back()); props.pop_back(); }
    };
    for (int i = 0; i < 4; ++i) w.add(i % 2 ? "bad" : "4g");
    w.run(900);
    moving = false;
    w.run(240);     // 4 s of quiet: lost deltas, creates and removals must all be repaired
    bool same = true, all = true;
    for (auto& c : w.clients) {
        CHECK(c->ready());
        for (ObjectId id : props) all &= c->exists(id);
        for (ObjectId id : c->objects()) {
            if (!w.server.exists(id)) { same = false; continue; }
            ClassId cls = c->classOf(id);
            for (size_t f = 0; f < w.game.schema.cls(cls).fields.size(); ++f)
                same &= sameValue(c->latest(id, int(f)), expected(w.game.schema, w.server, id, cls, int(f)));
        }
        CHECK(c->objects().size() == props.size() + 1 + avatar.size());
    }
    CHECK(same && all);
}

// The owner reconciles its prediction against (owned state, lastAppliedInput) of one snapshot: the two must
// match even when the snapshot that carried the last change was lost and the value has not changed since.
TEST(owned_state_matches_last_applied_input_under_loss) {
    SimWorld w(31);
    ObjectId avatar = 0;
    w.server.onJoin = [&](uint8_t slot) { avatar = w.server.create(w.game.player); w.server.setOwner(avatar, slot); };
    // The state advances only with input (a character that moves per input frame): pos.x = last applied seq.
    w.server.onInput = [&](uint8_t, uint32_t seq, const Bytes&) { w.server.set(avatar, w.game.pPos, vec(float(seq), 0, 0)); };
    Client& c = w.add("bad");
    for (int i = 0; i < 600 && !c.ready(); ++i) w.frame();
    CHECK(c.ready());
    uint32_t tick = 0;
    int checked = 0, stale = 0;
    for (int f = 0; f < 1800; ++f) {
        if ((f / 20) % 3 != 2) c.sendInput(Bytes(4, 1));     // bursts of input with pauses (unchanged state)
        w.frame();
        if (c.latestTick() == tick || !c.lastAppliedInput()) continue;
        tick = c.latestTick();
        ++checked;
        stale += c.latest(avatar, w.game.pPos).f[0] != float(c.lastAppliedInput());
    }
    std::printf("    %d snapshots checked, %d with a stale owned state\n", checked, stale);
    CHECK(checked > 300);
    CHECK(stale == 0);
}

} // namespace rt
