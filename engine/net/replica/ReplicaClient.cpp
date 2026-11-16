// Replica client: hello, snapshot decode (hostile input), interpolation buffer, input frames (docs/replica.md).
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include "engine/net/replica/ReplicaWire.h"

namespace aiverse::net::replica {
using namespace wire;

namespace {
const Value kEmpty{};
constexpr size_t kMaxHistory = 32;
constexpr size_t kKeepFrames = 64;
constexpr int kMaxMessagesPerPoll = 4096;
constexpr uint32_t kAckKeepaliveMs = 50;

float lerp(float a, float b, float t) { return a + (b - a) * t; }

Value slerp(const Value& a, const Value& b, float t) {
    double d = 0;
    for (size_t i = 0; i < 4; ++i) d += double(a.f[i]) * b.f[i];
    float sign = d < 0 ? -1.0f : 1.0f;
    d = std::fabs(d);
    double wa, wb;
    if (d > 0.9995) { wa = 1 - t; wb = t; }        // nearly equal: normalised lerp
    else {
        double th = std::acos(std::min(1.0, d)), s = std::sin(th);
        wa = std::sin((1 - t) * th) / s; wb = std::sin(t * th) / s;
    }
    Value o;
    double len = 0;
    for (size_t i = 0; i < 4; ++i) { o.f[i] = float(wa * a.f[i] + wb * sign * b.f[i]); len += double(o.f[i]) * o.f[i]; }
    len = std::sqrt(len);
    if (len > 1e-9) for (auto& c : o.f) c = float(c / len);
    return o;
}
} // namespace

struct Client::Impl {
    struct Sample { double t; std::vector<Value> v; };   // v: snapshot fields in layout order
    struct Obj {
        ClassId cls = 0;
        uint8_t owner = 0;
        std::vector<Value> v;                            // every field (reliable ones from the store)
        std::deque<Sample> hist;
    };
    struct Reliable { ClassId cls = 0; std::map<int, Value> v; };
    struct Entry { ObjectId id; bool create; ClassId cls; uint64_t mask; size_t q; };

    Schema schema;
    Layout layout;
    ClientOptions opt;
    std::unique_ptr<Conn> conn;
    Client* self = nullptr;
    bool helloSent = false, welcomed = false, rejected = false, haveSnapshot = false;
    uint8_t slot = 0;
    uint16_t tickRate = 60, snapRate = 20;
    uint32_t inputSeq = 0, lastApplied = 0, latestTick = 0;
    std::deque<std::pair<uint32_t, Bytes>> frames;
    size_t unsent = 0;
    uint32_t newest = 0, ackBits = 0;
    bool ackDirty = false;
    uint64_t lastPacketAt = 0, now = 0;
    std::unordered_map<ObjectId, Obj> objs;
    std::unordered_map<ObjectId, Reliable> reliable;
    double offset = 0, newestMs = 0;
    bool haveOffset = false;
    ClientStats stats;
    std::vector<Entry> entries;
    std::vector<uint64_t> qbuf;
    std::vector<ObjectId> removed;

    Impl(const Schema& s, std::unique_ptr<Conn> c, ClientOptions o) : schema(s), layout(s), opt(o), conn(std::move(c)) {
        opt.inputRedundancy = std::clamp(opt.inputRedundancy, 1, int(kMaxInputFramesPerPacket));
    }

    double interval() const { return 1000.0 / std::max<uint16_t>(snapRate, 1); }
    double renderTime() const { return double(now) - offset - double(opt.interpolationDelayMs); }

    void sendRaw(const Bytes& b, bool latest) {
        if (latest) conn->sendLatest(b); else conn->send(b);
        stats.bytesOut += b.size();
    }

    void handleWelcome(const Bytes& m) {
        ByteReader r(m.data() + 1, m.size() - 1);
        uint32_t protocol = r.u32();
        uint8_t s = r.u8();
        uint16_t tr = r.u16(), sr = r.u16();
        if (!r.ok() || protocol != kProtocol || s == 0 || tr == 0 || sr == 0 || welcomed) { ++stats.dropped; return; }
        welcomed = true; slot = s; tickRate = tr; snapRate = sr;
    }

    void handleField(const Bytes& m) {
        ByteReader r(m.data() + 1, m.size() - 1);
        ObjectId id = r.u32();
        ClassId cls = r.u16();
        uint8_t f = r.u8();
        if (!r.ok() || id == 0 || id >= kMaxObjectId || cls >= layout.classes.size() || f >= layout.classes[cls].fields.size() ||
            layout.classes[cls].fields[f].snapshot) { ++stats.dropped; return; }
        Value v;
        if (!readValue(r, layout.classes[cls].fields[f], v) || !r.atEnd()) { ++stats.dropped; return; }
        auto& rs = reliable[id];
        if (rs.cls != cls) { rs.v.clear(); rs.cls = cls; }
        rs.v[f] = v;
        auto it = objs.find(id);
        if (it != objs.end() && it->second.cls == cls) it->second.v[f] = v;
        if (self->onReliableChange) self->onReliableChange(id, f);
    }

