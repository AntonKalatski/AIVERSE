// Replica core, second round (docs/replica.md): unchanged objects cost nothing, delta-coded numbers, 3D interest,
// object generations on id reuse, the render clock.
#include <algorithm>
#include <deque>
#include <random>
#include <set>
#include "tests/replica/ReplicaTest.h"

namespace rt {

namespace {
// Client side of a connection that can hold back the reliable stream (everything but snapshots), as a
// retransmitting reliable channel does behind a loss burst while newer snapshots get through.
struct HoldConn : Conn {
    std::unique_ptr<Conn> in;
    bool hold = false;
    std::deque<Bytes> held;
    explicit HoldConn(std::unique_ptr<Conn> c) : in(std::move(c)) {}
    void send(const Bytes& m) override { in->send(m); }
    void sendLatest(const Bytes& m) override { in->sendLatest(m); }
    bool recv(Bytes& m) override {
        for (;;) {
            if (!hold && !held.empty()) { m = std::move(held.front()); held.pop_front(); return true; }
            Bytes x;
            if (!in->recv(x)) return false;
            const bool reliable = x.empty() || x[0] != uint8_t(wire::Msg::Snapshot);
            if (reliable && (hold || !held.empty())) { held.push_back(std::move(x)); continue; }
            m = std::move(x);
            return true;
        }
    }
    void update(uint64_t now) override { in->update(now); }
    ConnState state() const override { return in->state(); }
    void close() override { in->close(); }
    uint32_t rttMs() const override { return in->rttMs(); }
    std::string describe() const override { return "hold " + in->describe(); }
};

} // namespace

TEST(unchanged_objects_cost_nothing) {
    ServerOptions so; so.defaultRadius = 5000; so.bytesPerSecond = 1 << 22; so.maxSnapshotBytes = 100000;
    LoopWorld w(so);
    Client& c = w.add();
    w.server.setViewers(1, {{0, 0, 0}});
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-500, 500);
    std::vector<ObjectId> ids;
    for (int i = 0; i < 1000; ++i) {
        ids.push_back(w.server.create(w.game.prop));
        w.server.set(ids.back(), w.game.bPos, vec(u(rng), u(rng), u(rng)));
        w.server.set(ids.back(), w.game.bRot, quatYaw(u(rng)));
    }
    w.run(60);
    CHECK(c.objects().size() == ids.size());
    // at rest: no object is looked at, no distance is tested
    ServerStats s0 = w.server.stats();
    w.run(120);
    ServerStats s1 = w.server.stats();
    const uint64_t snaps = s1.snapshots - s0.snapshots;
    std::printf("    at rest: %llu snapshots, %llu objects evaluated, %llu distance tests, %llu relevant\n", (unsigned long long)snaps,
                (unsigned long long)(s1.evaluated - s0.evaluated), (unsigned long long)(s1.interestChecks - s0.interestChecks),
                (unsigned long long)(s1.relevant - s0.relevant));
    CHECK(snaps >= 39);
    CHECK(s1.evaluated == s0.evaluated);
    CHECK(s1.interestChecks == s0.interestChecks);
    CHECK(s1.relevant - s0.relevant == snaps * ids.size());
    CHECK((s1.snapshotBytes - s0.snapshotBytes) / snaps < 24);
    // ten move: about ten evaluations per snapshot, the rest stay free; the client stays exact
    w.step = [&] {
        for (int i = 0; i < 10; ++i) {
            Value p = w.server.get(ids[size_t(i)], w.game.bPos);
            p.f[0] += 0.05f;
            w.server.set(ids[size_t(i)], w.game.bPos, p);
        }
    };
    w.run(120);
    ServerStats s2 = w.server.stats();
    const uint64_t snaps2 = s2.snapshots - s1.snapshots;
    const double perSnap = double(s2.evaluated - s1.evaluated) / double(snaps2);
    std::printf("    10 moving: %.1f evaluated per snapshot, %.1f bytes per update, %llu delta-coded fields of %llu updates\n", perSnap,
                double(s2.snapshotBytes - s1.snapshotBytes - snaps2 * 14) / double(s2.entries - s1.entries),
                (unsigned long long)(s2.deltaFields - s1.deltaFields), (unsigned long long)(s2.entries - s1.entries));
    CHECK(perSnap >= 9.5 && perSnap <= 11);
    CHECK(s2.deltaFields - s1.deltaFields >= (s2.entries - s1.entries) * 9 / 10);   // moving positions go as differences
    w.step = nullptr;
    w.run(10);
    bool same = true;
    for (ObjectId id : ids) same &= sameState(w, c, id);
    CHECK(same);
    // a viewer that moves a little needs no grid pass; far does, and relevance follows
    w.server.setViewers(1, {{50, 0, 0}});
    ServerStats s3 = w.server.stats();
    w.run(10);
    CHECK(w.server.stats().interestChecks == s3.interestChecks);
    w.server.setViewers(1, {{20000, 0, 0}});
    w.run(30);
    CHECK(c.objects().empty());
    w.server.setViewers(1, {{0, 0, 0}});
    w.run(30);
    CHECK(c.objects().size() == ids.size());
}

