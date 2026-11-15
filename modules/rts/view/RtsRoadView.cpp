#include "RtsRoadView.h"
#include "RtsRoadKit.h"
#include "../../../engine/platform/Platform.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace aiverse::game {

using namespace render;

namespace {

float f(Fixed v) { return float(v.toDouble()); }
constexpr int kChunk = 32;          // cells per chunk side
constexpr float kLift = 0.025f;     // world units over the terrain (no z-fighting with the ground)

struct Acc {
    std::vector<std::vector<ModelVertex>> v;   // per material (the kit, then the painted road surfaces)
    std::vector<std::vector<uint32_t>> i;
    void need(int mat) { if (int(v.size()) <= mat) { v.resize(size_t(mat) + 1); i.resize(size_t(mat) + 1); } }
    bool empty() const { for (auto& x : i) if (!x.empty()) return false; return true; }
};

float sidewalkHeight(const RoadSideDef& s) {
    switch (s.curb) {
    case RoadCurb::Low: case RoadCurb::Sloped: case RoadCurb::High: return f(s.curbHeight) * (s.curb == RoadCurb::High ? 1.35f : 1.0f);
    default: return 0;
    }
}
int surfaceMat(RoadSurface s) {
    switch (s) {
    case RoadSurface::Asphalt: return RtsRoadView::kAsphalt;
    case RoadSurface::Concrete: return RtsRoadView::kConcrete;
    case RoadSurface::Cobble: return RtsRoadView::kCobble;
    case RoadSurface::Paving: return RtsRoadView::kPaving;
    case RoadSurface::Gravel: return RtsRoadView::kGravel;
    case RoadSurface::Dirt: return RtsRoadView::kDirt;
    case RoadSurface::Grass: return RtsRoadView::kGrass;
    }
    return RtsRoadView::kAsphalt;
}
bool inGaps2(const std::vector<std::pair<float, float>>& g, float s) {
    for (auto& [a, b] : g) if (s >= a && s <= b) return true;
    return false;
}
// slow variation of the asphalt over tens of metres (newer patches, older stretches): a vertex colour multiplier
uint32_t largeScaleTint(float x, float z) {
    auto h = [](int a, int b) { uint32_t v = uint32_t(a) * 374761393u + uint32_t(b) * 668265263u; v = (v ^ (v >> 13)) * 1274126177u; return float((v ^ (v >> 16)) & 0xffffu) / 65535.0f; };
    auto noise = [&](float px, float pz) {
        const int x0 = int(std::floor(px)), z0 = int(std::floor(pz));
        const float fx = px - float(x0), fz = pz - float(z0), u = fx * fx * (3 - 2 * fx), v = fz * fz * (3 - 2 * fz);
        const float a = h(x0, z0) + (h(x0 + 1, z0) - h(x0, z0)) * u, b = h(x0, z0 + 1) + (h(x0 + 1, z0 + 1) - h(x0, z0 + 1)) * u;
        return a + (b - a) * v;
    };
    const float n = noise(x / 9.0f, z / 9.0f) * 0.65f + noise(x / 3.5f + 17, z / 3.5f + 5) * 0.35f;
    const float k = 0.86f + 0.24f * n;   // linear multiplier
    const uint32_t c = uint32_t(std::clamp(k, 0.0f, 1.0f) * 255.0f + 0.5f);
    return c | (c << 8) | (std::min(255u, c + 3) << 16) | 0xff000000u;
}

bool inBay(const RoadEdgeGeom& g, int side, float s) {
    for (auto& b : g.bays) if (!b.inLane && b.side == side && s >= float(b.s0.toDouble()) && s <= float(b.s3.toDouble())) return true;
    return false;
}
// within `margin` of a driveway on that side of the edge
static bool inDriveway(const RoadEdgeGeom& g, int side, float s, float margin) {
    for (auto& d : g.driveways)
        if (d.side == side && std::fabs(s - float(d.s.toDouble())) <= float((d.half + d.flare).toDouble()) + margin) return true;
    return false;
}
bool inGaps(const std::vector<std::pair<Fixed, Fixed>>& g, float s) {
    for (auto& [a, b] : g) if (s >= f(a) && s <= f(b)) return true;
    return false;
}

// Builds the triangles of a network into chunks.
class Mesher {
public:
    using Painted = std::function<int(int type, int variant)>;   // material of a type's painted carriageway, -1: none
    Mesher(const std::vector<RoadTypeDef>& types, const RoadNetwork& n, const RoadGeometry& g, float cm,
           const std::function<float(float, float)>& ground, bool singleChunk, Painted painted = {})
        : T_(types), N_(n), G_(g), cm_(cm), ground_(ground), single_(singleChunk), painted_(std::move(painted)) {}

    std::map<int64_t, Acc> chunks;
    struct Prop { std::string shape, model; mat4 xf; };
    std::vector<Prop> props;
    std::vector<render::SpriteVertex> decals;   // road paint (4 vertices per quad)
    std::vector<render::SpriteVertex> verges;   // soft verges (4 vertices per quad)

    void build() {
        for (size_t i = 0; i < N_.edges.size(); ++i) edge(int(i));
        for (auto& J : G_.junctions) if (!J.split) junction(J);
        joints();
    }

    // Two roads meeting at a node at an angle: fill the wedge on the outer side of the bend (carriageway at road
    // level, sidewalk / shoulder at its height with a curb face), so the surfaces meet without a gap.
    void joints() {
        std::vector<std::vector<std::pair<int, int>>> at(N_.nodes.size());
        for (size_t i = 0; i < N_.edges.size(); ++i) {
            at[size_t(N_.nodeIndex(N_.edges[i].a))].push_back({int(i), 0});
            at[size_t(N_.nodeIndex(N_.edges[i].b))].push_back({int(i), 1});
        }
        for (size_t ni = 0; ni < at.size(); ++ni) {
            if (at[ni].size() != 2 || G_.nodeJunction[ni] >= 0) continue;
            struct Arm { Pt P; float sgn; float cwR, cwL, outR, outL, hR, hL; int walkR, walkL; bool walkRraised, walkLraised, softR, softL; int ei, end, fs; bool fading; };
            Arm arm[2];
            bool ok = true;
            for (int k = 0; k < 2; ++k) {
                const auto [ei, end] = at[ni][size_t(k)];
                const RoadEdge& E = N_.edges[size_t(ei)];
                const RoadEdgeGeom& g = G_.edges[size_t(ei)];
                if (g.pts.size() < 2) { ok = false; break; }
                // at the node the road already has the other road's cross-section if it narrows there
                const RoadTypeDef& t = g.wearInto[size_t(end)] >= 0 ? T_[size_t(g.wearInto[size_t(end)])] : T_[size_t(E.typeIndex)];
                const int fs = g.wearInto[size_t(end)] >= 0 ? 1 : (E.reversed ? -1 : 1);
                Arm& a = arm[k];
                a.P = sample(g, end == 0 ? 0.0f : f(g.length));
                a.sgn = end == 0 ? 1.0f : -1.0f;   // leaving-frame offset -> edge-frame offset
                // leaving right = edge right at end a, edge left at end b
                const int er = end == 0 ? 1 : -1;
                const RoadSideDef& SR = (er * fs) > 0 ? t.right : t.left;
                const RoadSideDef& SL = (-er * fs) > 0 ? t.right : t.left;
                a.cwR = f(t.carriageway(er * fs)); a.cwL = f(t.carriageway(-er * fs));
                a.outR = a.cwR + f(SR.shoulder + SR.sidewalk); a.outL = a.cwL + f(SL.shoulder + SL.sidewalk);
                a.hR = sidewalkHeight(SR); a.hL = sidewalkHeight(SL);
                a.walkR = SR.sidewalk.raw > 0 ? surfaceMat(SR.sidewalkSurface) : surfaceMat(SR.shoulderSurface);
                a.walkL = SL.sidewalk.raw > 0 ? surfaceMat(SL.sidewalkSurface) : surfaceMat(SL.shoulderSurface);
                a.walkRraised = a.hR > 0; a.walkLraised = a.hL > 0;
                a.softR = soft(SR); a.softL = soft(SL);
                a.ei = ei; a.end = end; a.fs = E.reversed ? -1 : 1; a.fading = g.wearInto[size_t(end)] >= 0;
            }
            if (!ok) continue;
            const int road = surfaceMat(T_[size_t(N_.edges[size_t(at[ni][0].first)].typeIndex)].surface);
            const float um = 1.0f / (4.0f / cm_);
            auto P = [&](const Arm& a, float leavingOff, float h) { return this->at(a.P, a.sgn * leavingOff, h); };
            // region between arm i's right side and arm j's left side, both ways round
            for (int k = 0; k < 2; ++k) {
                const Arm& A = arm[k];
                const Arm& B = arm[1 - k];
                const float3 c = this->at(A.P, 0, 0);
                const float3 a0 = P(A, A.cwR, 0), b0 = P(B, -B.cwL, 0);
                // only the outside of the bend is open: on the inside the two roads already overlap, and a wedge
                // there would lie over their carriageways in another texture
                const float3 dA = A.P.t * A.sgn;
                if ((b0.x - c.x) * dA.x + (b0.z - c.z) * dA.z > -1e-4f) continue;
                // the wedge carries on the carriageway texture of the road that does not change at this node
                // (without lines), in that road's own frame run on backwards, so no edge shows between the two
                const Arm* F = !B.fading ? &B : !A.fading ? &A : nullptr;
                int pm = -1;
                if (F && painted_) pm = painted_(N_.edges[size_t(F->ei)].typeIndex, 4 | 8);
                if (pm >= 0) {
                    const RoadTypeDef& ft = T_[size_t(N_.edges[size_t(F->ei)].typeIndex)];
                    const float M = roadkit::paintMargin(ft), tl = f(ft.carriageway(-1)) + M, tw = std::max(0.01f, f(ft.carriageway(-1)) + f(ft.carriageway(1)) + 2 * M);
                    const float pv = cm_ / roadkit::kRoadPaintPeriod;
                    auto uvOf = [&](float3 q) {
                        const float3 d = q - F->P.c;
                        const float o = d.x * F->P.n.x + d.z * F->P.n.z, along = d.x * F->P.t.x + d.z * F->P.t.z;
                        const float u = std::clamp((float(F->fs) * o + tl) / tw, 0.0f, 1.0f);
                        return float2{u, (F->P.s + along) * pv};
                    };
                    tri(pm, c, a0, b0, uvOf(c), uvOf(a0), uvOf(b0), {0, 1, 0});
                } else {
                    const float ku = road == RtsRoadView::kAsphalt ? um * 0.25f : um;   // the carriageways' scale
                    tri(road, c, a0, b0, {c.x * ku, c.z * ku}, {a0.x * ku, a0.z * ku}, {b0.x * ku, b0.z * ku}, {0, 1, 0});
                }
                if (A.softR || B.softL) continue;   // a soft side: the verge decals run round the bend on their own
                if (A.outR > A.cwR + 1e-3f && B.outL > B.cwL + 1e-3f) {
                    const float h = std::min(A.hR, B.hL);
                    const float3 a1 = P(A, A.cwR, h), b1 = P(B, -B.cwL, h), a2 = P(A, A.outR, h), b2 = P(B, -B.outL, h);
                    quad(A.walkR, a1, a2, b2, b1, {a1.x * um, a1.z * um}, {a2.x * um, a2.z * um}, {b2.x * um, b2.z * um}, {b1.x * um, b1.z * um}, {0, 1, 0});
                    if (h > 0) {
                        const float3 out = normalize((a0 + b0) * 0.5f - c);
                        const float len = std::hypot(b0.x - a0.x, b0.z - a0.z), hh = wy(h);
                        quad(RtsRoadView::kCurb, a0, a1, b1, b0, curbUv(0, 0), curbUv(0, hh), curbUv(len, hh), curbUv(len, 0), -out);
                    }
                }
            }
        }
    }

private:
    const std::vector<RoadTypeDef>& T_;
    const RoadNetwork& N_;
    const RoadGeometry& G_;
    float cm_;
    const std::function<float(float, float)>& ground_;
    bool single_;
    Painted painted_;

    float wy(float metres) const { return metres / cm_; }   // metres -> world units
    float gnd(float x, float z) const { return ground_ ? ground_(x, z) : 0.0f; }

    Acc& acc(float3 p) {
        if (single_) return chunks[0];
        const int64_t cx = int64_t(std::floor(p.x / float(kChunk))), cz = int64_t(std::floor(p.z / float(kChunk)));
        return chunks[(cx << 32) ^ (cz & 0xffffffff)];
    }
    // triangle with a flat normal turned toward `hint` (up for surfaces, outwards for faces)
    void tri(int mat, float3 a, float3 b, float3 c, float2 ua, float2 ub, float2 uc, float3 hint) {
        float3 nrm = cross(b - a, c - a);
        if (dot(nrm, nrm) < 1e-14f) return;
        if (dot(nrm, hint) < 0) { std::swap(b, c); std::swap(ub, uc); nrm = -nrm; }
        nrm = normalize(nrm);
        Acc& A = acc(a);
        A.need(mat);
        auto& V = A.v[size_t(mat)];
        auto& I = A.i[size_t(mat)];
        const uint32_t base = uint32_t(V.size());
        const bool tint = mat >= RtsRoadView::kMatCount || mat == RtsRoadView::kAsphalt || mat == RtsRoadView::kAsphaltWorn;
        for (auto [p, uv] : {std::pair{a, ua}, std::pair{b, ub}, std::pair{c, uc}}) {
            ModelVertex mv;
            mv.pos = p; mv.nrm = nrm; mv.uv = uv;
            if (tint) mv.color = largeScaleTint(p.x, p.z);
            V.push_back(mv);
        }
        I.push_back(base); I.push_back(base + 1); I.push_back(base + 2);
    }
    void quad(int mat, float3 a, float3 b, float3 c, float3 d, float2 ua, float2 ub, float2 uc, float2 ud, float3 hint) {
        tri(mat, a, b, c, ua, ub, uc, hint);
        tri(mat, a, c, d, ua, uc, ud, hint);
    }
    void box(int mat, float3 c, float3 ax, float3 ay, float3 az) {   // centre, half axes
        const float3 p[8] = {c - ax - ay - az, c + ax - ay - az, c + ax + ay - az, c - ax + ay - az,
                             c - ax - ay + az, c + ax - ay + az, c + ax + ay + az, c - ax + ay + az};
        const float2 u0{0, 0}, u1{1, 0}, u2{1, 1}, u3{0, 1};
        quad(mat, p[4], p[5], p[6], p[7], u0, u1, u2, u3, az);
        quad(mat, p[0], p[1], p[2], p[3], u0, u1, u2, u3, -az);
        quad(mat, p[1], p[2], p[6], p[5], u0, u1, u2, u3, ax);
        quad(mat, p[0], p[3], p[7], p[4], u0, u1, u2, u3, -ax);
        quad(mat, p[3], p[2], p[6], p[7], u0, u1, u2, u3, ay);
        quad(mat, p[0], p[1], p[5], p[4], u0, u1, u2, u3, -ay);
    }

