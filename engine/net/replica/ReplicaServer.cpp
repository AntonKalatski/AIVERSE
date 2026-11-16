// Replica server: authoritative objects, per-client delta snapshots, interest, budget, input (docs/replica.md).
//
// Delta model: per client and object the server remembers what it last sent of every snapshot field and in
// which snapshot (seq). Acks (newest seq + 32 previous bits, on input packets) resolve those sends to
// confirmed or lost. A field goes out when its quantized value differs from the last send or that send was
// lost; values are absolute, so the client simply overwrites in seq order (it drops older snapshots).
// An object goes out as a full "create" until a snapshot carrying its create is confirmed; a removal repeats
// until confirmed, and only then the client is told to forget the object's reliable fields and the
// object's id may return to the free list (once no client tracks it any more).
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include "engine/net/replica/ReplicaWire.h"

namespace aiverse::net::replica {
using namespace wire;

namespace {
enum St : uint8_t { kPending = 0, kInFlight = 1, kConfirmed = 2, kLost = 3 };
constexpr float kHysteresis = 1.15f;        // an object leaves relevance only past radius x this
constexpr uint32_t kInputWindow = 64;       // input frames ahead of the next expected one
constexpr size_t kMaxPendingInputs = 64;
constexpr int kMaxMessagesPerPoll = 512;
constexpr size_t kMaxRemovalsPerSnapshot = 4096;
const Value kEmpty{};

uint64_t meta(uint32_t seq, uint8_t st) { return uint64_t(seq) | (uint64_t(st) << 32); }
uint32_t metaSeq(uint64_t m) { return uint32_t(m); }
uint8_t metaSt(uint64_t m) { return uint8_t(m >> 32); }
} // namespace

struct Server::Impl {
    struct Obj {
        bool alive = false;
        ClassId cls = 0;
        uint8_t owner = 0;
        uint32_t refs = 0;                       // clients tracking it (id reuse waits for 0)
        std::vector<Value> values;
        std::vector<uint64_t> q;                 // quantized snapshot slots + owner
        std::vector<std::pair<uint8_t, bool>> vis;
        uint64_t relDirty = 0;                   // reliable fields changed since the last broadcast
        std::array<float, 3> pos{};
    };
    struct Track {
        bool on = false, removing = false;
        uint8_t cst = kPending, rst = kPending;  // create / removal state
        uint32_t cseq = 0, rseq = 0;
        uint32_t stamp = 0;                      // relevance stamp of the snapshot being built
        float acc = 0, dist = 0;
        std::vector<uint64_t> sent;              // last sent quantized slots
        std::vector<uint64_t> meta;              // per snapshot field + owner: seq | state << 32
    };
    struct Rec { uint32_t seq = 0; uint64_t at = 0; bool acked = false; };
    struct Peer {
        std::unique_ptr<Conn> conn;
        uint8_t slot = 0;
        bool welcomed = false, closing = false;
        uint64_t attachedAt = 0, lastPoll = 0, lastTokenAt = 0;
        double nextSnap = 0, tokens = 0, msgTokens = 0;
        std::vector<Track> tracks;               // by object id
        std::vector<ObjectId> tracked;
        std::vector<Rec> ring;
        uint32_t seq = 0, newestAck = 0, stamp = 0, srtt = 0;
        std::vector<std::array<float, 3>> viewers;
        uint32_t nextInput = 1, lastApplied = 0;
        int gapWait = 0;
        std::map<uint32_t, Bytes> inputs;
    };
    struct Cand { ObjectId id; uint32_t bits; uint64_t mask; bool create; float key; };

    Schema schema;
    ServerOptions opt;
    Layout layout;
    Server* self = nullptr;
    std::vector<Obj> objs{1};                    // index = id; 0 unused
    std::vector<ObjectId> freeIds;               // FIFO: oldest freed first
    size_t freeHead = 0;
    std::array<std::unique_ptr<Peer>, 256> peers;
    uint32_t tick = 0;
    ServerStats stats;
    std::vector<ObjectId> relDirty;
    bool inPoll = false;
    float maxRadius = 0, cell = 32;
    std::unordered_map<uint64_t, std::vector<ObjectId>> grid;
    std::vector<ObjectId> always, forced;
    std::array<std::vector<ObjectId>, 256> owned;
    std::vector<ObjectId> rel, removals;
    std::vector<Cand> cands, taken;

