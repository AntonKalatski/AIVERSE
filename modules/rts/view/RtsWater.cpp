#include "RtsWater.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace aiverse::game::water {

namespace {

float num(const Json& j, const char* key, float def) { return j.has(key) ? float(j[key].toDouble(def)) : def; }
std::string str(const Json& j, const char* key, const std::string& def) { return j.has(key) ? j.getStr(key, def) : def; }
// [a, b] or a single number for both
void range(const Json& j, const char* key, float& lo, float& hi) {
    if (!j.has(key)) return;
    const Json& v = j[key];
    if (v.isArray() && v.size() >= 2) { lo = float(v[size_t(0)].toDouble(lo)); hi = float(v[size_t(1)].toDouble(hi)); }
    else if (v.isNumber()) lo = hi = float(v.toDouble(lo));
    if (hi < lo) std::swap(lo, hi);
}
Json rangeJson(float a, float b) { Json r = Json::array(); r.push(Json::number(double(a), 2)); r.push(Json::number(double(b), 2)); return r; }

ShoreKind parseKind(const std::string& s, const std::string& id) {
    if (s == "embankment") return ShoreKind::Embankment;
    if (s == "cliff") return ShoreKind::Cliff;
    if (s == "beach") return ShoreKind::Beach;
    if (s == "natural") return ShoreKind::Natural;
    throw std::runtime_error("shore type '" + id + "': unknown kind '" + s + "' (embankment | cliff | beach | natural)");
}

void readType(ShoreType& t, const Json& j) {
    if (j.has("name")) t.name = j.getStr("name");
    if (j.has("kind")) t.kind = parseKind(j.getStr("kind"), t.id);
    const Json& wall = j["wall"];
    t.wallHeight = num(wall, "height", t.wallHeight);
    t.wallSurface = str(wall, "surface", t.wallSurface);
    const Json& edge = j["edge"];
    t.parapet = str(edge, "parapet", t.parapet);
    if (t.parapet != "none" && t.parapet != "railing" && t.parapet != "stone")
        throw std::runtime_error("shore type '" + t.id + "': edge.parapet '" + t.parapet + "' (none | railing | stone)");
    t.parapetHeight = num(edge, "height", t.parapetHeight);
    t.postSpacing = std::max(0.5f, num(edge, "post", t.postSpacing));
    const Json& prom = j["promenade"];
    t.promenade = std::max(0.0f, num(prom, "width", t.promenade));
    t.promenadeSurface = str(prom, "surface", t.promenadeSurface);
    t.stairs = num(j, "stairs", t.stairs);
    range(j, "height", t.heightMin, t.heightMax);
    range(j, "slope", t.slopeMin, t.slopeMax);
    t.slopeMin = std::clamp(t.slopeMin, 20.0f, 89.0f); t.slopeMax = std::clamp(t.slopeMax, t.slopeMin, 89.0f);
    t.top = std::max(0.0f, num(j, "topWidth", t.top));
    if (t.kind == ShoreKind::Cliff) { t.rockSurface = str(j, "surface", t.rockSurface); t.topSurface = str(j, "top", t.topSurface); }
    t.rubble = j.has("rubble") ? j.getBool("rubble") : t.rubble;
    t.foam = j.has("foam") ? j.getBool("foam") : t.foam;
    range(j, "width", t.widthMin, t.widthMax);
    if (t.kind == ShoreKind::Beach || t.kind == ShoreKind::Natural) t.sandSurface = str(j, "surface", t.sandSurface);
    t.surf = j.has("surf") ? j.getBool("surf") : t.surf;
    if (j.has("props")) {
        if (!j["props"].isArray()) throw std::runtime_error("shore type '" + t.id + "': props must be an array");
        t.props.clear();
        for (auto& p : j["props"].items()) {
            ShoreProp sp;
            sp.shape = p.getStr("shape"); sp.model = p.getStr("model");
            if (sp.shape.empty() && sp.model.empty()) throw std::runtime_error("shore type '" + t.id + "': a prop needs \"shape\" or \"model\"");
            sp.spacing = std::max(1.0f, num(p, "spacing", sp.spacing));
            sp.phase = num(p, "phase", sp.phase); sp.offset = num(p, "offset", sp.offset); sp.scale = num(p, "scale", sp.scale);
            t.props.push_back(sp);
        }
    }
    if (j.has("textures")) for (auto& [k, v] : j["textures"].members()) if (v.isString()) t.textures[k] = v.str();
}

std::vector<ShoreType> builtins() {
    std::vector<ShoreType> out;
    ShoreType e; e.id = "embankment"; e.name = "Набережная"; e.kind = ShoreKind::Embankment;
    e.props = {{"lamp", "", 25, 4, -1, 1}, {"bench", "", 40, 12, -1, 1}, {"bollard", "", 30, 20, -1, 1}};
    out.push_back(e);
    ShoreType s = e; s.id = "embankment_stone"; s.name = "Набережная с каменным парапетом"; s.parapet = "stone"; s.parapetHeight = 0.8f;
    out.push_back(s);
    ShoreType c; c.id = "cliff"; c.name = "Отвесные скалы"; c.kind = ShoreKind::Cliff; c.props = {};
    out.push_back(c);
    ShoreType b; b.id = "beach"; b.name = "Пляж"; b.kind = ShoreKind::Beach;
    b.props = {{"umbrella", "", 16, 6, -1, 1}, {"lounger", "", 16, 7.5f, -1, 1}, {"lifeguard", "", 200, 60, -1, 1}};
    out.push_back(b);
    ShoreType n; n.id = "bank"; n.name = "Травяной берег"; n.kind = ShoreKind::Natural; n.widthMin = 6; n.widthMax = 10;
    n.sandSurface = "soil"; n.surf = false; n.props = {};
    out.push_back(n);
    for (auto& t : out) t.builtin = true;
    return out;
}

// Catmull-Rom through the points, sampled about every `step` cells
std::vector<std::array<float, 3>> smooth(const std::vector<std::array<float, 3>>& p, float step) {
    std::vector<std::array<float, 3>> out;
    if (p.size() < 2) return p;
    for (size_t i = 0; i + 1 < p.size(); ++i) {
        const auto& p0 = p[i ? i - 1 : 0]; const auto& p1 = p[i]; const auto& p2 = p[i + 1]; const auto& p3 = p[std::min(i + 2, p.size() - 1)];
        const float len = std::hypot(p2[0] - p1[0], p2[1] - p1[1]);
        const int n = std::max(1, int(std::ceil(len / step)));
        for (int k = 0; k < n; ++k) {
            const float t = float(k) / float(n), t2 = t * t, t3 = t2 * t;
            std::array<float, 3> q{};
            for (int c = 0; c < 3; ++c)
                q[size_t(c)] = 0.5f * (2 * p1[size_t(c)] + (-p0[size_t(c)] + p2[size_t(c)]) * t + (2 * p0[size_t(c)] - 5 * p1[size_t(c)] + 4 * p2[size_t(c)] - p3[size_t(c)]) * t2 +
                                      (-p0[size_t(c)] + 3 * p1[size_t(c)] - 3 * p2[size_t(c)] + p3[size_t(c)]) * t3);
            out.push_back(q);
        }
    }
    out.push_back(p.back());
    return out;
}

void finishBank(Bank& b, float cm, int w, int h) {
    // drop points off the map, metres along, smoothed normals
    std::vector<BankPoint> in;
    for (auto& p : b.pts) if (p.x >= -0.5f && p.z >= -0.5f && p.x <= float(w) + 0.5f && p.z <= float(h) + 0.5f) in.push_back(p);
    auto edge = [&](const BankPoint& p) { return p.x < 1.0f || p.z < 1.0f || p.x > float(w) - 1.0f || p.z > float(h) - 1.0f; };
    b.openStart = !b.pts.empty() && edge(b.pts.front());
    b.openEnd = !b.pts.empty() && edge(b.pts.back());
    b.pts = std::move(in);
    float s = 0;
    for (size_t i = 0; i < b.pts.size(); ++i) {
        if (i) s += std::hypot(b.pts[i].x - b.pts[i - 1].x, b.pts[i].z - b.pts[i - 1].z) * cm;
        b.pts[i].s = s;
    }
    b.length = s;
}

float segDist(float px, float pz, float ax, float az, float bx, float bz, float& t) {
    const float dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
    t = l2 > 0 ? std::clamp(((px - ax) * dx + (pz - az) * dz) / l2, 0.0f, 1.0f) : 0;
    return std::hypot(px - (ax + dx * t), pz - (az + dz * t));
}

} // namespace