    struct Pt { float3 c; float3 t, n; float s, y, e; bool ground; };
    Pt sample(const RoadEdgeGeom& g, float s) const {
        const RoadSample r = roadSampleAt(g, Fixed::fromRaw(int64_t(std::llround(double(s) * 65536.0))));
        Pt p;
        p.t = normalize(float3{f(r.t.x), 0, f(r.t.y)});
        p.n = float3{-p.t.z, 0, p.t.x};
        p.s = s;
        p.e = f(r.e);
        p.ground = std::fabs(p.e) < 0.05f;
        p.c = {f(r.p.x), 0, f(r.p.y)};
        p.y = p.ground ? gnd(p.c.x, p.c.z) + kLift : wy(f(r.y)) + kLift;
        p.c.y = p.y;
        return p;
    }
    // point of the cross-section: lateral offset (cells, + right), height over the road (metres)
    float3 at(const Pt& p, float off, float hMetres) const {
        float3 q = p.c + p.n * off;
        q.y = p.ground ? gnd(q.x, q.z) + kLift + wy(hMetres) : p.y + wy(hMetres);
        return q;
    }

    // A road that runs on as a narrower one: over its last stretch every point of the cross-section moves toward
    // the matching point of the other road (median -> its median, carriageway edge -> its edge, outer edge -> its
    // outer edge), so the two meet without a step and a median closes to nothing.
    float narrowMap(const RoadTypeDef& t, const RoadEdgeGeom& g, int fs, float off, float s) const {
        float out = off;
        for (int end = 0; end < 2; ++end) {
            if (g.wear[size_t(end)].raw <= 0 || g.wearInto[size_t(end)] < 0) continue;
            const float d = end == 0 ? s : f(g.length) - s, L = f(g.wear[size_t(end)]);
            if (d >= L) continue;
            float u = 1 - d / L;
            u = u * u * (3 - 2 * u);
            const RoadTypeDef& into = T_[size_t(g.wearInto[size_t(end)])];
            const int sd = off < 0 ? -1 : 1;
            const RoadSideDef& S = (sd * fs) > 0 ? t.right : t.left;
            const RoadSideDef& Si = sd > 0 ? into.right : into.left;
            const float a[4] = {0, f(t.median) * 0.5f, f(t.carriageway(sd * fs)), f(t.carriageway(sd * fs) + S.shoulder + S.sidewalk)};
            const float b[4] = {0, f(into.median) * 0.5f, f(into.carriageway(sd)), f(into.carriageway(sd) + Si.shoulder + Si.sidewalk)};
            const float x = std::fabs(off);
            float y = b[3] + (x - a[3]);
            for (int k = 0; k < 3; ++k)
                if (x <= a[k + 1] + 1e-5f) { const float span = a[k + 1] - a[k]; y = span > 1e-5f ? b[k] + (b[k + 1] - b[k]) * (x - a[k]) / span : b[k + 1]; break; }
            out = off + (float(sd) * y - off) * u;
        }
        return out;
    }

    struct Prof { float off, h; int mat; int role = 0; };   // mat: of the segment ending at this point (-1: none)

    // stretches of an edge under the zebra crossings at its signalised ends
    std::vector<std::pair<float, float>> zebras(int ei, const RoadTypeDef& t, const RoadEdgeGeom& g) const {
        std::vector<std::pair<float, float>> out;
        if (!t.crossings || (t.forward.empty() && t.backward.empty())) return out;
        const RoadEdge& E = N_.edges[size_t(ei)];
        for (int end = 0; end < 2; ++end) {
            const int ni = N_.nodeIndex(end == 0 ? E.a : E.b);
            const int ji = ni >= 0 ? G_.nodeJunction[size_t(ni)] : -1;
            if (ji < 0 || !G_.junctions[size_t(ji)].signal) continue;
            const float trim = f(end == 0 ? g.trim0 : g.trim1), a = 0.3f / cm_, b = a + 3.0f / cm_;
            out.push_back(end == 0 ? std::pair{trim + a, trim + b} : std::pair{f(g.length) - trim - b, f(g.length) - trim - a});
        }
        return out;
    }

    void edge(int ei) {
        const RoadEdge& E = N_.edges[size_t(ei)];
        const RoadTypeDef& t = T_[size_t(E.typeIndex)];
        const RoadEdgeGeom& g = G_.edges[size_t(ei)];
        if (g.pts.size() < 2) return;
        const int fs = E.reversed ? -1 : 1;
        const RoadSideDef& L = fs > 0 ? t.left : t.right;
        const RoadSideDef& R = fs > 0 ? t.right : t.left;
        const float cwl = f(t.carriageway(-fs)), cwr = f(t.carriageway(fs));
        const float s0 = f(g.trim0), s1 = f(g.length - g.trim1);
        if (s1 - s0 < 0.05f) return;
        // stations along the edge: the samples, gap / span boundaries, at most 0.5 cells apart
        std::vector<float> st{s0, s1};
        for (auto& p : g.pts) if (f(p.s) > s0 && f(p.s) < s1) st.push_back(f(p.s));
        for (auto& side : g.gaps) for (auto& [a, b] : side) { st.push_back(f(a)); st.push_back(f(b)); }
        for (auto& sp : g.spans) { st.push_back(f(sp.s0)); st.push_back(f(sp.s1)); }
        const auto zebra = zebras(ei, t, g);
        for (auto& [a, b] : zebra) { st.push_back(a); st.push_back(b); }
        for (auto& d : g.driveways)   // where the kerb starts to come down, is down, and comes back up
            for (float q : {-f(d.half) - f(d.flare), -f(d.half), f(d.half), f(d.half) + f(d.flare)}) st.push_back(f(d.s) + q);
        std::sort(st.begin(), st.end());
        std::vector<float> stations;
        for (float s : st) {
            if (s < s0 || s > s1) continue;
            if (!stations.empty() && s - stations.back() < 0.02f) continue;
            if (!stations.empty()) {
                const float step = (inGaps(g.gaps[0], s) || inGaps(g.gaps[1], s)) ? 0.15f : 0.5f;   // fine where a branch cuts the side
                for (float q = stations.back() + step; q < s - step * 0.2f; q += step) stations.push_back(q);
            }
            stations.push_back(s);
        }
        auto inTunnel = [&](float s) { for (auto& sp : g.spans) if (sp.tunnel && s > f(sp.s0) && s < f(sp.s1)) return true; return false; };
        // the worn end of a paved road that runs on as an unpaved one: 1 at the node, 0 where the wear starts
        auto wearAt = [&](float s) {
            float w = 0;
            if (g.worn[0] && g.wear[0].raw > 0 && s < f(g.wear[0])) w = std::max(w, 1 - s / f(g.wear[0]));
            if (g.worn[1] && g.wear[1].raw > 0 && s > f(g.length) - f(g.wear[1])) w = std::max(w, 1 - (f(g.length) - s) / f(g.wear[1]));
            return w;
        };
        auto narrowAt = [&](float s) {   // cross-section scale toward the unpaved road's width
            float k = 1;
            for (int end = 0; end < 2; ++end) {
                if (g.wear[size_t(end)].raw <= 0 || g.wearInto[size_t(end)] < 0) continue;
                const float d = end == 0 ? s : f(g.length) - s, L = f(g.wear[size_t(end)]);
                if (d >= L) continue;
                const RoadTypeDef& into = T_[size_t(g.wearInto[size_t(end)])];
                const float ratio = f(into.carriageway(1) + into.carriageway(-1)) / std::max(0.01f, f(t.carriageway(1) + t.carriageway(-1)));
                float u = 1 - d / L;
                u = u * u * (3 - 2 * u);
                k = std::min(k, 1 + (ratio - 1) * u);
            }
            return k;
        };
        const float hsL = sidewalkHeight(L), hsR = sidewalkHeight(R);
        const bool medCurb = t.look["median"].getBool("curb", false);
        const float hm = medCurb ? 0.15f : 0.0f;
        const int roadMat = surfaceMat(t.surface);
        const float um = 1.0f / (4.0f / cm_);   // texture tiles every 4 metres: uv per cell
        for (size_t k = 0; k + 1 < stations.size(); ++k) {
            const float sa = stations[k], sb = stations[k + 1], sm = (sa + sb) * 0.5f;
            if (inTunnel(sm)) continue;
            const Pt A = sample(g, sa), B = sample(g, sb);
            const bool gapL = inGaps(g.gaps[0], sm), gapR = inGaps(g.gaps[1], sm);
            // the cross-section, left to right; role 1 points move out with a bus stop bay (the kerb), role 2 points
            // only when the sidewalk would get narrower than 1.5 m (its outer edge)
            std::vector<Prof> pr;
            const float swL = f(L.sidewalk), swR = f(R.sidewalk);
            const int road = roadMat;
            // the carriageway with its lines and wheel tracks painted in (a texture per type and open side); where
            // the road wears out or narrows the same texture without the lines (the tracks run on), and a stretch
            // wearing out gets the broken asphalt as a decal over it, growing toward the unpaved road
            int carr = road;
            const float wearK = wearAt(sm);
            const bool fading = wearK > 0.0f || narrowAt(sm) < 0.999f;
            if (painted_) {
                // a bay or a driveway opens the edge line on its side; a driveway also breaks the centre line
                const bool dwR = inDriveway(g, 1, sm, 0), dwL = inDriveway(g, -1, sm, 0);
                const bool openR = gapR || inBay(g, 1, sm) || dwR, openL = gapL || inBay(g, -1, sm) || dwL;
                const int variant = 8 | (dwR || dwL ? 16 : 0) | (fading ? 4 : fs > 0 ? (openR ? 1 : 0) | (openL ? 2 : 0) : (openL ? 1 : 0) | (openR ? 2 : 0));   // 8: tracks as decals
                const int m = painted_(E.typeIndex, variant);
                if (m >= 0) carr = m;
            }
            const float ct = curbTop();
            if (!gapL && L.sidewalk.raw > 0) {
                pr.push_back({-(cwl + f(L.shoulder) + swL), hsL, -1, 2});
                if (hsL > 0) { pr.push_back({-(cwl + f(L.shoulder) + ct), hsL, surfaceMat(L.sidewalkSurface), 1}); pr.push_back({-(cwl + f(L.shoulder)), hsL, RtsRoadView::kCurb, 1}); }
                else pr.push_back({-(cwl + f(L.shoulder)), hsL, surfaceMat(L.sidewalkSurface), 1});
            }
            if (!gapL && hsL > 0 && !pr.empty()) pr.push_back({-(cwl + f(L.shoulder)), 0, RtsRoadView::kCurb, 1});
            const bool softL = soft(L) && A.ground && B.ground, softR = soft(R) && A.ground && B.ground;   // verges as decals on the ground
            if (!gapL && L.shoulder.raw > 0 && !softL) { if (pr.empty()) pr.push_back({-(cwl + f(L.shoulder)), 0, -1, 1}); pr.push_back({-cwl, 0, surfaceMat(L.shoulderSurface), 1}); }
            if (pr.empty()) pr.push_back({-cwl, 0, -1, 1});
            pr.push_back({-cwl, 0, carr, 0});   // the bay's own surface (no width without a bay): the carriageway texture's margin
            if (t.median.raw > 0) {
                const float m = f(t.median) / 2;
                // a crossing cuts through the median at road level, paved (no grass, no curb under the zebra)
                const bool cut = inGaps2(zebra, sm);
                const float h = cut ? 0.0f : hm;
                pr.push_back({-m, 0, carr, 0});
                if (h > 0) pr.push_back({-m, h, RtsRoadView::kCurb, 0});
                pr.push_back({m, h, cut ? RtsRoadView::kConcrete : surfaceMat(t.medianSurface), 0});
                if (h > 0) pr.push_back({m, 0, RtsRoadView::kCurb, 0});
            }
            pr.push_back({cwr, 0, carr, 0});
            pr.push_back({cwr, 0, carr, 1});
            if (!gapR && R.shoulder.raw > 0 && !softR) pr.push_back({cwr + f(R.shoulder), 0, surfaceMat(R.shoulderSurface), 1});
            if (!gapL && softL) verge(g, sa, sb, -1, std::fabs(narrowMap(t, g, fs, -1.0f * cwl, sa)), std::fabs(narrowMap(t, g, fs, -1.0f * cwl, sb)), f(L.shoulder), L.shoulder.raw > 0 ? L.shoulderSurface : t.surface == RoadSurface::Asphalt ? RoadSurface::Gravel : t.surface);
            if (!gapR && softR) verge(g, sa, sb, 1, std::fabs(narrowMap(t, g, fs, 1.0f * cwr, sa)), std::fabs(narrowMap(t, g, fs, 1.0f * cwr, sb)), f(R.shoulder), R.shoulder.raw > 0 ? R.shoulderSurface : t.surface == RoadSurface::Asphalt ? RoadSurface::Gravel : t.surface);
            // a kerbed side narrowing into a soft one: its verge fades in over the stretch (the kerb comes down)
            for (int end = 0; end < 2; ++end) {
                if (g.wear[size_t(end)].raw <= 0 || g.wearInto[size_t(end)] < 0) continue;
                const float Lw = f(g.wear[size_t(end)]);
                auto ux = [&](float s) { const float d = end == 0 ? s : f(g.length) - s; if (d >= Lw) return 0.0f; const float u = 1 - d / Lw; return u * u * (3 - 2 * u); };
                const float ua = ux(sa), ub = ux(sb);
                if (ua <= 0 && ub <= 0) continue;
                const RoadTypeDef& into = T_[size_t(g.wearInto[size_t(end)])];
                for (int side : {-1, 1}) {
                    const RoadSideDef& S = side < 0 ? L : R;
                    const RoadSideDef& Si = side > 0 ? into.right : into.left;
                    if (soft(S) || !soft(Si) || !A.ground || !B.ground || (side < 0 ? gapL : gapR)) continue;
                    const float cw = side < 0 ? cwl : cwr;
                    const float ca = std::fabs(narrowMap(t, g, fs, float(side) * cw, sa)), cb = std::fabs(narrowMap(t, g, fs, float(side) * cw, sb));
                    const RoadSurface vs = Si.shoulder.raw > 0 ? Si.shoulderSurface : into.surface == RoadSurface::Asphalt ? RoadSurface::Gravel : into.surface;
                    verge(g, sa, sb, side, ca, cb, f(Si.shoulder), vs, ua, ub);
                }
            }
            if (!gapR && R.sidewalk.raw > 0) {
                const float o = cwr + f(R.shoulder);
                if (hsR > 0) { pr.push_back({o, hsR, RtsRoadView::kCurb, 1}); pr.push_back({o + ct, hsR, RtsRoadView::kCurb, 1}); }
                pr.push_back({o + swR, hsR, surfaceMat(R.sidewalkSurface), 2});
            }
            const float minWalk = 1.5f / cm_;
            auto offBay = [&](const Prof& p, const Pt& P) {
                if (p.role == 0) return p.off;
                const int sd = p.off < 0 ? -1 : 1;
                const float w = f(roadBayWidth(g, sd, Fixed::fromRaw(int64_t(std::llround(double(P.s) * 65536.0)))));
                if (w <= 0) return p.off;
                const float spare = std::max(0.0f, (sd < 0 ? swL : swR) - minWalk);
                return p.off + float(sd) * (p.role == 1 ? w : std::max(0.0f, w - spare));
            };
            auto offAt = [&](const Prof& p, const Pt& P) { return narrowMap(t, g, fs, offBay(p, P), P.s); };
            // where the road runs on as one without a raised sidewalk (or median) on that side, the kerb comes down
            // over the same stretch it narrows in, to nothing at the node
            auto hk = [&](const Prof& p, const Pt& P) {
                if (p.h <= 0) return 1.0f;
                // a driveway: the kerb and the sidewalk on its side come down to a low lip over the flares
                const int pside = p.off < 0 ? -1 : 1;
                const float cwSide = pside < 0 ? cwl : cwr;
                float drop = 1.0f;
                if (std::fabs(p.off) >= cwSide - 1e-4f)
                    for (auto& d : g.driveways) {
                        if (d.side != pside) continue;
                        const float x = std::fabs(P.s - f(d.s)), h = f(d.half), fl = f(d.flare);
                        if (x >= h + fl) continue;
                        const float k = x <= h ? 0.0f : (x - h) / fl;
                        drop = std::min(drop, 0.12f + 0.88f * k);
                    }
                if (drop < 1.0f) return drop;
                for (int end = 0; end < 2; ++end) {
                    if (g.wear[size_t(end)].raw <= 0 || g.wearInto[size_t(end)] < 0) continue;
                    const float d = end == 0 ? P.s : f(g.length) - P.s, Lw = f(g.wear[size_t(end)]);
                    if (d >= Lw) continue;
                    float u = 1 - d / Lw;
                    u = u * u * (3 - 2 * u);
                    const RoadTypeDef& into = T_[size_t(g.wearInto[size_t(end)])];
                    const bool median = p.role == 0 && std::fabs(p.off) <= f(t.median) * 0.5f + 1e-4f;
                    const int sd = p.off < 0 ? -1 : 1;
                    const bool raised = median ? into.look["median"].getBool("curb", false) && into.median.raw > 0
                                               : sidewalkHeight(sd > 0 ? into.right : into.left) > 0;
                    if (!raised) return 1 - u;
                }
                return 1.0f;
            };
            for (size_t q = 1; q < pr.size(); ++q) {
                const Prof& p0 = pr[q - 1];
                const Prof& p1 = pr[q];
                if (p1.mat < 0) continue;
                const float oa0 = offAt(p0, A), oa1 = offAt(p1, A), ob0 = offAt(p0, B), ob1 = offAt(p1, B);
                const bool face = std::fabs(p1.off - p0.off) < 1e-4f && p0.role == p1.role;
                if (!face && std::fabs(oa1 - oa0) < 1e-4f && std::fabs(ob1 - ob0) < 1e-4f) continue;   // no width here
                if (face && std::fabs(p1.h - p0.h) < 1e-4f) continue;
                const float3 a0 = at(A, oa0, p0.h * hk(p0, A)), a1 = at(A, oa1, p1.h * hk(p1, A)), b0 = at(B, ob0, p0.h * hk(p0, B)), b1 = at(B, ob1, p1.h * hk(p1, B));
                float3 hint{0, 1, 0};
                if (face) hint = A.n * (p1.h < p0.h ? 1.0f : -1.0f);   // curb faces look at the lower side
                if (p1.mat == carr && carr != road) {   // painted carriageway: u across the type's carriageway, v per dash period
                    const float M = roadkit::paintMargin(t), tl = f(t.carriageway(-1)) + M, tw = std::max(0.01f, f(t.carriageway(-1)) + f(t.carriageway(1)) + 2 * M);
                    // where the road narrows the texture is not squeezed (its grain would change): each point keeps
                    // its distance from its side's edge, so the grime at the curbs stays where the curbs are
                    auto tex = [&](float o, const Pt& P, float pre) {
                        if (!fading || t.unpaved()) return pre;   // ruts down the middle: squeezed they stay there
                        const int sd = o < 0 ? -1 : 1;
                        const float src = sd < 0 ? cwl : cwr;
                        const float cur = std::fabs(narrowMap(t, g, fs, float(sd) * src, P.s));
                        return float(sd) * std::max(0.0f, src - (cur - std::fabs(o)));
                    };
                    const float pa0 = tex(oa0, A, offBay(p0, A)), pa1 = tex(oa1, A, offBay(p1, A)), pb0 = tex(ob0, B, offBay(p0, B)), pb1 = tex(ob1, B, offBay(p1, B));
                    const float ua0 = (float(fs) * pa0 + tl) / tw, ua1 = (float(fs) * pa1 + tl) / tw, ub0 = (float(fs) * pb0 + tl) / tw, ub1 = (float(fs) * pb1 + tl) / tw;
                    const float pv = cm_ / roadkit::kRoadPaintPeriod;
                    quad(p1.mat, a0, a1, b1, b0, {ua0, sa * pv}, {ua1, sa * pv}, {ub1, sb * pv}, {ub0, sb * pv}, hint);
                    continue;
                }
                // plain asphalt at the scale of the carriageway texture (the two must look alike side by side)
                const float k = (p1.mat == RtsRoadView::kAsphalt || p1.mat == RtsRoadView::kAsphaltWorn) ? 0.25f : 1.0f;
                if (!face && p1.mat != RtsRoadView::kAsphalt && p1.mat != RtsRoadView::kAsphaltWorn) {
                    // paving, cobbles, gravel, grass: laid in the world, not along the road, so the pattern stays whole
                    // where the kerb bends round a bay or a corner (and matches the junction corners)
                    auto w = [&](float3 q) { return float2{q.x * um, q.z * um}; };
                    quad(p1.mat, a0, a1, b1, b0, w(a0), w(a1), w(b1), w(b0), hint);
                    continue;
                }
                if (face) {   // a curb face: up the face, along the road
                    const float hh = wy(std::fabs(p1.h - p0.h));
                    const bool up = p1.h > p0.h;
                    quad(p1.mat, a0, a1, b1, b0, curbUv(sa, up ? 0 : hh), curbUv(sa, up ? hh : 0), curbUv(sb, up ? hh : 0), curbUv(sb, up ? 0 : hh), hint);
                    continue;
                }
                const float va = sa * um * k, vb = sb * um * k;
                const float u0 = oa0 * um * k, u1 = oa1 * um * k;
                quad(p1.mat, a0, a1, b1, b0, {u0, va}, {u1, va}, {u1, vb}, {u0, vb}, hint);
            }
            if (wearK > 0.0f || wearAt(sa) > 0.0f || wearAt(sb) > 0.0f) {   // the asphalt breaking up toward the unpaved road
                const Prof el{-cwl, 0, 0, 0}, er{cwr, 0, 0, 0};
                const float m = 0.15f / cm_;   // a little over the edges, under the verges
                const float la = offAt(el, A) - m, ra = offAt(er, A) + m, lb = offAt(el, B) - m, rb = offAt(er, B) + m;
                const float u0 = float(roadkit::kVergeWearSlot) / float(roadkit::kVergeSlots) + 0.002f, u1 = float(roadkit::kVergeWearSlot + 1) / float(roadkit::kVergeSlots) - 0.002f;
                const float va = std::clamp(wearAt(sa), 0.002f, 0.998f), vb = std::clamp(wearAt(sb), 0.002f, 0.998f);
                const float3 c[4] = {at(B, lb, 0), at(B, rb, 0), at(A, ra, 0), at(A, la, 0)};
                const float2 uv[4] = {{u0, vb}, {u1, vb}, {u1, va}, {u0, va}};
                for (int k = 0; k < 4; ++k) {
                    render::SpriteVertex v;
                    v.pos = c[k]; v.pos.y += 0.002f;
                    v.uv = uv[k]; v.color = 0xffffffffu; v.team = 0;
                    v.rpos = {v.pos.x, v.pos.y, v.pos.z, 0};
                    verges.push_back(v);
                }
            }
            if (painted_) wheelTracks(t, g, fs, A, B);   // every paved road's wheel tracks: one look everywhere
            for (auto& d : g.driveways)   // the whole driveway, once: from the station its middle falls in
                if (f(d.s) >= sa && f(d.s) < sb) driveway(g, d, d.side > 0 ? R : L, d.side > 0 ? cwr : cwl);
            if (t.unpaved()) dirtLeadIn(ei, t, g, A, B);
            // where a branch leaves / joins: what is left of each side outside the other roads' carriageways
            if (gapL) clippedSide(ei, A, B, -1, cwl, cwl + f(L.shoulder) + f(L.sidewalk), hsL, L);
            if (gapR) clippedSide(ei, A, B, 1, cwr, cwr + f(R.shoulder) + f(R.sidewalk), hsR, R);
            // under the road: embankment walls, bridge deck edges and its underside
            const float e = std::max(A.e, B.e);
            if (e > 0.05f) {
                const float lo = pr.front().off, hi = pr.back().off;
                const bool bridge = e > f(kRoadBridgeAbove);
                const float th = 0.9f;   // deck thickness, metres
                for (int side = 0; side < 2; ++side) {
                    const float off = side ? hi : lo, h = side ? pr.back().h : pr.front().h;
                    const float3 ta = at(A, off, h), tb = at(B, off, h);
                    float3 ba = ta, bb = tb;
                    if (bridge) { ba.y = A.y - wy(th); bb.y = B.y - wy(th); }
                    else { ba.y = gnd(ta.x, ta.z); bb.y = gnd(tb.x, tb.z); }
                    quad(bridge ? RtsRoadView::kBridge : RtsRoadView::kConcrete, ta, tb, bb, ba, {0, sa * um}, {0, sb * um}, {1, sb * um}, {1, sa * um}, A.n * (side ? 1.0f : -1.0f));
                    // railing on a bridge without a barrier of its own
                    const RoadSideDef& S = side ? R : L;
                    if (bridge && S.barrier == RoadBarrier::None) barrierSeg(RoadBarrier::Concrete, A, B, off, h, side ? 1.0f : -1.0f, 0);
                }
                if (bridge) {
                    float3 a0 = at(A, lo, 0), a1 = at(A, hi, 0), b0 = at(B, lo, 0), b1 = at(B, hi, 0);
                    a0.y = a1.y = A.y - wy(th); b0.y = b1.y = B.y - wy(th);
                    quad(RtsRoadView::kBridge, a0, a1, b1, b0, {0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, -1, 0});
                }
            }
            // barriers at the outer edges and in the median
            if (!gapL && L.barrier != RoadBarrier::None) barrierSeg(L.barrier, A, B, pr.front().off, pr.front().h, -1, f(L.barrierHeight));
            if (!gapR && R.barrier != RoadBarrier::None) barrierSeg(R.barrier, A, B, pr.back().off, pr.back().h, 1, f(R.barrierHeight));
            if (t.medianBarrier != RoadBarrier::None) barrierSeg(t.medianBarrier, A, B, 0, hm, 0, 0);
        }
        const auto worn = [&](float s) { return inTunnel(s) || wearAt(s) > 0.0f || narrowAt(s) < 0.999f; };
        markings(t, g, fs, s0, s1, worn);
        pillars(t, g, fs, s0, s1);
        portals(t, g, fs, s0, s1);
        propsOf(ei, t, g, fs, s0, s1, worn);
        shelters(t, g, fs);
        crossings(ei, t, g, fs);
    }