    Impl(const Schema& s, ServerOptions o) : schema(s), opt(o), layout(s) {
        opt.snapshotRate = std::clamp(opt.snapshotRate, 1, 1000);
        opt.tickRate = std::clamp(opt.tickRate, 1, 10000);
        opt.history = std::clamp(opt.history, 2, 1024);
        opt.maxSnapshotBytes = std::clamp(opt.maxSnapshotBytes, 64, 120000);
        opt.maxInputsPerTick = std::max(1, opt.maxInputsPerTick);
        opt.inputGapTicks = std::max(0, opt.inputGapTicks);
        if (!(opt.defaultRadius > 0)) opt.defaultRadius = 64;
        maxRadius = opt.defaultRadius;
        for (size_t c = 0; c < schema.size(); ++c) maxRadius = std::max(maxRadius, schema.cls(ClassId(c)).radius);
        cell = std::max(opt.cellSize > 0 ? opt.cellSize : 32.0f, maxRadius * kHysteresis / 64.0f);
    }

    Obj* obj(ObjectId id) { return id && id < objs.size() && objs[id].alive ? &objs[id] : nullptr; }
    const Obj* obj(ObjectId id) const { return id && id < objs.size() && objs[id].alive ? &objs[id] : nullptr; }
    float radiusOf(ClassId c) const { float r = schema.cls(c).radius; return r > 0 ? r : opt.defaultRadius; }

    void release(ObjectId id) {
        Obj& o = objs[id];
        if (o.refs) --o.refs;
        if (!o.alive && o.refs == 0) freeIds.push_back(id);
    }

    // ---------------------------------------------------------------- sending helpers
    void sendReliable(Peer& p, const Bytes& b) { p.conn->send(b); stats.reliableBytes += b.size(); }
    void sendField(Peer& p, ObjectId id, const Obj& o, int f) {
        ByteWriter w;
        w.u8(uint8_t(Msg::Field)); w.u32(id); w.u16(o.cls); w.u8(uint8_t(f));
        writeValue(w, layout.classes[o.cls].fields[size_t(f)], o.values[size_t(f)]);
        sendReliable(p, w.buf);
    }
    void sendReject(Peer& p, RejectReason why) {
        ByteWriter w; w.u8(uint8_t(Msg::Reject)); w.u8(uint8_t(why));
        sendReliable(p, w.buf);
        p.closing = true;
    }

    // ---------------------------------------------------------------- peers
    void drop(uint8_t slot) {
        auto& pp = peers[slot];
        if (!pp) return;
        bool was = pp->welcomed;
        for (ObjectId id : pp->tracked) release(id);
        pp->conn->close();
        pp.reset();
        if (was && self->onLeave) self->onLeave(slot);
    }

    uint8_t resolve(const Peer& p, uint32_t seq) const {
        const Rec& r = p.ring[seq % p.ring.size()];
        if (r.seq != seq) return kLost;           // evicted without an ack: resend
        if (r.acked) return kConfirmed;
        return seq < p.newestAck ? kLost : kInFlight;   // the client drops older snapshots once a newer one arrived
    }
    uint8_t fieldState(const Peer& p, Track& t, int k) const {
        uint64_t m = t.meta[size_t(k)];
        uint8_t st = metaSt(m);
        if (st == kInFlight) { st = resolve(p, metaSeq(m)); if (st != kInFlight) t.meta[size_t(k)] = meta(metaSeq(m), st); }
        return st;
    }

    void handleAck(Peer& p, uint32_t newest, uint32_t bits, uint64_t now) {
        if (newest == 0) return;
        if (newest > p.seq) { ++stats.rejected; return; }
        for (uint32_t i = 0; i <= 32; ++i) {
            if (i > 0 && !(bits & (1u << (i - 1)))) continue;
            if (newest < i + 1 && i > 0) break;
            uint32_t s = newest - i;
            if (s == 0) break;
            Rec& r = p.ring[s % p.ring.size()];
            if (r.seq != s || r.acked) continue;
            r.acked = true;
            if (i == 0) {
                uint32_t sample = uint32_t(std::min<uint64_t>(now - std::min(now, r.at), 60000));
                p.srtt = p.srtt ? (p.srtt * 7 + sample) / 8 : sample;
            }
        }
        p.newestAck = std::max(p.newestAck, newest);
    }