const char* kindName(Kind k) { return k == Kind::Strait ? "strait" : k == Kind::Sea ? "sea" : "river"; }
const char* shoreKindName(ShoreKind k) {
    switch (k) { case ShoreKind::Cliff: return "cliff"; case ShoreKind::Beach: return "beach"; case ShoreKind::Natural: return "natural"; default: return "embankment"; }
}

int ShoreTypes::find(const std::string& id) const {
    for (size_t i = 0; i < list.size(); ++i) if (list[i].id == id) return int(i);
    return -1;
}
int ShoreTypes::defaultOf(ShoreKind k) const {
    for (size_t i = 0; i < list.size(); ++i) if (list[i].kind == k) return int(i);
    return -1;
}

ShoreTypes loadShoreTypes(const Json& j) {
    if (!j.isNull() && !j.isObject()) throw std::runtime_error("shoreTypes: an object of shore type id -> definition");
    ShoreTypes T;
    if (j.isNull() || j.getBool("builtin", true)) T.list = builtins();
    if (j.isObject()) T.cellMeters = std::max(0.1f, num(j, "cellMeters", 4));
    if (j.isObject())
        for (auto& [id, v] : j.members()) {
            if (id == "cellMeters" || id == "builtin") continue;
            if (!v.isObject()) throw std::runtime_error("shore type '" + id + "': an object");
            int at = T.find(id);
            ShoreType t;
            if (at >= 0) t = T.list[size_t(at)];
            else if (v.has("extends")) {
                const int base = T.find(v.getStr("extends"));
                if (base < 0) throw std::runtime_error("shore type '" + id + "': unknown base '" + v.getStr("extends") + "'");
                t = T.list[size_t(base)];
            } else if (!v.has("kind")) throw std::runtime_error("shore type '" + id + "': needs \"kind\" or \"extends\"");
            t.id = id; t.builtin = false;
            if (t.name.empty()) t.name = id;
            readType(t, v);
            if (at >= 0) T.list[size_t(at)] = t; else T.list.push_back(t);
        }
    return T;
}