TEST(delta_coded_numbers_survive_loss_and_reordering_of_acks) {
    // Differences are only taken against a confirmed value: under latency and jitter every client still ends up
    // with the server's exact quantized state, and an owner's state matches its last applied input.
    ServerOptions so; so.defaultRadius = 5000;
    LoopbackOptions lo; lo.latencyMs = 60; lo.jitterMs = 40;
    LoopWorld w(so, lo);
    for (int i = 0; i < 4; ++i) w.add();
    for (uint8_t s = 1; s <= 4; ++s) w.server.setViewers(s, {{0, 0, 0}});
    std::vector<ObjectId> ids;
    for (int i = 0; i < 200; ++i) ids.push_back(w.server.create(i % 2 ? w.game.prop : w.game.player));
    std::mt19937 rng(3);
    w.step = [&] {
        for (size_t i = 0; i < ids.size(); ++i) {
            const float a = float(w.ticks) * 0.02f + float(i);
            if ((i + w.ticks) % 13 != 0) continue;                       // every ~200 ms: the last send is confirmed
            w.server.set(ids[i], 0, vec(std::cos(a) * float(10 + i), float(i % 5), std::sin(a) * float(10 + i)));
            if (rng() % 97 == 0) w.server.set(ids[i], 0, vec(1000, 0, -1000));   // a jump: absolute again
            if (i % 2 == 0) w.server.set(ids[i], w.game.pHp, ival(int64_t((w.ticks / 7 + i) % 100)));
        }
    };
    w.run(600);
    w.step = nullptr;
    w.run(60);
    bool same = true;
    for (auto& c : w.clients) for (ObjectId id : ids) same &= sameState(w, *c, id);
    CHECK(same);
    std::printf("    %llu delta-coded fields in %llu updates\n", (unsigned long long)w.server.stats().deltaFields,
                (unsigned long long)w.server.stats().entries);
    CHECK(w.server.stats().deltaFields > 5000);
    for (auto& c : w.clients) CHECK(c->stats().dropped == 0);
}

TEST(creates_confirm_when_rtt_exceeds_the_snapshot_interval) {
    // Regression: an object changing every snapshot over a link with RTT > interval was re-sent as a full create
    // forever (each resend moved the create to a newer, still unacked snapshot).
    ServerOptions so; so.defaultRadius = 5000;
    LoopbackOptions lo; lo.latencyMs = 80;
    LoopWorld w(so, lo);
    Client& c = w.add();
    w.server.setViewers(1, {{0, 0, 0}});
    std::vector<ObjectId> ids;
    for (int i = 0; i < 50; ++i) ids.push_back(w.server.create(w.game.prop));
    w.step = [&] { for (size_t i = 0; i < ids.size(); ++i) w.server.set(ids[i], 0, vec(float(w.ticks) * 0.1f, float(i), 0)); };
    w.run(120);
    const ServerStats s0 = w.server.stats();
    w.run(300);
    const ServerStats s1 = w.server.stats();
    std::printf("    RTT 160 ms: %llu creates in %llu updates after the first 2 s\n", (unsigned long long)(s1.creates - s0.creates),
                (unsigned long long)(s1.entries - s0.entries));
    CHECK(s1.entries - s0.entries > 4000);
    CHECK(s1.creates == s0.creates);
    bool same = true;
    w.step = nullptr;
    w.run(30);
    for (ObjectId id : ids) same &= sameState(w, c, id);
    CHECK(same);
}