    void handleInput(Peer& p, const Bytes& m, uint64_t now) {
        ByteReader r(m.data() + 1, m.size() - 1);
        uint32_t newest = r.u32(), bits = r.u32();
        uint8_t count = r.u8();
        uint32_t first = r.u32();
        if (!r.ok() || count > kMaxInputFramesPerPacket) { ++stats.rejected; return; }
        std::vector<Bytes> frames(count);
        for (auto& f : frames) {
            uint16_t n = r.u16();
            if (!r.ok() || n > opt.maxInputBytes) { ++stats.rejected; return; }
            f.resize(n);
            for (auto& b : f) b = r.u8();
        }
        if (!r.ok() || !r.atEnd()) { ++stats.rejected; return; }
        handleAck(p, newest, bits, now);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t s = first + i;
            if (s == 0 || s < p.nextInput) continue;                       // already applied or skipped
            if (s - p.nextInput >= kInputWindow) {
                // far ahead: the start of a stream we never saw (lost join packets) resyncs; else hostile
                if (p.inputs.empty() && i == 0) p.nextInput = s;
                else { ++stats.rejected; continue; }
            }
            if (p.inputs.size() >= kMaxPendingInputs) { ++stats.rejected; continue; }
            p.inputs.emplace(s, std::move(frames[i]));
        }
    }

    void handleMessage(Peer& p, const Bytes& m) {
        ByteReader r(m.data() + 1, m.size() - 1);
        uint16_t channel = r.u16();
        Bytes payload;
        if (!readBytes(r, payload, opt.maxMessageBytes) || !r.atEnd()) { ++stats.rejected; return; }
        if (p.msgTokens < 1) { ++stats.rejected; return; }
        p.msgTokens -= 1;
        if (self->onMessage) self->onMessage(p.slot, channel, payload);
    }

    void handleHello(Peer& p, const Bytes& m, uint64_t now) {
        if (p.welcomed) return;
        ByteReader r(m.data() + 1, m.size() - 1);
        uint32_t magic = r.u32(), protocol = r.u32();
        uint64_t hash = r.u64();
        if (!r.ok() || magic != kMagic || protocol != kProtocol) { sendReject(p, RejectReason::Protocol); return; }
        if (hash != schema.hash()) { sendReject(p, RejectReason::Schema); return; }
        p.welcomed = true;
        ByteWriter w;
        w.u8(uint8_t(Msg::Welcome)); w.u32(kProtocol); w.u8(p.slot); w.u16(uint16_t(std::min(opt.tickRate, 65535)));
        w.u16(uint16_t(std::min(opt.snapshotRate, 65535)));
        sendReliable(p, w.buf);
        const double interval = 1000.0 / opt.snapshotRate;
        p.nextSnap = double(now) + std::fmod(p.slot * 7.0, interval);   // spread clients over the interval
        p.lastTokenAt = now;
        p.tokens = double(opt.bytesPerSecond) / opt.snapshotRate;
        p.msgTokens = opt.maxMessagesPerSecond;
        if (self->onJoin) self->onJoin(p.slot);
    }

    void deliverInputs(Peer& p) {
        int n = 0;
        while (n < opt.maxInputsPerTick && !p.closing) {
            auto it = p.inputs.begin();
            if (it == p.inputs.end()) break;
            if (it->first < p.nextInput) { p.inputs.erase(it); continue; }
            if (it->first != p.nextInput) {
                if (p.gapWait >= opt.inputGapTicks) { p.nextInput = it->first; p.gapWait = 0; continue; }
                ++p.gapWait;
                break;
            }
            uint32_t s = it->first;
            Bytes frame = std::move(it->second);
            p.inputs.erase(it);
            p.nextInput = s + 1;
            p.lastApplied = s;
            p.gapWait = 0;
            ++n;
            ++stats.inputs;
            if (self->onInput) self->onInput(p.slot, s, frame);
        }
    }

