// Schema, quantization, bit I/O and hostile input (docs/replica.md § Wire format).
#include <algorithm>
#include <random>
#include <stdexcept>
#include "tests/replica/ReplicaTest.h"

namespace rt {

TEST(bits_round_trip_any_width) {
    std::mt19937_64 rng(7);
    std::vector<std::pair<uint64_t, int>> vals;
    wire::BitWriter w;
    for (int i = 0; i < 5000; ++i) {
        int bits = int(rng() % 65);
        uint64_t v = rng();
        if (bits < 64) v &= bits ? (uint64_t(1) << bits) - 1 : 0;
        vals.push_back({v, bits});
        w.put(v, bits);
        if (i % 7 == 0) { uint32_t x = uint32_t(rng() >> (rng() % 32)); vals.push_back({x, -1}); w.var(x); }
    }
    Bytes out;
    w.finish(out);
    wire::BitReader r(out.data(), out.size());
    bool ok = true;
    for (auto& [v, bits] : vals) ok &= (bits < 0 ? uint64_t(r.var()) : r.get(bits)) == v;
    CHECK(ok && r.ok() && r.remaining() < 8);
    r.get(64); r.get(64);
    CHECK(!r.ok());                 // reading past the end fails, never crashes
}

TEST(quantize_int_bool_bounds) {
    FieldDef f{"n", FieldKind::Int, -5, 10};
    Schema s; ClassDef c{"c", {f}}; s.add(c);
    wire::Layout L(s);
    auto& fl = L.classes[0].fields[0];
    CHECK(fl.bits == 4);
    for (int v = -5; v <= 10; ++v) CHECK(wire::normalize(fl, ival(v)).i == v);
    CHECK(wire::normalize(fl, ival(-100)).i == -5);
    CHECK(wire::normalize(fl, ival(1000)).i == 10);
    CHECK(wire::normalize(fl, ival(INT64_MIN)).i == -5);
    // default Int range: 32 bits signed
    Schema s2; s2.add(ClassDef{"c", {{"n", FieldKind::Int}}});
    wire::Layout L2(s2);
    auto& f2 = L2.classes[0].fields[0];
    CHECK(f2.bits == 32);
    CHECK(wire::normalize(f2, ival(-123456789)).i == -123456789);
    CHECK(wire::normalize(f2, ival(int64_t(1) << 40)).i == 2147483647);
    // the widest range
    Schema s3; s3.add(ClassDef{"c", {{"n", FieldKind::Int, -9.0e18, 9.0e18}}});
    wire::Layout L3(s3);
    CHECK(L3.classes[0].fields[0].bits == 64);
    CHECK(wire::normalize(L3.classes[0].fields[0], ival(-8000000000000000000ll)).i == -8000000000000000000ll);
    Schema s4; s4.add(ClassDef{"c", {{"b", FieldKind::Bool}}});
    wire::Layout L4(s4);
    CHECK(wire::normalize(L4.classes[0].fields[0], ival(5)).i == 1 && wire::normalize(L4.classes[0].fields[0], ival(0)).i == 0);
}

TEST(quantize_float_vec3_precision_and_clamp) {
    Schema s;
    s.add(ClassDef{"c", {{"f", FieldKind::Float, -10, 10, 0.01}, {"p", FieldKind::Vec3, -1000, 1000, 0.05}}});
    wire::Layout L(s);
    auto& ff = L.classes[0].fields[0];
    auto& fp = L.classes[0].fields[1];
    CHECK(ff.bits == 11 && fp.bits == 16 && fp.comps == 3);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> u(-10, 10), u3(-1000, 1000);
    float worst = 0, worst3 = 0;
    for (int i = 0; i < 20000; ++i) {
        float x = u(rng);
        worst = std::max(worst, std::fabs(wire::normalize(ff, fval(x)).f[0] - x));
        Value v = vec(u3(rng), u3(rng), u3(rng));
        Value n = wire::normalize(fp, v);
        for (int a = 0; a < 3; ++a) worst3 = std::max(worst3, std::fabs(n.f[size_t(a)] - v.f[size_t(a)]));
        CHECK(wire::normalize(ff, n) == wire::normalize(ff, wire::normalize(ff, n)));   // idempotent
    }
    CHECK(worst <= 0.005f + 1e-5f);
    CHECK(worst3 <= 0.025f + 1e-4f);
    CHECK(wire::normalize(ff, fval(50)).f[0] == 10.0f);
    CHECK(wire::normalize(ff, fval(-50)).f[0] == -10.0f);
    CHECK(wire::normalize(ff, fval(NAN)).f[0] == -10.0f);
    CHECK(wire::normalize(ff, fval(INFINITY)).f[0] == -10.0f);   // non-finite -> min (never NaN on the wire)
    Value big = wire::normalize(fp, vec(1e30f, -1e30f, 3));
    CHECK(big.f[0] == 1000 && big.f[1] == -1000 && std::fabs(big.f[2] - 3) < 0.03f);
}

TEST(quantize_quat_smallest_three) {
    Schema s;
    s.add(ClassDef{"c", {{"q", FieldKind::Quat}, {"q8", FieldKind::Quat, 0, 0, 0, 8}}});
    wire::Layout L(s);
    auto& fq = L.classes[0].fields[0];
    auto& f8 = L.classes[0].fields[1];
    CHECK(fq.bits == 2 + 36 && f8.bits == 2 + 24);
    std::mt19937 rng(11);
    std::normal_distribution<float> n(0, 1);
    double worst = 0, worst8 = 0;
    for (int i = 0; i < 20000; ++i) {
        Value v; double len = 0;
        for (auto& c : v.f) { c = n(rng); len += double(c) * c; }
        for (auto& c : v.f) c = float(c / std::sqrt(len));
        auto angle = [&](const Value& a) {
            double d = 0; for (size_t k = 0; k < 4; ++k) d += double(a.f[k]) * v.f[k];
            return 2 * std::acos(std::min(1.0, std::fabs(d)));
        };
        Value a = wire::normalize(fq, v), b = wire::normalize(f8, v);
        double l = 0; for (auto c : a.f) l += double(c) * c;
        CHECK(std::fabs(l - 1) < 1e-4);
        worst = std::max(worst, angle(a));
        worst8 = std::max(worst8, angle(b));
    }
    CHECK(worst < 0.003);      // 12 bits: well under 0.2 degrees
    CHECK(worst8 < 0.05);
    Value zero;                // degenerate -> identity
    Value id = wire::normalize(fq, zero);
    CHECK(std::fabs(id.f[3] - 1) < 1e-6);
    Value nan; nan.f = {NAN, 0, 0, 1};
    CHECK(std::isfinite(wire::normalize(fq, nan).f[3]));
    // any packed bit pattern decodes to a unit quaternion
    for (int i = 0; i < 1000; ++i) {
        uint64_t q = rng() & ((uint64_t(1) << 38) - 1);
        Value o; wire::dequantize(fq, &q, o);
        double l = 0; for (auto c : o.f) { CHECK(std::isfinite(c)); l += double(c) * c; }
        CHECK(std::fabs(l - 1) < 1e-4);
    }
}

TEST(schema_rejects_malformed_and_hash_tracks_the_wire) {
    auto bad = [](ClassDef c) { try { Schema s; s.add(c); return false; } catch (const std::runtime_error&) { return true; } };
    CHECK(bad(ClassDef{"", {}}));
    CHECK(bad(ClassDef{"c", {{"a", FieldKind::Float, 1, 1, 0.1}}}));
    CHECK(bad(ClassDef{"c", {{"a", FieldKind::Float, 0, 1, 0}}}));
    CHECK(bad(ClassDef{"c", {{"a", FieldKind::Float, 0, 1e12, 1e-6}}}));   // > 32 bits
    CHECK(bad(ClassDef{"c", {{"a", FieldKind::Int, 5, 1}}}));
    CHECK(bad(ClassDef{"c", {{"a", FieldKind::Quat, 0, 0, 0, 30}}}));
    CHECK(bad(ClassDef{"c", {{"a", FieldKind::Bool}, {"a", FieldKind::Bool}}}));
    CHECK(bad(ClassDef{"c", {{"a", FieldKind::Bool, 0, 0, 0, 0, false, true}}}));   // Bool cannot interpolate
    CHECK(bad(ClassDef{"c", {{"a", FieldKind::Float, 0, 1, 0.1}}, 1, 0, false, 0}));  // position must be Vec3
    CHECK(bad(ClassDef{"c", {}, 1, 0, false, 3}));
    ClassDef many{"c", {}};
    for (int i = 0; i < 64; ++i) many.fields.push_back({"f" + std::to_string(i), FieldKind::Bool});
    CHECK(bad(many));
    Schema dup; dup.add(ClassDef{"c", {}});
    bool threw = false;
    try { dup.add(ClassDef{"c", {}}); } catch (const std::runtime_error& e) { threw = std::string(e.what()).find("'c'") != std::string::npos; }
    CHECK(threw);

    Game a, b;
    CHECK(a.schema.hash() == b.schema.hash());
    CHECK(a.schema.field(a.player, "hp") == 2 && a.schema.field(a.player, "nope") == -1 && a.schema.field(99, "hp") == -1);
    Schema c = a.schema;
    ClassDef extra{"extra", {{"x", FieldKind::Float, 0, 1, 0.5}}};
    c.add(extra);
    CHECK(c.hash() != a.schema.hash());
    Schema d1, d2;
    d1.add(ClassDef{"c", {{"x", FieldKind::Float, 0, 1, 0.01}}, 1});
    d2.add(ClassDef{"c", {{"x", FieldKind::Float, 0, 1, 0.02}}, 5});
    CHECK(d1.hash() != d2.hash());
    Schema d3; d3.add(ClassDef{"c", {{"x", FieldKind::Float, 0, 1, 0.01}}, 7, 99});   // priority / radius: server only
    CHECK(d3.hash() == d1.hash());
}

TEST(reliable_value_round_trip) {
    Game g;
    wire::Layout L(g.schema);
    const auto& name = L.classes[g.player].fields[size_t(g.pName)];
    ByteWriter w;
    wire::writeValue(w, name, sval(std::string(5000, 'x')));
    ByteReader r(w.buf.data(), w.buf.size());
    Value v;
    CHECK(wire::readValue(r, name, v) && v.s.size() == wire::kMaxStringBytes);
    ByteWriter w2; w2.u32(1u << 30);
    ByteReader r2(w2.buf.data(), w2.buf.size());
    CHECK(!wire::readValue(r2, name, v));    // huge size: refused before allocating
}

// ---------------------------------------------------------------- hostile peers
struct TapConn final : Conn {
    std::unique_ptr<Conn> in;
    std::vector<Bytes>* log;
    TapConn(std::unique_ptr<Conn> c, std::vector<Bytes>* l) : in(std::move(c)), log(l) {}
    void send(const Bytes& m) override { in->send(m); }
    void sendLatest(const Bytes& m) override { in->sendLatest(m); }
    bool recv(Bytes& m) override { if (!in->recv(m)) return false; log->push_back(m); return true; }
    void update(uint64_t now) override { in->update(now); }
    ConnState state() const override { return in->state(); }
    void close() override { in->close(); }
    std::string describe() const override { return "tap"; }
};

static Bytes welcomeBytes() {
    ByteWriter w; w.u8(64); w.u32(kProtocol); w.u8(3); w.u16(60); w.u16(20);
    return w.buf;
}

TEST(client_survives_garbage_truncation_and_mutation) {
    // record a real session: creates, deltas, removals, reliable fields, messages
    std::vector<Bytes> log;
    Game g;
    ServerOptions so; so.defaultRadius = 30;
    Server srv(g.schema, so);
    auto [a, b] = makeLoopbackPair();
    srv.attach(std::move(a));
    Client rec(g.schema, std::make_unique<TapConn>(std::move(b), &log));
    std::vector<ObjectId> ids;
    for (int i = 0; i < 40; ++i) ids.push_back(srv.create(i % 2 ? g.prop : g.player));
    ObjectId st = srv.create(g.state);
    srv.set(st, g.sTitle, sval("round one"));
    for (uint8_t s : std::vector<uint8_t>{1}) srv.setViewers(s, {{0, 0, 0}});
    uint64_t now = 1000;
    for (int t = 0; t < 300; ++t) {
        now += 16;
        srv.poll(now);
        for (size_t i = 0; i < ids.size(); ++i) {
            float a2 = float(t) * 0.02f + float(i);
            srv.set(ids[i], 0, vec(std::cos(a2) * 40, 0, std::sin(a2) * 40));
            srv.set(ids[i], 1, quatYaw(a2));
        }
        if (t % 50 == 0) srv.set(ids[0], g.pName, sval("n" + std::to_string(t)));
        if (t == 150) { srv.destroy(ids[5]); ids.erase(ids.begin() + 5); }
        if (t % 40 == 0) srv.send(0, 7, Bytes{1, 2, 3});
        srv.tick(); srv.broadcast(now); rec.poll(now);
    }
    CHECK(rec.ready() && log.size() > 100);

    std::mt19937 rng(99);
    auto feed = [&](const std::function<Bytes(size_t)>& make, int count) {
        auto [x, y] = makeLoopbackPair();
        Client c(g.schema, std::move(y));
        int fires = 0;
        c.onCreate = [&](ObjectId) { ++fires; };
        c.onDestroy = [&](ObjectId) { ++fires; };
        c.onReliableChange = [&](ObjectId, int) { ++fires; };
        uint64_t t = 1;
        c.poll(t);
        x->send(welcomeBytes());
        for (int i = 0; i < count; ++i) {
            x->send(make(size_t(i)));
            if (i % 8 == 0) { c.poll(++t); for (auto id : c.objects()) for (int f = 0; f < 4; ++f) (void)c.sample(id, f); }
        }
        c.poll(++t);
        return c.stats().dropped;
    };
    // pure garbage of any size, with valid type bytes
    feed([&](size_t) {
        Bytes m(rng() % 300);
        for (auto& v : m) v = uint8_t(rng());
        if (!m.empty() && rng() % 2) m[0] = uint8_t(64 + rng() % 8);
        return m;
    }, 20000);
    // truncated and bit-flipped real messages, in order
    feed([&](size_t i) {
        Bytes m = log[i % log.size()];
        switch (rng() % 4) {
        case 0: m.resize(m.empty() ? 0 : rng() % m.size()); break;
        case 1: for (int k = 0; k < 3 && !m.empty(); ++k) m[rng() % m.size()] ^= uint8_t(1 << (rng() % 8)); break;
        case 2: m.push_back(uint8_t(rng())); break;
        default: break;
        }
        return m;
    }, int(log.size()) * 30);
    // huge declared sizes
    feed([&](size_t i) {
        ByteWriter w;
        if (i % 2) { w.u8(69); w.u16(1); w.u32(0xFFFFFFFFu); }                        // remote: 4 GiB payload
        else { w.u8(67); w.u32(1); w.u16(uint16_t(g.player)); w.u8(uint8_t(g.pName)); w.u32(0x7FFFFFFFu); }   // field string
        return w.buf;
    }, 200);
    CHECK(true);   // reaching here = no crash
}

TEST(server_survives_garbage_and_lies) {
    Game g;
    Server srv(g.schema);
    int inputs = 0, messages = 0;
    srv.onInput = [&](uint8_t, uint32_t, const Bytes&) { ++inputs; };
    srv.onMessage = [&](uint8_t, uint16_t, const Bytes&) { ++messages; };
    for (int i = 0; i < 50; ++i) srv.create(g.prop);
    std::mt19937 rng(5);
    auto [a, b] = makeLoopbackPair();
    uint8_t slot = srv.attach(std::move(a));
    CHECK(slot == 1);
    uint64_t now = 10;
    // garbage before the hello
    for (int i = 0; i < 200; ++i) { Bytes m(rng() % 64); for (auto& v : m) v = uint8_t(rng()); if (!m.empty()) m[0] = uint8_t(2 + rng() % 2); b->send(m); }
    srv.poll(++now);
    CHECK(srv.clients().empty() && inputs == 0 && messages == 0);
    ByteWriter h; h.u8(1); h.u32(wire::kMagic); h.u32(kProtocol); h.u64(g.schema.hash());
    b->send(h.buf);
    srv.poll(++now);
    CHECK(srv.clients().size() == 1);
    for (int i = 0; i < 3000; ++i) {
        ByteWriter w;
        switch (rng() % 6) {
        case 0: w.u8(2); w.u32(rng()); w.u32(rng()); w.u8(uint8_t(rng())); w.u32(rng()); for (int k = 0; k < 20; ++k) w.u16(uint16_t(rng())); break;
        case 1: w.u8(2); w.u32(0); w.u32(0); w.u8(255); w.u32(1); break;            // count above the limit
        case 2: w.u8(2); w.u32(0); w.u32(0); w.u8(1); w.u32(1); w.u16(60000); break;  // frame larger than allowed
        case 3: w.u8(3); w.u16(1); w.u32(0xFFFFFFF0u); break;                         // message: huge size
        case 4: w.u8(2); w.u32(4000000000u); w.u32(~0u); w.u8(0); w.u32(0); break;     // ack of snapshots never sent
        default: { Bytes m(rng() % 100); for (auto& v : m) v = uint8_t(rng()); w.buf = m; } break;
        }
        b->send(w.buf);
        if (i % 50 == 0) { srv.poll(++now); srv.tick(); srv.broadcast(now); }
    }
    srv.poll(++now); srv.tick(); srv.broadcast(now);
    CHECK(srv.stats().rejected > 1000);
    CHECK(srv.clients().size() == 1);       // garbage is dropped, the client is not crashed out
    // the server's own snapshots still make sense to a real client afterwards
    auto [c, d] = makeLoopbackPair();
    srv.attach(std::move(c));
    Client cl(g.schema, std::move(d));
    srv.setViewers(2, {{0, 0, 0}});
    for (int i = 0; i < 30; ++i) { now += 16; srv.poll(now); srv.tick(); srv.broadcast(now); cl.poll(now); }
    CHECK(cl.ready() && cl.objects().size() == 50);
}

TEST(hello_checks_protocol_and_schema) {
    Game g;
    Schema other = g.schema;
    other.add(ClassDef{"x", {{"v", FieldKind::Bool}}});
    Server srv(g.schema);
    auto [a, b] = makeLoopbackPair();
    srv.attach(std::move(a));
    Client wrong(other, std::move(b));
    auto [c, d] = makeLoopbackPair();
    srv.attach(std::move(c));
    Client right(g.schema, std::move(d));
    int joins = 0;
    srv.onJoin = [&](uint8_t) { ++joins; };
    uint64_t now = 1;
    for (int i = 0; i < 20; ++i) { now += 16; srv.poll(now); srv.tick(); srv.broadcast(now); wrong.poll(now); right.poll(now); }
    CHECK(!wrong.connected() && !wrong.ready());
    CHECK(right.ready() && right.slot() == 2 && joins == 1);
    CHECK(srv.clients() == std::vector<uint8_t>{2});
    // silent peer: dropped after the hello timeout
    ServerOptions so; so.helloTimeoutMs = 500;
    Server s2(g.schema, so);
    auto [e, f] = makeLoopbackPair();
    s2.attach(std::move(e));
    for (int i = 0; i < 60; ++i) { now += 16; s2.poll(now); s2.broadcast(now); f->update(now); }
    CHECK(f->state() == ConnState::Closed);
}

} // namespace rt