    void handleForget(const Bytes& m) {
        ByteReader r(m.data() + 1, m.size() - 1);
        ObjectId id = r.u32();
        if (!r.ok() || !r.atEnd()) { ++stats.dropped; return; }
        reliable.erase(id);
    }

    void handleRemote(const Bytes& m) {
        ByteReader r(m.data() + 1, m.size() - 1);
        uint16_t channel = r.u16();
        Bytes payload;
        if (!readBytes(r, payload, kMaxMessageSize) || !r.atEnd()) { ++stats.dropped; return; }
        if (self->onMessage) self->onMessage(channel, payload);
    }

    static ObjectId nextId(BitReader& br, ObjectId prev, bool first) {
        uint32_t d = br.var();
        uint64_t id = first ? uint64_t(d) : uint64_t(prev) + d + 1;
        return id == 0 || id >= kMaxObjectId ? 0 : ObjectId(id);
    }

    // Parses the whole snapshot first; nothing is applied unless it is well formed.
    void handleSnapshot(const Bytes& m) {
        if (!welcomed) { ++stats.dropped; return; }
        BitReader br(m.data() + 1, m.size() - 1);
        uint32_t seq = uint32_t(br.get(32)), tick = uint32_t(br.get(32)), applied = uint32_t(br.get(32));
        if (!br.ok() || seq == 0) { ++stats.dropped; return; }
        if (seq <= newest) return;                                     // older than what we have
        removed.clear(); entries.clear(); qbuf.clear();
        uint32_t nrem = br.var();
        if (!br.ok() || nrem > kMaxListCount || nrem > br.remaining()) { ++stats.dropped; return; }
        ObjectId prev = 0;
        for (uint32_t i = 0; i < nrem; ++i) {
            ObjectId id = nextId(br, prev, i == 0);
            if (!br.ok() || !id) { ++stats.dropped; return; }
            removed.push_back(id); prev = id;
        }
        uint32_t nent = br.var();
        if (!br.ok() || nent > kMaxListCount || nent > br.remaining()) { ++stats.dropped; return; }
        prev = 0;
        for (uint32_t i = 0; i < nent; ++i) {
            Entry e{};
            e.id = nextId(br, prev, i == 0);
            if (!br.ok() || !e.id) { ++stats.dropped; return; }
            prev = e.id;
            e.create = br.flag();
            if (e.create) {
                e.cls = ClassId(br.get(layout.classBits));
                if (!br.ok() || e.cls >= layout.classes.size()) { ++stats.dropped; return; }
            } else {
                auto it = objs.find(e.id);
                if (it == objs.end() || std::binary_search(removed.begin(), removed.end(), e.id)) { ++stats.dropped; return; }
                e.cls = it->second.cls;
            }
            const ClassLayout& L = layout.classes[e.cls];
            e.mask = e.create ? ~uint64_t(0) : br.get(L.maskBits);
            e.q = qbuf.size();
            qbuf.resize(qbuf.size() + size_t(L.slots), 0);
            for (int k = 0; k < L.maskBits; ++k) {
                if (!(e.mask & (uint64_t(1) << k))) continue;
                if (k >= int(L.snapFields.size())) { qbuf[e.q + size_t(L.ownerSlot)] = br.get(8); continue; }
                const FieldLayout& f = L.fields[size_t(L.snapFields[size_t(k)])];
                for (int a = 0; a < f.comps; ++a) qbuf[e.q + size_t(f.slot + a)] = br.get(f.bits);
            }
            if (!br.ok()) { ++stats.dropped; return; }
            entries.push_back(e);
        }
        if (br.remaining() >= 8) { ++stats.dropped; return; }        // trailing garbage

        // ---- apply
        if (newest && seq - newest <= 32) {
            const uint32_t d = seq - newest;
            ackBits = uint32_t((uint64_t(ackBits) << d) | (uint64_t(1) << (d - 1)));
        } else {
            ackBits = 0;
        }
        newest = seq;
        ackDirty = true;
        lastApplied = applied;
        latestTick = tick;
        ++stats.snapshots;
        const double t = double(tick) * 1000.0 / tickRate;
        const double sampleOffset = double(now) - t;
        if (!haveOffset || sampleOffset < offset) offset = sampleOffset, haveOffset = true;
        else offset += (sampleOffset - offset) * 0.05;   // slow drift: server ticks need not match our clock
        newestMs = t;

        std::vector<std::pair<ObjectId, bool>> events;   // id, created (false: destroyed)
        for (ObjectId id : removed)
            if (objs.erase(id)) events.push_back({id, false});
        for (auto& e : entries) {
            const ClassLayout& L = layout.classes[e.cls];
            auto it = objs.find(e.id);
            if (e.create && it != objs.end() && it->second.cls != e.cls) { objs.erase(it); events.push_back({e.id, false}); it = objs.end(); }
            if (it == objs.end()) {
                Obj o;
                o.cls = e.cls;
                o.v.assign(L.fields.size(), Value{});
                auto rs = reliable.find(e.id);
                if (rs != reliable.end() && rs->second.cls == e.cls) for (auto& [f, v] : rs->second.v) o.v[size_t(f)] = v;
                it = objs.emplace(e.id, std::move(o)).first;
                events.push_back({e.id, true});
            }
            Obj& o = it->second;
            for (int k = 0; k < L.maskBits; ++k) {
                if (!(e.mask & (uint64_t(1) << k))) continue;
                if (k >= int(L.snapFields.size())) { o.owner = uint8_t(qbuf[e.q + size_t(L.ownerSlot)]); continue; }
                int fi = L.snapFields[size_t(k)];
                const FieldLayout& f = L.fields[size_t(fi)];
                dequantize(f, &qbuf[e.q + size_t(f.slot)], o.v[size_t(fi)]);
            }
            addSample(o, L, t);
        }
        haveSnapshot = true;
        for (auto& [id, created] : events) {
            if (created) { if (self->onCreate && objs.count(id)) self->onCreate(id); }
            else if (self->onDestroy) self->onDestroy(id);
        }
    }