    // ---------------------------------------------------------------- interest
    static uint64_t cellKey(int32_t x, int32_t z) { return (uint64_t(uint32_t(x)) << 32) | uint32_t(z); }
    int32_t cellOf(float v) const { double c = std::floor(double(v) / cell); return int32_t(std::clamp(c, -2.0e9, 2.0e9)); }
    static int visOf(const Obj& o, uint8_t slot) {
        for (auto& [s, on] : o.vis) if (s == slot) return on ? 1 : 0;
        return -1;
    }

    void buildGrid() {
        if (grid.size() > 4 * objs.size() + 64) grid.clear();
        for (auto& [k, v] : grid) v.clear();
        always.clear(); forced.clear();
        for (auto& o : owned) o.clear();
        for (ObjectId id = 1; id < objs.size(); ++id) {
            const Obj& o = objs[id];
            if (!o.alive) continue;
            const ClassDef& c = schema.cls(o.cls);
            if (c.alwaysRelevant || c.positionField < 0) always.push_back(id);
            else grid[cellKey(cellOf(o.pos[0]), cellOf(o.pos[2]))].push_back(id);
            if (o.owner) owned[o.owner].push_back(id);
            if (!o.vis.empty()) forced.push_back(id);
        }
    }

    void markRelevant(Peer& p, ObjectId id, float d) {
        if (p.tracks.size() <= id) p.tracks.resize(objs.size());
        Track& t = p.tracks[id];
        if (t.stamp != p.stamp) { t.stamp = p.stamp; t.dist = d; rel.push_back(id); }
        else t.dist = std::min(t.dist, d);
    }

    void collectRelevant(Peer& p) {
        rel.clear();
        ++p.stamp;
        for (ObjectId id : always) if (visOf(objs[id], p.slot) != 0) markRelevant(p, id, 0);
        for (ObjectId id : owned[p.slot]) if (visOf(objs[id], p.slot) != 0) markRelevant(p, id, 0);
        for (ObjectId id : forced) if (visOf(objs[id], p.slot) == 1) markRelevant(p, id, 0);
        const float reach = maxRadius * kHysteresis;
        for (auto& v : p.viewers) {
            int32_t x0 = cellOf(v[0] - reach), x1 = cellOf(v[0] + reach), z0 = cellOf(v[2] - reach), z1 = cellOf(v[2] + reach);
            for (int32_t x = x0; x <= x1; ++x)
                for (int32_t z = z0; z <= z1; ++z) {
                    auto it = grid.find(cellKey(x, z));
                    if (it == grid.end()) continue;
                    for (ObjectId id : it->second) {
                        const Obj& o = objs[id];
                        float r = radiusOf(o.cls);
                        if (id < p.tracks.size() && p.tracks[id].on && !p.tracks[id].removing) r *= kHysteresis;
                        float dx = o.pos[0] - v[0], dy = o.pos[1] - v[1], dz = o.pos[2] - v[2];
                        float d2 = dx * dx + dy * dy + dz * dz;
                        if (d2 > r * r) continue;
                        if (!o.vis.empty() && visOf(o, p.slot) == 0) continue;
                        markRelevant(p, id, std::sqrt(d2));
                    }
                }
        }
    }

    // ---------------------------------------------------------------- snapshot
    void writeSlots(BitWriter& bw, const ClassLayout& L, const std::vector<uint64_t>& q, int k) {
        if (k >= int(L.snapFields.size())) { bw.put(q[size_t(L.ownerSlot)], 8); return; }
        const FieldLayout& f = L.fields[size_t(L.snapFields[size_t(k)])];
        for (int a = 0; a < f.comps; ++a) bw.put(q[size_t(f.slot + a)], f.bits);
    }
    static bool slotsDiffer(const ClassLayout& L, const std::vector<uint64_t>& a, const std::vector<uint64_t>& b, int k) {
        if (k >= int(L.snapFields.size())) return a[size_t(L.ownerSlot)] != b[size_t(L.ownerSlot)];
        const FieldLayout& f = L.fields[size_t(L.snapFields[size_t(k)])];
        for (int a2 = 0; a2 < f.comps; ++a2) if (a[size_t(f.slot + a2)] != b[size_t(f.slot + a2)]) return true;
        return false;
    }