Json shoreTypeToJson(const ShoreType& t) {
    Json r = Json::object();
    r.set("id", Json::string(t.id)); r.set("name", Json::string(t.name)); r.set("kind", Json::string(shoreKindName(t.kind)));
    if (t.kind == ShoreKind::Embankment) {
        Json wall = Json::object(); wall.set("height", Json::number(double(t.wallHeight), 2)); wall.set("surface", Json::string(t.wallSurface)); r.set("wall", wall);
        Json e = Json::object(); e.set("parapet", Json::string(t.parapet)); e.set("height", Json::number(double(t.parapetHeight), 2)); e.set("post", Json::number(double(t.postSpacing), 2)); r.set("edge", e);
        Json p = Json::object(); p.set("width", Json::number(double(t.promenade), 2)); p.set("surface", Json::string(t.promenadeSurface)); r.set("promenade", p);
        r.set("stairs", Json::number(double(t.stairs), 1));
    } else if (t.kind == ShoreKind::Cliff) {
        r.set("height", rangeJson(t.heightMin, t.heightMax)); r.set("slope", rangeJson(t.slopeMin, t.slopeMax));
        r.set("topWidth", Json::number(double(t.top), 2)); r.set("surface", Json::string(t.rockSurface)); r.set("top", Json::string(t.topSurface));
        r.set("rubble", Json::fromBool(t.rubble)); r.set("foam", Json::fromBool(t.foam));
    } else {
        r.set("width", rangeJson(t.widthMin, t.widthMax)); r.set("surface", Json::string(t.sandSurface)); r.set("surf", Json::fromBool(t.surf));
    }
    Json props = Json::array();
    for (auto& p : t.props) {
        Json q = Json::object();
        if (!p.shape.empty()) q.set("shape", Json::string(p.shape));
        if (!p.model.empty()) q.set("model", Json::string(p.model));
        q.set("spacing", Json::number(double(p.spacing), 2));
        props.push(q);
    }
    r.set("props", props);
    r.set("builtin", Json::fromBool(t.builtin));
    return r;
}

int Network::index(int id) const {
    for (size_t i = 0; i < bodies.size(); ++i) if (bodies[i].id == id) return int(i);
    return -1;
}

