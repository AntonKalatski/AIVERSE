#include "RtsRoadView.h"
#include "RtsRoadKit.h"
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
    std::vector<ModelVertex> v[RtsRoadView::kMatCount];
    std::vector<uint32_t> i[RtsRoadView::kMatCount];
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
bool inBay(const RoadEdgeGeom& g, int side, float s) {
    for (auto& b : g.bays) if (b.side == side && s >= float(b.s0.toDouble()) && s <= float(b.s3.toDouble())) return true;
    return false;
}
bool inGaps(const std::vector<std::pair<Fixed, Fixed>>& g, float s) {
    for (auto& [a, b] : g) if (s >= f(a) && s <= f(b)) return true;
    return false;
}

// Builds the triangles of a network into chunks.
class Mesher {
public:
    Mesher(const std::vector<RoadTypeDef>& types, const RoadNetwork& n, const RoadGeometry& g, float cm,
           const std::function<float(float, float)>& ground, bool singleChunk)
        : T_(types), N_(n), G_(g), cm_(cm), ground_(ground), single_(singleChunk) {}

    std::map<int64_t, Acc> chunks;
    struct Prop { std::string shape, model; mat4 xf; };
    std::vector<Prop> props;
    std::vector<render::SpriteVertex> decals;   // road paint (4 vertices per quad)