    void snapshot(Peer& p, uint64_t now) {
        const double perSnap = double(opt.bytesPerSecond) / opt.snapshotRate;
        p.tokens = std::min(p.tokens + double(opt.bytesPerSecond) * double(now - std::min(now, p.lastTokenAt)) / 1000.0,
                            2 * perSnap);
        p.lastTokenAt = now;
        collectRelevant(p);

        // relevance changes, removals
        removals.clear();
        for (size_t i = 0; i < p.tracked.size();) {
            ObjectId id = p.tracked[i];
            Track& t = p.tracks[id];
            bool relevant = t.stamp == p.stamp && objs[id].alive;
            if (relevant) {
                if (t.removing) { t.removing = false; if (t.rseq) t.cst = kPending; }
                ++i;
                continue;
            }
            if (!t.removing) { t.removing = true; t.rseq = 0; t.rst = kPending; }
            uint8_t st = t.rseq ? t.rst : uint8_t(kPending);
            if (st == kInFlight) { st = resolve(p, t.rseq); t.rst = st; }
            if (st == kConfirmed) {
                ByteWriter w; w.u8(uint8_t(Msg::Forget)); w.u32(id);
                sendReliable(p, w.buf);
                t.on = false; t.removing = false;
                t.sent.clear(); t.meta.clear();
                release(id);
                p.tracked[i] = p.tracked.back();
                p.tracked.pop_back();
                continue;
            }
            if ((st == kPending || st == kLost) && removals.size() < kMaxRemovalsPerSnapshot) removals.push_back(id);
            ++i;
        }

        // candidates
        cands.clear();
        for (ObjectId id : rel) {
            Track& t = p.tracks[id];
            Obj& o = objs[id];
            const ClassLayout& L = layout.classes[o.cls];
            if (!t.on) {
                const float d = t.dist;
                t = Track{};
                t.on = true; t.stamp = p.stamp; t.dist = d;
                t.sent.assign(size_t(L.slots), 0);
                t.meta.assign(size_t(L.maskBits), 0);
                p.tracked.push_back(id);
                ++o.refs;
                for (int f : L.reliableFields) if (!(o.values[size_t(f)] == kEmpty)) sendField(p, id, o, f);
            }
            uint8_t cst = t.cst;
            if (cst == kInFlight) { cst = resolve(p, t.cseq); t.cst = cst; }
            const bool mine = o.owner == p.slot;
            uint64_t mask = 0;
            uint32_t bits = 0;
            bool create = cst == kPending || cst == kLost;
            if (!create) {
                for (int k = 0; k < L.maskBits; ++k) {
                    bool changed = slotsDiffer(L, o.q, t.sent, k);
                    if (cst == kInFlight) { if (changed) { create = true; break; } continue; }
                    uint8_t fs = fieldState(p, t, k);
                    // The owner reconciles its prediction against this snapshot's lastApplied input: every field it
                    // has not confirmed yet goes again, or a lost update would pair a stale value with a newer input.
                    if (changed || fs == kLost || fs == kPending || (mine && fs == kInFlight)) { mask |= uint64_t(1) << k; bits += uint32_t(L.fieldBits(k)); }
                }
            }
            if (create) bits = uint32_t(layout.classBits + L.fullBits);
            else if (!mask) continue;
            else bits += uint32_t(L.maskBits);
            bits += 1 + uint32_t(BitWriter::varBits(id));
            float r = radiusOf(o.cls);
            t.acc += schema.cls(o.cls).priority * (1.0f / (1.0f + t.dist / r)) + 1e-6f;
            cands.push_back({id, bits, mask, create, mine ? 1e30f : t.acc});
        }
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.key != b.key ? a.key > b.key : a.id < b.id; });

        // budget
        const uint64_t hardBits = uint64_t(opt.maxSnapshotBytes) * 8;
        const uint64_t budgetBits = std::min<uint64_t>(uint64_t(std::max(0.0, p.tokens) * 8), hardBits);
        uint64_t used = 8 + 96 + 12;
        { uint32_t prev = 0; for (ObjectId id : removals) { used += uint64_t(BitWriter::varBits(prev ? id - prev - 1 : id)); prev = id; } }
        taken.clear();
        for (auto& c : cands) {
            const bool mine = c.key >= 1e29f;
            if (used + c.bits <= (mine ? hardBits : budgetBits)) { taken.push_back(c); used += c.bits; }
            else ++stats.starved;
        }
        std::sort(taken.begin(), taken.end(), [](const Cand& a, const Cand& b) { return a.id < b.id; });
        std::sort(removals.begin(), removals.end());

        // write
        const uint32_t seq = ++p.seq;
        BitWriter bw;
        bw.put(seq, 32); bw.put(tick, 32); bw.put(p.lastApplied, 32);
        bw.var(uint32_t(removals.size()));
        uint32_t prev = 0;
        for (ObjectId id : removals) { bw.var(prev ? id - prev - 1 : id); prev = id; }
        bw.var(uint32_t(taken.size()));
        prev = 0;
        for (auto& c : taken) {
            Obj& o = objs[c.id];
            const ClassLayout& L = layout.classes[o.cls];
            Track& t = p.tracks[c.id];
            bw.var(prev ? c.id - prev - 1 : c.id);
            prev = c.id;
            bw.flag(c.create);
            if (c.create) {
                bw.put(o.cls, layout.classBits);
                for (int k = 0; k < L.maskBits; ++k) { writeSlots(bw, L, o.q, k); t.meta[size_t(k)] = meta(seq, kInFlight); }
                t.sent = o.q;
                t.cst = kInFlight; t.cseq = seq;
            } else {
                bw.put(c.mask, L.maskBits);
                for (int k = 0; k < L.maskBits; ++k) {
                    if (!(c.mask & (uint64_t(1) << k))) continue;
                    writeSlots(bw, L, o.q, k);
                    t.meta[size_t(k)] = meta(seq, kInFlight);
                    if (k >= int(L.snapFields.size())) t.sent[size_t(L.ownerSlot)] = o.q[size_t(L.ownerSlot)];
                    else {
                        const FieldLayout& f = L.fields[size_t(L.snapFields[size_t(k)])];
                        for (int a = 0; a < f.comps; ++a) t.sent[size_t(f.slot + a)] = o.q[size_t(f.slot + a)];
                    }
                }
            }
            t.acc = 0;
        }
        for (ObjectId id : removals) { Track& t = p.tracks[id]; t.rseq = seq; t.rst = kInFlight; }

        Bytes out;
        out.reserve(bw.bits() / 8 + 2);
        out.push_back(uint8_t(Msg::Snapshot));
        bw.finish(out);
        p.ring[seq % p.ring.size()] = Rec{seq, now, false};
        p.tokens -= double(out.size());
        p.conn->sendLatest(out);
        ++stats.snapshots;
        stats.snapshotBytes += out.size();
    }

    void flushReliable() {
        for (ObjectId id : relDirty) {
            Obj* o = obj(id);
            if (!o || !o->relDirty) continue;
            for (auto& pp : peers) {
                if (!pp || !pp->welcomed || pp->closing || id >= pp->tracks.size() || !pp->tracks[id].on) continue;
                for (int f = 0; f < 64; ++f) if (o->relDirty & (uint64_t(1) << f)) sendField(*pp, id, *o, f);
            }
            o->relDirty = 0;
        }
        relDirty.clear();
    }
};