Network parseNetwork(const Json& j, const ShoreTypes& T) {
    Network n;
    if (j.isNull()) return n;
    if (!j.isObject()) throw std::runtime_error("waterNetwork: an object with \"bodies\"");
    const Json& bodies = j["bodies"];
    if (!bodies.isNull() && !bodies.isArray()) throw std::runtime_error("waterNetwork.bodies: an array");
    for (auto& b : bodies.items()) {
        Body B;
        B.id = int(b.getInt("id", int64_t(n.bodies.size()) + 1));
        const std::string who = "water body " + std::to_string(B.id);
        if (n.index(B.id) >= 0) throw std::runtime_error(who + ": the id is used twice");
        const std::string k = b.getStr("kind", "river");
        if (k == "strait") B.kind = Kind::Strait; else if (k == "river") B.kind = Kind::River; else if (k == "sea") B.kind = Kind::Sea;
        else throw std::runtime_error(who + ": unknown kind '" + k + "' (strait | river | sea)");
        const Json& pts = B.kind == Kind::Sea ? (b.has("shore") ? b["shore"] : b["points"]) : b["points"];
        if (!pts.isArray() || pts.size() < 2) throw std::runtime_error(who + (B.kind == Kind::Sea ? ": \"shore\" needs at least two points [x, z]" : ": \"points\" needs at least two points [x, z, width]"));
        for (auto& p : pts.items()) {
            if (!p.isArray() || p.size() < 2) throw std::runtime_error(who + ": a point is [x, z" + std::string(B.kind == Kind::Sea ? "]" : ", width]"));
            std::array<float, 3> q{float(p[size_t(0)].toDouble()), float(p[size_t(1)].toDouble()), p.size() > 2 ? float(p[size_t(2)].toDouble()) : 30.0f};
            if (B.kind != Kind::Sea && q[2] <= 0) throw std::runtime_error(who + ": width must be over 0 metres");
            B.points.push_back(q);
        }
        if (B.kind == Kind::Sea) {
            B.side = b.getStr("side", "south");
            if (B.side != "north" && B.side != "south" && B.side != "east" && B.side != "west")
                throw std::runtime_error(who + ": side '" + B.side + "' (north | south | east | west)");
        }
        B.depth = std::max(0.5f, num(b, "depth", B.kind == Kind::Sea ? 6.0f : 3.0f));
        const Json& sh = b["shores"];
        B.autoShores = sh.isNull() || (sh.isString() && sh.str() == "auto");
        if (sh.isString() && sh.str() != "auto") throw std::runtime_error(who + ": shores is \"auto\" or a list of segments");
        if (sh.isArray()) {
            B.autoShores = true;   // a list: hand-drawn entries are kept, auto fills the gaps
            for (auto& s : sh.items()) {
                Segment g;
                g.side = int(s.getInt("side", 1)) < 0 ? -1 : 1;
                g.from = num(s, "from", 0); g.to = num(s, "to", 0);
                g.type = s.getStr("type");
                g.generated = s.getBool("auto", false);
                if (T.find(g.type) < 0) throw std::runtime_error(who + ": unknown shore type '" + g.type + "' (water_types lists them)");
                if (g.to <= g.from) throw std::runtime_error(who + ": a shore segment needs to > from");
                B.shores.push_back(g);
            }
        }
        n.bodies.push_back(std::move(B));
    }
    if (j.has("tiles")) for (auto& [k, v] : j["tiles"].members()) if (v.isString() && v.str().size() == 1) n.tiles[k] = v.str();
    n.baked = j["baked"];
    return n;
}

Json networkToJson(const Network& n) {
    Json r = Json::object();
    Json bodies = Json::array();
    for (auto& B : n.bodies) {
        Json b = Json::object();
        b.set("id", Json::number(int64_t(B.id)));
        b.set("kind", Json::string(kindName(B.kind)));
        if (B.kind == Kind::Sea) b.set("side", Json::string(B.side));
        Json pts = Json::array();
        for (auto& p : B.points) {
            Json q = Json::array(); q.push(Json::number(double(p[0]), 2)); q.push(Json::number(double(p[1]), 2));
            if (B.kind != Kind::Sea) q.push(Json::number(double(p[2]), 1));
            pts.push(q);
        }
        b.set(B.kind == Kind::Sea ? "shore" : "points", pts);
        b.set("depth", Json::number(double(B.depth), 1));
        if (B.shores.empty()) b.set("shores", Json::string("auto"));
        else {
            Json sh = Json::array();
            for (auto& s : B.shores) {
                Json q = Json::object();
                q.set("side", Json::number(int64_t(s.side)));
                q.set("from", Json::number(double(s.from), 1)); q.set("to", Json::number(double(s.to), 1));
                q.set("type", Json::string(s.type));
                if (s.generated) q.set("auto", Json::fromBool(true));
                sh.push(q);
            }
            b.set("shores", sh);
        }
        bodies.push(b);
    }
    r.set("bodies", bodies);
    if (!n.tiles.empty()) { Json t = Json::object(); for (auto& [k, v] : n.tiles) t.set(k, Json::string(v)); r.set("tiles", t); }
    if (!n.baked.isNull()) r.set("baked", n.baked);
    return r;
}