    bool inCut(int ei, float x, float z) const {
        for (size_t k = 0; k < G_.cuts.size(); ++k) {
            if (G_.cutEdge[k] == ei) continue;
            const auto& p = G_.cuts[k];
            int pos = 0, neg = 0;
            for (size_t i = 0; i < p.size(); ++i) {
                const float ax = f(p[i].x), az = f(p[i].y), bx = f(p[(i + 1) % p.size()].x), bz = f(p[(i + 1) % p.size()].y);
                const float c = (bx - ax) * (z - az) - (bz - az) * (x - ax);
                pos += c > 0; neg += c < 0;
            }
            if (!pos || !neg) return true;
        }
        return false;
    }
    // one side of the road between two stations, minus the parts another road's carriageway covers: raised pieces
    // with curb faces along the cut, so the curb follows the branch's edge and the gore between the roads stays paved
    void clippedSide(int ei, const Pt& A, const Pt& B, int side, float inner, float outer, float h, const RoadSideDef& S) {
        if (outer <= inner + 1e-4f) return;
        const int mat = S.sidewalk.raw > 0 ? surfaceMat(S.sidewalkSurface) : surfaceMat(S.shoulderSurface);
        const int n = std::max(2, int((outer - inner) / 0.08f));
        const float3 mid = (A.c + B.c) * 0.5f, nm = normalize(A.n + B.n) * float(side);
        auto covered = [&](float o) { const float3 q = mid + nm * o; return inCut(ei, q.x, q.z); };
        const float um = 1.0f / (4.0f / cm_);
        int k = 0;
        while (k < n) {
            const float o0 = inner + (outer - inner) * float(k) / float(n);
            if (covered(o0 + (outer - inner) * 0.5f / float(n))) { ++k; continue; }
            int e = k;
            while (e < n && !covered(inner + (outer - inner) * (float(e) + 0.5f) / float(n))) ++e;
            const float a = o0, b = inner + (outer - inner) * float(e) / float(n);
            const float sa = float(side) * a, sb = float(side) * b;
            const float3 a0 = at(A, sa, h), a1 = at(A, sb, h), b0 = at(B, sa, h), b1 = at(B, sb, h);
            auto wuv = [&](float3 q) { return float2{q.x * um, q.z * um}; };
            quad(mat, a0, a1, b1, b0, wuv(a0), wuv(a1), wuv(b1), wuv(b0), {0, 1, 0});
            if (h > 0)   // curb faces at both ends of the piece (toward the road / the branch)
                for (int end = 0; end < 2; ++end) {
                    const float o = end ? sb : sa;
                    if (end == 1 && e >= n) continue;   // the outer edge of the sidewalk: no face
                    const float3 top0 = at(A, o, h), top1 = at(B, o, h), bot0 = at(A, o, 0), bot1 = at(B, o, 0);
                    const float hh = wy(h);
                    quad(RtsRoadView::kCurb, bot0, top0, top1, bot1, curbUv(A.s, 0), curbUv(A.s, hh), curbUv(B.s, hh), curbUv(B.s, 0), A.n * (float(side) * (end ? 1.0f : -1.0f)));
                }
            k = e;
        }
    }