    void build() {
        for (size_t i = 0; i < N_.edges.size(); ++i) edge(int(i));
        for (auto& J : G_.junctions) if (!J.split) junction(J);
    }

private:
    const std::vector<RoadTypeDef>& T_;
    const RoadNetwork& N_;
    const RoadGeometry& G_;
    float cm_;
    const std::function<float(float, float)>& ground_;
    bool single_;

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
        auto& V = A.v[mat];
        auto& I = A.i[mat];
        const uint32_t base = uint32_t(V.size());
        for (auto [p, uv] : {std::pair{a, ua}, std::pair{b, ub}, std::pair{c, uc}}) {
            ModelVertex mv;
            mv.pos = p; mv.nrm = nrm; mv.uv = uv;
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
            const int road = wearAt(sm) > 0.08f ? RtsRoadView::kAsphaltWorn : roadMat;
            if (!gapL && L.sidewalk.raw > 0) { pr.push_back({-(cwl + f(L.shoulder) + swL), hsL, -1, 2}); pr.push_back({-(cwl + f(L.shoulder)), hsL, surfaceMat(L.sidewalkSurface), 1}); }
            if (!gapL && hsL > 0 && !pr.empty()) pr.push_back({-(cwl + f(L.shoulder)), 0, RtsRoadView::kCurb, 1});
            if (!gapL && L.shoulder.raw > 0) { if (pr.empty()) pr.push_back({-(cwl + f(L.shoulder)), 0, -1, 1}); pr.push_back({-cwl, 0, surfaceMat(L.shoulderSurface), 1}); }
            if (pr.empty()) pr.push_back({-cwl, 0, -1, 1});
            pr.push_back({-cwl, 0, road, 0});   // the bay's own surface (no width without a bay)
            if (t.median.raw > 0) {
                const float m = f(t.median) / 2;
                // a crossing cuts through the median at road level, paved (no grass, no curb under the zebra)
                const bool cut = inGaps2(zebra, sm);
                const float h = cut ? 0.0f : hm;
                pr.push_back({-m, 0, road, 0});
                if (h > 0) pr.push_back({-m, h, RtsRoadView::kCurb, 0});
                pr.push_back({m, h, cut ? RtsRoadView::kConcrete : surfaceMat(t.medianSurface), 0});
                if (h > 0) pr.push_back({m, 0, RtsRoadView::kCurb, 0});
            }
            pr.push_back({cwr, 0, road, 0});
            pr.push_back({cwr, 0, road, 1});
            if (!gapR && R.shoulder.raw > 0) pr.push_back({cwr + f(R.shoulder), 0, surfaceMat(R.shoulderSurface), 1});
            if (!gapR && R.sidewalk.raw > 0) {
                const float o = cwr + f(R.shoulder);
                if (hsR > 0) pr.push_back({o, hsR, RtsRoadView::kCurb, 1});
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
            auto offAt = [&](const Prof& p, const Pt& P) { return offBay(p, P) * narrowAt(P.s); };
            for (size_t q = 1; q < pr.size(); ++q) {
                const Prof& p0 = pr[q - 1];
                const Prof& p1 = pr[q];
                if (p1.mat < 0) continue;
                const float oa0 = offAt(p0, A), oa1 = offAt(p1, A), ob0 = offAt(p0, B), ob1 = offAt(p1, B);
                const bool face = std::fabs(p1.off - p0.off) < 1e-4f && p0.role == p1.role;
                if (!face && std::fabs(oa1 - oa0) < 1e-4f && std::fabs(ob1 - ob0) < 1e-4f) continue;   // no width here
                if (face && std::fabs(p1.h - p0.h) < 1e-4f) continue;
                const float3 a0 = at(A, oa0, p0.h), a1 = at(A, oa1, p1.h), b0 = at(B, ob0, p0.h), b1 = at(B, ob1, p1.h);
                float3 hint{0, 1, 0};
                if (face) hint = A.n * (p1.h < p0.h ? 1.0f : -1.0f);   // curb faces look at the lower side
                const float va = sa * um, vb = sb * um;
                const float u0 = face ? 0.0f : oa0 * um, u1 = face ? wy(std::fabs(p1.h - p0.h)) * um : oa1 * um;
                quad(p1.mat, a0, a1, b1, b0, {u0, va}, {u1, va}, {u1, vb}, {u0, vb}, hint);
            }
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
            quad(mat, a0, a1, b1, b0, {sa * um, A.s * um}, {sb * um, A.s * um}, {sb * um, B.s * um}, {sa * um, B.s * um}, {0, 1, 0});
            if (h > 0)   // curb faces at both ends of the piece (toward the road / the branch)
                for (int end = 0; end < 2; ++end) {
                    const float o = end ? sb : sa;
                    if (end == 1 && e >= n) continue;   // the outer edge of the sidewalk: no face
                    const float3 top0 = at(A, o, h), top1 = at(B, o, h), bot0 = at(A, o, 0), bot1 = at(B, o, 0);
                    quad(RtsRoadView::kCurb, bot0, top0, top1, bot1, {0, 0}, {0, 1}, {1, 1}, {1, 0}, A.n * (float(side) * (end ? 1.0f : -1.0f)));
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
    void markings(const RoadTypeDef& t, const RoadEdgeGeom& g, int fs, float s0, float s1, const std::function<bool(float)>& tunnel) {
        if (t.surface == RoadSurface::Gravel || t.surface == RoadSurface::Dirt || t.surface == RoadSurface::Grass) return;
        auto none = [&](float s) { return tunnel(s); };
        const bool both = !t.forward.empty() && !t.backward.empty();
        const float med = f(t.median) * 0.5f;
        if (both && t.median.raw == 0) {
            switch (t.centre) {
            case RoadMarking::Dashed: line(g, s0, s1, 0, 0.15f, 3, 6, RtsRoadView::kWhite, none); break;
            case RoadMarking::Solid: line(g, s0, s1, 0, 0.15f, 1, 0, RtsRoadView::kWhite, none); break;
            case RoadMarking::Double: line(g, s0, s1, -0.15f / cm_, 0.12f, 1, 0, RtsRoadView::kWhite, none); line(g, s0, s1, 0.15f / cm_, 0.12f, 1, 0, RtsRoadView::kWhite, none); break;
            case RoadMarking::Yellow: line(g, s0, s1, -0.12f / cm_, 0.12f, 1, 0, RtsRoadView::kYellow, none); line(g, s0, s1, 0.12f / cm_, 0.12f, 1, 0, RtsRoadView::kYellow, none); break;
            default: break;
            }
        }
        if (!t.laneLines) return;
        for (int dir : {1, -1}) {
            const auto& lanes = t.lanesOf(dir);
            const float sgn = float(dir * fs);
            float off = med;
            for (size_t k = 0; k < lanes.size(); ++k) {
                if (k == 0 && (!both || t.median.raw > 0)) line(g, s0, s1, sgn * (off + 0.1f / cm_), 0.12f, 1, 0, both ? RtsRoadView::kYellow : RtsRoadView::kWhite, none);
                off += f(lanes[k].width);
                if (lanes[k].use == RoadLaneUse::Bus)   // BUS painted along bus lanes
                    for (float sb = s0 + 8.0f / cm_; sb + 3.0f / cm_ < s1; sb += 50.0f / cm_)
                        if (!none(sb)) paint(g, sb, sb + 3.0f / cm_, sgn * (off - f(lanes[k].width) * 0.5f), f(lanes[k].width) * 0.75f, roadkit::kMarkBus, paintColor(RtsRoadView::kWhite), dir * fs);
                const bool last = k + 1 == lanes.size();
                const bool parkingNext = !last && lanes[k + 1].use == RoadLaneUse::Parking;
                const bool bus = lanes[k].use == RoadLaneUse::Bus || (!last && lanes[k + 1].use == RoadLaneUse::Bus);
                if (!last) line(g, s0, s1, sgn * off, bus ? 0.25f : 0.12f, parkingNext ? 1.0f : bus ? 1.0f : 3.0f, parkingNext ? 1.0f : bus ? 0.0f : 9.0f, RtsRoadView::kWhite, none);
                else if (lanes[k].use != RoadLaneUse::Parking) {   // edge line, open where a branch leaves / joins
                    const auto& gaps = g.gaps[sgn > 0 ? 1 : 0];
                    const int sd = sgn > 0 ? 1 : -1;
                    line(g, s0, s1, sgn * (off - 0.15f / cm_), 0.12f, 1, 0, RtsRoadView::kWhite, [&](float s) { return none(s) || inGaps(gaps, s) || inBay(g, sd, s); });
                    // bus stop bays: a dashed line along the lane, BUS in the middle of the bay
                    for (auto& b : g.bays) {
                        if (b.side != sd) continue;
                        line(g, f(b.s0), f(b.s3), sgn * (off - 0.15f / cm_), 0.2f, 1, 1, RtsRoadView::kWhite, none);
                        const float mid = (f(b.s1) + f(b.s2)) * 0.5f, len = std::min(3.0f / cm_, (f(b.s2) - f(b.s1)) * 0.8f);
                        paint(g, mid - len * 0.5f, mid + len * 0.5f, sgn * (off + f(b.width) * 0.5f), f(b.width) * 0.7f, roadkit::kMarkBus, paintColor(RtsRoadView::kYellow), dir * fs);
                    }
                }
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
                    if (pd.shape == "tree" && pd.model.empty() && paved) props.push_back({"tree_pit", {}, m});
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
            if (J.signal && t.crossings) {   // zebra: stripes along the road across the whole carriageway
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
            const bool stopSign = !J.signal && minor;
            if (J.signal || stopSign) {   // stop line across the incoming lanes
                const float w = (stopSign ? 0.5f : 0.4f) / cm_;
                const float sa = std::min(sAt(d), sAt(d + w)), sb = std::max(sAt(d), sAt(d + w));
                const Pt A = sample(g, sa), B = sample(g, sb);
                const float lo = std::min(sg * o0, sg * o1), hi = std::max(sg * o0, sg * o1);
                const float3 c[4] = {at(B, lo, 0), at(B, hi, 0), at(A, hi, 0), at(A, lo, 0)};
                const float4 r = roadkit::markUv(roadkit::kMarkLine);
                decalQuad(c, {r.x, r.y, r.z, r.w}, white);
            }
            if (J.signal) signal(t, sample(g, sAt(d)), inDir, tdir, end == 0 ? -1.0f : 1.0f);
            if (stopSign) {   // STOP painted before the line, a sign at the kerb
                const float len = 4.0f / cm_, gapS = 1.5f / cm_;
                paint(g, std::min(sAt(d + gapS), sAt(d + gapS + len)), std::max(sAt(d + gapS), sAt(d + gapS + len)), sg * (o0 + o1) * 0.5f,
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
            if (J.arms.size() >= 3) {
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

    void junction(const RoadJunctionGeom& J) {
        if (J.outline.size() < 3) return;
        const bool onGround = std::fabs(f(J.y) - (ground_ ? gnd(f(J.centre.x), f(J.centre.y)) * cm_ : 0.0f)) < 0.1f || !ground_;
        auto h = [&](float x, float z, float hm) { return (onGround ? gnd(x, z) + kLift : wy(f(J.y)) + kLift) + wy(hm); };
        // surface of the most important arm (the widest drivable one)
        int mat = RtsRoadView::kAsphalt;
        float widest = -1, curb = 0;
        for (int e : J.arms) {
            const RoadTypeDef& t = T_[size_t(N_.edges[size_t(e)].typeIndex)];
            const float w = f(t.carriageway(1) + t.carriageway(-1));
            if (t.drivable() && w > widest) { widest = w; mat = surfaceMat(t.surface); }
            curb = std::max({curb, sidewalkHeight(t.left), sidewalkHeight(t.right)});
        }
        float cx = 0, cz = 0;
        for (auto& p : J.outline) { cx += f(p.x); cz += f(p.y); }
        cx /= float(J.outline.size()); cz /= float(J.outline.size());
        const float um = 1.0f / (4.0f / cm_);
        const float3 c{cx, h(cx, cz, 0), cz};
        for (size_t i = 0; i < J.outline.size(); ++i) {
            const Vec2 a = J.outline[i], b = J.outline[(i + 1) % J.outline.size()];
            const float3 pa{f(a.x), h(f(a.x), f(a.y), 0), f(a.y)}, pb{f(b.x), h(f(b.x), f(b.y), 0), f(b.y)};
            tri(mat, c, pa, pb, {cx * um, cz * um}, {pa.x * um, pa.z * um}, {pb.x * um, pb.z * um}, {0, 1, 0});
        }
        // sidewalk corners: a raised strip and the curb face along the inner curve
        for (auto& strip : J.corners) {
            for (size_t q = 0; q + 3 < strip.size(); q += 2) {
                const Vec2 i0 = strip[q], o0 = strip[q + 1], i1 = strip[q + 2], o1 = strip[q + 3];
                auto P = [&](Vec2 v, float hm) { return float3{f(v.x), h(f(v.x), f(v.y), hm), f(v.y)}; };
                const float3 a = P(i0, curb), b = P(o0, curb), cc = P(o1, curb), d = P(i1, curb);
                quad(RtsRoadView::kPaving, a, b, cc, d, {a.x * um, a.z * um}, {b.x * um, b.z * um}, {cc.x * um, cc.z * um}, {d.x * um, d.z * um}, {0, 1, 0});
                if (curb > 0) {
                    const float3 a0 = P(i0, 0), d0 = P(i1, 0);
                    const float3 outw = normalize(float3{f(o0.x - i0.x), 0, f(o0.y - i0.y)});
                    quad(RtsRoadView::kCurb, a0, a, d, d0, {0, 0}, {0, 1}, {1, 1}, {1, 0}, -outw);
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
    paint_ = paintTex_.addGenerated("builtin:road-paint", 1024, 1024, roadkit::markingAtlas(256), false);
    if (hasNet_) {   // new types: the network's type indices change
        try { net_ = parseRoadNetwork(netJson_, types_); }
        catch (const std::exception& e) { warnings_.push_back(e.what()); net_ = RoadNetwork{}; }
    }
}

void RtsRoadView::release(gfx::Device& dev) {
    if (decalMesh_.vb.valid()) decalBatch_.destroyMesh(dev, decalMesh_);
    decalMesh_ = {};
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
    if (dev_) { decalBatch_.shutdown(); paintTex_.shutdown(); }
    paint_ = {};
    dev_ = nullptr;
}

void RtsRoadView::setMap(const Json& map) {
    hasNet_ = map["roadNetwork"].isObject();
    net_ = RoadNetwork{};
    dirty_ = true;
    if (!hasNet_) return;
    netJson_ = map["roadNetwork"];
    try { net_ = parseRoadNetwork(netJson_, types_); }
    catch (const std::exception& e) { warnings_.push_back(e.what()); net_ = RoadNetwork{}; }
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
    for (int k = 0; k < RtsRoadView::kMatCount; ++k) {
        if (a.i[k].empty()) continue;
        Primitive p;
        p.firstIndex = uint32_t(m->indices.size());
        p.indexCount = uint32_t(a.i[k].size());
        p.firstVertex = uint32_t(m->vertices.size());
        p.vertexCount = uint32_t(a.v[k].size());
        p.material = k;
        const uint32_t base = uint32_t(m->vertices.size());
        float3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (auto& v : a.v[k]) {
            lo = {std::min(lo.x, v.pos.x), std::min(lo.y, v.pos.y), std::min(lo.z, v.pos.z)};
            hi = {std::max(hi.x, v.pos.x), std::max(hi.y, v.pos.y), std::max(hi.z, v.pos.z)};
            m->vertices.push_back(v);
        }
        for (uint32_t i : a.i[k]) m->indices.push_back(base + i);
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
        auto& V = a.v[t.mat];
        const uint32_t base = uint32_t(V.size());
        for (auto& q : p) { ModelVertex mv; mv.pos = q; mv.nrm = n; mv.uv = {q.x + q.z, q.y}; V.push_back(mv); }
        a.i[t.mat].push_back(base); a.i[t.mat].push_back(base + 1); a.i[t.mat].push_back(base + 2);
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
    Mesher mesh(types_, net_, geom_, cm, heightAt, false);
    mesh.build();
    std::map<std::string, std::vector<roadkit::Tri>> shapes;
    for (auto& p : mesh.props) {
        if (!p.model.empty()) { const int k = propModel(dev, p.model); if (k >= 0) props_.push_back({k, p.xf}); continue; }
        const float3 at = p.xf.transformPoint({0, 0, 0});
        const int64_t cx = int64_t(std::floor(at.x / float(kChunk))), cz = int64_t(std::floor(at.z / float(kChunk)));
        addShapes(mesh.chunks[(cx << 32) ^ (cz & 0xffffffff)], p.shape, p.xf, 1.0f / cm, shapes);
    }
    if (!mesh.decals.empty()) decalMesh_ = decalBatch_.buildMesh(dev, mesh.decals);
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
        Mesher mesh(types_, ghost_.net, g, cm, heightAt, true);
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
    if (decalMesh_.vb.valid() && paint_.valid()) {
        r.drawCustom([this, &r](gfx::Device& d) {
            SpriteLighting lighting;
            lighting.bind = [&r] { r.bindLitFrame(); };
            lighting.receiveShadows = r.shadowQuality() >= ShadowQuality::Medium;
            decalBatch_.drawMesh(d, decalMesh_, paint_.tex, r.viewProj(), &lighting, true);
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
