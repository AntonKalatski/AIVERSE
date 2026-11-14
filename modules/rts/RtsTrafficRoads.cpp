// Traffic on spline roads (docs/traffic.md § Spline roads): the traffic network built from the map's "roadNetwork"
// (RtsRoadNet) instead of road tiles. Every drivable edge is a curved piece (its trimmed centre line, lanes per
// direction from its road type), every node where two or more of them meet is a junction whose arms get the four
// sides by their directions; cells are filled from the carriageway / junction shapes, the sidewalk mask from the
// sidewalks. Fixed math only: the same network on every machine.
#include "RtsTrafficRoads.h"
#include <algorithm>
#include "../../engine/core/FixedMath.h"

namespace aiverse::game {

namespace tl = lib::genre;

namespace {

int drivingLanes(const RoadTypeDef& t, int dir) {
    int n = 0;
    for (auto& l : t.lanesOf(dir)) n += l.use == RoadLaneUse::Car || l.use == RoadLaneUse::Bus;
    return n;
}

// sides for arms leaving a node in directions `ang` (turns): a rotation of the four sides that gives every arm its
// own side with the least total error; arms that find no side are left out (-1)
std::vector<int> assignSides(const std::vector<Fixed>& ang) {
    const size_t n = ang.size();
    std::vector<int> best(n, -1);
    if (n == 0) return best;
    static const int sideOfK[4] = {tl::kTrafficEast, tl::kTrafficSouth, tl::kTrafficWest, tl::kTrafficNorth};   // 0, 1/4, 1/2, -1/4 turn
    const Fixed quarter = Fixed::fromRatio(1, 4);
    Fixed bestErr{};
    bool have = false;
    for (size_t r = 0; r < n; ++r) {
        // rotation: arm r's angle snapped into [-1/8, 1/8)
        Fixed rot = ang[r];
        while (rot >= Fixed::fromRatio(1, 8)) rot -= quarter;
        while (rot < -Fixed::fromRatio(1, 8)) rot += quarter;
        std::vector<int> sides(n, -1);
        bool used[4] = {false, false, false, false};
        Fixed err{};
        bool ok = true;
        for (size_t i = 0; i < n; ++i) {
            Fixed a = ang[i] - rot;
            while (a < Fixed{}) a += Fixed::fromInt(1);
            while (a >= Fixed::fromInt(1)) a -= Fixed::fromInt(1);
            const int k = int(((a + Fixed::fromRatio(1, 8)) / quarter).floorInt()) % 4;
            if (used[k]) { ok = false; break; }
            used[k] = true;
            sides[i] = sideOfK[k];
            Fixed d = a - quarter * k;
            if (d > Fixed::fromRatio(1, 2)) d -= Fixed::fromInt(1);
            err += fxAbs(d);
        }
        if (ok && (!have || err < bestErr)) { have = true; bestErr = err; best = sides; }
    }
    if (!have) {   // five or more arms, or two in one quarter: the first ones that fit
        bool used[4] = {false, false, false, false};
        for (size_t i = 0; i < n; ++i) {
            Fixed a = ang[i];
            while (a < Fixed{}) a += Fixed::fromInt(1);
            const int k = int(((a + Fixed::fromRatio(1, 8)) / quarter).floorInt()) % 4;
            if (!used[k]) { used[k] = true; best[i] = sideOfK[k]; }
        }
    }
    return best;
}

} // namespace

tl::TrafficNet buildTrafficNetFromRoads(int w, int h, const RoadNetwork& n, const std::vector<RoadTypeDef>& types,
                                        const RoadGeometry& G, const tl::TrafficNetConfig& cfg, std::vector<uint8_t>& sidewalk) {
    tl::TrafficNet net;
    net.w = w; net.h = h; net.cfg = cfg;
    const size_t N = size_t(std::max(0, w)) * size_t(std::max(0, h));
    net.pieceAt.assign(N, -1);
    net.junctionAt.assign(N, -1);
    sidewalk.assign(N, 0);
    if (N == 0) return net;

    // arms of every node (drivable edges only)
    struct Arm { int edge; int end; Fixed angle; };
    std::vector<std::vector<Arm>> arms(n.nodes.size());
    std::vector<int> pieceOf(n.edges.size(), -1);
    for (size_t i = 0; i < n.edges.size(); ++i) {
        const RoadTypeDef& t = types[size_t(n.edges[i].typeIndex)];
        const RoadEdgeGeom& g = G.edges[i];
        if (!t.drivable() || g.pts.size() < 2 || g.length - g.trim0 - g.trim1 < Fixed::fromInt(1)) continue;
        for (int end = 0; end < 2; ++end) {
            const RoadSample r = roadSampleAt(g, end == 0 ? g.trim0 : g.length - g.trim1);
            const Vec2 d = end == 0 ? r.t : Vec2{-r.t.x, -r.t.y};
            arms[size_t(n.nodeIndex(end == 0 ? n.edges[i].a : n.edges[i].b))].push_back({int(i), end, fxAtan2Turns(d.y, d.x)});
        }
    }

    // pieces: the trimmed centre lines
    for (size_t i = 0; i < n.edges.size(); ++i) {
        const RoadEdge& e = n.edges[i];
        const RoadTypeDef& t = types[size_t(e.typeIndex)];
        const RoadEdgeGeom& g = G.edges[i];
        if (!t.drivable() || g.pts.size() < 2) continue;
        const Fixed s0 = g.trim0, s1 = g.length - g.trim1;
        if (s1 - s0 < Fixed::fromInt(1)) continue;
        tl::TrafficPiece p;
        std::vector<RoadSample> pts{roadSampleAt(g, s0)};
        for (auto& q : g.pts) if (q.s > s0 && q.s < s1) pts.push_back(q);
        pts.push_back(roadSampleAt(g, s1));
        for (auto& q : pts) {
            if (!p.line.empty() && (q.p - p.line.back()).lengthSq() < Fixed::fromRatio(1, 1024)) continue;
            p.at.push_back(p.line.empty() ? Fixed{} : p.at.back() + (q.p - p.line.back()).length());
            p.line.push_back(q.p);
        }
        if (p.line.size() < 2) continue;
        p.len = p.at.back();
        const Vec2 chord = p.line.back() - p.line.front();
        p.axis = fxAbs(chord.x) >= fxAbs(chord.y) ? tl::TrafficAxis::X : tl::TrafficAxis::Z;
        p.along0 = 0;
        p.along1 = std::max(0, int((p.len - Fixed::fromRatio(1, 1024)).floorInt()));
        const int fs = e.reversed ? -1 : 1;
        const int fwd = drivingLanes(t, fs), bwd = drivingLanes(t, -fs);   // +along = a -> b
        p.dirLanes = {int8_t(bwd), int8_t(fwd)};
        p.lanes = std::max(fwd, bwd);
        const auto& lanes = t.forward.empty() ? t.backward : t.forward;
        p.laneW = lanes.empty() ? cfg.laneWidth : lanes[0].width;
        p.median = t.median;
        const Fixed half = fxMax(t.carriageway(1), t.carriageway(-1));
        p.across0 = 0;
        p.across1 = std::max(0, int((half * 2 + Fixed::fromRatio(1, 2)).floorInt()) - 1);
        pieceOf[i] = int(net.pieces.size());
        net.pieces.push_back(std::move(p));
        // cells of its carriageway
        std::vector<std::vector<Vec2>> polys;
        // every cell the carriageway touches (as a road of tiles covers its lanes whole): half a cell wider
        roadStripPolygons(g, s0, s1, -t.carriageway(-fs) - Fixed::fromRatio(1, 2), t.carriageway(fs) + Fixed::fromRatio(1, 2), polys);
        const int32_t id = int32_t(net.pieces.size() - 1);
        for (auto& poly : polys) roadFillPolygon(poly, w, h, [&](int c) { if (net.pieceAt[size_t(c)] < 0) net.pieceAt[size_t(c)] = id; });
    }

    // junctions: every node two or more drivable roads meet at
    for (size_t ni = 0; ni < n.nodes.size(); ++ni) {
        auto& A = arms[ni];
        if (A.size() < 2) continue;
        std::sort(A.begin(), A.end(), [&](const Arm& x, const Arm& y) { return x.angle != y.angle ? x.angle < y.angle : n.edges[size_t(x.edge)].id < n.edges[size_t(y.edge)].id; });
        std::vector<Fixed> ang;
        for (auto& a : A) ang.push_back(a.angle);
        const std::vector<int> sides = assignSides(ang);
        tl::TrafficJunction j;
        const int32_t jid = int32_t(net.junctions.size());
        int x0 = INT32_MAX, z0 = INT32_MAX, x1 = -1, z1 = -1;
        auto mark = [&](int c) {
            if (net.junctionAt[size_t(c)] < 0) net.junctionAt[size_t(c)] = jid;
            const int cx = c % w, cz = c / w;
            x0 = std::min(x0, cx); x1 = std::max(x1, cx); z0 = std::min(z0, cz); z1 = std::max(z1, cz);
        };
        const int gj = G.nodeJunction[ni];
        if (gj >= 0 && !G.junctions[size_t(gj)].split && G.junctions[size_t(gj)].outline.size() >= 3) roadFillPolygon(G.junctions[size_t(gj)].outline, w, h, mark);
        if (x1 < 0) {   // a bend, a lane branch: the cell of the node
            const Vec2 p = n.nodes[ni].p;
            const int cx = int(p.x.floorInt()), cz = int(p.y.floorInt());
            if (cx >= 0 && cz >= 0 && cx < w && cz < h) mark(cz * w + cx);
        }
        if (x1 < 0) continue;
        j.x0 = x0; j.x1 = x1; j.z0 = z0; j.z1 = z1;
        int driven = 0;
        for (size_t k = 0; k < A.size(); ++k) {
            const int pi = pieceOf[size_t(A[k].edge)];
            if (pi < 0 || sides[k] < 0) continue;
            tl::TrafficPiece& p = net.pieces[size_t(pi)];
            int& slot = j.piece[size_t(sides[k])];
            if (slot >= 0 && net.pieces[size_t(slot)].lanes >= p.lanes) continue;
            slot = pi;
            p.junction[size_t(A[k].end)] = jid;
            p.side[size_t(A[k].end)] = int8_t(sides[k]);
            ++driven;
        }
        j.signal = gj >= 0 && G.junctions[size_t(gj)].signal && driven >= 3;
        net.junctions.push_back(j);
    }
    // sidewalks for the pedestrians
    const std::vector<uint8_t> cover = rasterizeRoads(n, types, G, w, h);
    for (size_t i = 0; i < N; ++i) sidewalk[i] = cover[i] == 2 ? 1 : 0;
    return net;
}

} // namespace aiverse::game