    void barrierSeg(RoadBarrier b, const Pt& A, const Pt& B, float off, float h, float outward, float height) {
        // pulled in a little from the edge so it stands on the sidewalk / shoulder
        const float in = outward == 0 ? 0.0f : 0.06f * outward;
        const float o = off - in;
        auto up = [&](const Pt& P, float hh) { return at(P, o, h + hh); };
        auto outer = [&](const Pt& P, float d, float hh) { return at(P, o + d, h + hh); };
        const float3 nA = A.n * (outward == 0 ? 1.0f : outward);
        switch (b) {
        case RoadBarrier::Guardrail: {
            const float hh = height > 0 ? height : 0.75f;
            const float3 a0 = up(A, hh - 0.32f), a1 = up(A, hh), b0 = up(B, hh - 0.32f), b1 = up(B, hh);
            quad(RtsRoadView::kMetal, a0, a1, b1, b0, {0, 0}, {0, 1}, {1, 1}, {1, 0}, -nA);
            quad(RtsRoadView::kMetal, a0, a1, b1, b0, {0, 0}, {0, 1}, {1, 1}, {1, 0}, nA);
            if (std::fmod(A.s * cm_, 2.0f) < (B.s - A.s) * cm_ + 1e-3f) {   // a post every 2 m
                const float3 c = at(A, o + 0.04f * (outward == 0 ? 1 : outward), h + hh * 0.5f);
                box(RtsRoadView::kMetal, c, A.t * wy(0.08f), {0, wy(hh * 0.5f), 0}, A.n * wy(0.08f));
            }
            break;
        }
        case RoadBarrier::Concrete: {   // New Jersey profile: wide foot, narrow top
            const float hh = height > 0 ? height : 0.85f, w0 = wy(0.3f), w1 = wy(0.1f);
            const float side = outward == 0 ? 0.0f : outward;
            const float c = side * w0;   // keeps the whole barrier on the outer side of the edge
            Prof p[5] = {{c - w0, 0, -1}, {c - w1, hh, RtsRoadView::kConcrete}, {c + w1, hh, RtsRoadView::kConcrete}, {c + w0, 0, RtsRoadView::kConcrete}, {c - w0, 0, -1}};
            for (int q = 1; q < 4; ++q) {
                const float3 a0 = outer(A, p[q - 1].off, p[q - 1].h), a1 = outer(A, p[q].off, p[q].h), b0 = outer(B, p[q - 1].off, p[q - 1].h), b1 = outer(B, p[q].off, p[q].h);
                const float3 hint = q == 2 ? float3{0, 1, 0} : A.n * (q == 1 ? -1.0f : 1.0f);
                quad(RtsRoadView::kConcrete, a0, a1, b1, b0, {0, 0}, {0, 1}, {1, 1}, {1, 0}, hint);
            }
            break;
        }
        case RoadBarrier::Wall: case RoadBarrier::Hedge: case RoadBarrier::Fence: {
            const float hh = height > 0 ? height : b == RoadBarrier::Wall ? 2.2f : b == RoadBarrier::Hedge ? 1.2f : 1.6f;
            const float w = wy(b == RoadBarrier::Hedge ? 0.45f : b == RoadBarrier::Wall ? 0.25f : 0.03f);
            const int mat = b == RoadBarrier::Hedge ? RtsRoadView::kHedge : b == RoadBarrier::Wall ? RtsRoadView::kConcrete : RtsRoadView::kDark;
            const float side = outward == 0 ? 0.0f : outward;
            for (int sd = -1; sd <= 1; sd += 2) {
                const float d = side * w + sd * w;
                quad(mat, outer(A, d, 0), outer(A, d, hh), outer(B, d, hh), outer(B, d, 0), {0, 0}, {0, 1}, {1, 1}, {1, 0}, A.n * float(sd));
            }
            quad(mat, outer(A, side * w - w, hh), outer(A, side * w + w, hh), outer(B, side * w + w, hh), outer(B, side * w - w, hh), {0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 1, 0});
            if (b == RoadBarrier::Fence && std::fmod(A.s * cm_, 2.5f) < (B.s - A.s) * cm_ + 1e-3f)
                box(RtsRoadView::kMetal, outer(A, side * w, hh * 0.5f), A.t * wy(0.05f), {0, wy(hh * 0.5f), 0}, A.n * wy(0.05f));
            break;
        }
        default: break;
        }
    }

    float curbTop() const { return 0.15f / cm_; }   // the width of the curb stone's top, cells
    // UV of a curb face: u up the face, v along the curb (the same scale everywhere, so the stone runs on unbroken)
    float2 curbUv(float along, float height) const { const float um = 1.0f / (4.0f / cm_); return {height * um, along * um}; }

    // one wheel track as a decal from A to B at edge offsets oa / ob (cells), `hw` half wide; rgb: the track's tint
    // in the atlas' sRGB (0.55 grey on asphalt, browner as it runs into a dirt road's ruts), alpha its strength
    void trackQuad(const Pt& A, const Pt& B, float oa, float ob, float hw, float va, float vb, uint32_t col) {
        const float u0 = float(roadkit::kVergeTrackSlot) / float(roadkit::kVergeSlots) + 0.002f, u1 = float(roadkit::kVergeTrackSlot + 1) / float(roadkit::kVergeSlots) - 0.002f;
        const float3 c[4] = {at(B, ob - hw, 0), at(B, ob + hw, 0), at(A, oa + hw, 0), at(A, oa - hw, 0)};
        const float2 uv[4] = {{u0, vb}, {u1, vb}, {u1, va}, {u0, va}};
        for (int k = 0; k < 4; ++k) {
            render::SpriteVertex v;
            v.pos = c[k]; v.pos.y += 0.0025f;
            v.uv = uv[k]; v.color = col; v.team = 0;
            v.rpos = {v.pos.x, v.pos.y, v.pos.z, 0};
            verges.push_back(v);
        }
    }
    static uint32_t trackColor(float r, float g, float b, float a) {
        auto c = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return c(r) | c(g) << 8 | c(b) << 16 | c(a) << 24;
    }
    // where a road narrows or wears out into another one its wheel tracks are decals (the texture there has none):
    // every lane's pair runs from where it is on this road to the lane it joins on the other (two lanes merge into
    // one; on a dirt road all of them into its one pair of ruts down the middle), turning brown as the asphalt
    // breaks up
    static constexpr float kTrackAlpha = 0.38f;   // one strength for every wheel track: roads, transitions, junctions
    void wheelTracks(const RoadTypeDef& t, const RoadEdgeGeom& g, int fs, const Pt& A, const Pt& B) {
        if (t.unpaved() || !t.drivable()) return;   // dirt and gravel have their ruts in the texture
        // the end this stretch narrows or wears into another road at (if any), and how far along it is
        int end = -1;
        float pa = 0, pb = 0;
        for (int e = 0; e < 2 && end < 0; ++e) {
            if (g.wear[size_t(e)].raw <= 0 || g.wearInto[size_t(e)] < 0) continue;
            const float L = f(g.wear[size_t(e)]);
            auto param = [&](float s) {
                const float d = e == 0 ? s : f(g.length) - s;
                if (d >= L) return 0.0f;
                const float u = 1 - d / L;
                return u * u * (3 - 2 * u);
            };
            pa = param(A.s); pb = param(B.s);
            if (pa > 0 || pb > 0) end = e;
        }
        const RoadTypeDef* into = end >= 0 ? &T_[size_t(g.wearInto[size_t(end)])] : nullptr;
        const auto& srcLanes = t.forward.empty() ? t.backward : t.forward;
        const float perM = f(srcLanes[0].width) / 3.5f, gauge = 0.82f * perM, hw = 0.32f * 2.0f * perM;
        const bool dirt = into && into->unpaved();
        float intoGauge = gauge;
        if (dirt) { const auto& il = into->forward.empty() ? into->backward : into->forward; intoGauge = 0.80f * f(il[0].width) / 3.0f; }
        const float per = roadkit::kRoadPaintPeriod / 3.0f / cm_;
        // tracks are not ruler-straight: a slow sway, none at the ends (they meet the junctions' tracks there)
        const float s0 = f(g.trim0), s1 = f(g.length - g.trim1), ramp = 3.0f / cm_;
        auto sway = [&](float s) {
            const float env = std::clamp(std::min(s - s0, s1 - s) / ramp, 0.0f, 1.0f);
            const float x = s * cm_;   // metres
            return env * 0.12f * perM * (std::sin(x * 0.21f + 1.3f) + 0.6f * std::sin(x * 0.53f + 0.4f));
        };
        const float wa = sway(A.s), wb = sway(B.s);
        for (int dir : {1, -1}) {
            const int sd = fs * dir;   // the side of the edge this direction drives on
            float off = f(t.median) * 0.5f;
            int k = 0;
            for (auto& l : t.lanesOf(dir)) {
                const float w = f(l.width), c = off + w * 0.5f;
                off += w;
                if (l.use == RoadLaneUse::Parking || l.use == RoadLaneUse::Bike) continue;
                // the lane it runs into: the same place counted from the centre, the outermost if there are fewer
                float tc = c;
                if (into && !dirt) {
                    std::vector<float> centres;
                    float o = f(into->median) * 0.5f;
                    for (auto& m : into->lanesOf(sd)) { if (m.use != RoadLaneUse::Parking && m.use != RoadLaneUse::Bike) centres.push_back(o + f(m.width) * 0.5f); o += f(m.width); }
                    if (!centres.empty()) tc = centres[std::min(size_t(k), centres.size() - 1)];
                }
                ++k;
                for (int wheel : {-1, 1}) {
                    const float from = float(sd) * c + float(wheel) * gauge;
                    const float to = !into ? from : dirt ? float(wheel) * intoGauge : float(sd) * tc + float(wheel) * intoGauge;
                    const float oa = from + (to - from) * pa + wa * (1 - pa), ob = from + (to - from) * pb + wb * (1 - pb);
                    // asphalt grey (sRGB 0.55 of the atlas' 0.48); browner only where it runs into a dirt road's ruts
                    const float km = dirt ? (pa + pb) * 0.5f : 0.0f;
                    const uint32_t col = trackColor(0.55f + (0.875f - 0.55f) * km, 0.55f + (0.79f - 0.55f) * km, 0.55f + (0.67f - 0.55f) * km, kTrackAlpha);
                    trackQuad(A, B, oa, ob, hw, A.s / per, B.s / per, col);
                }
            }
        }
    }

    // the first metres of a dirt road that a paved one wears out into: the broken-asphalt decal at its fullest and
    // the wheel tracks run on over it and fade, so the end of the paved road leaves no edge across the dirt
    void dirtLeadIn(int ei, const RoadTypeDef& t, const RoadEdgeGeom& g, const Pt& A, const Pt& B) {
        if (!t.drivable()) return;
        const RoadEdge& E = N_.edges[size_t(ei)];
        const float X = 3.0f / cm_;
        for (int end = 0; end < 2; ++end) {
            const float da = end == 0 ? A.s : f(g.length) - A.s, db = end == 0 ? B.s : f(g.length) - B.s;
            if (std::min(da, db) >= X) continue;
            const int32_t node = end == 0 ? E.a : E.b;
            bool fromPaved = false;
            for (size_t k = 0; k < N_.edges.size() && !fromPaved; ++k) {
                if (int(k) == ei) continue;
                const RoadEdge& o = N_.edges[k];
                for (int oe = 0; oe < 2; ++oe)
                    if ((oe == 0 ? o.a : o.b) == node && G_.edges[k].worn[size_t(oe)]) fromPaved = true;
            }
            if (!fromPaved) continue;
            auto fade = [&](float d) { const float u = std::clamp(1 - d / X, 0.0f, 1.0f); return u * u * (3 - 2 * u); };
            const float fa = fade(da), fb = fade(db);
            const float m = 0.15f / cm_;
            const float l = -f(t.carriageway(-1)) - m, r = f(t.carriageway(1)) + m;
            const float u0 = float(roadkit::kVergeWearSlot) / float(roadkit::kVergeSlots) + 0.002f, u1 = float(roadkit::kVergeWearSlot + 1) / float(roadkit::kVergeSlots) - 0.002f;
            const float3 c[4] = {at(B, l, 0), at(B, r, 0), at(A, r, 0), at(A, l, 0)};
            // v in the fully broken-up band of the slot, running back and forth along the road (no stretched rows)
            auto vv = [&](float s) { const float q = s * cm_ / 3.0f; const float fr = q - std::floor(q); return 0.90f + 0.096f * std::fabs(2 * fr - 1); };
            const float va = vv(A.s), vb = vv(B.s);
            const float2 uv[4] = {{u0, vb}, {u1, vb}, {u1, va}, {u0, va}};
            const float al[4] = {fb, fb, fa, fa};
            for (int k = 0; k < 4; ++k) {
                render::SpriteVertex v;
                v.pos = c[k]; v.pos.y += 0.002f;
                v.uv = uv[k]; v.color = trackColor(1, 1, 1, al[k]); v.team = 0;
                v.rpos = {v.pos.x, v.pos.y, v.pos.z, 0};
                verges.push_back(v);
            }
            const auto& il = t.forward.empty() ? t.backward : t.forward;
            const float perM = f(il[0].width) / 3.0f, gauge = 0.80f * perM, hw = 0.32f * 2.0f * perM * (3.0f / 3.5f);
            const float per = roadkit::kRoadPaintPeriod / 3.0f / cm_;
            // only the quads' mean alpha can fade here: split by the fade so the track thins out evenly
            const uint32_t col = trackColor(0.875f, 0.79f, 0.67f, kTrackAlpha * (fa + fb) * 0.5f);
            for (int wheel : {-1, 1}) trackQuad(A, B, float(wheel) * gauge, float(wheel) * gauge, hw, A.s / per, B.s / per, col);
        }
    }

    // A driveway: an apron of the roads' own asphalt (no lines, dust and grime along both edges) across the lowered
    // sidewalk and on into the yard, edged on both sides by low kerb stones; the road side stays open.
    void driveway(const RoadEdgeGeom& g, const RoadDrivewayGeom& d, const RoadSideDef& S, float cwSide) {
        const float sg = float(d.side), h = f(d.half), W = 2 * h;
        const float in = cwSide - 0.02f / cm_;
        const float walk = cwSide + f(S.shoulder) + f(S.sidewalk);
        const float out = walk + f(d.depth);
        const float lip = sidewalkHeight(S) * 0.12f + 0.006f;
        const int mat = painted_ ? painted_(RtsRoadView::kDrivewaySurface, int(std::lround(W * 1000))) : -1;
        const float um = 1.0f / (4.0f / cm_) * 0.25f;
        const float pv = cm_ / roadkit::kRoadPaintPeriod;
        const int n = 4;   // across (along the road): enough for a gentle bend
        auto P = [&](float s, float o, float hh) { return at(sample(g, s), sg * o, hh); };
        auto hAt = [&](float o) { return o <= walk ? lip : 0.006f; };
        auto uvOf = [&](float x, float o, float3 q) {
            if (mat < 0) return float2{q.x * um, q.z * um};
            return float2{(W + x) / (3 * W), (o - in) * pv};   // the texture spans the apron plus a lane's margin each side
        };
        const int m = mat >= 0 ? mat : RtsRoadView::kAsphalt;
        const float rows[3] = {in, walk, out};
        for (int k = 0; k < n; ++k) {
            const float x0 = W * float(k) / float(n), x1 = W * float(k + 1) / float(n);
            const float s0 = f(d.s) - h + x0, s1 = f(d.s) - h + x1;
            for (int r = 0; r < 2; ++r) {
                const float o0 = rows[r], o1 = rows[r + 1];
                const float3 a0 = P(s0, o0, hAt(o0)), a1 = P(s0, o1, hAt(o1 - 1e-4f)), b0 = P(s1, o0, hAt(o0)), b1 = P(s1, o1, hAt(o1 - 1e-4f));
                const float3 up{0, 1, 0};
                tri(m, a0, a1, b1, uvOf(x0, o0, a0), uvOf(x0, o1, a1), uvOf(x1, o1, b1), up);
                tri(m, a0, b1, b0, uvOf(x0, o0, a0), uvOf(x1, o1, b1), uvOf(x1, o0, b0), up);
            }
        }
        // kerb stones along both edges, from the road to the end in the yard
        const float kw = 0.15f / cm_;
        for (int e = -1; e <= 1; e += 2) {
            const float sIn = f(d.s) + float(e) * h, sOut = sIn + float(e) * kw;
            const float hs[3] = {lip + 0.05f, lip + 0.05f, 0.08f};
            for (int r = 0; r < 2; ++r) {
                const float o0 = rows[r] + (r == 0 ? 0.08f / cm_ : 0), o1 = rows[r + 1];
                const float t0 = hs[r], t1 = r == 0 ? hs[1] : hs[2];
                const float3 i0 = P(sIn, o0, t0), i1 = P(sIn, o1, t1), x0 = P(sOut, o0, t0), x1 = P(sOut, o1, t1);
                const float3 i0b = P(sIn, o0, hAt(o0)), i1b = P(sIn, o1, r == 0 ? lip : 0.006f);
                const float3 up{0, 1, 0};
                const float L0 = (o0 - in), L1 = (o1 - in);
                tri(RtsRoadView::kCurb, i0, i1, x1, curbUv(L0, 0), curbUv(L1, 0), curbUv(L1, 0.15f), up);
                tri(RtsRoadView::kCurb, i0, x1, x0, curbUv(L0, 0), curbUv(L1, 0.15f), curbUv(L0, 0.15f), up);
                const float3 face = normalize(i0 - x0);   // the face towards the apron
                tri(RtsRoadView::kCurb, i0b, i1b, i1, curbUv(L0, 0), curbUv(L1, 0), curbUv(L1, 0.1f), face);
                tri(RtsRoadView::kCurb, i0b, i1, i0, curbUv(L0, 0), curbUv(L1, 0.1f), curbUv(L0, 0.1f), face);
            }
        }
    }

