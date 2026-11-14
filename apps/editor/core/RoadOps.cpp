#include "RoadOps.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace aiverse::editor {

using namespace game;

namespace roads {

namespace {
const Fixed kOne = Fixed::fromInt(1);
Vec2 lerp(Vec2 a, Vec2 b, Fixed t) { return a + (b - a) * t; }
Fixed lerpF(Fixed a, Fixed b, Fixed t) { return a + (b - a) * t; }
void ends(const RoadNetwork& n, const RoadEdge& e, Vec2& A, Vec2& c1, Vec2& c2, Vec2& B) {
    A = n.nodes[size_t(n.nodeIndex(e.a))].p;
    B = n.nodes[size_t(n.nodeIndex(e.b))].p;
    c1 = e.curved ? e.c1 : lerp(A, B, Fixed::fromRatio(1, 3));
    c2 = e.curved ? e.c2 : lerp(A, B, Fixed::fromRatio(2, 3));
}
} // namespace

std::vector<RoadTypeDef> types(const Workspace& ws) {
    try { return loadRoadTypes(ws.project.rulesJson["roadTypes"]); }
    catch (const std::exception&) { return loadRoadTypes(Json()); }
}

RoadNetwork read(const MapDoc& doc, const std::vector<RoadTypeDef>& types) {
    return parseRoadNetwork(doc.data.extra["roadNetwork"], types);
}

void write(MapDoc& doc, const RoadNetwork& n) {
    if (n.nodes.empty() && n.edges.empty()) { doc.data.extra.erase("roadNetwork"); return; }
    doc.data.extra.set("roadNetwork", roadNetworkToJson(n));
}

Fixed nearestT(const RoadNetwork& n, int ei, Vec2 p) {
    Vec2 A, c1, c2, B;
    ends(n, n.edges[size_t(ei)], A, c1, c2, B);
    Fixed best{}, bt{};
    bool have = false;
    for (int i = 0; i <= 64; ++i) {   // coarse, then refine around the best sample
        const Fixed t = Fixed::fromRatio(i, 64);
        const Fixed d = (roadBezier(A, c1, c2, B, t) - p).lengthSq();
        if (!have || d < best) { have = true; best = d; bt = t; }
    }
    Fixed lo = fxMax(bt - Fixed::fromRatio(1, 64), Fixed{}), hi = fxMin(bt + Fixed::fromRatio(1, 64), kOne);
    for (int it = 0; it < 20; ++it) {
        const Fixed m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3;
        if ((roadBezier(A, c1, c2, B, m1) - p).lengthSq() < (roadBezier(A, c1, c2, B, m2) - p).lengthSq()) hi = m2; else lo = m1;
    }
    return (lo + hi) / 2;
}

Snap snap(const RoadNetwork& n, const RoadGeometry& g, Vec2 p, Fixed radius) {
    Snap s;
    s.p = p;
    Fixed best = radius * radius;
    for (auto& d : n.nodes) {
        const Fixed q = (d.p - p).lengthSq();
        if (q <= best) { best = q; s.kind = Snap::Node; s.node = d.id; s.p = d.p; }
    }
    if (s.kind == Snap::Node) return s;
    // on an edge: anywhere over its carriageway (lane picking) counts
    Fixed bestEdge{};
    bool have = false;
    for (size_t i = 0; i < n.edges.size() && i < g.edges.size(); ++i) {
        Fixed along, lat;
        roadProject(g.edges[i], p, along, lat);
        if (along <= Fixed{} || along >= g.edges[i].length) continue;
        const Fixed a = fxAbs(lat);
        if (a > radius + Fixed::fromInt(4)) continue;
        if (!have || a < bestEdge) {
            have = true; bestEdge = a;
            s.kind = Snap::Edge; s.edge = int(i); s.s = along; s.lateral = lat;
            s.t = nearestT(n, int(i), p);
            s.p = roadSampleAt(g.edges[i], along).p;
        }
    }
    if (have && bestEdge > radius * 3) { s = Snap{}; s.p = p; }
    return s;
}

bool laneAt(const RoadTypeDef& t, const RoadEdge& e, Fixed lateral, int& dir, int& lane) {
    const int fs = e.reversed ? -1 : 1;
    for (int d : {1, -1}) {
        const auto& v = t.lanesOf(d);
        Fixed off = t.median / 2;
        for (size_t k = 0; k < v.size(); ++k) {
            const Fixed lo = off, hi = off + v[k].width;
            off = hi;
            const Fixed side = lateral * (d * fs);   // positive when on that direction's side
            if (side >= lo && side < hi) { dir = d * fs; lane = int(k); return true; }
        }
    }
    return false;
}

int32_t splitEdge(RoadNetwork& n, int ei, Fixed t) {
    RoadEdge e = n.edges[size_t(ei)];
    Vec2 A, c1, c2, B;
    ends(n, e, A, c1, c2, B);
    const RoadNode& NA = n.nodes[size_t(n.nodeIndex(e.a))];
    const RoadNode& NB = n.nodes[size_t(n.nodeIndex(e.b))];
    // de Casteljau in x, z and height
    const Vec2 p01 = lerp(A, c1, t), p12 = lerp(c1, c2, t), p23 = lerp(c2, B, t);
    const Vec2 p012 = lerp(p01, p12, t), p123 = lerp(p12, p23, t), P = lerp(p012, p123, t);
    const Fixed h01 = lerpF(NA.y, e.y1, t), h12 = lerpF(e.y1, e.y2, t), h23 = lerpF(e.y2, NB.y, t);
    const Fixed h012 = lerpF(h01, h12, t), h123 = lerpF(h12, h23, t), H = lerpF(h012, h123, t);
    RoadNode mid;
    mid.id = n.nextNodeId();
    mid.p = P;
    mid.y = H;
    n.nodes.push_back(mid);
    RoadEdge first = e, second = e;
    first.b = mid.id;
    second.id = n.nextEdgeId();
    second.a = mid.id;
    first.attach[1] = {};
    second.attach[0] = {};
    if (e.curved) {
        first.c1 = p01; first.c2 = p012;
        second.c1 = p123; second.c2 = p23;
    }
    first.y1 = h01; first.y2 = h012;
    second.y1 = h123; second.y2 = h23;
    n.edges[size_t(ei)] = first;
    n.edges.push_back(second);
    // branches that left the old edge at its far end now leave the second half
    for (auto& o : n.edges)
        for (int end = 0; end < 2; ++end) {
            RoadAttach& at = o.attach[size_t(end)];
            if (at.edge != e.id) continue;
            const int32_t node = end == 0 ? o.a : o.b;
            if (node == e.b) at.edge = second.id;
        }
    return mid.id;
}

int32_t nodeFor(RoadNetwork& n, const Snap& s, Fixed y) {
    if (s.kind == Snap::Node) return s.node;
    if (s.kind == Snap::Edge) {
        if (s.t <= Fixed::fromRatio(1, 50)) return n.edges[size_t(s.edge)].a;
        if (s.t >= Fixed::fromRatio(49, 50)) return n.edges[size_t(s.edge)].b;
        return splitEdge(n, s.edge, s.t);
    }
    RoadNode d;
    d.id = n.nextNodeId();
    d.p = s.p;
    d.y = y;
    n.nodes.push_back(d);
    return d.id;
}

void quadraticToCubic(Vec2 a, Vec2 q, Vec2 b, Vec2& c1, Vec2& c2) {
    c1 = a + (q - a) * Fixed::fromRatio(2, 3);
    c2 = b + (q - b) * Fixed::fromRatio(2, 3);
}

int32_t addEdge(RoadNetwork& n, const std::vector<RoadTypeDef>& types, const AddSpec& spec) {
    const int ti = findRoadType(types, spec.type);
    if (ti < 0) throw std::runtime_error("unknown road type '" + spec.type + "' (road_types lists them)");
    // the parent of a lane branch: remember its id before splits renumber nothing (ids are stable)
    const int32_t parentA = spec.attachA.edge >= 0 && size_t(spec.attachA.edge) < n.edges.size() ? n.edges[size_t(spec.attachA.edge)].id : -1;
    const int32_t parentB = spec.attachB.edge >= 0 && size_t(spec.attachB.edge) < n.edges.size() ? n.edges[size_t(spec.attachB.edge)].id : -1;
    Snap sb = spec.b;
    const int32_t a = nodeFor(n, spec.a, spec.ya);
    // the first split may have moved the edge the second end snaps to: find it again by position
    if (sb.kind == Snap::Edge && spec.a.kind == Snap::Edge && spec.a.edge == sb.edge) {
        const RoadGeometry g = buildRoadGeometry(n, types);
        sb = snap(n, g, sb.p, Fixed::fromRatio(1, 2));
    }
    const int32_t b = nodeFor(n, sb, spec.yb);
    if (a == b) throw std::runtime_error("both ends of the road are at the same point");
    for (auto& e : n.edges)
        if ((e.a == a && e.b == b) || (e.a == b && e.b == a))
            if (!spec.curved && !e.curved) throw std::runtime_error("these two points are already joined by a road");
    const RoadNode& NA = n.nodes[size_t(n.nodeIndex(a))];
    const RoadNode& NB = n.nodes[size_t(n.nodeIndex(b))];
    if ((NA.p - NB.p).length() < Fixed::fromRatio(1, 2)) throw std::runtime_error("the road is too short");
    RoadEdge e;
    e.id = n.nextEdgeId();
    e.a = a; e.b = b;
    e.type = spec.type;
    e.typeIndex = ti;
    e.curved = spec.curved;
    e.c1 = spec.c1; e.c2 = spec.c2;
    e.y1 = spec.heights ? spec.y1 : lerpF(NA.y, NB.y, Fixed::fromRatio(1, 3));
    e.y2 = spec.heights ? spec.y2 : lerpF(NA.y, NB.y, Fixed::fromRatio(2, 3));
    e.reversed = spec.reversed;
    auto attach = [&](const RoadAttach& in, int32_t parentId, int32_t node) {
        RoadAttach at = in;
        if (parentId < 0) return RoadAttach{};
        // the parent is now split at `node`: either half that ends there will do (same orientation)
        for (auto& o : n.edges) if ((o.id == parentId || o.type == n.edges[size_t(n.edgeIndex(parentId))].type) && (o.a == node || o.b == node) && o.id != e.id) { at.edge = o.id; break; }
        const RoadEdge& P = n.edges[size_t(n.edgeIndex(at.edge))];
        const RoadTypeDef& pt = types[size_t(P.typeIndex)];
        if (at.lane1 >= pt.lanes(at.dir)) throw std::runtime_error("the parent road has no such lanes");
        return at;
    };
    e.attach[0] = attach(spec.attachA, parentA, a);
    e.attach[1] = attach(spec.attachB, parentB, b);
    n.edges.push_back(e);
    // an unpaved road may only run on from a narrow paved one (asphalt -> broken asphalt -> dirt); a wide street is
    // joined at a junction instead
    for (auto& issue : buildRoadGeometry(n, types).issues)
        if (issue.find("cannot run on from") != std::string::npos &&
            (issue.rfind("node " + std::to_string(a) + ":", 0) == 0 || issue.rfind("node " + std::to_string(b) + ":", 0) == 0)) {
            n.edges.pop_back();
            throw std::runtime_error("Грунтовая или гравийная дорога не может продолжать широкую улицу: присоедините её к дороге на "
                                     "перекрёстке (сбоку) или продолжите дорогу в 1–2 полосы — переход будет «асфальт → разбитый асфальт → грунт».");
        }
    // a one-way branch drawn against the traffic of the lanes it leaves / joins: turn its lanes round
    if (e.attach[0].edge >= 0 || e.attach[1].edge >= 0) {
        const std::string mark = "edge " + std::to_string(e.id) + ": no lanes drive";
        auto bad = [&] { for (auto& s : buildRoadGeometry(n, types).issues) if (s.rfind(mark, 0) == 0) return true; return false; };
        if (bad()) { n.edges.back().reversed = !n.edges.back().reversed; if (bad()) n.edges.back().reversed = !n.edges.back().reversed; }
    }
    return e.id;
}

void deleteEdge(RoadNetwork& n, int32_t id) {
    const int i = n.edgeIndex(id);
    if (i < 0) throw std::runtime_error("no road edge " + std::to_string(id));
    n.edges.erase(n.edges.begin() + i);
    for (auto& o : n.edges) for (auto& at : o.attach) if (at.edge == id) at = {};
    n.nodes.erase(std::remove_if(n.nodes.begin(), n.nodes.end(), [&](const RoadNode& d) {
        for (auto& o : n.edges) if (o.a == d.id || o.b == d.id) return false;
        return true;
    }), n.nodes.end());
}

void deleteNode(RoadNetwork& n, int32_t id) {
    if (n.nodeIndex(id) < 0) throw std::runtime_error("no road node " + std::to_string(id));
    std::vector<int32_t> gone;
    for (auto& e : n.edges) if (e.a == id || e.b == id) gone.push_back(e.id);
    for (int32_t e : gone) deleteEdge(n, e);
    const int k = n.nodeIndex(id);
    if (k >= 0) n.nodes.erase(n.nodes.begin() + k);
}

void moveNode(RoadNetwork& n, int32_t id, Vec2 p, Fixed y) {
    const int k = n.nodeIndex(id);
    if (k < 0) throw std::runtime_error("no road node " + std::to_string(id));
    RoadNode& d = n.nodes[size_t(k)];
    const Vec2 delta = p - d.p;
    const Fixed dy = y - d.y;
    d.p = p; d.y = y;
    for (auto& e : n.edges) {
        if (e.a == id) { if (e.curved) e.c1 = e.c1 + delta; e.y1 += dy; }
        if (e.b == id) { if (e.curved) e.c2 = e.c2 + delta; e.y2 += dy; }
    }
}

} // namespace roads

