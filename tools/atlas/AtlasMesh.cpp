#include "AtlasMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aiverse::atlas {

namespace {

struct P2 { double x, y; };
P2 operator-(P2 a, P2 b) { return {a.x - b.x, a.y - b.y}; }
P2 operator+(P2 a, P2 b) { return {a.x + b.x, a.y + b.y}; }
P2 operator*(P2 a, double k) { return {a.x * k, a.y * k}; }
double cross(P2 a, P2 b) { return a.x * b.y - a.y * b.x; }

struct I2 { int64_t x, y; };
int64_t cross(I2 o, I2 a, I2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

double signedArea(const std::vector<P2>& v) {
    double s = 0;
    for (size_t i = 0; i < v.size(); ++i) s += cross(v[i], v[(i + 1) % v.size()]);
    return s * 0.5;
}

// Andrew's monotone chain; counter-clockwise in x-right / y-down terms (positive signed area), no
// collinear points.
std::vector<I2> hull(std::vector<I2> p) {
    std::sort(p.begin(), p.end(), [](I2 a, I2 b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });
    p.erase(std::unique(p.begin(), p.end(), [](I2 a, I2 b) { return a.x == b.x && a.y == b.y; }), p.end());
    if (p.size() < 3) return p;
    std::vector<I2> h(p.size() * 2);
    size_t k = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        while (k >= 2 && cross(h[k - 2], h[k - 1], p[i]) <= 0) --k;
        h[k++] = p[i];
    }
    for (size_t i = p.size() - 1, t = k + 1; i-- > 0;) {
        while (k >= t && cross(h[k - 2], h[k - 1], p[i]) <= 0) --k;
        h[k++] = p[i];
    }
    h.resize(k - 1);
    return h;
}

std::vector<P2> clipRect(const std::vector<P2>& poly, double w, double h) {
    std::vector<P2> a = poly, b;
    for (int side = 0; side < 4 && !a.empty(); ++side) {
        auto value = [&](P2 p) { return side == 0 ? p.x : side == 1 ? w - p.x : side == 2 ? p.y : h - p.y; };   // >= 0 inside
        b.clear();
        for (size_t i = 0; i < a.size(); ++i) {
            P2 p = a[i], q = a[(i + 1) % a.size()];
            double vp = value(p), vq = value(q);
            if (vp >= 0) b.push_back(p);
            if ((vp >= 0) != (vq >= 0)) b.push_back(p + (q - p) * (vp / (vp - vq)));
        }
        a.swap(b);
    }
    return a;
}

// drops repeated and collinear corners (they would only cost vertices)
void simplify(std::vector<P2>& v, double eps) {
    bool changed = true;
    while (changed && v.size() >= 3) {
        changed = false;
        for (size_t i = 0; i < v.size() && v.size() >= 3; ++i) {
            P2 a = v[(i + v.size() - 1) % v.size()], b = v[i], c = v[(i + 1) % v.size()];
            P2 ab = b - a, bc = c - b;
            double len = std::sqrt(ab.x * ab.x + ab.y * ab.y) + std::sqrt(bc.x * bc.x + bc.y * bc.y);
            if (std::fabs(cross(ab, bc)) <= eps * std::max(len, 1e-9) || (std::fabs(ab.x) + std::fabs(ab.y)) <= eps) {
                v.erase(v.begin() + std::ptrdiff_t(i));
                changed = true;
                break;
            }
        }
    }
}

} // namespace

double polygonArea(const std::vector<float>& poly) {
    double s = 0;
    const size_t n = poly.size() / 2;
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        s += double(poly[2 * i]) * double(poly[2 * j + 1]) - double(poly[2 * j]) * double(poly[2 * i + 1]);
    }
    return std::fabs(s) * 0.5;
}