    // a side without a curb and without a sidewalk: its verge blends into the ground
    static bool soft(const RoadSideDef& S) { return (S.curb == RoadCurb::None || S.curb == RoadCurb::Shoulder) && S.sidewalk.raw == 0; }
    // the verge strip of one side between two stations: from a little over the carriageway edge (ragged) to past the
    // shoulder (fading into the ground); v tiles every kVergePeriod metres, split at the tile seams
    // cw0 / cw1: the carriageway's half width at s0 / s1 (it changes where the road narrows)
    void verge(const RoadEdgeGeom& g, float s0, float s1, int side, float cw0, float cw1, float shoulder, RoadSurface surf, float alpha0 = 1, float alpha1 = 1) {
        auto cwAt = [&](float s) { return s1 > s0 ? cw0 + (cw1 - cw0) * (s - s0) / (s1 - s0) : cw0; };
        auto innerAt = [&](float s) { return cwAt(s) - 0.12f / cm_; };
        auto outerAt = [&](float s) { return cwAt(s) + std::max(shoulder * 0.6f, 0.3f / cm_) + 0.9f / cm_; };
        const float per = roadkit::kVergePeriod / cm_;   // cells per tile
        const int slot = roadkit::vergeSlot(surf);
        for (float a = s0; a < s1 - 1e-4f;) {
            const float tileEnd = (std::floor(a / per) + 1) * per;
            const float b = std::min(s1, tileEnd);
            const Pt A = sample(g, a), B = sample(g, b);
            const float va = a / per - std::floor(a / per), vb = std::min(1.0f, va + (b - a) / per);
            const float u0 = float(slot) / float(roadkit::kVergeSlots) + 0.002f, u1 = float(slot + 1) / float(roadkit::kVergeSlots) - 0.002f;
            const float sg = float(side);
            // corners: forward-left, forward-right, back-right, back-left as decalQuad wants; u 0 at the road side
            const float3 c[4] = {at(B, sg * innerAt(b), 0), at(B, sg * outerAt(b), 0), at(A, sg * outerAt(a), 0), at(A, sg * innerAt(a), 0)};
            for (int k = 0; k < 4; ++k) {
                render::SpriteVertex v;
                v.pos = c[k]; v.pos.y += 0.003f;
                const float u = (k == 0 || k == 3) ? u0 : u1, vv = (k < 2) ? vb : va;
                v.uv = {u, vv};
                const float sk = (k < 2) ? b : a, al = s1 > s0 ? alpha0 + (alpha1 - alpha0) * (sk - s0) / (s1 - s0) : alpha0;
                v.color = 0x00ffffffu | uint32_t(std::clamp(al, 0.0f, 1.0f) * 255.0f + 0.5f) << 24; v.team = 0;
                v.rpos = {v.pos.x, v.pos.y, v.pos.z, 0};
                verges.push_back(v);
            }
            a = b;
        }
    }

    // ------------------------------------------------------------ paint (decals)
    uint32_t paintColor(int mat) const { return mat == RtsRoadView::kYellow ? 0xff2aa8e6u : 0xffe8e8e8u; }   // r | g << 8 | b << 16 | a << 24
    void decalQuad(const float3 (&c)[4], float4 uv, uint32_t color) {   // corners: forward-left, forward-right, back-right, back-left
        const float2 tuv[4] = {{uv.x, uv.y}, {uv.z, uv.y}, {uv.z, uv.w}, {uv.x, uv.w}};
        for (int k = 0; k < 4; ++k) {
            render::SpriteVertex v;
            v.pos = c[k];
            v.pos.y += 0.004f;
            v.uv = tuv[k];
            v.color = color;
            v.team = 0;
            v.rpos = {v.pos.x, v.pos.y, v.pos.z, 0};
            decals.push_back(v);
        }
    }
    // a mark painted along the edge between s0 and s1 centred at `off` (cells, + right), `width` cells wide; `dir`
    // = the way the mark points (+1 toward s1); curved roads get it in short pieces
    void paint(const RoadEdgeGeom& g, float s0, float s1, float off, float width, int mark, uint32_t color, int dir = 1) {
        const float4 r = roadkit::markUv(mark);
        const float len = s1 - s0;
        if (len <= 1e-4f) return;
        const int pieces = std::max(1, int(std::ceil(len / 0.5f)));
        for (int i = 0; i < pieces; ++i) {
            const float a = s0 + len * float(i) / float(pieces), b = s0 + len * float(i + 1) / float(pieces);
            const Pt A = sample(g, a), B = sample(g, b);
            // texture v: 0 at the end the mark points to
            const float fa = (a - s0) / len, fb = (b - s0) / len;
            const float va = dir > 0 ? 1 - fa : fa, vb = dir > 0 ? 1 - fb : fb;
            const float h = width * 0.5f;
            // u = 0 on the left of the way the mark points
            const float lA = dir > 0 ? off - h : off + h, rA = dir > 0 ? off + h : off - h;
            const float3 c[4] = {at(B, lA, 0), at(B, rA, 0), at(A, rA, 0), at(A, lA, 0)};
            const float v0 = r.y + (r.w - r.y) * vb, v1 = r.y + (r.w - r.y) * va;
            decalQuad(c, {r.x, v0, r.z, v1}, color);
        }
    }
    // lane lines, the centre line, edge lines
    void line(const RoadEdgeGeom& g, float s0, float s1, float off, float width, float dash, float gap, int mat, const std::function<bool(float)>& skip) {
        const float period = dash + gap;
        for (float s = s0; s < s1 - 1e-3f;) {
            float e = s1;
            if (gap > 0) {
                const float ph = std::fmod(s * cm_, period);
                if (ph >= dash) { s += (period - ph) / cm_; continue; }
                e = std::min(s1, s + (dash - ph) / cm_);
            }
            // in pieces, so a stretch the line must not cross (a worn end, a bay, a tunnel) cuts it
            for (float a = s; a < e - 1e-4f;) {
                float b = std::min(e, a + 0.5f);
                if (skip((a + b) * 0.5f)) { a = b; continue; }
                while (b < e - 1e-4f && !skip(std::min(e, b + 0.5f) - 0.25f)) b = std::min(e, b + 0.5f);
                paint(g, a, b, off, width / cm_, roadkit::kMarkLine, paintColor(mat));
                a = b;
            }
            s = e;
        }
    }
    // paint that is not part of the carriageway texture: BUS along bus lanes, bus stop bays
    void markings(const RoadTypeDef& t, const RoadEdgeGeom& g, int fs, float s0, float s1, const std::function<bool(float)>& tunnel) {
        if (t.unpaved()) return;
        auto none = [&](float s) { return tunnel(s); };
        const float med = f(t.median) * 0.5f;
        for (int dir : {1, -1}) {
            const auto& lanes = t.lanesOf(dir);
            const float sgn = float(dir * fs);
            float off = med;
            for (size_t k = 0; k < lanes.size(); ++k) {
                off += f(lanes[k].width);
                if (lanes[k].use == RoadLaneUse::Bus)
                    for (float sb = s0 + 8.0f / cm_; sb + 3.0f / cm_ < s1; sb += 50.0f / cm_)
                        if (!none(sb)) paint(g, sb, sb + 3.0f / cm_, sgn * (off - f(lanes[k].width) * 0.5f), f(lanes[k].width) * 0.75f, roadkit::kMarkBus, paintColor(RtsRoadView::kWhite), dir * fs);
            }
            if (lanes.empty()) continue;
            const int sd = sgn > 0 ? 1 : -1;
            for (auto& b : g.bays) {   // a dashed line along the lane, BUS in the middle of the bay
                if (b.side != sd) continue;
                if (b.parking) {   // parking: the spaces marked across the bay (or the parking lane), a line along it
                    const float a = f(b.s1), z = f(b.s2), st = std::max(0.5f, f(b.stall));
                    const float in = b.inLane ? off - f(b.laneWidth) : off, out = b.inLane ? off : off + f(b.width);
                    const float w = 0.12f / cm_;
                    if (!b.inLane) line(g, a, z, sgn * (off - 0.1f / cm_), 0.12f, 1, 0, RtsRoadView::kWhite, none);
                    const int n = std::max(1, int(std::floor((z - a) / st + 0.01f)));
                    const float sp = (z - a) / float(n);
                    for (int q = 0; q <= n; ++q) {
                        const float sq = std::clamp(a + sp * float(q), a, z - w);
                        if (none(sq)) continue;
                        paint(g, sq, sq + w, sgn * (in + out) * 0.5f, out - in, roadkit::kMarkLine, paintColor(RtsRoadView::kWhite));
                    }
                    continue;
                }
                line(g, f(b.s0), f(b.s3), sgn * (off - 0.15f / cm_), 0.2f, 1, 1, RtsRoadView::kWhite, none);
                const float mid = (f(b.s1) + f(b.s2)) * 0.5f, len = std::min(3.0f / cm_, (f(b.s2) - f(b.s1)) * 0.8f);
                paint(g, mid - len * 0.5f, mid + len * 0.5f, sgn * (off + f(b.width) * 0.5f), f(b.width) * 0.7f, roadkit::kMarkBus, paintColor(RtsRoadView::kYellow), dir * fs);
            }
        }
    }

    void pillars(const RoadTypeDef& t, const RoadEdgeGeom& g, int fs, float s0, float s1) {
        const float cwl = f(t.carriageway(-fs)), cwr = f(t.carriageway(fs));
        for (auto& sp : g.spans) {
            if (sp.tunnel) continue;
            const float a = std::max(s0, f(sp.s0)), b = std::min(s1, f(sp.s1));
            const float step = 12.0f / cm_;
            const int n = std::max(1, int((b - a) / step));
            for (int k = 0; k <= n; ++k) {
                const float s = a + (b - a) * (float(k) + 0.5f) / float(n + 1);
                const Pt P = sample(g, s);
                if (P.e < 1.3f) continue;
                const float top = P.y - wy(0.9f), bottom = gnd(P.c.x, P.c.z) - wy(0.5f);
                if (top - bottom < 0.05f) continue;
                const float half = (top - bottom) * 0.5f;
                const float width = (cwl + cwr) * 0.5f;
                // cap beam under the deck and one or two columns
                box(RtsRoadView::kBridge, at(P, (cwr - cwl) * 0.5f, 0) + float3{0, -wy(0.9f) - wy(0.5f) - (P.y - at(P, (cwr - cwl) * 0.5f, 0).y), 0}, P.n * (width * 0.95f), {0, wy(0.5f), 0}, P.t * wy(0.7f));
                const int cols = width > 2.0f ? 2 : 1;
                for (int c = 0; c < cols; ++c) {
                    const float off = cols == 1 ? (cwr - cwl) * 0.5f : (c ? cwr : -cwl) * 0.55f;
                    float3 base = P.c + P.n * off;
                    base.y = bottom + half;
                    box(RtsRoadView::kBridge, base, P.n * wy(0.6f), {0, half, 0}, P.t * wy(0.6f));
                }
            }
        }
    }

    void portals(const RoadTypeDef& t, const RoadEdgeGeom& g, int fs, float s0, float s1) {
        const float cwl = f(t.carriageway(-fs)), cwr = f(t.carriageway(fs));
        for (auto& sp : g.spans) {
            if (!sp.tunnel) continue;
            for (float s : {f(sp.s0), f(sp.s1)}) {
                if (s <= s0 + 0.01f || s >= s1 - 0.01f) continue;
                const Pt P = sample(g, s);
                const float h = 5.0f;   // clearance, metres
                const float3 base = at(P, 0, 0);
                const float y0 = base.y;
                for (int side : {-1, 1}) {
                    float3 c = P.c + P.n * ((side > 0 ? cwr : -cwl) + float(side) * wy(0.5f));
                    c.y = y0 + wy(h) * 0.5f;
                    box(RtsRoadView::kConcrete, c, P.n * wy(0.5f), {0, wy(h) * 0.5f + wy(0.6f), 0}, P.t * wy(0.8f));
                }
                float3 top = P.c + P.n * ((cwr - cwl) * 0.5f);
                top.y = y0 + wy(h) + wy(0.6f);
                box(RtsRoadView::kConcrete, top, P.n * ((cwl + cwr) * 0.5f + wy(1.0f)), {0, wy(0.6f), 0}, P.t * wy(0.8f));
            }
        }
    }