// ------------------------------------------------------------------ MCP

namespace {
void needProject(const Workspace& ws) { if (ws.project.dir.empty()) throw std::runtime_error("no game is open: call open_game first"); }
Fixed num(const Json& v, const char* what) {
    if (!v.isNumber()) throw std::runtime_error(std::string(what) + ": a number");
    return v.fx();
}
Vec2 pt(const Json& v, const char* what) {
    if (!v.isArray() || v.size() < 2) throw std::runtime_error(std::string(what) + ": [x, z] or [x, z, height]");
    return {num(v[size_t(0)], what), num(v[size_t(1)], what)};
}
Json summary(const RoadNetwork& n, const std::vector<RoadTypeDef>& T, const RoadGeometry& g) {
    Json j = roadNetworkToJson(n);
    Json info = Json::array();
    for (size_t i = 0; i < n.edges.size(); ++i) {
        Json e = Json::object();
        e.set("id", Json::number(int64_t(n.edges[i].id)));
        e.set("length", Json::number(g.edges[i].length));
        int bridges = 0, tunnels = 0;
        for (auto& sp : g.edges[i].spans) (sp.tunnel ? tunnels : bridges)++;
        if (bridges) e.set("bridges", Json::number(int64_t(bridges)));
        if (tunnels) e.set("tunnels", Json::number(int64_t(tunnels)));
        e.set("lanes", Json::string(std::to_string(T[size_t(n.edges[i].typeIndex)].lanes(1)) + "+" + std::to_string(T[size_t(n.edges[i].typeIndex)].lanes(-1))));
        info.push(e);
    }
    j.set("edgeInfo", info);
    Json js = Json::array();
    for (auto& J : g.junctions) {
        Json o = Json::object();
        o.set("node", Json::number(int64_t(J.node)));
        o.set("arms", Json::number(int64_t(J.arms.size())));
        o.set("signal", Json::fromBool(J.signal));
        if (J.split) o.set("split", Json::fromBool(true));
        js.push(o);
    }
    j.set("junctions", js);
    Json is = Json::array();
    for (auto& s : g.issues) is.push(Json::string(s));
    j.set("issues", is);
    return j;
}
roads::Snap snapJson(const Json& v, const RoadNetwork& n, const RoadGeometry& g, Fixed radius, const char* what) {
    roads::Snap s;
    if (v.isNumber()) {   // a node id
        const int32_t id = int32_t(v.integer());
        if (n.nodeIndex(id) < 0) throw std::runtime_error(std::string(what) + ": no node " + std::to_string(id));
        s.kind = roads::Snap::Node; s.node = id; s.p = n.nodes[size_t(n.nodeIndex(id))].p;
        return s;
    }
    return roads::snap(n, g, pt(v, what), radius);
}
} // namespace