std::vector<float> buildMesh(const Image& img, int maxVertices, float minSaving) {
    const int W = img.width, H = img.height;
    if (W <= 0 || H <= 0) return {};
    maxVertices = std::clamp(maxVertices, 4, 32);
    // corners of the outermost covered pixel of every row: their hull is the hull of all covered pixels
    std::vector<I2> pts;
    for (int y = 0; y < H; ++y) {
        int x0 = -1, x1 = -1;
        for (int x = 0; x < W; ++x)
            if (img.px(x, y)[3] != 0) { if (x0 < 0) x0 = x; x1 = x; }
        if (x0 < 0) continue;
        for (int64_t px : {int64_t(x0), int64_t(x1) + 1}) { pts.push_back({px, y}); pts.push_back({px, y + 1}); }
    }
    std::vector<I2> h = hull(pts);
    if (h.size() < 3) return {};

    std::vector<P2> v;
    for (I2 p : h) v.push_back({double(p.x), double(p.y)});
    auto clippedArea = [&](const std::vector<P2>& poly) { return std::fabs(signedArea(clipRect(poly, W, H))); };
    auto clippedCount = [&](const std::vector<P2>& poly) { std::vector<P2> c = clipRect(poly, W, H); simplify(c, 1e-9); return int(c.size()); };
    // cut edges until the clipped polygon has few enough corners: removing edge b->c extends its
    // neighbours a->b and c->d to their meeting point (only where they converge)
    while (clippedCount(v) > maxVertices && v.size() > 3) {
        const size_t n = v.size();
        const double base = clippedArea(v);
        double best = 1e300;
        size_t bi = n;
        P2 bp{};
        for (size_t i = 0; i < n; ++i) {
            P2 a = v[(i + n - 1) % n], b = v[i], c = v[(i + 1) % n], d = v[(i + 2) % n];
            P2 d1 = b - a, d2 = d - c;
            double den = cross(d1, d2);
            if (den <= 1e-12) continue;   // parallel or diverging: they never meet outside
            double t = cross(c - b, d2) / den;
            if (t < 0) continue;
            P2 m = b + d1 * t;
            std::vector<P2> nv;
            for (size_t k = 0; k < n; ++k) {
                if (k == i) nv.push_back(m);
                else if (k != (i + 1) % n) nv.push_back(v[k]);
            }
            double added = clippedArea(nv) - base;
            if (added < best) { best = added; bi = i; bp = m; }
        }
        if (bi == n) break;
        std::vector<P2> nv;
        for (size_t k = 0; k < n; ++k) {
            if (k == bi) nv.push_back(bp);
            else if (k != (bi + 1) % n) nv.push_back(v[k]);
        }
        v.swap(nv);
    }
    std::vector<P2> r = clipRect(v, W, H);
    simplify(r, 1e-9);
    if (r.size() < 3 || std::fabs(signedArea(r)) > (1.0 - double(minSaving)) * double(W) * double(H)) return {};

    // safety margin: every edge pushed out by one mesh unit, then back inside the image
    const double eps = 1.0 / kMeshUnits;
    std::vector<P2> grown;
    const size_t n = r.size();
    for (size_t i = 0; i < n; ++i) {
        P2 a = r[(i + n - 1) % n], b = r[i], c = r[(i + 1) % n];
        auto outward = [](P2 p, P2 q) { P2 e = q - p; double l = std::sqrt(e.x * e.x + e.y * e.y); return P2{e.y / l, -e.x / l}; }; // positive signed area: outside is on the right of travel
        P2 n1 = outward(a, b), n2 = outward(b, c);
        P2 p1 = a + n1 * eps, p2 = b + n2 * eps, d1 = b - a, d2 = c - b;
        double den = cross(d1, d2);
        if (std::fabs(den) < 1e-12) { grown.push_back(b + n2 * eps); continue; }
        double t = cross(p2 - p1, d2) / den;
        grown.push_back(p1 + d1 * t);
    }
    grown = clipRect(grown, W, H);
    std::vector<I2> q;
    for (P2 p : grown)
        q.push_back({std::clamp<int64_t>(std::llround(p.x * kMeshUnits), 0, int64_t(W) * kMeshUnits),
                     std::clamp<int64_t>(std::llround(p.y * kMeshUnits), 0, int64_t(H) * kMeshUnits)});
    q = hull(q);   // rounding must not leave a dent: fans of a non-convex polygon overlap
    if (q.size() < 3) return {};
    // exact check: every corner of the covered pixels' hull lies inside (or on) the mesh
    for (I2 p : h) {
        I2 s{p.x * kMeshUnits, p.y * kMeshUnits};
        for (size_t i = 0; i < q.size(); ++i)
            if (cross(q[i], q[(i + 1) % q.size()], s) < 0) return {};
    }
    std::vector<float> out;
    for (I2 p : q) { out.push_back(float(p.x) / kMeshUnits); out.push_back(float(p.y) / kMeshUnits); }
    if (polygonArea(out) > (1.0 - double(minSaving) * 0.5) * double(W) * double(H)) return {};
    return out;
}

} // namespace aiverse::atlas
