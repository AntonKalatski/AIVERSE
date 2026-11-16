// Server <-> client behaviour over loopback (docs/replica.md): deltas, joins, interest, budget, ids,
// reliable fields, messages, interpolation, prediction.
#include <algorithm>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include "tests/replica/ReplicaTest.h"

namespace rt {

TEST(delta_snapshots_equal_full_state) {
    ServerOptions so; so.bytesPerSecond = 1 << 24; so.maxSnapshotBytes = 100000; so.defaultRadius = 5000;
    LoopWorld w(so);
    Client& c = w.add();
    w.server.setViewers(1, {{0, 0, 0}});
    std::mt19937 rng(1);
    std::vector<ObjectId> ids;
    for (int i = 0; i < 120; ++i) ids.push_back(w.server.create(ClassId(rng() % 2 ? w.game.player : w.game.prop)));
    ObjectId st = w.server.create(w.game.state);
    std::uniform_real_distribution<float> u(-500, 500);
    bool allEqual = true;
    int checked = 0;
    w.step = [&] {
        // random sparse changes: some fields, some objects, sometimes back to an older value
        for (int k = 0; k < 40; ++k) {
            ObjectId id = ids[rng() % ids.size()];
            switch (rng() % 5) {
            case 0: w.server.set(id, 0, vec(u(rng), u(rng), u(rng))); break;
            case 1: w.server.set(id, 1, quatYaw(u(rng))); break;
            case 2: w.server.set(id, 2, ival(rng() % 2 ? 1 : int64_t(rng() % 100))); break;
            case 3: w.server.setOwner(id, uint8_t(rng() % 3)); break;
            default: w.server.set(id, 0, vec(0, 0, 0)); break;
            }
        }
        w.server.set(st, w.game.sClock, fval(float(w.ticks) / 60));
        if (w.ticks % 30 == 0) w.server.set(st, w.game.sTitle, sval("t" + std::to_string(w.ticks)));
    };
    for (int f = 0; f < 600; ++f) {
        const uint64_t snaps = w.server.stats().snapshots;
        w.frame();
        if (f > 10 && w.server.stats().snapshots != snaps) {     // zero latency: the client holds exactly what was sent
            for (ObjectId id : ids) { allEqual &= sameState(w, c, id); ++checked; }
            allEqual &= sameState(w, c, st);
        }
    }
    CHECK(allEqual && checked > 10000);
    CHECK(c.stats().dropped == 0);
    // deltas are much smaller than full states: a quiet world costs only headers
    w.step = nullptr;
    w.run(30);
    uint64_t before = w.server.stats().snapshotBytes, snaps = w.server.stats().snapshots;
    w.run(60);
    CHECK((w.server.stats().snapshotBytes - before) / std::max<uint64_t>(1, w.server.stats().snapshots - snaps) < 24);
}

TEST(eight_clients_converge_with_latency) {
    ServerOptions so; so.defaultRadius = 200;
    LoopbackOptions lo; lo.latencyMs = 40; lo.jitterMs = 20;
    LoopWorld w(so, lo);
    std::map<uint8_t, ObjectId> avatar;
    w.server.onJoin = [&](uint8_t slot) {
        ObjectId id = w.server.create(w.game.player);
        w.server.setOwner(id, slot);
        w.server.set(id, w.game.pName, sval("player" + std::to_string(slot)));
        w.server.set(id, w.game.pPos, vec(float(slot) * 10, 0, 0));
        avatar[slot] = id;
    };
    w.server.onInput = [&](uint8_t slot, uint32_t, const Bytes& in) {
        if (in.size() != 2) return;
        ObjectId id = avatar[slot];
        Value p = w.server.get(id, w.game.pPos);
        p.f[0] += (int8_t(in[0])) * 0.05f; p.f[2] += (int8_t(in[1])) * 0.05f;
        w.server.set(id, w.game.pPos, p);
    };
    std::vector<ObjectId> props;
    for (int i = 0; i < 200; ++i) props.push_back(w.server.create(w.game.prop));
    bool moving = true;
    w.step = [&] {
        for (size_t i = 0; i < props.size(); ++i) {
            float a = moving ? float(w.ticks) * 0.01f + float(i) : float(i);
            w.server.set(props[i], w.game.bPos, vec(std::cos(a) * float(20 + i % 80), 0, std::sin(a) * float(20 + i % 80)));
            w.server.set(props[i], w.game.bAwake, ival(moving));
        }
        for (auto& [slot, id] : avatar) { Value p = w.server.get(id, w.game.pPos); w.server.setViewers(slot, {{p.f[0], p.f[1], p.f[2]}}); }
    };
    for (int i = 0; i < 8; ++i) w.add();
    for (int f = 0; f < 600; ++f) {
        if (f >= 30) for (size_t i = 0; i < w.clients.size(); ++i) {
            int8_t dx = int8_t(int(i % 3) - 1), dz = int8_t((f / 100) % 2 ? 1 : -1);
            if (f < 450) w.clients[i]->sendInput({uint8_t(dx), uint8_t(dz)});
        }
        if (f == 450) moving = false;
        w.frame();
    }
    CHECK(w.allReady() && avatar.size() == 8);
    bool same = true, ownedOk = true;
    size_t seen = 0;
    for (auto& c : w.clients) {
        for (ObjectId id : c->objects()) { same &= sameState(w, *c, id); ++seen; }
        ownedOk &= c->exists(avatar[c->slot()]) && c->ownerOf(avatar[c->slot()]) == c->slot();
        ownedOk &= c->latest(avatar[c->slot()], w.game.pName).s == "player" + std::to_string(c->slot());
        ownedOk &= c->lastAppliedInput() == 420;   // inputs 1..420 all applied, in order
        CHECK(c->stats().dropped == 0);
    }
    CHECK(same && ownedOk && seen > 8 * 150);
    // every object within range of a viewer is present
    bool complete = true;
    for (auto& c : w.clients) {
        Value me = w.server.get(avatar[c->slot()], w.game.pPos);
        for (ObjectId id : props) {
            Value p = w.server.get(id, w.game.bPos);
            float d = std::hypot(p.f[0] - me.f[0], p.f[2] - me.f[2]);
            if (d < 190) complete &= c->exists(id);
            if (d > 240) complete &= !c->exists(id);
        }
    }
    CHECK(complete);
}

TEST(late_join_gets_full_state) {
    LoopbackOptions lo; lo.latencyMs = 25;
    LoopWorld w({}, lo);
    w.add();
    std::vector<ObjectId> ids;
    for (int i = 0; i < 60; ++i) {
        ObjectId id = w.server.create(i % 3 ? w.game.prop : w.game.player);
        w.server.set(id, 0, vec(float(i) * 0.5f, 1, -float(i) * 0.5f));
        if (i % 3 == 0) w.server.set(id, w.game.pName, sval("p" + std::to_string(i)));
        ids.push_back(id);
    }
    ObjectId st = w.server.create(w.game.state);
    w.server.set(st, w.game.sRound, ival(7));
    w.server.set(st, w.game.sTitle, sval("final"));
    w.server.setViewers(1, {{0, 0, 0}});
    w.run(300);
    // a client that joins now sees everything without any further change on the server
    Client& late = w.add();
    w.server.setViewers(2, {{0, 0, 0}});
    int creates = 0;
    late.onCreate = [&](ObjectId) { ++creates; };
    w.run(40);
    CHECK(late.ready());
    bool same = true;
    for (ObjectId id : ids) same &= sameState(w, late, id);
    same &= sameState(w, late, st);
    CHECK(same && creates == 61);
    CHECK(late.latest(st, w.game.sTitle).s == "final" && late.latest(ids[3], w.game.pName).s == "p3");
}

TEST(destroy_and_id_reuse_wait_for_every_client) {
    LoopbackOptions fast; fast.latencyMs = 5;
    LoopWorld w({}, fast);
    w.add();
    w.link.latencyMs = 400;       // the second client is far away
    w.add();
    w.server.setViewers(1, {{0, 0, 0}});
    w.server.setViewers(2, {{0, 0, 0}});
    ObjectId a = w.server.create(w.game.player);
    w.server.set(a, w.game.pName, sval("doomed"));
    w.run(120);
    CHECK(w.clients[0]->exists(a) && w.clients[1]->exists(a));
    int destroyed = 0;
    w.clients[0]->onDestroy = [&](ObjectId id) { destroyed += id == a; };
    w.server.destroy(a);
    CHECK(!w.server.exists(a));
    w.run(10);
    CHECK(!w.clients[0]->exists(a) && destroyed == 1);
    ObjectId b = w.server.create(w.game.prop);     // the far client has not acked yet: no reuse
    CHECK(b != a);
    w.run(120);
    CHECK(!w.clients[1]->exists(a));
    ObjectId c = w.server.create(w.game.item);     // both acked: the id comes back
    CHECK(c == a);
    w.server.set(c, w.game.iKind, ival(3));
    w.run(120);
    for (auto& cl : w.clients) {
        CHECK(cl->exists(c) && cl->classOf(c) == w.game.item);
        CHECK(cl->latest(c, w.game.iKind).i == 3);
        CHECK(cl->latest(c, w.game.pName).s.empty());    // nothing of the old object's reliable fields
    }
}

TEST(interest_far_objects_leave_and_come_back) {
    ServerOptions so; so.defaultRadius = 50; so.cellSize = 16;
    LoopWorld w(so);
    Client& c = w.add();
    w.server.setViewers(1, {{0, 0, 0}});
    ObjectId near = w.server.create(w.game.prop), far = w.server.create(w.game.prop), mover = w.server.create(w.game.player);
    ObjectId st = w.server.create(w.game.state);
    w.server.set(near, 0, vec(10, 0, 10));
    w.server.set(far, 0, vec(500, 0, 0));
    w.server.set(mover, 0, vec(40, 0, 0));
    w.server.set(mover, w.game.pName, sval("walker"));
    w.run(30);
    CHECK(c.exists(near) && !c.exists(far) && c.exists(mover) && c.exists(st));   // state: always relevant
    // walk away: hysteresis keeps it up to 1.15 x radius
    w.server.set(mover, 0, vec(55, 0, 0)); w.run(10);
    CHECK(c.exists(mover));
    w.server.set(mover, 0, vec(300, 0, 0)); w.run(10);
    CHECK(!c.exists(mover));
    uint64_t bytes = w.server.stats().snapshotBytes;
    for (int i = 0; i < 60; ++i) { w.server.set(mover, 0, vec(300 + float(i), 0, 0)); w.frame(); }
    CHECK(w.server.stats().snapshotBytes - bytes < 20 * 20 * 2);   // only headers: far motion is not sent
    // back near: full state again, reliable fields included
    w.server.set(mover, 0, vec(20, 0, 0)); w.run(10);
    CHECK(c.exists(mover) && sameState(w, c, mover) && c.latest(mover, w.game.pName).s == "walker");
    // two viewers (e.g. character + remote camera)
    w.server.setViewers(1, {{0, 0, 0}, {500, 0, 0}});
    w.run(10);
    CHECK(c.exists(far) && c.exists(near));
    w.server.setViewers(1, {});
    w.run(10);
    CHECK(!c.exists(far) && !c.exists(near) && c.exists(st));
    // owned objects are always relevant, hidden information overrides everything
    w.server.setOwner(far, 1);
    w.run(10);
    CHECK(c.exists(far) && c.ownerOf(far) == 1);
    w.server.setVisibleTo(far, 1, false);
    w.server.setVisibleTo(near, 1, true);
    w.run(10);
    CHECK(!c.exists(far) && c.exists(near));
    w.server.resetVisibility(far);
    w.server.resetVisibility(near, 1);
    w.run(10);
    CHECK(c.exists(far) && !c.exists(near));
}

TEST(budget_caps_bytes_and_starved_objects_catch_up) {
    ServerOptions so; so.bytesPerSecond = 8000; so.defaultRadius = 5000;
    LoopWorld w(so);
    Client& c = w.add();
    w.server.setViewers(1, {{0, 0, 0}});
    std::vector<ObjectId> ids;
    for (int i = 0; i < 300; ++i) ids.push_back(w.server.create(w.game.prop));
    std::mt19937 rng(4);
    w.step = [&] {
        for (size_t i = 0; i < ids.size(); ++i) {
            float a = float(w.ticks) * 0.03f + float(i);
            w.server.set(ids[i], w.game.bPos, vec(std::cos(a) * 100, float(i % 7), std::sin(a) * 100));
            w.server.set(ids[i], w.game.bRot, quatYaw(a));
        }
    };
    w.run(120);   // warm up: creates go out over the first seconds
    std::map<ObjectId, std::pair<Value, uint64_t>> last;
    uint64_t worstGap = 0;
    const uint64_t bytes0 = w.server.stats().snapshotBytes, t0 = w.now;
    for (int f = 0; f < 600; ++f) {
        w.frame();
        for (ObjectId id : ids) {
            const Value& v = c.latest(id, w.game.bPos);
            auto it = last.find(id);
            if (it == last.end()) { last[id] = {v, w.now}; continue; }
            if (!(it->second.first == v)) {
                worstGap = std::max(worstGap, w.now - it->second.second);
                it->second = {v, w.now};
            }
        }
    }
    for (auto& [id, e] : last) worstGap = std::max(worstGap, w.now - e.second);
    const double secs = double(w.now - t0) / 1000.0;
    const double rate = double(w.server.stats().snapshotBytes - bytes0) / secs;
    std::printf("    budget: %.0f bytes/s (limit 8000), slowest object updated every %llu ms, starved %llu\n", rate,
                (unsigned long long)worstGap, (unsigned long long)w.server.stats().starved);
    CHECK(rate <= 8000 * 1.03);
    CHECK(rate >= 8000 * 0.8);           // the budget is actually used
    CHECK(last.size() == ids.size());
    CHECK(worstGap < 1500);              // everybody gets a turn
    CHECK(w.server.stats().starved > 0);
}

TEST(priority_prefers_near_and_important_objects) {
    ServerOptions so; so.bytesPerSecond = 4000; so.defaultRadius = 1000;
    LoopWorld w(so);
    Client& c = w.add();
    w.server.setViewers(1, {{0, 0, 0}});
    std::vector<ObjectId> nearIds, farIds;
    for (int i = 0; i < 100; ++i) nearIds.push_back(w.server.create(w.game.prop));
    for (int i = 0; i < 100; ++i) farIds.push_back(w.server.create(w.game.prop));
    std::map<ObjectId, int> updates;
    w.step = [&] {
        for (int i = 0; i < 100; ++i) {
            float a = float(w.ticks) * 0.05f + float(i);
            w.server.set(nearIds[size_t(i)], 0, vec(std::cos(a) * 20, 0, std::sin(a) * 20));
            w.server.set(farIds[size_t(i)], 0, vec(900 + std::cos(a) * 20, 0, std::sin(a) * 20));
        }
    };
    std::map<ObjectId, Value> prev;
    for (int f = 0; f < 900; ++f) {
        w.frame();
        if (f < 300) continue;
        for (ObjectId id : c.objects()) { const Value& v = c.latest(id, 0); if (!(prev[id] == v)) ++updates[id]; prev[id] = v; }
    }
    int nearSum = 0, farSum = 0;
    for (ObjectId id : nearIds) nearSum += updates[id];
    for (ObjectId id : farIds) { farSum += updates[id]; CHECK(updates[id] > 0); }
    CHECK(nearSum > farSum * 3 / 2);
}

TEST(reliable_fields_and_remote_messages) {
    ServerOptions so; so.maxMessagesPerSecond = 10; so.maxMessageBytes = 100;
    LoopbackOptions lo; lo.latencyMs = 30;
    LoopWorld w(so, lo);
    Client& c = w.add();
    w.server.setViewers(1, {{0, 0, 0}});
    ObjectId p = w.server.create(w.game.player);
    w.run(30);
    std::vector<std::string> names;
    c.onReliableChange = [&](ObjectId id, int f) { if (id == p && f == w.game.pName) names.push_back(c.latest(p, f).s); };
    for (int i = 0; i < 5; ++i) { w.server.set(p, w.game.pName, sval("name" + std::to_string(i))); w.frame(); }
    w.server.set(p, w.game.pName, sval("name4"));   // unchanged: not resent
    w.run(30);
    CHECK((names == std::vector<std::string>{"name0", "name1", "name2", "name3", "name4"}));
    // remote events both ways
    std::vector<std::pair<uint16_t, Bytes>> got, back;
    w.server.onMessage = [&](uint8_t slot, uint16_t ch, const Bytes& b) { CHECK(slot == 1); got.push_back({ch, b}); };
    c.onMessage = [&](uint16_t ch, const Bytes& b) { back.push_back({ch, b}); };
    c.send(3, {1, 2, 3});
    c.send(4, Bytes(101, 0));             // over the size limit: dropped
    w.run(10);
    CHECK(got.size() == 1 && got[0].first == 3 && got[0].second == Bytes({1, 2, 3}));
    w.server.send(0, 9, {7});
    w.server.send(1, 10, {8});
    w.server.send(5, 11, {9});            // nobody in slot 5
    w.run(10);
    CHECK(back.size() == 2 && back[0].first == 9 && back[1].first == 10);
    // rate limit: a burst of 100 gets at most the burst allowance through
    got.clear();
    for (int i = 0; i < 100; ++i) c.send(1, {uint8_t(i)});
    w.run(10);
    CHECK(got.size() >= 15 && got.size() <= 22);
    CHECK(w.server.stats().rejected >= 78);
    // long run: about the configured rate
    got.clear();
    for (int f = 0; f < 120; ++f) { for (int k = 0; k < 3; ++k) c.send(1, {1}); w.frame(); }
    CHECK(got.size() >= 15 && got.size() <= 30);
}

TEST(input_order_cap_and_ack) {
    ServerOptions so; so.maxInputsPerTick = 2;
    LoopWorld w(so);
    Client& c = w.add();
    std::vector<uint32_t> seqs;
    std::vector<int> perTick;
    w.server.onInput = [&](uint8_t, uint32_t s, const Bytes& b) {
        uint32_t v; std::memcpy(&v, b.data(), 4);
        CHECK(v == s);
        seqs.push_back(s);
        ++perTick.back();
    };
    w.run(10);
    uint32_t next = 1;
    for (int f = 0; f < 60; ++f) {
        for (int k = 0; k < 3; ++k) { Bytes b(4); std::memcpy(b.data(), &next, 4); CHECK(c.sendInput(b) == next); ++next; }
        perTick.push_back(0);
        w.frame();
    }
    CHECK(c.lastAppliedInput() > 0);
    bool inOrder = std::is_sorted(seqs.begin(), seqs.end()) && std::adjacent_find(seqs.begin(), seqs.end()) == seqs.end();
    CHECK(inOrder);
    CHECK(*std::max_element(perTick.begin(), perTick.end()) <= 2);
}

TEST(interpolation_is_smooth) {
    ServerOptions so; so.snapshotRate = 20;
    LoopbackOptions lo; lo.latencyMs = 40; lo.jitterMs = 20;
    ClientOptions co; co.interpolationDelayMs = 100;
    LoopWorld w(so, lo, co);
    Client& c = w.add();
    w.server.setViewers(1, {{0, 0, 0}});
    ObjectId id = w.server.create(w.game.prop);
    const float speed = 5;      // units per second along x, turning at 1 rad/s
    w.step = [&] {
        float t = float(w.ticks) / 60.0f;
        w.server.set(id, w.game.bPos, vec(-40 + speed * t, 0, 0));
        w.server.set(id, w.game.bRot, quatYaw(t));
    };
    w.run(60);
    float prevX = c.sample(id, w.game.bPos).f[0];
    uint64_t prevT = w.now;
    double worst = 0, sum = 0;
    int n = 0, holds = 0;
    Value prevQ = c.sample(id, w.game.bRot);
    double worstTurn = 0;
    for (int f = 0; f < 300; ++f) {
        w.frame();
        Value p = c.sample(id, w.game.bPos);
        double v = (p.f[0] - prevX) / (double(w.now - prevT) / 1000.0);
        worst = std::max(worst, std::fabs(v - speed));
        if (p.f[0] == prevX) ++holds;
        sum += v; ++n;
        prevX = p.f[0]; prevT = w.now;
        Value q = c.sample(id, w.game.bRot);
        double d = 0; for (size_t k = 0; k < 4; ++k) d += double(q.f[k]) * prevQ.f[k];
        worstTurn = std::max(worstTurn, 2 * std::acos(std::min(1.0, std::fabs(d))));
        prevQ = q;
    }
    std::printf("    interpolation: mean %.3f u/s (true %.1f), worst frame %.3f, holds %d, worst turn/frame %.4f rad\n",
                sum / n, speed, worst, holds, worstTurn);
    CHECK(std::fabs(sum / n - speed) < 0.1);
    CHECK(worst < speed * 0.35);
    CHECK(holds == 0);
    CHECK(worstTurn < 0.03);    // 1 rad/s at ~16 ms frames = 0.017 per frame
    // render time trails the newest snapshot by about the interpolation delay
    double lag = (w.server.get(id, w.game.bPos).f[0] - c.sample(id, w.game.bPos).f[0]) / speed;
    CHECK(lag > 0.08 && lag < 0.2);
}

TEST(interpolation_holds_after_stall) {
    LoopWorld w;
    w.copt.extrapolateMs = 100;
    Client& c = w.add();
    w.server.setViewers(1, {{0, 0, 0}});
    ObjectId id = w.server.create(w.game.prop);
    bool move = true;
    w.step = [&] { if (move) w.server.set(id, 0, vec(float(w.ticks) * 0.1f, 0, 0)); };   // 6 u/s
    w.run(120);
    // the object stops: it must not drift past where it stopped
    move = false;
    float stopX = w.server.get(id, 0).f[0];
    w.run(60);
    CHECK(std::fabs(c.sample(id, 0).f[0] - stopX) < 0.02f);
    // the network stalls while it moves: bounded extrapolation, then hold
    move = true;
    w.run(60);
    float last = w.server.get(id, 0).f[0];
    float prev = c.sample(id, 0).f[0], peak = prev;
    int still = 0;
    for (int i = 0; i < 90; ++i) {       // no broadcast: no snapshots arrive
        ++w.ticks; w.now = 1000 + w.ticks * 1000 / 60;
        w.server.poll(w.now); w.step(); w.server.tick();
        c.poll(w.now);
        float x = c.sample(id, 0).f[0];
        CHECK(x >= prev - 1e-4f);
        if (x == prev) ++still;
        prev = x; peak = std::max(peak, x);
    }
    CHECK(peak <= last + 6 * 0.1f + 0.05f);   // at most extrapolateMs past the newest snapshot
    CHECK(still > 60);                         // then it holds
}

TEST(predictor_reconciles_with_toy_step) {
    LoopbackOptions lo; lo.latencyMs = 50; lo.jitterMs = 10;
    LoopWorld w({}, lo);
    Client& c = w.add();
    ObjectId mine = kNoObject;
    w.server.onJoin = [&](uint8_t slot) { mine = w.server.create(w.game.player); w.server.setOwner(mine, slot); };
    auto step = [](Value& s, const float& in) { s.f[0] += in; };
    w.server.onInput = [&](uint8_t, uint32_t, const Bytes& b) {
        float in; if (b.size() != 4) return; std::memcpy(&in, b.data(), 4);
        Value p = w.server.get(mine, w.game.pPos); step(p, in); w.server.set(mine, w.game.pPos, p);
    };
    Predictor<Value, float> pred(step);
    w.run(20);
    CHECK(c.ready() && c.exists(mine) && c.ownerOf(mine) == c.slot());
    pred.reset(c.latest(mine, w.game.pPos));
    uint32_t seenTick = c.latestTick();
    float worst = 0, teleport = 0;
    float total = 0;
    for (int f = 0; f < 400; ++f) {
        float in = (f / 50) % 2 ? 0.25f : -0.1f;
        Bytes b(4); std::memcpy(b.data(), &in, 4);
        pred.local(c.sendInput(b), in);
        total += in;
        if (f == 300) { Value p = w.server.get(mine, w.game.pPos); p.f[0] += 5; w.server.set(mine, w.game.pPos, p); }
        w.frame();
        if (c.latestTick() != seenTick) {
            seenTick = c.latestTick();
            float corr = pred.reconcile(c.latest(mine, w.game.pPos), c.lastAppliedInput(),
                                        [](const Value& a, const Value& b2) { return std::fabs(a.f[0] - b2.f[0]); });
            if (f < 300) worst = std::max(worst, corr); else teleport = std::max(teleport, corr);
        }
    }
    std::printf("    predictor: worst correction %.4f before the teleport, %.3f at it, pending %zu\n", worst, teleport, pred.pending());
    CHECK(worst < 0.011f);                       // only quantization (0.01)
    CHECK(teleport > 4.9f && teleport < 5.1f);   // a server-side change shows up as one correction
    CHECK(std::fabs(pred.state().f[0] - (total + 5)) < 0.05f);
    CHECK(pred.pending() < 16);                  // ~RTT worth of inputs in flight
}

TEST(kick_and_leave_release_tracking) {
    LoopWorld w;
    w.add(); w.add();
    w.server.setViewers(1, {{0, 0, 0}});
    w.server.setViewers(2, {{0, 0, 0}});
    std::vector<uint8_t> left;
    w.server.onLeave = [&](uint8_t s) { left.push_back(s); };
    ObjectId a = w.server.create(w.game.prop);
    w.run(20);
    CHECK(w.clients[0]->exists(a) && w.clients[1]->exists(a));
    w.server.kick(1);
    w.clients[1].reset();          // the other one just disappears
    w.run(5);
    CHECK((left == std::vector<uint8_t>{1, 2}));
    CHECK(!w.clients[0]->connected());
    CHECK(w.server.clients().empty());
    w.server.destroy(a);
    CHECK(w.server.create(w.game.prop) == a);   // nobody tracks it any more: immediate reuse
}

} // namespace rt