Geometry buildGeometry(const Network& n, int w, int h, float cm) {
    Geometry g;
    for (size_t bi = 0; bi < n.bodies.size(); ++bi) {
        const Body& B = n.bodies[bi];
        const auto c = smooth(B.points, 0.25f);
        if (c.size() < 2) { g.issues.push_back("water body " + std::to_string(B.id) + ": too short"); continue; }
        if (B.kind == Kind::Sea) {
            float wx = 0, wz = 0;
            if (B.side == "south") wz = 1; else if (B.side == "north") wz = -1; else if (B.side == "east") wx = 1; else wx = -1;
            Bank bank; bank.body = int(bi); bank.side = 1;
            for (size_t i = 0; i < c.size(); ++i) {
                const auto& a = c[i ? i - 1 : 0]; const auto& b = c[std::min(i + 1, c.size() - 1)];
                float tx = b[0] - a[0], tz = b[1] - a[1]; const float l = std::hypot(tx, tz); if (l <= 0) continue;
                tx /= l; tz /= l;
                float nx = -tz, nz = tx;
                if (nx * wx + nz * wz > 0) { nx = -nx; nz = -nz; }   // landward = away from the water's edge of the map
                bank.pts.push_back({c[i][0], c[i][1], nx, nz, 0});
            }
            finishBank(bank, cm, w, h);
            if (bank.pts.size() >= 2) g.banks.push_back(std::move(bank));
            continue;
        }
        for (int side : {-1, 1}) {
            Bank bank; bank.body = int(bi); bank.side = side;
            for (size_t i = 0; i < c.size(); ++i) {
                const auto& a = c[i ? i - 1 : 0]; const auto& b = c[std::min(i + 1, c.size() - 1)];
                float tx = b[0] - a[0], tz = b[1] - a[1]; const float l = std::hypot(tx, tz); if (l <= 0) continue;
                tx /= l; tz /= l;
                const float rx = -tz, rz = tx, hw = c[i][2] * 0.5f / cm;   // right of the direction (z down)
                bank.pts.push_back({c[i][0] + rx * hw * float(side), c[i][1] + rz * hw * float(side), rx * float(side), rz * float(side), 0});
            }
            finishBank(bank, cm, w, h);
            if (bank.pts.size() >= 2) g.banks.push_back(std::move(bank));
        }
    }
    return g;
}

const Bank* findBank(const Geometry& g, int bi, int side) {
    for (auto& b : g.banks) if (b.body == bi && b.side == side) return &b;
    return nullptr;
}

int shoreAt(const Network& n, const ShoreTypes& T, int bi, int side, float s) {
    if (bi < 0 || size_t(bi) >= n.bodies.size()) return -1;
    for (auto& g : n.bodies[size_t(bi)].shores) if (g.side == side && s >= g.from && s < g.to) return T.find(g.type);
    return -1;
}