// ---------------------------------------------------------------- public API
Server::Server(const Schema& schema, ServerOptions options) : impl_(std::make_unique<Impl>(schema, options)) { impl_->self = this; }
Server::~Server() { for (auto& p : impl_->peers) if (p) p->conn->close(); }

ObjectId Server::create(ClassId cls) {
    auto& I = *impl_;
    if (cls >= I.schema.size()) return kNoObject;
    ObjectId id;
    if (I.freeHead < I.freeIds.size()) {
        id = I.freeIds[I.freeHead++];
        if (I.freeHead > 1024 && I.freeHead * 2 > I.freeIds.size()) {
            I.freeIds.erase(I.freeIds.begin(), I.freeIds.begin() + long(I.freeHead));
            I.freeHead = 0;
        }
    } else {
        if (I.objs.size() >= kMaxObjectId) return kNoObject;
        id = ObjectId(I.objs.size());
        I.objs.emplace_back();
    }
    auto& o = I.objs[id];
    const auto& L = I.layout.classes[cls];
    o = Impl::Obj{};
    o.alive = true;
    o.cls = cls;
    o.values.assign(L.fields.size(), Value{});
    o.q.assign(size_t(L.slots), 0);
    for (int f : L.snapFields) quantize(L.fields[size_t(f)], o.values[size_t(f)], &o.q[size_t(L.fields[size_t(f)].slot)]);
    int pf = I.schema.cls(cls).positionField;
    if (pf >= 0) { Value n = normalize(L.fields[size_t(pf)], Value{}); o.pos = {n.f[0], n.f[1], n.f[2]}; }
    return id;
}