    void addSample(Obj& o, const ClassLayout& L, double t) {
        Sample s{t, {}};
        s.v.reserve(L.snapFields.size());
        for (int fi : L.snapFields) s.v.push_back(o.v[size_t(fi)]);
        if (!o.hist.empty()) {
            // The object was absent from snapshots in between (unchanged or starved): hold its old value
            // until one interval before this sample instead of crawling from it over the whole gap.
            const double gap = t - o.hist.back().t;
            if (gap > 1.5 * interval()) { Sample hold = o.hist.back(); hold.t = t - interval(); o.hist.push_back(std::move(hold)); }
        }
        o.hist.push_back(std::move(s));
        const double rt = renderTime();
        while (o.hist.size() > 2 && (o.hist[1].t <= rt || o.hist.size() > kMaxHistory)) o.hist.pop_front();
    }

    void sendInputPacket() {
        while (!frames.empty() && int32_t(frames.front().first - lastApplied) <= 0) frames.pop_front();
        size_t n = std::min<size_t>({frames.size(), kMaxInputFramesPerPacket, std::max<size_t>(size_t(opt.inputRedundancy), unsent)});
        ByteWriter w;
        w.u8(uint8_t(Msg::Input));
        w.u32(newest); w.u32(ackBits);
        w.u8(uint8_t(n));
        w.u32(n ? frames[frames.size() - n].first : 0);
        for (size_t i = frames.size() - n; i < frames.size(); ++i) {
            const Bytes& f = frames[i].second;
            size_t len = std::min<size_t>(f.size(), 65535);
            w.u16(uint16_t(len));
            w.buf.insert(w.buf.end(), f.begin(), f.begin() + long(len));
        }
        sendRaw(w.buf, true);
        unsent = 0; ackDirty = false; lastPacketAt = now;
    }
};

Client::Client(const Schema& schema, std::unique_ptr<Conn> connection, ClientOptions options)
    : impl_(std::make_unique<Impl>(schema, std::move(connection), options)) { impl_->self = this; }
Client::~Client() { if (impl_->conn) impl_->conn->close(); }

void Client::poll(uint64_t nowMs) {
    auto& I = *impl_;
    if (!I.conn) return;
    I.now = nowMs;
    I.conn->update(nowMs);
    if (I.conn->state() == ConnState::Closed || I.rejected) return;
    if (!I.helloSent && I.conn->state() == ConnState::Connected) {
        ByteWriter w;
        w.u8(uint8_t(Msg::Hello)); w.u32(kMagic); w.u32(kProtocol); w.u64(I.schema.hash());
        I.sendRaw(w.buf, false);
        I.helloSent = true;
    }
    Bytes m;
    for (int n = 0; n < kMaxMessagesPerPoll && !I.rejected && I.conn->recv(m); ++n) {
        I.stats.bytesIn += m.size();
        if (m.empty()) { ++I.stats.dropped; continue; }
        switch (Msg(m[0])) {
        case Msg::Welcome: I.handleWelcome(m); break;
        case Msg::Reject: I.rejected = true; I.conn->close(); break;
        case Msg::Snapshot: I.handleSnapshot(m); break;
        case Msg::Field: I.handleField(m); break;
        case Msg::Forget: I.handleForget(m); break;
        case Msg::Remote: I.handleRemote(m); break;
        default: ++I.stats.dropped; break;
        }
    }
    if (I.welcomed && !I.rejected && (I.unsent || I.ackDirty || nowMs - std::min(nowMs, I.lastPacketAt) >= kAckKeepaliveMs))
        I.sendInputPacket();
    if (!I.rejected) I.conn->update(nowMs);
}

bool Client::ready() const { return impl_->welcomed && impl_->haveSnapshot && connected(); }
bool Client::connected() const { return impl_->conn && !impl_->rejected && impl_->conn->state() == ConnState::Connected; }
uint8_t Client::slot() const { return impl_->slot; }

uint32_t Client::sendInput(const Bytes& input) {
    auto& I = *impl_;
    uint32_t s = ++I.inputSeq;
    I.frames.push_back({s, input});
    while (I.frames.size() > kKeepFrames) I.frames.pop_front();
    I.unsent = std::min(I.unsent + 1, I.frames.size());
    return s;
}

uint32_t Client::lastAppliedInput() const { return impl_->lastApplied; }

void Client::send(uint16_t channel, const Bytes& payload) {
    ByteWriter w;
    w.u8(uint8_t(Msg::Message)); w.u16(channel); writeBytes(w, payload);
    impl_->sendRaw(w.buf, false);
}

std::vector<ObjectId> Client::objects() const {
    std::vector<ObjectId> out;
    out.reserve(impl_->objs.size());
    for (auto& [id, o] : impl_->objs) out.push_back(id);
    std::sort(out.begin(), out.end());
    return out;
}

bool Client::exists(ObjectId id) const { return impl_->objs.count(id) != 0; }
ClassId Client::classOf(ObjectId id) const { auto it = impl_->objs.find(id); return it == impl_->objs.end() ? 0 : it->second.cls; }
uint8_t Client::ownerOf(ObjectId id) const { auto it = impl_->objs.find(id); return it == impl_->objs.end() ? 0 : it->second.owner; }

const Value& Client::latest(ObjectId id, int field) const {
    auto it = impl_->objs.find(id);
    if (it == impl_->objs.end() || field < 0 || size_t(field) >= it->second.v.size()) return kEmpty;
    return it->second.v[size_t(field)];
}

Value Client::sample(ObjectId id, int field) const {
    auto& I = *impl_;
    auto it = I.objs.find(id);
    if (it == I.objs.end() || field < 0 || size_t(field) >= it->second.v.size()) return kEmpty;
    const Impl::Obj& o = it->second;
    const ClassLayout& L = I.layout.classes[o.cls];
    const FieldLayout& f = L.fields[size_t(field)];
    if (!f.snapshot || o.hist.empty()) return o.v[size_t(field)];
    size_t k = size_t(std::find(L.snapFields.begin(), L.snapFields.end(), field) - L.snapFields.begin());
    const auto& h = o.hist;
    const double T = I.renderTime();
    if (T <= h.front().t) return h.front().v[k];
    if (T >= h.back().t) {
        const auto& a = h.back();
        if (f.interpolate && f.kind != FieldKind::Quat && I.opt.extrapolateMs && T > I.newestMs && a.t >= I.newestMs && h.size() >= 2) {
            const auto& b = h[h.size() - 2];
            const double span = a.t - b.t;
            if (span > 0) {
                const float dt = float(std::min(T - a.t, double(I.opt.extrapolateMs)) / span);
                Value v = a.v[k];
                for (int c = 0; c < f.comps; ++c) v.f[size_t(c)] = a.v[k].f[size_t(c)] + (a.v[k].f[size_t(c)] - b.v[k].f[size_t(c)]) * dt;
                return v;
            }
        }
        return a.v[k];
    }
    size_t i = 0;
    while (i + 1 < h.size() && h[i + 1].t <= T) ++i;
    const auto& a = h[i];
    const auto& b = h[i + 1];
    if (!f.interpolate) return a.v[k];
    const float t = float((T - a.t) / std::max(1e-9, b.t - a.t));
    if (f.kind == FieldKind::Quat) return slerp(a.v[k], b.v[k], t);
    Value v = a.v[k];
    for (int c = 0; c < f.comps; ++c) v.f[size_t(c)] = lerp(a.v[k].f[size_t(c)], b.v[k].f[size_t(c)], t);
    return v;
}

uint32_t Client::latestTick() const { return impl_->latestTick; }
const ClientStats& Client::stats() const { return impl_->stats; }

} // namespace aiverse::net::replica