    void propsOf(int ei, const RoadTypeDef& t, const RoadEdgeGeom& g, int fs, float s0, float s1, const std::function<bool(float)>& tunnel) {
        // ends at a node where a branch leaves: keep that stretch clear too (the branch may run off the neighbour)
        const RoadEdge& E = N_.edges[size_t(ei)];
        bool splitEnd[2] = {false, false};
        for (int end = 0; end < 2; ++end) {
            const int ni = N_.nodeIndex(end == 0 ? E.a : E.b);
            const int ji = ni >= 0 ? G_.nodeJunction[size_t(ni)] : -1;
            splitEnd[end] = ji >= 0 && G_.junctions[size_t(ji)].split;
        }
        const float endClear = 4.0f / cm_;
        const auto zebra = zebras(ei, t, g);
        for (auto& pd : t.props) {
            if (!pd.type.empty()) continue;   // unit types: placed by the simulation
            const float spacing = f(pd.spacing);
            for (int side : {-1, 1, 2}) {
                if (pd.side == 2 ? side != 2 : (side == 2 || (pd.side != 0 && pd.side != side))) continue;
                for (float s = s0 + f(pd.phase) + 0.25f; s < s1 - 0.25f; s += spacing) {
                    if (tunnel(s)) continue;
                    if ((splitEnd[0] && s < s0 + endClear) || (splitEnd[1] && s > s1 - endClear)) continue;
                    if (side != 2 && (inBay(g, side, s) || inBay(g, side, s - 1.5f / cm_) || inBay(g, side, s + 1.5f / cm_))) continue;
                    if (side != 2 && inDriveway(g, side, s, 2.0f / cm_)) continue;   // nothing stands in an entrance
                    if (side == 2 && (inGaps2(zebra, s) || inGaps2(zebra, s - 1.5f / cm_) || inGaps2(zebra, s + 1.5f / cm_))) continue;   // the crossing's way through the median
                    // keep clear of a gap (a branch leaves there): trees and lamps would stand in its way
                    const float clear = 2.5f / cm_;
                    if (side != 2 && (inGaps(g.gaps[side > 0 ? 1 : 0], s) || inGaps(g.gaps[side > 0 ? 1 : 0], s - clear) || inGaps(g.gaps[side > 0 ? 1 : 0], s + clear))) continue;
                    const Pt P = sample(g, s);
                    if (!pd.onBridges && P.e > f(kRoadBridgeAbove)) continue;
                    float off, h = 0;
                    if (side == 2) { off = f(pd.offset); h = t.look["median"].getBool("curb", false) ? 0.15f : 0.0f; }
                    else {
                        const RoadSideDef& S = (side * fs) > 0 ? t.right : t.left;
                        off = float(side) * (f(t.carriageway(side * fs)) + f(S.shoulder) + f(pd.offset));
                        if (S.sidewalk.raw > 0) h = sidewalkHeight(S);
                    }
                    const float3 pos = at(P, off, h);
                    // frame: x along the road, z away from it (the built-in shapes hang their arm over -z)
                    const float out = side == 2 ? 1.0f : float(side);
                    const float3 zx = P.n * out;
                    mat4 m;
                    m.at(0, 0) = P.t.x; m.at(1, 0) = 0; m.at(2, 0) = P.t.z;
                    m.at(0, 1) = 0; m.at(1, 1) = 1; m.at(2, 1) = 0;
                    m.at(0, 2) = zx.x; m.at(1, 2) = 0; m.at(2, 2) = zx.z;
                    m.at(0, 3) = pos.x; m.at(1, 3) = pos.y - kLift; m.at(2, 3) = pos.z;
                    const float k = f(pd.scale);
                    m = m * mat4::scale({k, k, k});
                    props.push_back({pd.model.empty() ? pd.shape : std::string(), pd.model, m});
                    // a tree on paving grows out of a pit of soil, not out of the concrete
                    const bool paved = side == 2 ? (t.medianSurface != RoadSurface::Grass && t.medianSurface != RoadSurface::Dirt)
                                                 : [&] { const RoadSideDef& S = (side * fs) > 0 ? t.right : t.left;
                                                         return S.sidewalk.raw > 0 && S.sidewalkSurface != RoadSurface::Grass && S.sidewalkSurface != RoadSurface::Dirt; }();
                    if (pd.shape == "tree" && pd.model.empty() && paved) {   // the pit sits on the paving itself (props stand kLift lower)
                        mat4 pm = m;
                        pm.at(1, 3) = pos.y + 0.002f;
                        props.push_back({"tree_pit", {}, pm});
                    }
                }
            }
        }
    }

    // a bus shelter on the sidewalk behind each bay, its open side toward the road
    void shelters(const RoadTypeDef& t, const RoadEdgeGeom& g, int fs) {
        for (auto& b : g.bays) {
            if (!b.shelter) continue;
            const RoadSideDef& S = (b.side * fs) > 0 ? t.right : t.left;
            if (S.sidewalk.raw <= 0) continue;
            const float mid = (f(b.s1) + f(b.s2)) * 0.5f;
            const Pt P = sample(g, mid);
            const float off = float(b.side) * (f(t.carriageway(b.side * fs)) + f(S.shoulder) + f(b.width) + 0.5f / cm_);
            const float3 pos = at(P, off, sidewalkHeight(S));
            const float3 zx = P.n * float(b.side);
            mat4 m;
            m.at(0, 0) = P.t.x; m.at(2, 0) = P.t.z; m.at(1, 1) = 1; m.at(0, 2) = zx.x; m.at(2, 2) = zx.z;
            m.at(0, 3) = pos.x; m.at(1, 3) = pos.y - kLift; m.at(2, 3) = pos.z;
            props.push_back({"shelter", {}, m});
        }
    }

    // ways out of a junction for traffic that comes in along arm `arm`: straight / left / right
    void exits(const RoadJunctionGeom& J, size_t arm, bool& S, bool& L, bool& R) const {
        S = L = R = false;
        auto leaving = [&](size_t k) {
            const RoadEdgeGeom& g = G_.edges[size_t(J.arms[k])];
            return J.armEnd[k] == 0 ? g.pts.front().t : Vec2{-g.pts.back().t.x, -g.pts.back().t.y};
        };
        const Vec2 in0 = leaving(arm);
        const float ix = -f(in0.x), iz = -f(in0.y);
        for (size_t k = 0; k < J.arms.size(); ++k) {
            if (k == arm) continue;
            const RoadEdge& E = N_.edges[size_t(J.arms[k])];
            const RoadTypeDef& t = T_[size_t(E.typeIndex)];
            // lanes that drive away from the node along that arm
            const int away = J.armEnd[k] == 0 ? 1 : -1;
            if (!t.drivable() || t.lanes(away * (E.reversed ? -1 : 1)) == 0) continue;
            const Vec2 o = leaving(k);
            const float ox = f(o.x), oz = f(o.y);
            const float ang = std::atan2(ix * oz - iz * ox, ix * ox + iz * oz);
            if (std::fabs(ang) < 0.6f) S = true; else if (ang > 0) R = true; else L = true;
        }
    }

    // paint and furniture at the arm mouths of junctions: zebra crossings, stop lines, lane arrows, signals; at
    // junctions without signals the minor roads get STOP painted and a stop sign
    void crossings(int ei, const RoadTypeDef& t, const RoadEdgeGeom& g, int fs) {
        if (t.forward.empty() && t.backward.empty()) return;
        if (g.pts.size() < 2) return;
        const RoadEdge& E = N_.edges[size_t(ei)];
        const uint32_t white = paintColor(RtsRoadView::kWhite);
        // nothing is painted on dirt or gravel, nor on a road drawn without lines (a quiet residential street);
        // signals and signs still stand
        const bool paints = !t.unpaved() && (t.laneLines || t.centre != RoadMarking::None);
        for (int end = 0; end < 2; ++end) {
            const int ni = N_.nodeIndex(end == 0 ? E.a : E.b);
            const int ji = ni >= 0 ? G_.nodeJunction[size_t(ni)] : -1;
            if (ji < 0) continue;
            const RoadJunctionGeom& J = G_.junctions[size_t(ji)];
            if (J.split) continue;
            size_t arm = 0;
            for (size_t k = 0; k < J.arms.size(); ++k) if (J.arms[k] == ei && J.armEnd[k] == end) arm = k;
            float widest = 0;
            for (int a : J.arms) { const RoadTypeDef& at = T_[size_t(N_.edges[size_t(a)].typeIndex)]; widest = std::max(widest, f(at.carriageway(1) + at.carriageway(-1))); }
            const bool minor = f(t.carriageway(1) + t.carriageway(-1)) < widest - 0.01f;
            const float trim = f(end == 0 ? g.trim0 : g.trim1);
            const float sgnS = end == 0 ? 1.0f : -1.0f;   // away from the junction along s
            auto sAt = [&](float d) { return end == 0 ? trim + d : f(g.length) - trim - d; };
            const float cwl = f(t.carriageway(-fs)), cwr = f(t.carriageway(fs));
            float d = 0.3f / cm_;
            if (J.signal && t.crossings && paints) {   // zebra: stripes along the road across the whole carriageway
                const float len = 3.0f / cm_;
                const float med = f(t.median) * 0.5f;
                for (float o = -cwl + 0.3f / cm_; o < cwr - 0.3f / cm_; o += 1.0f / cm_)
                    if (std::fabs(o + 0.25f / cm_) > med + 0.3f / cm_ || med <= 0) paint(g, std::min(sAt(d), sAt(d + len)), std::max(sAt(d), sAt(d + len)), o + 0.25f / cm_, 0.5f / cm_, roadkit::kMarkLine, white);
                d += len + 0.5f / cm_;
            }
            const int inDir = end == 0 ? -1 : 1;   // lanes that drive toward this end (a -> b sense): on that side of the edge
            const int tdir = inDir * fs;            // the same lanes in the type's own sense
            const int lanes = t.lanes(tdir);
            if (lanes == 0) continue;
            const float sg = float(inDir);
            const float o0 = f(t.median) * 0.5f, o1 = f(t.carriageway(tdir));
            const bool stopSign = !J.signal && minor && J.arms.size() >= 3;
            if ((J.signal || stopSign) && paints) {   // stop line across the incoming lanes
                const float w = (stopSign ? 0.5f : 0.4f) / cm_;
                const float sa = std::min(sAt(d), sAt(d + w)), sb = std::max(sAt(d), sAt(d + w));
                const Pt A = sample(g, sa), B = sample(g, sb);
                const float lo = std::min(sg * o0, sg * o1), hi = std::max(sg * o0, sg * o1);
                const float3 c[4] = {at(B, lo, 0), at(B, hi, 0), at(A, hi, 0), at(A, lo, 0)};
                const float4 r = roadkit::markUv(roadkit::kMarkLine);
                decalQuad(c, {r.x, r.y, r.z, r.w}, white);
            }
            if (J.signal && N_.signals) signal(t, sample(g, sAt(d)), inDir, tdir, end == 0 ? -1.0f : 1.0f);
            if (stopSign) {   // STOP painted before the line, a sign at the kerb
                const float len = 4.0f / cm_, gapS = 1.5f / cm_;
                if (paints) paint(g, std::min(sAt(d + gapS), sAt(d + gapS + len)), std::max(sAt(d + gapS), sAt(d + gapS + len)), sg * (o0 + o1) * 0.5f,
                      std::min(o1 - o0, 3.2f / cm_), roadkit::kMarkStop, white, inDir);
                const RoadSideDef& S = tdir > 0 ? t.right : t.left;
                const Pt P = sample(g, sAt(d));
                const float3 pos = at(P, sg * (o1 + f(S.shoulder) + 0.6f / cm_), sidewalkHeight(S));
                mat4 m;
                const float3 fx = P.t * (end == 0 ? -1.0f : 1.0f), fz = float3{-fx.z, 0, fx.x};
                m.at(0, 0) = fx.x; m.at(2, 0) = fx.z; m.at(1, 1) = 1; m.at(0, 2) = fz.x; m.at(2, 2) = fz.z;
                m.at(0, 3) = pos.x; m.at(1, 3) = pos.y - kLift; m.at(2, 3) = pos.z;
                props.push_back({"stop_sign", {}, m});
            }
            // lane arrows a few metres before the line
            if (J.arms.size() >= 3 && paints) {
                bool S = false, L = false, R = false;
                exits(J, arm, S, L, R);
                int n = lanes;
                if (t.lanesOf(tdir).back().use == RoadLaneUse::Parking) --n;
                const float a0 = d + (stopSign ? 7.0f : 3.0f) / cm_, a1 = a0 + 5.0f / cm_;
                if (a1 + trim < f(g.length) * 0.5f)
                    for (int k = 0; k < n; ++k) {
                        int mark = roadkit::kMarkArrowS;
                        const bool first = k == 0, last = k == n - 1;
                        if (n == 1) mark = S ? (L && !R ? roadkit::kMarkArrowSL : R && !L ? roadkit::kMarkArrowSR : roadkit::kMarkArrowS) : (L && R ? roadkit::kMarkArrowLR : L ? roadkit::kMarkArrowL : roadkit::kMarkArrowR);
                        else if (first && L) mark = S && n == 2 ? roadkit::kMarkArrowSL : roadkit::kMarkArrowL;
                        else if (last && R) mark = S && n == 2 ? roadkit::kMarkArrowSR : roadkit::kMarkArrowR;
                        else if (!S) mark = L ? roadkit::kMarkArrowL : roadkit::kMarkArrowR;
                        const float off = f(roadLaneOffset(t, tdir, k)) * (fs > 0 ? 1.0f : -1.0f);
                        const float lw = f(t.lanesOf(tdir)[size_t(k)].width);
                        paint(g, std::min(sAt(a0), sAt(a1)), std::max(sAt(a0), sAt(a1)), off, lw * 0.7f, mark, white, inDir);
                    }
            }
            (void)sgnS;
        }
    }

    // traffic signal for the lanes that drive into the junction: a pole at their kerb, a mast arm over them with a head
    // per lane, and a pedestrian head on the pole
    void signal(const RoadTypeDef& t, const Pt& P, int inDir, int tdir, float facing) {
        const float sg = float(inDir);
        const RoadSideDef& S = tdir > 0 ? t.right : t.left;
        const float kerb = f(t.carriageway(tdir)) + f(S.shoulder) + 0.5f / cm_;
        const float h = sidewalkHeight(S);
        const float3 base = at(P, sg * kerb, h);
        const float3 up{0, 1, 0}, across = P.n * sg, along = P.t * facing;   // across: away from the road centre
        const float pole = 6.0f;
        box(RtsRoadView::kMetal, base + up * wy(pole * 0.5f), across * wy(0.1f), up * wy(pole * 0.5f), along * wy(0.1f));
        // the arm reaches over the incoming lanes
        const float reach = kerb - f(t.median) * 0.5f;
        const float3 armMid = base + up * wy(pole - 0.3f) - across * (reach * 0.5f);
        box(RtsRoadView::kMetal, armMid, across * (reach * 0.5f), up * wy(0.08f), along * wy(0.08f));
        const auto& lanes = t.lanesOf(tdir);
        float off = f(t.median) * 0.5f;
        for (size_t k = 0; k < lanes.size(); ++k) {
            const float c = off + f(lanes[k].width) * 0.5f;
            off += f(lanes[k].width);
            if (lanes[k].use == RoadLaneUse::Parking) continue;
            const float3 head = base - across * (kerb - c) + up * wy(pole - 1.0f);
            head3(head, up, across, along);
        }
        head3(base + up * wy(2.6f) + along * wy(0.18f), up, across, along);   // on the pole, at eye height
    }
    void head3(float3 c, float3 up, float3 across, float3 along) {
        box(RtsRoadView::kDark, c, across * wy(0.18f), up * wy(0.5f), along * wy(0.14f));
        const int lamps[3] = {RtsRoadView::kSignalRed, RtsRoadView::kSignalAmber, RtsRoadView::kSignalGreen};
        for (int i = 0; i < 3; ++i)
            box(lamps[i], c + up * wy(0.32f - 0.32f * float(i)) + along * wy(0.15f), across * wy(0.11f), up * wy(0.11f), along * wy(0.02f));
    }