void Server::destroy(ObjectId id) {
    auto& I = *impl_;
    auto* o = I.obj(id);
    if (!o) return;
    uint32_t refs = o->refs;
    *o = Impl::Obj{};
    o->refs = refs;
    if (refs == 0) I.freeIds.push_back(id);
}

bool Server::exists(ObjectId id) const { return impl_->obj(id) != nullptr; }

void Server::set(ObjectId id, int field, const Value& v) {
    auto& I = *impl_;
    auto* o = I.obj(id);
    if (!o) return;
    const auto& L = I.layout.classes[o->cls];
    if (field < 0 || size_t(field) >= L.fields.size()) return;
    const FieldLayout& f = L.fields[size_t(field)];
    Value& cur = o->values[size_t(field)];
    if (f.snapshot) {
        quantize(f, v, &o->q[size_t(f.slot)]);
        if (field == I.schema.cls(o->cls).positionField) {
            Value n; dequantize(f, &o->q[size_t(f.slot)], n);
            o->pos = {n.f[0], n.f[1], n.f[2]};
        }
        cur = v;
        return;
    }
    Value n = normalize(f, v);
    if (normalize(f, cur) == n) { cur = v; return; }
    cur = (f.kind == FieldKind::String || f.kind == FieldKind::Bytes) ? n : v;
    if (!o->relDirty) I.relDirty.push_back(id);
    o->relDirty |= uint64_t(1) << field;
}

const Value& Server::get(ObjectId id, int field) const {
    auto* o = impl_->obj(id);
    if (!o || field < 0 || size_t(field) >= o->values.size()) return kEmpty;
    return o->values[size_t(field)];
}

void Server::setOwner(ObjectId id, uint8_t slot) {
    auto* o = impl_->obj(id);
    if (!o) return;
    o->owner = slot;
    o->q[size_t(impl_->layout.classes[o->cls].ownerSlot)] = slot;
}

uint8_t Server::ownerOf(ObjectId id) const { auto* o = impl_->obj(id); return o ? o->owner : 0; }

void Server::setVisibleTo(ObjectId id, uint8_t slot, bool on) {
    auto* o = impl_->obj(id);
    if (!o || slot == 0) return;
    for (auto& [s, v] : o->vis) if (s == slot) { v = on; return; }
    o->vis.push_back({slot, on});
}

void Server::resetVisibility(ObjectId id, uint8_t slot) {
    auto* o = impl_->obj(id);
    if (!o) return;
    if (slot == 0) { o->vis.clear(); return; }
    o->vis.erase(std::remove_if(o->vis.begin(), o->vis.end(), [&](auto& e) { return e.first == slot; }), o->vis.end());
}

uint8_t Server::attach(std::unique_ptr<Conn> connection) {
    auto& I = *impl_;
    if (!connection) return 0;
    for (int s = 1; s < 256; ++s) {
        if (I.peers[size_t(s)]) continue;
        auto p = std::make_unique<Impl::Peer>();
        p->conn = std::move(connection);
        p->slot = uint8_t(s);
        p->ring.resize(size_t(I.opt.history));
        I.peers[size_t(s)] = std::move(p);
        return uint8_t(s);
    }
    ByteWriter w; w.u8(uint8_t(Msg::Reject)); w.u8(uint8_t(RejectReason::Full));
    connection->send(w.buf);
    connection->update(nowMs());
    return 0;
}

