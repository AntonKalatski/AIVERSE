// Schema validation + hash, wire layout and quantization (docs/replica.md § Wire format).
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include "engine/core/Hash.h"
#include "engine/net/replica/ReplicaWire.h"

namespace aiverse::net::replica {

namespace {
constexpr int kMaxFields = 63;   // + the owner pseudo-field = 64 mask bits

const char* kindName(FieldKind k) {
    switch (k) {
    case FieldKind::Bool: return "Bool"; case FieldKind::Int: return "Int"; case FieldKind::Float: return "Float";
    case FieldKind::Vec3: return "Vec3"; case FieldKind::Quat: return "Quat"; case FieldKind::String: return "String";
    case FieldKind::Bytes: return "Bytes";
    }
    return "?";
}

[[noreturn]] void fail(const ClassDef& c, const std::string& what) {
    throw std::runtime_error("replica class '" + c.name + "': " + what);
}

uint64_t floatSteps(const FieldDef& f) {
    double s = std::ceil((f.max - f.min) / f.precision - 1e-9);
    return s < 1 ? 1 : uint64_t(s);
}
} // namespace

ClassId Schema::add(ClassDef def) {
    if (def.name.empty()) throw std::runtime_error("replica class: empty name");
    for (auto& c : classes_) if (c.name == def.name) fail(def, "duplicate class name");
    if (classes_.size() >= 65535) fail(def, "too many classes");
    if (def.fields.size() > size_t(kMaxFields)) fail(def, "more than 63 fields");
    if (!(def.priority >= 0) || !std::isfinite(def.priority)) fail(def, "priority must be >= 0");
    if (!(def.radius >= 0) || !std::isfinite(def.radius)) fail(def, "radius must be >= 0");
    for (size_t i = 0; i < def.fields.size(); ++i) {
        auto& f = def.fields[i];
        if (f.name.empty()) fail(def, "field " + std::to_string(i) + ": empty name");
        for (size_t j = 0; j < i; ++j) if (def.fields[j].name == f.name) fail(def, "duplicate field '" + f.name + "'");
        const std::string at = "field '" + f.name + "' (" + kindName(f.kind) + "): ";
        switch (f.kind) {
        case FieldKind::Bool: case FieldKind::String: case FieldKind::Bytes: break;
        case FieldKind::Int:
            if (!std::isfinite(f.min) || !std::isfinite(f.max) || f.min > f.max || (f.min == f.max && f.min != 0))
                fail(def, at + "needs min < max (or both 0 for a 32-bit range)");
            if (f.min < -9.2e18 || f.max > 9.2e18) fail(def, at + "range exceeds 64 bits");
            break;
        case FieldKind::Float: case FieldKind::Vec3:
            if (!std::isfinite(f.min) || !std::isfinite(f.max) || !(f.max > f.min)) fail(def, at + "needs min < max");
            if (!(f.precision > 0) || !std::isfinite(f.precision)) fail(def, at + "needs precision > 0");
            if (floatSteps(f) >= (uint64_t(1) << 32)) fail(def, at + "(max - min) / precision needs more than 32 bits");
            break;
        case FieldKind::Quat:
            if (f.bits != 0 && (f.bits < 4 || f.bits > 20)) fail(def, at + "bits must be 4..20");
            break;
        }
        if (f.interpolate && f.kind != FieldKind::Float && f.kind != FieldKind::Vec3 && f.kind != FieldKind::Quat)
            fail(def, at + "only Float / Vec3 / Quat fields interpolate");
    }
    if (def.positionField >= int(def.fields.size()) || def.positionField < -1) fail(def, "positionField out of range");
    if (def.positionField >= 0) {
        auto& p = def.fields[size_t(def.positionField)];
        if (p.kind != FieldKind::Vec3 || p.reliable) fail(def, "positionField must be a non-reliable Vec3 field");
    }
    classes_.push_back(std::move(def));
    return ClassId(classes_.size() - 1);
}

int Schema::field(ClassId cls, const std::string& name) const {
    if (cls >= classes_.size()) return -1;
    auto& fs = classes_[cls].fields;
    for (size_t i = 0; i < fs.size(); ++i) if (fs[i].name == name) return int(i);
    return -1;
}

uint64_t Schema::hash() const {
    // Only what shapes the wire: priority, radius and relevance are the server's business.
    Hasher h;
    h.u32(kProtocol);
    h.u32(uint32_t(classes_.size()));
    for (auto& c : classes_) {
        h.str(c.name);
        h.u32(uint32_t(c.fields.size()));
        for (auto& f : c.fields) {
            h.str(f.name);
            h.u32(uint32_t(f.kind));
            uint64_t b;
            std::memcpy(&b, &f.min, 8); h.u64(b);
            std::memcpy(&b, &f.max, 8); h.u64(b);
            std::memcpy(&b, &f.precision, 8); h.u64(b);
            h.u32(f.bits);
            h.u32(f.reliable ? 1 : 0);
        }
    }
    return h.value();
}

namespace wire {

int ClassLayout::fieldBits(int snapIndex) const {
    if (snapIndex >= int(snapFields.size())) return 8;
    auto& f = fields[size_t(snapFields[size_t(snapIndex)])];
    return f.bits * f.comps;
}

Layout::Layout(const Schema& schema) {
    classBits = std::max(1, BitWriter::width(schema.size() > 0 ? schema.size() - 1 : 0));
    classes.resize(schema.size());
    for (size_t c = 0; c < schema.size(); ++c) {
        auto& def = schema.cls(ClassId(c));
        auto& L = classes[c];
        L.fields.resize(def.fields.size());
        int slot = 0;
        for (size_t i = 0; i < def.fields.size(); ++i) {
            auto& d = def.fields[i];
            auto& f = L.fields[i];
            f.kind = d.kind; f.min = d.min; f.max = d.max; f.precision = d.precision; f.interpolate = d.interpolate;
            f.snapshot = !d.reliable && d.kind != FieldKind::String && d.kind != FieldKind::Bytes;
            switch (d.kind) {
            case FieldKind::Bool: f.bits = 1; f.steps = 1; break;
            case FieldKind::Int: {
                double lo = d.min, hi = d.max;
                if (lo == 0 && hi == 0) { lo = -2147483648.0; hi = 2147483647.0; }
                f.imin = int64_t(lo);
                f.steps = uint64_t(int64_t(hi)) - uint64_t(int64_t(lo));
                f.bits = std::max(1, BitWriter::width(f.steps));
                break;
            }
            case FieldKind::Float: case FieldKind::Vec3:
                f.steps = floatSteps(d);
                f.bits = BitWriter::width(f.steps);
                f.comps = d.kind == FieldKind::Vec3 ? 3 : 1;
                break;
            case FieldKind::Quat: { int b = d.bits ? d.bits : 12; f.bits = 2 + 3 * b; f.steps = uint64_t(b); break; }
            case FieldKind::String: case FieldKind::Bytes: f.bits = 0; f.comps = 0; break;
            }
            if (f.snapshot) {
                f.snap = int(L.snapFields.size());
                f.delta = (d.kind == FieldKind::Int || d.kind == FieldKind::Float || d.kind == FieldKind::Vec3) && f.bits >= 8 &&
                          f.bits <= 62;
                f.slot = slot; slot += f.comps;
                L.snapFields.push_back(int(i));
                L.fullBits += f.bits * f.comps;
            } else {
                L.reliableFields.push_back(int(i));
            }
        }
        L.ownerSlot = slot;
        L.slots = slot + 1;
        L.maskBits = int(L.snapFields.size()) + 1;
        L.fullBits += 8;
    }
}

namespace {
float clampf(double v, double lo, double hi) { return float(v < lo ? lo : v > hi ? hi : v); }

uint64_t quantizeAxis(const FieldLayout& f, float v) {
    if (!std::isfinite(v)) return 0;
    double q = std::llround((double(v) - f.min) / f.precision);
    if (q < 0) q = 0;
    if (q > double(f.steps)) q = double(f.steps);
    return uint64_t(q);
}
float dequantizeAxis(const FieldLayout& f, uint64_t q) {
    if (q > f.steps) q = f.steps;
    return clampf(f.min + double(q) * f.precision, f.min, f.max);
}

constexpr float kInvSqrt2 = 0.70710678118f;
} // namespace

void quantize(const FieldLayout& f, const Value& v, uint64_t* out) {
    switch (f.kind) {
    case FieldKind::Bool: out[0] = v.i ? 1 : 0; break;
    case FieldKind::Int: {
        uint64_t off = v.i < f.imin ? 0 : uint64_t(v.i) - uint64_t(f.imin);
        out[0] = off > f.steps ? f.steps : off;
        break;
    }
    case FieldKind::Float: out[0] = quantizeAxis(f, v.f[0]); break;
    case FieldKind::Vec3: for (int a = 0; a < 3; ++a) out[a] = quantizeAxis(f, v.f[size_t(a)]); break;
    case FieldKind::Quat: {
        // smallest three: drop the largest component (sign-normalised so it is positive)
        float q[4] = {v.f[0], v.f[1], v.f[2], v.f[3]};
        double len = 0;
        for (float c : q) len += double(c) * c;
        if (!std::isfinite(len) || len < 1e-12) { q[0] = q[1] = q[2] = 0; q[3] = 1; len = 1; }
        float inv = float(1.0 / std::sqrt(len));
        int big = 0;
        for (int i = 0; i < 4; ++i) { q[i] *= inv; if (std::fabs(q[i]) > std::fabs(q[big])) big = i; }
        if (q[big] < 0) for (float& c : q) c = -c;
        int b = int(f.steps);
        uint64_t maxq = (uint64_t(1) << b) - 1, packed = uint64_t(big), shift = 2;
        for (int i = 0; i < 4; ++i) {
            if (i == big) continue;
            double t = (double(q[i]) + kInvSqrt2) / (2 * kInvSqrt2);
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            packed |= uint64_t(std::llround(t * double(maxq))) << shift;
            shift += uint64_t(b);
        }
        out[0] = packed;
        break;
    }
    case FieldKind::String: case FieldKind::Bytes: break;
    }
}

void dequantize(const FieldLayout& f, const uint64_t* in, Value& out) {
    out = Value{};
    switch (f.kind) {
    case FieldKind::Bool: out.i = in[0] ? 1 : 0; break;
    case FieldKind::Int: out.i = int64_t(uint64_t(f.imin) + std::min(in[0], f.steps)); break;
    case FieldKind::Float: out.f[0] = dequantizeAxis(f, in[0]); break;
    case FieldKind::Vec3: for (int a = 0; a < 3; ++a) out.f[size_t(a)] = dequantizeAxis(f, in[a]); break;
    case FieldKind::Quat: {
        int b = int(f.steps);
        uint64_t maxq = (uint64_t(1) << b) - 1, packed = in[0];
        int big = int(packed & 3);
        int shift = 2;
        double sum = 0;
        float q[4] = {0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) {
            if (i == big) continue;
            uint64_t c = (packed >> shift) & maxq;
            shift += b;
            q[i] = float(double(c) / double(maxq) * 2 * kInvSqrt2 - kInvSqrt2);
            sum += double(q[i]) * q[i];
        }
        q[big] = float(std::sqrt(std::max(0.0, 1.0 - sum)));
        double len = std::sqrt(sum + double(q[big]) * q[big]);
        for (int i = 0; i < 4; ++i) out.f[size_t(i)] = float(q[i] / len);
        break;
    }
    case FieldKind::String: case FieldKind::Bytes: break;
    }
}

Value normalize(const FieldLayout& f, const Value& v) {
    if (f.kind == FieldKind::String || f.kind == FieldKind::Bytes) {
        Value o;
        o.s = v.s.size() > kMaxStringBytes ? v.s.substr(0, kMaxStringBytes) : v.s;
        return o;
    }
    uint64_t q[3] = {0, 0, 0};
    quantize(f, v, q);
    Value o;
    dequantize(f, q, o);
    return o;
}

void writeValue(ByteWriter& w, const FieldLayout& f, const Value& v) {
    switch (f.kind) {
    case FieldKind::Bool: case FieldKind::Int: w.i64(v.i); break;
    case FieldKind::Float: case FieldKind::Vec3: case FieldKind::Quat: {
        uint64_t q[3] = {0, 0, 0};
        quantize(f, v, q);
        for (int a = 0; a < f.comps; ++a) w.u64(q[a]);
        break;
    }
    case FieldKind::String: case FieldKind::Bytes: {
        size_t n = std::min(v.s.size(), kMaxStringBytes);
        w.u32(uint32_t(n));
        w.buf.insert(w.buf.end(), v.s.begin(), v.s.begin() + long(n));
        break;
    }
    }
}

bool readValue(ByteReader& r, const FieldLayout& f, Value& v) {
    v = Value{};
    switch (f.kind) {
    case FieldKind::Bool: case FieldKind::Int: {
        Value raw; raw.i = r.i64();
        if (!r.ok()) return false;
        v = normalize(f, raw);
        return true;
    }
    case FieldKind::Float: case FieldKind::Vec3: case FieldKind::Quat: {
        uint64_t q[3] = {0, 0, 0};
        for (int a = 0; a < f.comps; ++a) q[a] = r.u64();
        if (!r.ok()) return false;
        if (f.kind == FieldKind::Quat && f.bits < 64) q[0] &= (uint64_t(1) << f.bits) - 1;
        dequantize(f, q, v);
        return true;
    }
    case FieldKind::String: case FieldKind::Bytes: {
        uint32_t n = r.u32();
        if (!r.ok() || n > kMaxStringBytes) return false;
        v.s.resize(n);
        for (uint32_t i = 0; i < n; ++i) v.s[i] = char(r.u8());
        return r.ok();
    }
    }
    return false;
}

} // namespace wire
} // namespace aiverse::net::replica