TEST(interest_3d_towers_and_floors) {
    uint64_t checks[2] = {0, 0};
    std::set<ObjectId> seen[2];
    for (int mode = 0; mode < 2; ++mode) {
        ServerOptions so; so.defaultRadius = 30; so.cellSize = 16; so.interest3d = mode == 0;
        LoopWorld w(so);
        Client& c = w.add();
        // two towers of 20 floors, 4 m apart vertically... 40 units per floor, 40 objects per floor
        std::vector<std::pair<ObjectId, int>> floorOf;
        for (int tower = 0; tower < 2; ++tower)
            for (int f = 0; f < 20; ++f)
                for (int i = 0; i < 40; ++i) {
                    ObjectId id = w.server.create(w.game.prop);
                    const float a = float(i) * 0.157f, r = float(i % 4) * 5;
                    w.server.set(id, 0, vec(float(tower) * 500 + std::cos(a) * r, float(f) * 40, std::sin(a) * r));
                    floorOf.push_back({id, tower * 100 + f});
                }
        auto onFloor = [&](int f) {
            for (auto& [id, fl] : floorOf) if ((fl == f) != c.exists(id)) return false;
            return true;
        };
        w.server.setViewers(1, {{0, 3 * 40 + 1, 0}});
        w.run(30);
        CHECK(onFloor(3));                      // floors 2 and 4 are 40 above / below: outside the 30 sphere
        const uint64_t before = w.server.stats().interestChecks;
        // walk up the stairs to floor 12, one unit per tick, then across to the other tower
        for (int y = 3 * 40 + 1; y <= 12 * 40 + 1; ++y) { w.server.setViewers(1, {{0, float(y), 0}}); w.frame(); }
        w.run(10);
        CHECK(onFloor(12));
        w.server.setViewers(1, {{500, 7 * 40 + 1, 0}});
        w.run(10);
        CHECK(onFloor(107));
        checks[mode] = w.server.stats().interestChecks - before;
        for (ObjectId id : c.objects()) seen[mode].insert(id);
    }
    std::printf("    distance tests while climbing: %llu with interest3d, %llu with x/z columns\n", (unsigned long long)checks[0],
                (unsigned long long)checks[1]);
    CHECK(seen[0] == seen[1]);                  // the same relevance either way
    CHECK(checks[0] * 8 < checks[1]);           // but a column of 20 floors is not scanned
}

TEST(id_reuse_never_shows_the_previous_objects_reliable_fields) {
    LoopWorld w;
    auto [a, b] = makeLoopbackPair();
    w.server.attach(std::move(a));
    auto holdOwned = std::make_unique<HoldConn>(std::move(b));
    HoldConn* hold = holdOwned.get();
    w.clients.push_back(std::make_unique<Client>(w.game.schema, std::move(holdOwned)));
    Client& c = *w.clients.back();
    w.server.setViewers(1, {{0, 0, 0}});
    ObjectId first = w.server.create(w.game.player);
    w.server.set(first, w.game.pName, sval("old"));
    w.run(30);
    CHECK(c.exists(first) && c.latest(first, w.game.pName).s == "old");
    int created = 0, destroyed = 0;
    c.onCreate = [&](ObjectId id) { created += id == first; };
    c.onDestroy = [&](ObjectId id) { destroyed += id == first; };
    // the reliable stream stalls: the removal snapshot is acked, the server forgets, but Forget is held back
    hold->hold = true;
    w.server.destroy(first);
    w.run(30);
    CHECK(!c.exists(first) && destroyed == 1);
    ObjectId second = w.server.create(w.game.player);   // same id, same class, no name
    CHECK(second == first);
    w.server.set(second, w.game.pPos, vec(1, 2, 3));
    w.run(30);
    CHECK(c.exists(second) && created == 1);
    CHECK(c.latest(second, w.game.pName).s.empty());     // not "old"
    w.server.set(second, w.game.pName, sval("new"));    // held too
    w.run(10);
    CHECK(c.latest(second, w.game.pName).s.empty());
    hold->hold = false;                                  // Forget(old generation), then Field(new generation)
    w.run(10);
    CHECK(c.latest(second, w.game.pName).s == "new");
    // a recreate of the same id seen as a create entry while the old one is still shown: replaced, not merged
    hold->hold = true;
    w.server.destroy(second);
    w.run(30);
    ObjectId third = w.server.create(w.game.player);
    CHECK(third == first);
    w.run(30);
    CHECK(c.exists(third) && c.latest(third, w.game.pName).s.empty());
    hold->hold = false;
    w.run(10);
    CHECK(c.latest(third, w.game.pName).s.empty());      // the late Forget of "new" clears nothing of the third
    CHECK(c.stats().dropped == 0);
}