void Server::kick(uint8_t slot) {
    auto& I = *impl_;
    auto& p = I.peers[slot];
    if (!p || p->closing) return;
    I.sendReject(*p, RejectReason::Kicked);
    if (!I.inPoll) { p->conn->update(p->lastPoll); I.drop(slot); }
}

std::vector<uint8_t> Server::clients() const {
    std::vector<uint8_t> out;
    for (auto& p : impl_->peers) if (p && p->welcomed && !p->closing) out.push_back(p->slot);
    return out;
}

void Server::setViewers(uint8_t slot, const std::vector<std::array<float, 3>>& points) {
    auto& p = impl_->peers[slot];
    if (!p) return;
    p->viewers.clear();
    for (auto& v : points)
        if (std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]) && p->viewers.size() < 16) p->viewers.push_back(v);
}

uint32_t Server::rttMs(uint8_t slot) const {
    auto& p = impl_->peers[slot];
    if (!p) return 0;
    uint32_t r = p->conn->rttMs();
    return r ? r : p->srtt;
}

void Server::send(uint8_t slot, uint16_t channel, const Bytes& payload) {
    auto& I = *impl_;
    ByteWriter w;
    w.u8(uint8_t(Msg::Remote)); w.u16(channel); writeBytes(w, payload);
    for (auto& p : I.peers)
        if (p && p->welcomed && !p->closing && (slot == 0 || p->slot == slot)) I.sendReliable(*p, w.buf);
}

void Server::poll(uint64_t now) {
    auto& I = *impl_;
    I.inPoll = true;
    for (int s = 1; s < 256; ++s) {
        auto* p = I.peers[size_t(s)].get();
        if (!p) continue;
        if (p->closing) { p->conn->update(now); I.drop(uint8_t(s)); continue; }
        p->conn->update(now);
        if (!p->attachedAt) p->attachedAt = now ? now : 1;
        double dt = double(now - std::min(now, p->lastPoll ? p->lastPoll : now)) / 1000.0;
        p->lastPoll = now;
        p->msgTokens = std::min(p->msgTokens + dt * I.opt.maxMessagesPerSecond, 2.0 * I.opt.maxMessagesPerSecond);
        if (p->conn->state() == ConnState::Closed) { I.drop(uint8_t(s)); continue; }
        Bytes m;
        for (int n = 0; n < kMaxMessagesPerPoll && !p->closing && p->conn->recv(m); ++n) {
            if (m.empty()) { ++I.stats.rejected; continue; }
            switch (Msg(m[0])) {
            case Msg::Hello: I.handleHello(*p, m, now); break;
            case Msg::Input: if (p->welcomed) I.handleInput(*p, m, now); break;
            case Msg::Message: if (p->welcomed) I.handleMessage(*p, m); break;
            default: ++I.stats.rejected; break;
            }
            if (!I.peers[size_t(s)]) break;   // dropped from a callback
        }
        if (!I.peers[size_t(s)] || p->closing) continue;
        if (!p->welcomed && now - p->attachedAt > I.opt.helloTimeoutMs) { I.sendReject(*p, RejectReason::Timeout); continue; }
        if (p->welcomed) I.deliverInputs(*p);
    }
    I.inPoll = false;
}

void Server::tick() { ++impl_->tick; }

void Server::broadcast(uint64_t now) {
    auto& I = *impl_;
    I.flushReliable();
    bool grid = false;
    const double interval = 1000.0 / I.opt.snapshotRate;
    for (auto& pp : I.peers) {
        if (!pp || !pp->welcomed || pp->closing || double(now) < pp->nextSnap) {
            if (pp && !pp->closing) pp->conn->update(now);   // flush reliable traffic
            continue;
        }
        if (!grid) { I.buildGrid(); grid = true; }
        I.snapshot(*pp, now);
        pp->nextSnap += interval;
        if (pp->nextSnap <= double(now)) pp->nextSnap = double(now) + interval;
        pp->conn->update(now);
    }
}

uint32_t Server::serverTick() const { return impl_->tick; }
const ServerStats& Server::stats() const { return impl_->stats; }

} // namespace aiverse::net::replica
