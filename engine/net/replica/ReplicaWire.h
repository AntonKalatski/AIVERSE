// Internal to engine/net/replica: wire layout, quantization and bit I/O shared by Server and Client.
// Not part of the public contract (Replica.h); tests include it for round-trip checks.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include "engine/net/replica/Replica.h"
#include "engine/sim/Command.h"

namespace aiverse::net::replica::wire {

constexpr uint32_t kMagic = 0x43504C52;   // "RLPC"
constexpr size_t kMaxStringBytes = 4096;
constexpr uint32_t kMaxObjectId = 1u << 24;
constexpr uint32_t kMaxListCount = 1u << 16;
constexpr uint32_t kMaxInputFramesPerPacket = 32;

enum class Msg : uint8_t {
    // client -> server
    Hello = 1, Input = 2, Message = 3,
    // server -> client
    Welcome = 64, Reject = 65, Snapshot = 66, Field = 67, Forget = 68, Remote = 69,
};
enum class RejectReason : uint8_t { Protocol = 1, Schema = 2, Full = 3, Timeout = 4, Kicked = 5 };

// ---------------------------------------------------------------- bits
class BitWriter {
public:
    void put(uint64_t v, int bits) {
        if (bits <= 0) return;
        if (bits < 64) v &= (uint64_t(1) << bits) - 1;
        while (bits > 0) {
            int room = 64 - used_;
            int n = bits < room ? bits : room;
            uint64_t part = n == 64 ? v : (v & ((uint64_t(1) << n) - 1));
            acc_ |= part << used_;
            used_ += n; bits -= n; v = n == 64 ? 0 : v >> n;
            if (used_ == 64) { flushWord(); }
        }
    }
    void flag(bool b) { put(b ? 1 : 0, 1); }
    // 0..2^32-1 in 6 + width bits (small values stay small).
    void var(uint32_t v) { int w = width(v); put(uint64_t(w), 6); put(v, w); }
    size_t bits() const { return buf_.size() * 8 + size_t(used_); }
    // Appends the bit stream (padded to whole bytes) to `out`.
    void finish(std::vector<uint8_t>& out) {
        int bytes = (used_ + 7) / 8;
        for (int i = 0; i < bytes; ++i) buf_.push_back(uint8_t(acc_ >> (8 * i)));
        acc_ = 0; used_ = 0;
        out.insert(out.end(), buf_.begin(), buf_.end());
        buf_.clear();
    }
    static int width(uint64_t v) { int w = 0; while (v) { ++w; v >>= 1; } return w; }
    static int varBits(uint32_t v) { return 6 + width(v); }
private:
    void flushWord() { for (int i = 0; i < 8; ++i) buf_.push_back(uint8_t(acc_ >> (8 * i))); acc_ = 0; used_ = 0; }
    std::vector<uint8_t> buf_;
    uint64_t acc_ = 0;
    int used_ = 0;
};

class BitReader {
public:
    BitReader(const uint8_t* d, size_t n) : d_(d), n_(n) {}
    uint64_t get(int bits) {
        if (bits <= 0) return 0;
        if (bits > 64 || pos_ + size_t(bits) > n_ * 8) { ok_ = false; pos_ = n_ * 8; return 0; }
        uint64_t v = 0;
        for (int i = 0; i < bits;) {
            size_t byte = pos_ >> 3; int off = int(pos_ & 7);
            int n = 8 - off; if (n > bits - i) n = bits - i;
            uint64_t part = (uint64_t(d_[byte]) >> off) & ((1u << n) - 1);
            v |= part << i;
            i += n; pos_ += size_t(n);
        }
        return v;
    }
    bool flag() { return get(1) != 0; }
    uint32_t var() { int w = int(get(6)); if (w > 32) { ok_ = false; return 0; } return uint32_t(get(w)); }
    bool ok() const { return ok_; }
    size_t remaining() const { return n_ * 8 - pos_; }
private:
    const uint8_t* d_; size_t n_; size_t pos_ = 0; bool ok_ = true;
};

// ---------------------------------------------------------------- layout
struct FieldLayout {
    FieldKind kind = FieldKind::Float;
    bool snapshot = true;       // false: reliable path (String / Bytes / reliable)
    bool interpolate = false;
    int comps = 1;              // quantized slots (Vec3: 3)
    int bits = 0;               // bits per slot
    int slot = 0;               // offset into the object's slot array
    double min = 0, max = 0, precision = 0;
    int64_t imin = 0;           // Int
    uint64_t steps = 0;         // Int / Float / Vec3: largest quantized value
};

struct ClassLayout {
    std::vector<FieldLayout> fields;
    std::vector<int> snapFields;    // field indices carried in snapshots (in order)
    std::vector<int> reliableFields;
    int slots = 0;                  // quantized slots of the snapshot fields + 1 for the owner
    int ownerSlot = 0;              // last slot: owner (8 bits), a pseudo-field after snapFields
    int maskBits = 0;               // snapFields.size() + 1
    int fullBits = 0;               // all snapshot fields + owner
    int fieldBits(int snapIndex) const;   // snapIndex == snapFields.size(): the owner
};

struct Layout {
    std::vector<ClassLayout> classes;
    int classBits = 1;
    explicit Layout(const Schema& schema);
};

// Quantized slots of one field (out[0..comps)).
void quantize(const FieldLayout& f, const Value& v, uint64_t* out);
void dequantize(const FieldLayout& f, const uint64_t* in, Value& out);
// Value as the client will see it (quantize + dequantize; strings truncated to the limit).
Value normalize(const FieldLayout& f, const Value& v);

// Reliable field values travel byte-aligned.
void writeValue(ByteWriter& w, const FieldLayout& f, const Value& v);
bool readValue(ByteReader& r, const FieldLayout& f, Value& v);

inline void writeBytes(ByteWriter& w, const Bytes& b) { w.u32(uint32_t(b.size())); w.buf.insert(w.buf.end(), b.begin(), b.end()); }
inline bool readBytes(ByteReader& r, Bytes& out, size_t limit) {
    uint32_t n = r.u32();
    if (!r.ok() || n > limit) return false;
    out.resize(n);
    for (uint32_t i = 0; i < n; ++i) out[i] = r.u8();
    return r.ok();
}

} // namespace aiverse::net::replica::wire