void registerRoadOps(Workspace& ws) {
    Workspace* w = &ws;
    auto add = [&ws](const char* name, const char* desc, const char* schema, Workspace::OpFn fn) {
        ws.registerOp({name, desc, Json::parse(schema), std::move(fn), false});
    };
    auto commit = [w](RoadNetwork& n, const std::vector<RoadTypeDef>& T, const std::string& text) {
        // validate by parsing what we write
        Json j = roadNetworkToJson(n);
        parseRoadNetwork(j, T);
        roads::write(w->map, n);
        w->map.touched();
        w->note({Change::Map, 0, 0, -1, -1, -1, text});
    };
    add("road_types",
        "Road types for spline roads (docs/roads.md): the built-in library (city streets, avenues, boulevard, one-way, "
        "highways and ramps, rural roads, gravel / dirt roads, paths) plus rules \"roadTypes\". Each: id, name, category, "
        "lanes per direction (use, width in cells), median, curbs / sidewalks / barriers per side, surface, width, props.",
        "{}", [w](const Json&) {
            needProject(*w);
            Json a = Json::array();
            for (auto& t : roads::types(*w)) a.push(roadTypeToJson(t));
            Json r = Json::object();
            r.set("cellMeters", Json::number(roadCellMeters(w->project.rulesJson["roadTypes"])));
            r.set("types", a);
            return r;
        });
    add("road_type_set",
        "Add or change a road type in rules \"roadTypes\" (metres, km/h). 'id', 'type': {\"extends\": \"street_2\", "
        "\"name\", \"lanes\": {\"forward\": 2 | [\"car\", \"bus\", \"parking\"], \"backward\": .., \"width\": 3.5}, "
        "\"median\": {\"width\", \"surface\", \"barrier\", \"curb\"}, \"sides\" / \"left\" / \"right\": {\"curb\": none|low|high|sloped|shoulder, "
        "\"sidewalk\", \"shoulder\", \"shoulderSurface\", \"barrier\": none|guardrail|concrete|fence|wall|hedge}, \"surface\", "
        "\"centre\": none|dashed|solid|double|yellow, \"speed\", \"props\": [{\"shape\": lamp|tree|bush|flowerbed|bollard|pole|bench|sign "
        "or \"model\" or \"type\", \"side\": -1|0|1|2, \"spacing\", \"phase\", \"offset\"}]}. null removes the game's type.",
        R"({"properties": {"id": {"type": "string"}, "type": {}}, "required": ["id"]})",
        [w](const Json& p) {
            needProject(*w);
            const std::string id = p.getStr("id");
            if (id.empty() || id[0] == '_') throw std::runtime_error("id: a name like \"my_street\"");
            Json all = w->project.rulesJson["roadTypes"].isObject() ? w->project.rulesJson["roadTypes"] : Json::object();
            if (p["type"].isNull()) all.erase(id); else all.set(id, p["type"]);
            loadRoadTypes(all);   // throws a readable error before anything is written
            Json q = Json::object();
            q.set("path", Json::string("roadTypes"));
            q.set("value", all);
            return w->call("rules_set", q);
        });
    add("road_network",
        "The open map's spline road network (map \"roadNetwork\"): nodes (id, x, z, y = metres over the ground), edges "
        "(id, a, b, type, c1 / c2 Bezier control points [x, z, height], attachA / attachB = lanes of another edge the "
        "end leaves from), per-edge length / bridges / tunnels, junctions (arms, signal) and problems.",
        "{}", [w](const Json&) {
            needProject(*w);
            const auto T = roads::types(*w);
            const RoadNetwork n = roads::read(w->map, T);
            return summary(n, T, buildRoadGeometry(n, T));
        });
    add("road_add",
        "Draw a road: 'points' [[x, z] or [x, z, height], ...] (2 or more; heights in metres over the ground: > 1.5 "
        "makes a bridge, < -2.5 a tunnel) or node ids; 'type' (road_types); 'curve': \"straight\" (a polyline, default), "
        "\"smooth\" (one smooth spline through the points) or \"bezier\" (exactly [a, c1, c2, b]). Ends snap to nodes "
        "and edges within 'snap' cells (default 1.5): a road ending on an edge splits it into a junction. 'fromLanes' "
        "{\"edge\": id, \"lanes\": [first, last], \"dir\": 1|-1} starts the road on those lanes of a road (a ramp / "
        "branch, the first point on that road); 'toLanes' likewise for the end (a merge). 'reversed' for one-way types.",
        R"({"properties": {"points": {"type": "array"}, "type": {"type": "string"}, "curve": {"type": "string"},
            "snap": {"type": "number"}, "fromLanes": {"type": "object"}, "toLanes": {"type": "object"}, "reversed": {"type": "boolean"}},
            "required": ["points", "type"]})",
        [w, commit](const Json& p) {
            needProject(*w);
            const auto T = roads::types(*w);
            RoadNetwork n = roads::read(w->map, T);
            const Json& pts = p["points"];
            if (!pts.isArray() || pts.size() < 2) throw std::runtime_error("points: at least two");
            const std::string curve = p.getStr("curve", "straight");
            const Fixed radius = p.has("snap") ? p.getFx("snap") : Fixed::fromRatio(3, 2);
            const std::string type = p.getStr("type");
            if (findRoadType(T, type) < 0) throw std::runtime_error("unknown road type '" + type + "' (road_types)");
            w->map.beginEdit();
            auto height = [&](const Json& v) { return v.isArray() && v.size() > 2 ? num(v[size_t(2)], "height") : Fixed{}; };
            auto lanes = [&](const Json& j, RoadAttach& at) {
                if (!j.isObject()) return;
                const int ei = n.edgeIndex(int32_t(j.getInt("edge", -1)));
                if (ei < 0) throw std::runtime_error("fromLanes / toLanes: no edge " + std::to_string(j.getInt("edge", -1)));
                at.edge = ei;
                at.dir = int8_t(j.getInt("dir", 1) < 0 ? -1 : 1);
                const Json& l = j["lanes"];
                at.lane0 = int8_t(l.isArray() ? l[size_t(0)].integer() : l.integer());
                at.lane1 = int8_t(l.isArray() && l.size() > 1 ? l[size_t(1)].integer() : at.lane0);
            };
            std::vector<int32_t> made;
            auto addOne = [&](const Json& A, const Json& B, bool curved, Vec2 c1, Vec2 c2, Fixed y1, Fixed y2, bool first, bool last) {
                const RoadGeometry g = buildRoadGeometry(n, T);
                roads::AddSpec s;
                s.type = type;
                s.a = snapJson(A, n, g, radius, "points");
                s.b = snapJson(B, n, g, radius, "points");
                s.ya = height(A); s.yb = height(B);
                s.curved = curved; s.c1 = c1; s.c2 = c2;
                if (curved) { s.heights = true; s.y1 = y1; s.y2 = y2; }
                s.reversed = p.getBool("reversed", false);
                if (first) lanes(p["fromLanes"], s.attachA);
                if (last) lanes(p["toLanes"], s.attachB);
                if (s.attachA.edge >= 0) { s.a.kind = roads::Snap::Edge; s.a.edge = s.attachA.edge; s.a.t = roads::nearestT(n, s.attachA.edge, s.a.p); }
                if (s.attachB.edge >= 0) { s.b.kind = roads::Snap::Edge; s.b.edge = s.attachB.edge; s.b.t = roads::nearestT(n, s.attachB.edge, s.b.p); }
                made.push_back(roads::addEdge(n, T, s));
            };
            if (curve == "bezier") {
                if (pts.size() != 4) throw std::runtime_error("curve \"bezier\": points [a, c1, c2, b]");
                addOne(pts[size_t(0)], pts[size_t(3)], true, pt(pts[size_t(1)], "c1"), pt(pts[size_t(2)], "c2"), height(pts[size_t(1)]), height(pts[size_t(2)]), true, true);
            } else if (curve == "smooth") {   // Catmull-Rom through the points, one edge per span
                std::vector<Vec2> P;
                std::vector<Fixed> H;
                for (auto& v : pts.items()) {
                    if (v.isNumber()) { const int k = n.nodeIndex(int32_t(v.integer())); if (k < 0) throw std::runtime_error("points: no node"); P.push_back(n.nodes[size_t(k)].p); H.push_back(n.nodes[size_t(k)].y); }
                    else { P.push_back(pt(v, "points")); H.push_back(height(v)); }
                }
                for (size_t i = 0; i + 1 < P.size(); ++i) {
                    const Vec2 p0 = P[i ? i - 1 : 0], p1 = P[i], p2 = P[i + 1], p3 = P[std::min(i + 2, P.size() - 1)];
                    const Vec2 c1 = p1 + (p2 - p0) / Fixed::fromInt(6), c2 = p2 - (p3 - p1) / Fixed::fromInt(6);
                    const Fixed y1 = H[i] + (H[i + 1] - H[i ? i - 1 : 0]) / 6, y2 = H[i + 1] - (H[std::min(i + 2, H.size() - 1)] - H[i]) / 6;
                    addOne(pts[i], pts[i + 1], true, c1, c2, y1, y2, i == 0, i + 2 == P.size());
                }
            } else if (curve == "straight") {
                for (size_t i = 0; i + 1 < pts.size(); ++i) addOne(pts[i], pts[i + 1], false, {}, {}, {}, {}, i == 0, i + 2 == pts.size());
            } else throw std::runtime_error("curve: straight | smooth | bezier");
            commit(n, T, "Дорога: " + type);
            Json out = summary(n, T, buildRoadGeometry(n, T));
            Json ids = Json::array();
            for (int32_t id : made) ids.push(Json::number(int64_t(id)));
            out.set("added", ids);
            return out;
        });
    add("road_node_set",
        "Move a road node: 'node' id, 'x', 'z' (cells), 'y' (metres over the ground: bridges / tunnels). The control "
        "points next to it follow.",
        R"({"properties": {"node": {"type": "integer"}, "x": {"type": "number"}, "z": {"type": "number"}, "y": {"type": "number"}}, "required": ["node"]})",
        [w, commit](const Json& p) {
            needProject(*w);
            const auto T = roads::types(*w);
            RoadNetwork n = roads::read(w->map, T);
            const int32_t id = int32_t(p.getInt("node", -1));
            const int k = n.nodeIndex(id);
            if (k < 0) throw std::runtime_error("no road node " + std::to_string(id));
            const RoadNode d = n.nodes[size_t(k)];
            w->map.beginEdit();
            roads::moveNode(n, id, {p.has("x") ? p.getFx("x") : d.p.x, p.has("z") ? p.getFx("z") : d.p.y}, p.has("y") ? p.getFx("y") : d.y);
            commit(n, T, "Узел дороги " + std::to_string(id));
            return summary(n, T, buildRoadGeometry(n, T));
        });
    add("road_edge_set",
        "Change a road edge: 'edge' id; 'type'; 'c1' / 'c2' ([x, z] or [x, z, height]) to bend it, 'straight': true to "
        "straighten; 'y1' / 'y2' heights of the control points; 'reversed'.",
        R"({"properties": {"edge": {"type": "integer"}, "type": {"type": "string"}, "c1": {"type": "array"}, "c2": {"type": "array"},
            "straight": {"type": "boolean"}, "y1": {"type": "number"}, "y2": {"type": "number"}, "reversed": {"type": "boolean"}}, "required": ["edge"]})",
        [w, commit](const Json& p) {
            needProject(*w);
            const auto T = roads::types(*w);
            RoadNetwork n = roads::read(w->map, T);
            const int ei = n.edgeIndex(int32_t(p.getInt("edge", -1)));
            if (ei < 0) throw std::runtime_error("no road edge " + std::to_string(p.getInt("edge", -1)));
            RoadEdge& e = n.edges[size_t(ei)];
            w->map.beginEdit();
            if (p.has("type")) {
                e.type = p.getStr("type");
                e.typeIndex = findRoadType(T, e.type);
                if (e.typeIndex < 0) throw std::runtime_error("unknown road type '" + e.type + "'");
            }
            if (p.getBool("straight", false)) e.curved = false;
            if (p.has("c1") || p.has("c2")) {
                Vec2 A, c1, c2, B;
                A = n.nodes[size_t(n.nodeIndex(e.a))].p; B = n.nodes[size_t(n.nodeIndex(e.b))].p;
                c1 = e.curved ? e.c1 : A + (B - A) / Fixed::fromInt(3); c2 = e.curved ? e.c2 : A + (B - A) * Fixed::fromRatio(2, 3);
                if (p.has("c1")) { c1 = pt(p["c1"], "c1"); if (p["c1"].size() > 2) e.y1 = p["c1"][size_t(2)].fx(); }
                if (p.has("c2")) { c2 = pt(p["c2"], "c2"); if (p["c2"].size() > 2) e.y2 = p["c2"][size_t(2)].fx(); }
                e.curved = true; e.c1 = c1; e.c2 = c2;
            }
            if (p.has("y1")) e.y1 = p.getFx("y1");
            if (p.has("y2")) e.y2 = p.getFx("y2");
            if (p.has("reversed")) e.reversed = p.getBool("reversed");
            commit(n, T, "Дорога " + std::to_string(e.id));
            return summary(n, T, buildRoadGeometry(n, T));
        });
    add("road_stop_set",
        "Bus stop bay (\"карман\") on a road edge: the carriageway widens at the kerb, the sidewalk gives way, a "
        "shelter stands behind it. 'edge' id; 'at' (0..1 along the edge) or 'point' [x, z] (its nearest place, and "
        "the side it is on); 'side' 1 right / -1 left of a -> b; 'length' / 'taper' metres (default 18 / 12); "
        "'width' metres (default: the kerb lane); 'shelter' (default true). 'remove': true deletes the bay nearest to "
        "'at' / 'point'.",
        R"({"properties": {"edge": {"type": "integer"}, "at": {"type": "number"}, "point": {"type": "array"}, "side": {"type": "integer"},
            "length": {"type": "number"}, "taper": {"type": "number"}, "width": {"type": "number"}, "shelter": {"type": "boolean"},
            "remove": {"type": "boolean"}}, "required": ["edge"]})",
        [w, commit](const Json& p) {
            needProject(*w);
            const auto T = roads::types(*w);
            RoadNetwork n = roads::read(w->map, T);
            const int ei = n.edgeIndex(int32_t(p.getInt("edge", -1)));
            if (ei < 0) throw std::runtime_error("no road edge " + std::to_string(p.getInt("edge", -1)));
            const RoadGeometry g = buildRoadGeometry(n, T);
            Fixed at = p.getFx("at", Fixed::fromRatio(1, 2));
            int side = int(p.getInt("side", 1));
            if (p.has("point")) {
                Fixed along, lat;
                roadProject(g.edges[size_t(ei)], pt(p["point"], "point"), along, lat);
                at = g.edges[size_t(ei)].length.raw > 0 ? along / g.edges[size_t(ei)].length : Fixed::fromRatio(1, 2);
                if (!p.has("side")) side = lat < Fixed{} ? -1 : 1;
            }
            w->map.beginEdit();
            RoadEdge& e = n.edges[size_t(ei)];
            if (p.getBool("remove", false)) {
                if (e.bays.empty()) throw std::runtime_error("this edge has no bus stop bays");
                size_t best = 0;
                for (size_t k = 1; k < e.bays.size(); ++k) if (fxAbs(e.bays[k].at - at) < fxAbs(e.bays[best].at - at)) best = k;
                e.bays.erase(e.bays.begin() + long(best));
                commit(n, T, "Остановка удалена");
            } else {
                RoadBay b;
                b.at = fxClamp(at, Fixed{}, Fixed::fromInt(1));
                b.side = int8_t(side < 0 ? -1 : 1);
                if (p.has("length")) b.length = p.getFx("length");
                if (p.has("taper")) b.taper = p.getFx("taper");
                if (p.has("width")) b.width = p.getFx("width");
                b.shelter = p.getBool("shelter", true);
                e.bays.push_back(b);
                commit(n, T, "Остановка с карманом");
            }
            return summary(n, T, buildRoadGeometry(n, T));
        });
    add("road_split",
        "Split a road edge into two at a point ('at' [x, z]) or Bezier parameter 't' (0..1); returns the new node.",
        R"({"properties": {"edge": {"type": "integer"}, "at": {"type": "array"}, "t": {"type": "number"}}, "required": ["edge"]})",
        [w, commit](const Json& p) {
            needProject(*w);
            const auto T = roads::types(*w);
            RoadNetwork n = roads::read(w->map, T);
            const int ei = n.edgeIndex(int32_t(p.getInt("edge", -1)));
            if (ei < 0) throw std::runtime_error("no road edge");
            const Fixed t = p.has("at") ? roads::nearestT(n, ei, pt(p["at"], "at")) : p.getFx("t", Fixed::fromRatio(1, 2));
            if (t <= Fixed::fromRatio(1, 100) || t >= Fixed::fromRatio(99, 100)) throw std::runtime_error("too close to an end");
            w->map.beginEdit();
            const int32_t node = roads::splitEdge(n, ei, t);
            commit(n, T, "Дорога разделена");
            Json out = summary(n, T, buildRoadGeometry(n, T));
            out.set("node", Json::number(int64_t(node)));
            return out;
        });
    add("road_delete",
        "Delete road edges ('edges': ids) and / or nodes with their edges ('nodes': ids); 'all': true clears the network.",
        R"({"properties": {"edges": {"type": "array"}, "nodes": {"type": "array"}, "all": {"type": "boolean"}}})",
        [w, commit](const Json& p) {
            needProject(*w);
            const auto T = roads::types(*w);
            RoadNetwork n = roads::read(w->map, T);
            w->map.beginEdit();
            if (p.getBool("all", false)) n = RoadNetwork{};
            for (auto& v : p["edges"].items()) roads::deleteEdge(n, int32_t(v.integer()));
            for (auto& v : p["nodes"].items()) roads::deleteNode(n, int32_t(v.integer()));
            commit(n, T, "Дороги удалены");
            return summary(n, T, buildRoadGeometry(n, T));
        });
}

} // namespace aiverse::editor