TEST(render_clock_is_monotonic_and_smooth_under_jitter) {
    LoopbackOptions lo; lo.latencyMs = 40; lo.jitterMs = 60;
    ServerOptions so; so.defaultRadius = 5000;
    LoopWorld w(so, lo);
    Client& c = w.add();
    w.server.setViewers(1, {{0, 0, 0}});
    ObjectId id = w.server.create(w.game.prop);
    w.step = [&] { w.server.set(id, 0, vec(float(w.ticks) / 12, 0, 0)); };   // 5 u/s
    w.run(120);
    double prevR = c.renderTimeMs();
    uint64_t prevNow = w.now;
    double minRate = 10, maxRate = 0, worstStep = 0;
    float prevX = c.sample(id, 0).f[0];
    for (int f = 0; f < 1800; ++f) {
        w.frame();
        const double r = c.renderTimeMs();
        CHECK(r >= prevR);
        const double rate = (r - prevR) / double(w.now - prevNow);
        minRate = std::min(minRate, rate); maxRate = std::max(maxRate, rate);
        const float x = c.sample(id, 0).f[0];
        worstStep = std::max(worstStep, std::fabs(double(x - prevX) / (double(w.now - prevNow) / 1000.0) - 5.0));
        prevR = r; prevNow = w.now; prevX = x;
    }
    const double newest = double(c.latestTick()) * 1000.0 / 60.0;
    const double server = double(w.server.serverTick()) * 1000.0 / 60.0;
    std::printf("    clock: rate %.3f..%.3f, jitter %.1f ms, delay %.0f ms, newest - render %.0f ms, server time error %.0f ms, "
                "underruns %llu, worst speed error %.2f u/s\n", minRate, maxRate, c.jitterMs(), c.interpolationDelayMs(),
                newest - c.renderTimeMs(), c.serverTimeMs() - server, (unsigned long long)c.stats().underruns, worstStep);
    CHECK(minRate >= 0.949 && maxRate <= 1.051);
    CHECK(c.stats().clockSnaps == 0);
    CHECK(c.jitterMs() > 5 && c.jitterMs() < 60);
    CHECK(std::fabs(c.renderTick() * 1000.0 / 60.0 - c.renderTimeMs()) < 1e-6);
    CHECK(std::fabs(c.serverTimeMs() - server) < 80);
    CHECK(c.stats().underruns <= 30);         // 30 s, delay 100 ms vs 50 ms interval + up to 60 ms jitter
    CHECK(worstStep < 5 * 0.3);
}

TEST(render_clock_follows_a_slow_server_without_drifting) {
    LoopbackOptions lo; lo.latencyMs = 30;
    LoopWorld w({}, lo);
    Client& c = w.add();
    w.server.setViewers(1, {{0, 0, 0}});
    ObjectId id = w.server.create(w.game.prop);
    w.run(60);
    // the server ticks 3 % slower than real time (an overloaded host): render time slows down with it
    double lagEarly = 0, lagLate = 0, prevR = c.renderTimeMs();
    for (int f = 0; f < 3600; ++f) {
        ++w.ticks;
        w.now = 1000 + w.ticks * 1000 / 60;
        w.server.poll(w.now);
        w.server.set(id, 0, vec(float(f), 0, 0));
        if (f % 33 != 0) w.server.tick();
        w.server.broadcast(w.now);
        c.poll(w.now);
        CHECK(c.renderTimeMs() >= prevR);
        prevR = c.renderTimeMs();
        const double lag = c.serverTimeMs() - c.rttMs() / 2.0 - c.renderTimeMs();   // newest server time - render
        if (f == 600) lagEarly = lag;
        if (f == 3599) lagLate = lag;
    }
    std::printf("    slow server: newest - render %.0f ms after 10 s, %.0f ms after 60 s (delay 100)\n", lagEarly, lagLate);
    CHECK(std::fabs(lagEarly - 100) < 15);
    CHECK(std::fabs(lagLate - 100) < 15);
    CHECK(c.stats().clockSnaps == 0);
}

TEST(adaptive_delay_follows_jitter) {
    double delays[2] = {0, 0};
    uint64_t under[2] = {0, 0};
    for (int k = 0; k < 2; ++k) {
        LoopbackOptions lo; lo.latencyMs = 30; lo.jitterMs = k == 0 ? 0 : 80;
        ClientOptions co; co.adaptiveDelay = true; co.interpolationDelayMs = 100;
        LoopWorld w({}, lo, co);
        Client& c = w.add();
        w.server.setViewers(1, {{0, 0, 0}});
        ObjectId id = w.server.create(w.game.prop);
        w.step = [&] { w.server.set(id, 0, vec(float(w.ticks) / 12, 0, 0)); };
        w.run(600);
        const uint64_t u0 = c.stats().underruns;
        double prevR = c.renderTimeMs();
        for (int f = 0; f < 1800; ++f) { w.frame(); CHECK(c.renderTimeMs() >= prevR); prevR = c.renderTimeMs(); }
        delays[k] = c.interpolationDelayMs();
        under[k] = c.stats().underruns - u0;
    }
    std::printf("    adaptive delay: %.0f ms (no jitter, %llu underruns), %.0f ms (80 ms jitter, %llu underruns) in 30 s\n", delays[0],
                (unsigned long long)under[0], delays[1], (unsigned long long)under[1]);
    CHECK(delays[0] >= 50 && delays[0] < 90);   // ~1.5 intervals: lower latency than the fixed 100 ms
    CHECK(delays[1] > delays[0] + 40);
    CHECK(under[0] == 0);
    CHECK(under[1] <= 15);
}

} // namespace rt