std::vector<uint8_t> waterCells(const Network& n, const Geometry&, int w, int h, float cm) {
    std::vector<uint8_t> out(size_t(w) * size_t(h), 0);
    for (auto& B : n.bodies) {
        const auto c = smooth(B.points, 0.25f);
        if (c.size() < 2) continue;
        if (B.kind == Kind::Sea) {
            const bool ns = B.side == "south" || B.side == "north";
            const int across = ns ? w : h, along = ns ? h : w;
            for (int a = 0; a < across; ++a) {
                const float u = float(a) + 0.5f;
                // shore position at u: interpolate over the polyline segments that span u (the one nearest the land wins)
                bool found = false; float v = 0;
                for (size_t i = 0; i + 1 < c.size(); ++i) {
                    const float u0 = ns ? c[i][0] : c[i][1], u1 = ns ? c[i + 1][0] : c[i + 1][1];
                    if ((u < std::min(u0, u1)) || (u > std::max(u0, u1)) || u0 == u1) continue;
                    const float t = (u - u0) / (u1 - u0);
                    const float vv = (ns ? c[i][1] : c[i][0]) + ((ns ? c[i + 1][1] : c[i + 1][0]) - (ns ? c[i][1] : c[i][0])) * t;
                    const bool far = B.side == "south" || B.side == "east";
                    if (!found || (far ? vv > v : vv < v)) v = vv;
                    found = true;
                }
                if (!found) {   // past the ends of the shore line: carry the end on
                    const auto& e = (std::fabs((ns ? c.front()[0] : c.front()[1]) - u) < std::fabs((ns ? c.back()[0] : c.back()[1]) - u)) ? c.front() : c.back();
                    v = ns ? e[1] : e[0];
                }
                for (int b = 0; b < along; ++b) {
                    const float cv = float(b) + 0.5f;
                    const bool wet = (B.side == "south" || B.side == "east") ? cv + 0.75f > v : cv - 0.75f < v;
                    if (wet) out[ns ? size_t(b) * size_t(w) + size_t(a) : size_t(a) * size_t(w) + size_t(b)] = 1;
                }
            }
            continue;
        }
        for (size_t i = 0; i + 1 < c.size(); ++i) {
            const float hw = std::max(c[i][2], c[i + 1][2]) * 0.5f / cm;
            const int x0 = std::max(0, int(std::floor(std::min(c[i][0], c[i + 1][0]) - hw - 1))), x1 = std::min(w - 1, int(std::ceil(std::max(c[i][0], c[i + 1][0]) + hw + 1)));
            const int z0 = std::max(0, int(std::floor(std::min(c[i][1], c[i + 1][1]) - hw - 1))), z1 = std::min(h - 1, int(std::ceil(std::max(c[i][1], c[i + 1][1]) + hw + 1)));
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x) {
                    float t;
                    const float d = segDist(float(x) + 0.5f, float(z) + 0.5f, c[i][0], c[i][1], c[i + 1][0], c[i + 1][1], t);
                    const float r = (c[i][2] + (c[i + 1][2] - c[i][2]) * t) * 0.5f / cm;
                    if (d <= r + 0.75f) out[size_t(z) * size_t(w) + size_t(x)] = 1;
                }
        }
    }
    return out;
}

void markOpen(Geometry& g, const std::vector<uint8_t>& wet, int w, int h) {
    for (auto& b : g.banks)
        for (auto& p : b.pts) {
            int hits = 0;
            for (float d : {1.8f, 2.6f}) {
                const int x = int(std::floor(p.x + p.nx * d)), z = int(std::floor(p.z + p.nz * d));
                if (x >= 0 && z >= 0 && x < w && z < h && wet[size_t(z) * size_t(w) + size_t(x)]) ++hits;
            }
            p.open = hits == 2;
        }
}

float hash01(int a, int b, int c) {
    uint32_t v = uint32_t(a) * 374761393u + uint32_t(b) * 668265263u + uint32_t(c) * 2246822519u;
    v = (v ^ (v >> 13)) * 1274126177u;
    return float((v ^ (v >> 16)) & 0xffffu) / 65535.0f;
}

float noise1(float s, int seed) {
    const int i = int(std::floor(s));
    const float f = s - float(i), u = f * f * (3 - 2 * f);
    return hash01(i, seed, 7) + (hash01(i + 1, seed, 7) - hash01(i, seed, 7)) * u;
}

float cliffHeight(const ShoreType& t, int body, int side, float s, float from, float to, bool taperStart, bool taperEnd) {
    const float n = noise1(s / 23.0f, body * 31 + side + 5) * 0.7f + noise1(s / 7.0f, body * 31 + side + 11) * 0.3f;
    float h = t.heightMin + (t.heightMax - t.heightMin) * n;
    const float ramp = 14;
    float k = 1;
    if (taperStart) k = std::min(k, std::clamp((s - from) / ramp, 0.0f, 1.0f));
    if (taperEnd) k = std::min(k, std::clamp((to - s) / ramp, 0.0f, 1.0f));
    k = k * k * (3 - 2 * k);
    return std::max(0.8f, h * (0.12f + 0.88f * k));
}