    // the side of a junction arm on its right (+1) or left (-1) as seen leaving the junction
    const RoadSideDef& armSide(const RoadJunctionGeom& J, size_t arm, int leaving) const {
        const RoadEdge& E = N_.edges[size_t(J.arms[arm])];
        const RoadTypeDef& t = T_[size_t(E.typeIndex)];
        const int er = J.armEnd[arm] == 0 ? 1 : -1, fs = E.reversed ? -1 : 1;
        return (leaving * er * fs) > 0 ? t.right : t.left;
    }
    // a verge decal round a junction corner between two soft sides: u from the carriageway edge outwards, v along
    template <class H>
    void cornerVerge(const std::vector<Vec2>& strip, RoadSurface surf, const H& h) {
        const int slot = roadkit::vergeSlot(surf);
        const float u0 = float(slot) / float(roadkit::kVergeSlots) + 0.002f, u1 = float(slot + 1) / float(roadkit::kVergeSlots) - 0.002f;
        const float per = roadkit::kVergePeriod / cm_;
        float along = 0;
        auto P = [&](Vec2 in, Vec2 out, bool outer) {
            float3 d{f(out.x - in.x), 0, f(out.y - in.y)};
            const float len = std::hypot(d.x, d.z);
            d = len > 1e-5f ? d * (1.0f / len) : float3{0, 0, 0};
            const float o = outer ? std::max(len * 0.6f, 0.3f / cm_) + 0.9f / cm_ : -0.12f / cm_;
            float3 q{f(in.x) + d.x * o, 0, f(in.y) + d.z * o};
            q.y = h(q.x, q.z, 0) + 0.003f;
            return q;
        };
        for (size_t q = 0; q + 3 < strip.size(); q += 2) {
            const Vec2 i0 = strip[q], o0 = strip[q + 1], i1 = strip[q + 2], o1 = strip[q + 3];
            const float seg = std::hypot(f(i1.x - i0.x), f(i1.y - i0.y));
            const float fl = std::floor(along / per);
            const float va = along / per - fl, vb = (along + seg) / per - fl;   // the atlas does not wrap: v from this tile
            along += seg;
            const float3 c[4] = {P(i1, o1, false), P(i1, o1, true), P(i0, o0, true), P(i0, o0, false)};
            const float2 uv[4] = {{u0, vb}, {u1, vb}, {u1, va}, {u0, va}};
            for (int k = 0; k < 4; ++k) {
                render::SpriteVertex v;
                v.pos = c[k]; v.uv = uv[k]; v.color = 0xffffffffu; v.team = 0;
                v.rpos = {v.pos.x, v.pos.y, v.pos.z, 0};
                verges.push_back(v);
            }
        }
    }

    void junction(const RoadJunctionGeom& J) {
        if (J.outline.size() < 3) return;
        const bool onGround = std::fabs(f(J.y) - (ground_ ? gnd(f(J.centre.x), f(J.centre.y)) * cm_ : 0.0f)) < 0.1f || !ground_;
        auto h = [&](float x, float z, float hm) { return (onGround ? gnd(x, z) + kLift : wy(f(J.y)) + kLift) + wy(hm); };
        // surface of the most important arm (the widest drivable one)
        int mat = RtsRoadView::kAsphalt;
        float widest = -1, curbAll = 0;
        for (int e : J.arms) {
            const RoadTypeDef& t = T_[size_t(N_.edges[size_t(e)].typeIndex)];
            const float w = f(t.carriageway(1) + t.carriageway(-1));
            if (t.drivable() && w > widest) { widest = w; mat = surfaceMat(t.surface); }
            curbAll = std::max({curbAll, sidewalkHeight(t.left), sidewalkHeight(t.right)});
        }
        float cx = 0, cz = 0;
        for (auto& p : J.outline) { cx += f(p.x); cz += f(p.y); }
        cx /= float(J.outline.size()); cz /= float(J.outline.size());
        const float um = 1.0f / (4.0f / cm_) * (mat == RtsRoadView::kAsphalt ? 0.25f : 1.0f);
        (void)um;
        const float3 c{cx, h(cx, cz, 0), cz};
        for (size_t i = 0; i < J.outline.size(); ++i) {
            const Vec2 a = J.outline[i], b = J.outline[(i + 1) % J.outline.size()];
            const float3 pa{f(a.x), h(f(a.x), f(a.y), 0), f(a.y)}, pb{f(b.x), h(f(b.x), f(b.y), 0), f(b.y)};
            tri(mat, c, pa, pb, {cx * um, cz * um}, {pa.x * um, pa.z * um}, {pb.x * um, pb.z * um}, {0, 1, 0});
        }
        // sidewalk corners: a raised strip and the curb face along the inner curve
        const float umw = 1.0f / (4.0f / cm_);
        for (size_t ci = 0; ci < J.corners.size(); ++ci) {
            const auto& strip = J.corners[ci];
            // the two sides this corner joins: soft ones (no kerb, no sidewalk) get a verge round the corner, not
            // paving; a kerb only as high as the higher of the two
            float curb = 0;
            bool softBoth = false;
            RoadSurface vs = RoadSurface::Gravel;
            if (ci < J.cornerArms.size()) {
                const auto [ia, ib] = J.cornerArms[ci];
                const RoadSideDef& SA = armSide(J, size_t(ia), 1), &SB = armSide(J, size_t(ib), -1);
                curb = std::max(sidewalkHeight(SA), sidewalkHeight(SB));
                softBoth = soft(SA) && soft(SB);
                const RoadTypeDef& ta = T_[size_t(N_.edges[size_t(J.arms[size_t(ia)])].typeIndex)];
                vs = SA.shoulder.raw > 0 ? SA.shoulderSurface : ta.surface == RoadSurface::Asphalt ? RoadSurface::Gravel : ta.surface;
            } else curb = curbAll;
            if (softBoth) { cornerVerge(strip, vs, h); continue; }
            float arc = 0;   // along the curb, so its texture runs on round the corner
            for (size_t q = 0; q + 3 < strip.size(); q += 2) {
                const Vec2 i0 = strip[q], o0 = strip[q + 1], i1 = strip[q + 2], o1 = strip[q + 3];
                auto P = [&](Vec2 v, float hm) { return float3{f(v.x), h(f(v.x), f(v.y), hm), f(v.y)}; };
                const float3 outw0 = normalize(float3{f(o0.x - i0.x), 0, f(o0.y - i0.y)}), outw1 = normalize(float3{f(o1.x - i1.x), 0, f(o1.y - i1.y)});
                const float top = curb > 0 ? curbTop() : 0.0f;
                const float3 a = P(i0, curb), d = P(i1, curb);
                const float3 at0 = a + outw0 * top, dt1 = d + outw1 * top;   // inner edge of the paving: behind the curb stone
                const float3 b = P(o0, curb), cc = P(o1, curb);
                auto w = [&](float3 v) { return float2{v.x * umw, v.z * umw}; };
                quad(RtsRoadView::kPaving, at0, b, cc, dt1, w(at0), w(b), w(cc), w(dt1), {0, 1, 0});
                const float seg = std::hypot(d.x - a.x, d.z - a.z);
                if (curb > 0) {
                    quad(RtsRoadView::kCurb, a, at0, dt1, d, w(a), w(at0), w(dt1), w(d), {0, 1, 0});   // the stone's top
                    const float3 a0 = P(i0, 0), d0 = P(i1, 0);
                    const float hh = wy(curb);
                    quad(RtsRoadView::kCurb, a0, a, d, d0, curbUv(arc, 0), curbUv(arc, hh), curbUv(arc + seg, hh), curbUv(arc + seg, 0), -outw0);
                }
                arc += seg;
            }
        }
        junctionTracks(J, h);   // paved or dirt: the box carries the tracks / ruts of the roads it joins
    }

    // wheel tracks across a junction: through every lane straight on, and from the outer lanes round the turns, so
    // the tracks painted along the roads run on through the box (darker where the paths cross)
    template <class H>
    void junctionTracks(const RoadJunctionGeom& J, const H& h) {
        struct Lane { float3 p, dir; float off; };   // p: the lane's centre where the arm meets the box; dir: driving
        struct Arm { std::vector<Lane> in, out; float3 leave; float perM; bool dirt = false; };
        std::vector<Arm> arms;
        for (size_t i = 0; i < J.arms.size(); ++i) {
            const int ei = J.arms[i], end = J.armEnd[i];
            const RoadEdge& E = N_.edges[size_t(ei)];
            const RoadEdgeGeom& g = G_.edges[size_t(ei)];
            const RoadTypeDef& t = T_[size_t(E.typeIndex)];
            Arm a;
            if (g.pts.size() < 2 || !t.drivable()) { arms.push_back(a); continue; }
            const Pt P = sample(g, end == 0 ? f(g.trim0) : f(g.length - g.trim1));
            const float sg = end == 0 ? 1.0f : -1.0f;
            a.leave = P.t * sg;
            const float3 rL = P.n * sg;   // right of the way out of the junction
            const int fs = E.reversed ? -1 : 1, outDir = (end == 0 ? 1 : -1) * fs;
            const auto& firstLanes = t.forward.empty() ? t.backward : t.forward;
            a.perM = f(firstLanes[0].width) / 3.5f;
            if (t.unpaved()) {   // a dirt or gravel road: its one pair of ruts down the middle, both ways
                a.dirt = true;
                a.perM = 0.80f * f(firstLanes[0].width) / 3.0f / 0.82f;   // the texture's rut gauge
                a.out.push_back(Lane{P.c, a.leave, 0});   // the ruts run on the centre line
                a.in.push_back(Lane{P.c, a.leave * -1.0f, 0});
                arms.push_back(a);
                continue;
            }
            for (int which = 0; which < 2; ++which) {   // 0 out, 1 in
                const int dir = which == 0 ? outDir : -outDir;
                float off = f(t.median) * 0.5f;
                for (auto& l : t.lanesOf(dir)) {
                    const float w = f(l.width), c = off + w * 0.5f;
                    off += w;
                    if (l.use == RoadLaneUse::Parking || l.use == RoadLaneUse::Bike) continue;
                    // outgoing lanes lie right of the way out, incoming ones right of the way in (left of out)
                    Lane L{P.c + rL * (which == 0 ? c : -c), which == 0 ? a.leave : a.leave * -1.0f, c};
                    (which == 0 ? a.out : a.in).push_back(L);
                }
            }
            arms.push_back(a);
        }
        const float w = 0.32f * 4.0f, gauge = 0.82f;   // the same width as along the roads   // metres: a track's decal width, half the wheel base
        const float u0 = float(roadkit::kVergeTrackSlot) / float(roadkit::kVergeSlots) + 0.002f, u1 = float(roadkit::kVergeTrackSlot + 1) / float(roadkit::kVergeSlots) - 0.002f;
        auto path = [&](float3 p0, float3 d0, float3 p3, float3 d3, float alpha, float perM0, float perM1, float dirt0, float dirt1) {
            const float len = std::hypot(p3.x - p0.x, p3.z - p0.z);
            if (len < 1e-3f) return;
            const float3 p1 = p0 + d0 * (len * 0.4f), p2 = p3 - d3 * (len * 0.4f);
            auto bz = [&](float u) { const float v = 1 - u; return p0 * (v * v * v) + p1 * (3 * v * v * u) + p2 * (3 * v * u * u) + p3 * (u * u * u); };
            const int n = std::max(2, int(std::ceil(len / 0.4f)));
            // asphalt grey, or the brown the paved roads' tracks turn as they run into a dirt road's ruts
            auto colAt = [&](float u) {
                const float k = dirt0 + (dirt1 - dirt0) * u;
                return trackColor(0.55f + (0.875f - 0.55f) * k, 0.55f + (0.79f - 0.55f) * k, 0.55f + (0.67f - 0.55f) * k, alpha * (1 + 0.35f * k));   // the ruts a little deeper
            };
            const float per = roadkit::kRoadPaintPeriod / 3.0f / cm_;   // the track texture repeats every 4 m
            for (int side : {-1, 1}) {
                float along = 0;
                float3 prev = bz(0);
                for (int k = 0; k < n; ++k) {
                    const float ua = float(k) / float(n), ub = float(k + 1) / float(n);
                    const float3 A = bz(ua), B = bz(ub);
                    auto nrm = [&](float u) {
                        const float e = 1e-3f;
                        const float3 d = bz(std::min(1.0f, u + e)) - bz(std::max(0.0f, u - e));
                        const float3 tt = normalize(float3{d.x, 0, d.z});
                        return float3{-tt.z, 0, tt.x};
                    };
                    const float3 nA = nrm(ua), nB = nrm(ub);
                    const float um = (ua + ub) * 0.5f, perM = perM0 + (perM1 - perM0) * um;
                    const uint32_t col = colAt(um);
                    const float ga = float(side) * gauge * perM, hw = w * 0.5f * perM;
                    const float seg = std::hypot(B.x - A.x, B.z - A.z);
                    const float va = along / per, vb = (along + seg) / per;
                    along += seg;
                    float3 c[4] = {B + nB * (ga - hw), B + nB * (ga + hw), A + nA * (ga + hw), A + nA * (ga - hw)};
                    const float2 uv[4] = {{u0, vb}, {u1, vb}, {u1, va}, {u0, va}};
                    for (int q = 0; q < 4; ++q) {
                        render::SpriteVertex v;
                        v.pos = c[q]; v.pos.y = h(c[q].x, c[q].z, 0) + 0.002f;
                        v.uv = uv[q]; v.color = col; v.team = 0;
                        v.rpos = {v.pos.x, v.pos.y, v.pos.z, 0};
                        verges.push_back(v);
                    }
                    prev = B;
                }
                (void)prev;
            }
        };
        for (size_t i = 0; i < arms.size(); ++i) {
            const Arm& A = arms[i];
            if (A.in.empty()) continue;
            for (size_t j = 0; j < arms.size(); ++j) {
                if (j == i || arms[j].out.empty()) continue;
                const Arm& B = arms[j];
                const float3 din = A.in[0].dir, dout = B.leave;
                const float straight = din.x * dout.x + din.z * dout.z, cross = din.x * dout.z - din.z * dout.x;
                // dirt to dirt: one pair of ruts serves both ways, so each pair of arms once
                if (A.dirt && B.dirt && j < i) continue;
                const float d0 = A.dirt ? 1.0f : 0.0f, d1 = B.dirt ? 1.0f : 0.0f;
                if (A.dirt || B.dirt) {   // into or out of a dirt road: every lane to its ruts down the middle
                    const float alpha = straight > 0.6f || arms.size() == 2 ? kTrackAlpha : kTrackAlpha * 0.8f;
                    if (straight <= -0.6f && arms.size() > 2) continue;
                    if (A.dirt && B.dirt) { path(A.in[0].p, A.in[0].dir, B.out[0].p, B.out[0].dir, alpha, A.perM, B.perM, 1, 1); continue; }
                    // a paved arm: straight on every lane merges into / spreads from the ruts; a turn only from the
                    // outer lane (right) or the inner one (left), as on paved junctions
                    const bool right = cross > 0, straightOn = straight > 0.6f || arms.size() == 2;
                    if (B.dirt) {
                        if (straightOn) for (auto& l : A.in) path(l.p, l.dir, B.out[0].p, B.out[0].dir, alpha, A.perM, B.perM, d0, d1);
                        else { const Lane& l = right ? A.in.back() : A.in.front(); path(l.p, l.dir, B.out[0].p, B.out[0].dir, alpha, A.perM, B.perM, d0, d1); }
                    } else {
                        if (straightOn) for (auto& l : B.out) path(A.in[0].p, A.in[0].dir, l.p, l.dir, alpha, A.perM, B.perM, d0, d1);
                        else { const Lane& l = right ? B.out.back() : B.out.front(); path(A.in[0].p, A.in[0].dir, l.p, l.dir, alpha, A.perM, B.perM, d0, d1); }
                    }
                    continue;
                }
                if (straight > 0.6f || arms.size() == 2) {   // straight on (or round a bend): lane k to lane k
                    for (size_t k = 0; k < A.in.size(); ++k) {
                        const Lane& o = B.out[std::min(k, B.out.size() - 1)];
                        path(A.in[k].p, A.in[k].dir, o.p, o.dir, kTrackAlpha, A.perM, A.perM, 0, 0);
                    }
                } else if (straight > -0.6f) {
                    // a right turn (right of a heading t is (-t.z, t.x): cross > 0) from the outer lane into the
                    // outer lane, a left turn from the inner lane into the inner lane
                    const bool right = cross > 0;
                    const Lane& a = right ? A.in.back() : A.in.front();
                    const Lane& b = right ? B.out.back() : B.out.front();
                    path(a.p, a.dir, b.p, b.dir, kTrackAlpha * 0.7f, A.perM, A.perM, 0, 0);
                }
            }
        }
    }
};

} // namespace

void RtsRoadView::init(gfx::Device& dev, const Json& roadTypes, FileReader reader) {
    shutdown(dev);
    dev_ = &dev;
    read_ = std::move(reader);
    warnings_.clear();
    dirty_ = true;
    try { types_ = loadRoadTypes(roadTypes); }
    catch (const std::exception& e) { warnings_.push_back(e.what()); types_ = loadRoadTypes(Json()); }
    cellMeters_ = float(roadCellMeters(roadTypes).toDouble());
    // the kit: textures uploaded once, chunk models borrow them
    Model kit;
    materials_.clear();
    std::vector<int> tex(kMatCount, -1);
    for (int m = 0; m < kMatCount; ++m) {
        Image img = roadkit::texture(m);
        if (!img.rgba.empty()) tex[size_t(m)] = addImage(dev, kit, std::move(img));
        Material mt = roadkit::material(m);
        mt.baseTex = tex[size_t(m)];
        materials_.push_back(mt);
    }
    images_ = std::move(kit.images);
    for (auto& img : images_) img.borrowedTexture = false;   // owned here, released in shutdown
    // road paint: decals (lines, zebras, arrows, text) from one generated atlas
    if (!decalBatch_.init(dev)) warnings_.push_back("road paint: decal pipeline: " + dev.lastError());
    paintTex_.init(dev);
    // the paint atlas: the game's roads/markings.png, else the engine's (tools/roads/make_markings.py), else drawn here
    std::string probe;
    for (const char* path : {"roads/markings.png", "engine/roads/markings.png"})
        if (platform::readContentFile(path, probe)) { paint_ = paintTex_.resolve(path, false, "", true); if (paint_.valid()) break; }
    if (!paint_.valid()) paint_ = paintTex_.addGenerated("builtin:road-paint", 1024, 1024, roadkit::markingAtlas(256), false);
    verge_ = paintTex_.addGenerated("builtin:road-verges", 256 * roadkit::kVergeSlots, 256, roadkit::vergeAtlas(256), false);
    if (hasNet_) {   // new types: the network's type indices change
        try { net_ = parseRoadNetwork(netJson_, types_); }
        catch (const std::exception& e) { warnings_.push_back(e.what()); net_ = RoadNetwork{}; }
    }
}

void RtsRoadView::release(gfx::Device& dev) {
    if (decalMesh_.vb.valid()) decalBatch_.destroyMesh(dev, decalMesh_);
    if (vergeMesh_.vb.valid()) decalBatch_.destroyMesh(dev, vergeMesh_);
    decalMesh_ = {}; vergeMesh_ = {};
    for (auto& c : chunks_) if (c.model) releaseModel(dev, *c.model);
    chunks_.clear();
    props_.clear();
    if (ghostModel_) { releaseModel(dev, *ghostModel_); ghostModel_.reset(); }
}

void RtsRoadView::shutdown(gfx::Device& dev) {
    release(dev);
    for (auto& p : propModels_) if (p.model) releaseModel(dev, *p.model);
    propModels_.clear();
    for (auto& img : images_) if (img.tex.valid()) { dev.destroyTexture(img.tex); img.tex = {}; }
    images_.clear();
    painted_.clear();
    if (dev_) { decalBatch_.shutdown(); paintTex_.shutdown(); }
    paint_ = {}; verge_ = {};
    dev_ = nullptr;
}

void RtsRoadView::setMap(const Json& map) {
    hasNet_ = map["roadNetwork"].isObject();
    net_ = RoadNetwork{};
    dirty_ = true;
    if (!hasNet_) return;
    netJson_ = map["roadNetwork"];
    replaces_.clear();
    for (auto& v : netJson_["replaces"].items()) if (v.isString()) replaces_.push_back(v.str());
    try { net_ = parseRoadNetwork(netJson_, types_); }
    catch (const std::exception& e) { warnings_.push_back(e.what()); net_ = RoadNetwork{}; }
}

int RtsRoadView::paintedMaterial(gfx::Device& dev, int type, int variant) {
    // type kDrivewaySurface: a driveway's apron, `variant` its width in thousandths of a cell — the same asphalt as
    // the roads (grain, tint), without lines or tracks, dust and grime along both its edges
    RoadTypeDef drive;
    if (type == kDrivewaySurface) {
        drive.id = "driveway";
        drive.surface = RoadSurface::Asphalt;
        drive.centre = RoadMarking::None;
        drive.laneLines = false;
        RoadLaneDef l;
        l.width = Fixed::fromRatio(std::max(variant, 1), 1000);
        drive.forward.push_back(l);
    } else if (type < 0 || size_t(type) >= types_.size() || !roadkit::roadSurfacePainted(types_[size_t(type)])) return -1;
    const RoadTypeDef& tdef = type == kDrivewaySurface ? drive : types_[size_t(type)];
    const auto key = std::pair{type, variant};
    if (auto it = painted_.find(key); it != painted_.end()) return it->second;
    Model holder;
    const int img = addImage(dev, holder, roadkit::roadSurface(tdef, type == kDrivewaySurface ? (4 | 8) : variant));
    int mat = -1;
    if (img >= 0) {
        images_.push_back(std::move(holder.images[size_t(img)]));
        images_.back().borrowedTexture = false;
        Material m = roadkit::material(kAsphalt);
        m.vertexColors = true;   // the large-scale tint (largeScaleTint)
        m.name = "road_paint_" + tdef.id;
        m.baseTex = int(images_.size() - 1);
        materials_.push_back(m);
        mat = int(materials_.size() - 1);
    }
    painted_[key] = mat;
    return mat;
}

int RtsRoadView::propModel(gfx::Device& dev, const std::string& path) {
    for (size_t i = 0; i < propModels_.size(); ++i) if (propModels_[i].path == path) return propModels_[i].model ? int(i) : -1;
    PropModel pm;
    pm.path = path;
    try {
        pm.model = loadModel(path, read_);
        if (pm.model) uploadModel(dev, *pm.model);
    } catch (const std::exception& e) { warnings_.push_back("road prop " + path + ": " + e.what()); pm.model.reset(); }
    propModels_.push_back(std::move(pm));
    return propModels_.back().model ? int(propModels_.size() - 1) : -1;
}

namespace {

std::unique_ptr<Model> toModel(const std::string& name, Acc& a, const std::vector<Image>& images, const std::vector<Material>& materials) {
    auto m = std::make_unique<Model>();
    m->path = name;
    m->materials = materials;
    for (auto& img : images) { Image b; b.name = img.name; b.tex = img.tex; b.borrowedTexture = true; b.repeat = true; b.width = img.width; b.height = img.height; m->images.push_back(b); }
    Node root; root.name = "roads"; root.mesh = 0;
    MeshDef md; md.name = name;
    for (int k = 0; k < int(a.i.size()); ++k) {
        if (a.i[size_t(k)].empty()) continue;
        Primitive p;
        p.firstIndex = uint32_t(m->indices.size());
        p.indexCount = uint32_t(a.i[size_t(k)].size());
        p.firstVertex = uint32_t(m->vertices.size());
        p.vertexCount = uint32_t(a.v[size_t(k)].size());
        p.material = k;
        const uint32_t base = uint32_t(m->vertices.size());
        float3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (auto& v : a.v[size_t(k)]) {
            lo = {std::min(lo.x, v.pos.x), std::min(lo.y, v.pos.y), std::min(lo.z, v.pos.z)};
            hi = {std::max(hi.x, v.pos.x), std::max(hi.y, v.pos.y), std::max(hi.z, v.pos.z)};
            m->vertices.push_back(v);
        }
        for (uint32_t i : a.i[size_t(k)]) m->indices.push_back(base + i);
        // smooth shading over curved decks and draped roads: average the normals of upward faces that share a corner
        {
            std::map<std::tuple<int64_t, int64_t, int64_t>, float3> acc;
            auto key = [](float3 q) { return std::tuple<int64_t, int64_t, int64_t>{std::llround(q.x * 512.0f), std::llround(q.y * 512.0f), std::llround(q.z * 512.0f)}; };
            for (uint32_t v = base; v < uint32_t(m->vertices.size()); ++v)
                if (m->vertices[v].nrm.y > 0.6f) acc[key(m->vertices[v].pos)] += m->vertices[v].nrm;
            for (uint32_t v = base; v < uint32_t(m->vertices.size()); ++v)
                if (m->vertices[v].nrm.y > 0.6f) m->vertices[v].nrm = normalize(acc[key(m->vertices[v].pos)]);
        }
        generateTangents(m->vertices, m->indices, p.firstIndex, p.indexCount);
        p.boundsMin = lo; p.boundsMax = hi;
        md.primitives.push_back(p);
    }
    if (md.primitives.empty()) return nullptr;
    m->meshes.push_back(md);
    m->nodes.push_back(root);
    m->finalize();
    return m;
}

void addShapes(Acc& a, const std::string& shape, const mat4& xf, float worldPerMetre, std::map<std::string, std::vector<roadkit::Tri>>& cache) {
    auto it = cache.find(shape);
    if (it == cache.end()) {
        std::vector<roadkit::Tri> tris;
        roadkit::shape(shape, worldPerMetre, tris);
        it = cache.emplace(shape, std::move(tris)).first;
    }
    for (auto& t : it->second) {
        float3 p[3] = {xf.transformPoint(t.a), xf.transformPoint(t.b), xf.transformPoint(t.c)};
        float3 n = normalize(cross(p[1] - p[0], p[2] - p[0]));
        a.need(t.mat);
        auto& V = a.v[size_t(t.mat)];
        const uint32_t base = uint32_t(V.size());
        for (auto& q : p) { ModelVertex mv; mv.pos = q; mv.nrm = n; mv.uv = {q.x + q.z, q.y}; V.push_back(mv); }
        a.i[size_t(t.mat)].push_back(base); a.i[size_t(t.mat)].push_back(base + 1); a.i[size_t(t.mat)].push_back(base + 2);
    }
}

} // namespace

void RtsRoadView::rebuild(gfx::Device& dev, const std::function<float(float, float)>& heightAt) {
    release(dev);
    dirty_ = false;
    ghostDirty_ = true;
    if (net_.empty()) return;
    const float cm = cellMeters_;
    const RoadGround ground = [&](Vec2 p) { return Fixed::fromRaw(int64_t(std::llround(double(heightAt(f(p.x), f(p.y)) * cm) * 65536.0))); };
    geom_ = buildRoadGeometry(net_, types_, heightAt ? ground : RoadGround{});
    Mesher mesh(types_, net_, geom_, cm, heightAt, false, [this, &dev](int type, int variant) { return paintedMaterial(dev, type, variant); });
    mesh.build();
    std::map<std::string, std::vector<roadkit::Tri>> shapes;
    for (auto& p : mesh.props) {
        if (!p.model.empty()) { const int k = propModel(dev, p.model); if (k >= 0) props_.push_back({k, p.xf}); continue; }
        const float3 at = p.xf.transformPoint({0, 0, 0});
        const int64_t cx = int64_t(std::floor(at.x / float(kChunk))), cz = int64_t(std::floor(at.z / float(kChunk)));
        addShapes(mesh.chunks[(cx << 32) ^ (cz & 0xffffffff)], p.shape, p.xf, 1.0f / cm, shapes);
    }
    if (!mesh.decals.empty()) decalMesh_ = decalBatch_.buildMesh(dev, mesh.decals);
    if (!mesh.verges.empty()) vergeMesh_ = decalBatch_.buildMesh(dev, mesh.verges);
    for (auto& [key, a] : mesh.chunks) {
        if (a.empty()) continue;
        auto m = toModel("roads", a, images_, materials_);
        if (!m) continue;
        uploadModel(dev, *m);
        chunks_.push_back({std::move(m)});
    }
}

void RtsRoadView::buildGhost(gfx::Device& dev, const std::function<float(float, float)>& heightAt) {
    ghostDirty_ = false;
    if (ghostModel_) { releaseModel(dev, *ghostModel_); ghostModel_.reset(); }
    if (!ghost_.on || ghost_.net.empty()) return;
    try {
        const float cm = cellMeters_;
        const RoadGround ground = [&](Vec2 p) { return Fixed::fromRaw(int64_t(std::llround(double(heightAt(f(p.x), f(p.y)) * cm) * 65536.0))); };
        RoadGeometry g = buildRoadGeometry(ghost_.net, types_, heightAt ? ground : RoadGround{});
        Mesher mesh(types_, ghost_.net, g, cm, heightAt, true, [this, &dev](int type, int variant) { return paintedMaterial(dev, type, variant); });
        mesh.build();
        std::map<std::string, std::vector<roadkit::Tri>> shapes;
        for (auto& p : mesh.props) if (p.model.empty()) addShapes(mesh.chunks[0], p.shape, p.xf, 1.0f / cm, shapes);
        if (mesh.chunks.empty()) return;
        ghostModel_ = toModel("road-ghost", mesh.chunks[0], images_, materials_);
        if (ghostModel_) uploadModel(dev, *ghostModel_);
    } catch (const std::exception&) { ghostModel_.reset(); }
}

void RtsRoadView::draw(Renderer3D& r, const std::function<float(float, float)>& heightAt) {
    if (!dev_) return;
    if (dirty_) rebuild(*dev_, heightAt);
    if (ghostDirty_) buildGhost(*dev_, heightAt);
    for (auto& c : chunks_) {
        ModelDraw d;
        d.model = c.model.get();
        d.flags = kDrawStatic;
        r.drawModel(d);
    }
    if ((decalMesh_.vb.valid() && paint_.valid()) || (vergeMesh_.vb.valid() && verge_.valid())) {
        r.drawCustom([this, &r](gfx::Device& d) {
            SpriteLighting lighting;
            lighting.bind = [&r] { r.bindLitFrame(); };
            lighting.receiveShadows = r.shadowQuality() >= ShadowQuality::Medium;
            if (vergeMesh_.vb.valid() && verge_.valid()) decalBatch_.drawMesh(d, vergeMesh_, verge_.tex, r.viewProj(), &lighting, true);   // under the paint
            if (decalMesh_.vb.valid() && paint_.valid()) decalBatch_.drawMesh(d, decalMesh_, paint_.tex, r.viewProj(), &lighting, true);
        });
    }
    for (auto& p : props_) {
        ModelDraw d;
        d.model = propModels_[size_t(p.model)].model.get();
        d.world = p.xf;
        d.flags = kDrawStatic;
        r.drawModel(d);
    }
    if (ghostModel_) {
        ModelDraw d;
        d.model = ghostModel_.get();
        d.tint = ghost_.ok ? float4{1, 1, 1, 0.75f} : float4{1.0f, 0.35f, 0.3f, 0.7f};
        d.flags = kDrawNoCastShadow | kDrawFadeDepth;
        r.drawModel(d);
    }
}

} // namespace aiverse::game