float beachWidth(const ShoreType& t, int body, int side, float s) {
    return t.widthMin + (t.widthMax - t.widthMin) * noise1(s / 31.0f, body * 17 + side + 3);
}

float footprintCells(const ShoreType& t, float cm) {
    switch (t.kind) {
    case ShoreKind::Embankment: return t.promenade / cm;
    case ShoreKind::Cliff: return (t.heightMax / std::tan(t.slopeMin * 3.14159265f / 180.0f) + t.top + t.heightMax * 1.4f) / cm;
    default: return t.widthMax / cm;
    }
}

void classify(Network& n, const Geometry& g, const ShoreTypes& T, const std::function<bool(int, int)>& built, int w, int h) {
    const float cm = T.cellMeters;
    const int tEmb = T.defaultOf(ShoreKind::Embankment), tCliff = T.defaultOf(ShoreKind::Cliff), tBeach = T.defaultOf(ShoreKind::Beach),
              tBank = T.defaultOf(ShoreKind::Natural);
    for (size_t bi = 0; bi < n.bodies.size(); ++bi) {
        Body& B = n.bodies[bi];
        if (!B.autoShores) continue;
        std::vector<Segment> keep;
        for (auto& s : B.shores) if (!s.generated) keep.push_back(s);
        std::vector<Segment> made;
        for (int side : {-1, 1}) {
            const Bank* bank = findBank(g, int(bi), side);
            if (!bank || bank->length < 1) continue;
            const float L = bank->length, step = 10;
            const int ns = std::max(1, int(std::ceil(L / step)));
            // per 10 m sample: 'E' town, 'C' headland / map end, 'O' open land
            std::vector<char> cat(size_t(ns), 'O');
            size_t pi = 0;
            for (int k = 0; k < ns; ++k) {
                const float s = (float(k) + 0.5f) * L / float(ns);
                while (pi + 1 < bank->pts.size() && bank->pts[pi + 1].s < s) ++pi;
                const BankPoint& p = bank->pts[pi];
                bool town = false;
                const float reach = (B.kind == Kind::River ? 40.0f : 60.0f) / cm;
                for (float d = 0.5f; d <= reach && !town; d += 0.5f)
                    for (float lat = -1.5f; lat <= 1.5f && !town; lat += 1.5f) {
                        const int x = int(std::floor(p.x + p.nx * d - p.nz * lat)), z = int(std::floor(p.z + p.nz * d + p.nx * lat));
                        if (x >= 0 && z >= 0 && x < w && z < h && built(x, z)) town = true;
                    }
                if (town) { cat[size_t(k)] = 'E'; continue; }
                if (B.kind != Kind::Sea) continue;
                if ((bank->openStart && s < 50) || (bank->openEnd && s > L - 50)) { cat[size_t(k)] = 'C'; continue; }
                // headland: the shore bulges into the water against the chord over +-60 m
                auto at = [&](float q) {
                    q = std::clamp(q, 0.0f, L);
                    size_t j = 0; while (j + 1 < bank->pts.size() && bank->pts[j + 1].s < q) ++j;
                    return bank->pts[j];
                };
                const BankPoint a = at(s - 60), b = at(s + 60);
                const float mx = (a.x + b.x) * 0.5f, mz = (a.z + b.z) * 0.5f;
                const float out = -((p.x - mx) * p.nx + (p.z - mz) * p.nz) * cm;   // metres towards the water
                if (out > 6) cat[size_t(k)] = 'C';
            }
            // runs
            struct Run { char c; float a, b; };
            std::vector<Run> runs;
            for (int k = 0; k < ns; ++k) {
                const float a = float(k) * L / float(ns), b = float(k + 1) * L / float(ns);
                if (!runs.empty() && runs.back().c == cat[size_t(k)]) runs.back().b = b; else runs.push_back({cat[size_t(k)], a, b});
            }
            std::vector<Segment> segs;
            auto push = [&](int type, float a, float b) {
                if (type < 0) return;
                if (!segs.empty() && segs.back().type == T.list[size_t(type)].id && std::fabs(segs.back().to - a) < 0.01f) { segs.back().to = b; return; }
                segs.push_back({side, a, b, T.list[size_t(type)].id, true});
            };
            int part = 0;
            for (auto& r : runs) {
                if (B.kind == Kind::Strait) { push(tEmb, r.a, r.b); continue; }
                if (B.kind == Kind::River) { push(r.c == 'E' ? tEmb : (tBank >= 0 ? tBank : tEmb), r.a, r.b); continue; }
                if (r.c == 'E') { push(tEmb, r.a, r.b); continue; }
                if (r.c == 'C') { push(tCliff, r.a, r.b); continue; }
                // open land: beaches with cliffs between them
                const float len = r.b - r.a;
                const int parts = std::max(1, int(std::lround(len / 110.0f)));
                float a = r.a;
                const bool beachFirst = hash01(B.id, side, int(r.a)) > 0.35f;
                for (int k = 0; k < parts; ++k) {
                    const float jitter = parts > 1 && k + 1 < parts ? (hash01(B.id, side, part + 101) - 0.5f) * 0.5f : 0;
                    const float b = k + 1 == parts ? r.b : a + len / float(parts) * (1 + jitter);
                    const bool beach = ((k % 2) == 0) == beachFirst;
                    push(beach ? tBeach : tCliff, a, b);
                    a = b; ++part;
                }
            }
            // no segment shorter than 30 m: merge into the longer neighbour
            for (bool again = true; again && segs.size() > 1;) {
                again = false;
                for (size_t i = 0; i < segs.size(); ++i) {
                    if (segs[i].to - segs[i].from >= 30) continue;
                    const size_t j = i == 0 ? 1 : i + 1 == segs.size() ? i - 1 : ((segs[i - 1].to - segs[i - 1].from) >= (segs[i + 1].to - segs[i + 1].from) ? i - 1 : i + 1);
                    segs[j].from = std::min(segs[j].from, segs[i].from); segs[j].to = std::max(segs[j].to, segs[i].to);
                    segs.erase(segs.begin() + long(i));
                    for (size_t k = 1; k < segs.size();)
                        if (segs[k].type == segs[k - 1].type) { segs[k - 1].to = segs[k].to; segs.erase(segs.begin() + long(k)); } else ++k;
                    again = true;
                    break;
                }
            }
            // hand-drawn segments win: cut them out of the auto ones
            for (auto& s : segs) {
                std::vector<std::pair<float, float>> pieces{{s.from, s.to}};
                for (auto& k : keep) {
                    if (k.side != side) continue;
                    std::vector<std::pair<float, float>> next;
                    for (auto [a, b] : pieces) {
                        if (k.to <= a || k.from >= b) { next.push_back({a, b}); continue; }
                        if (k.from > a) next.push_back({a, k.from});
                        if (k.to < b) next.push_back({k.to, b});
                    }
                    pieces = next;
                }
                for (auto [a, b] : pieces) if (b - a > 0.5f) made.push_back({side, a, b, s.type, true});
            }
        }
        B.shores = keep;
        B.shores.insert(B.shores.end(), made.begin(), made.end());
        std::sort(B.shores.begin(), B.shores.end(), [](const Segment& a, const Segment& b) { return a.side != b.side ? a.side < b.side : a.from < b.from; });
    }
}

std::vector<uint8_t> cliffCells(const Network& n, const Geometry& g, const ShoreTypes& T, int w, int h) {
    std::vector<uint8_t> out(size_t(w) * size_t(h), 0);
    const float cm = T.cellMeters;
    for (auto& bank : g.banks) {
        const Body& B = n.bodies[size_t(bank.body)];
        for (auto& seg : B.shores) {
            if (seg.side != bank.side) continue;
            const int ti = T.find(seg.type);
            if (ti < 0 || T.list[size_t(ti)].kind != ShoreKind::Cliff) continue;
            const ShoreType& t = T.list[size_t(ti)];
            for (auto& p : bank.pts) {
                if (p.s < seg.from || p.s > seg.to || p.open) continue;
                const float H = cliffHeight(t, B.id, bank.side, p.s, seg.from, seg.to, true, true);
                const float run = H / std::tan(t.slopeMax * 3.14159265f / 180.0f);
                const float reach = (run + t.top + H * 1.4f) / cm;
                for (float d = 0.25f; d <= reach; d += 0.25f) {
                    const int x = int(std::floor(p.x + p.nx * d)), z = int(std::floor(p.z + p.nz * d));
                    if (x >= 0 && z >= 0 && x < w && z < h) out[size_t(z) * size_t(w) + size_t(x)] = 1;
                }
            }
        }
    }
    return out;
}

} // namespace aiverse::game::water
