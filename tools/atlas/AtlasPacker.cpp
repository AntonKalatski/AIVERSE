#include "AtlasPacker.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <optional>
#include <tuple>

#include "engine/atlas/AtlasFormat.h"

namespace aiverse::atlas {

// ------------------------------------------------------------------ MaxRects (BSSF)
MaxRects::MaxRects(int width, int height) : w_(width), h_(height) { free_.push_back({0, 0, width, height}); }

bool MaxRects::insert(int w, int h, bool allowRotate, PackRect& out, bool& rotated) {
    // Score: (short side leftover, long side leftover, y, x) — lexicographic, fully deterministic.
    std::tuple<int, int, int, int> best{1 << 30, 1 << 30, 1 << 30, 1 << 30};
    bool found = false;
    for (const PackRect& f : free_) {
        for (int rot = 0; rot < (allowRotate && w != h ? 2 : 1); ++rot) {
            int rw = rot ? h : w, rh = rot ? w : h;
            if (rw > f.w || rh > f.h) continue;
            int dw = f.w - rw, dh = f.h - rh;
            std::tuple<int, int, int, int> score{std::min(dw, dh), std::max(dw, dh), f.y, f.x};
            if (score < best) { best = score; out = {f.x, f.y, rw, rh}; rotated = rot != 0; found = true; }
        }
    }
    if (found) place(out);
    return found;
}

void MaxRects::place(const PackRect& r) {
    // Free rects are kept maximal (none contains another). Split pieces are subsets of the rect
    // they came from, so only new pieces can be redundant: prune them against everything else.
    std::vector<PackRect> keep, fresh;
    keep.reserve(free_.size() + 4);
    for (const PackRect& f : free_) {
        if (r.x >= f.x + f.w || r.x + r.w <= f.x || r.y >= f.y + f.h || r.y + r.h <= f.y) { keep.push_back(f); continue; }
        if (r.x > f.x) fresh.push_back({f.x, f.y, r.x - f.x, f.h});
        if (r.x + r.w < f.x + f.w) fresh.push_back({r.x + r.w, f.y, f.x + f.w - (r.x + r.w), f.h});
        if (r.y > f.y) fresh.push_back({f.x, f.y, f.w, r.y - f.y});
        if (r.y + r.h < f.y + f.h) fresh.push_back({f.x, r.y + r.h, f.w, f.y + f.h - (r.y + r.h)});
    }
    auto inside = [](const PackRect& a, const PackRect& b) { return a.x >= b.x && a.y >= b.y && a.x + a.w <= b.x + b.w && a.y + a.h <= b.y + b.h; };
    size_t untouched = keep.size();
    for (size_t i = 0; i < fresh.size(); ++i) {
        bool redundant = false;
        for (size_t k = 0; k < untouched && !redundant; ++k) redundant = inside(fresh[i], keep[k]);
        // among new pieces, drop i if contained in j (equal pieces: keep the first one)
        for (size_t j = 0; j < fresh.size() && !redundant; ++j)
            if (j != i && inside(fresh[i], fresh[j]) && (!inside(fresh[j], fresh[i]) || j < i)) redundant = true;
        if (!redundant) keep.push_back(fresh[i]);
    }
    free_ = std::move(keep);
}

// ------------------------------------------------------------------ tight shapes
long long RowMask::area() const {
    long long a = 0;
    for (int i = 0; i < count(); ++i) a += std::max(0, rows[size_t(i) * 2 + 1] - rows[size_t(i) * 2]);
    return a;
}

namespace {

// Chebyshev growth by k: every row becomes the hull of the rows within k, widened by k (a
// superset of the exact dilation; convex shapes lose nothing).
RowMask grow(const RowMask& m, int k) {
    if (k <= 0) return m;
    RowMask o;
    o.y0 = m.y0 - k;
    const int n = m.count();
    o.rows.assign(size_t(n + 2 * k) * 2, 0);
    for (int r = 0; r < n + 2 * k; ++r) {
        int lo = 1 << 30, hi = -(1 << 30);
        for (int q = std::max(0, r - 2 * k); q <= std::min(n - 1, r); ++q) {
            int a = m.rows[size_t(q) * 2], b = m.rows[size_t(q) * 2 + 1];
            if (a < b) { lo = std::min(lo, a); hi = std::max(hi, b); }
        }
        if (lo < hi) { o.rows[size_t(r) * 2] = lo - k; o.rows[size_t(r) * 2 + 1] = hi + k; }
    }
    return o;
}

// content (sx, sy) -> turned clockwise (h-1-sy, sx), h = content height
RowMask rotateCw(const RowMask& m, int h) {
    int lo = 1 << 30, hi = -(1 << 30);
    for (int r = 0; r < m.count(); ++r)
        if (m.rows[size_t(r) * 2] < m.rows[size_t(r) * 2 + 1]) { lo = std::min(lo, m.rows[size_t(r) * 2]); hi = std::max(hi, m.rows[size_t(r) * 2 + 1]); }
    RowMask o;
    if (lo >= hi) return o;
    o.y0 = lo;
    o.rows.assign(size_t(hi - lo) * 2, 0);
    std::vector<int> mn(size_t(hi - lo), 1 << 30), mx(size_t(hi - lo), -(1 << 30));
    for (int r = 0; r < m.count(); ++r) {
        const int sy = m.y0 + r, x = h - 1 - sy;
        for (int sx = m.rows[size_t(r) * 2]; sx < m.rows[size_t(r) * 2 + 1]; ++sx) {
            size_t i = size_t(sx - lo);
            mn[i] = std::min(mn[i], x);
            mx[i] = std::max(mx[i], x + 1);
        }
    }
    for (size_t i = 0; i < mn.size(); ++i)
        if (mn[i] < mx[i]) { o.rows[i * 2] = mn[i]; o.rows[i * 2 + 1] = mx[i]; }
    return o;
}

int tightGrow(const PackItem& it, const PackSettings& s) { return std::max(s.extrude, it.mesh.empty() ? 0 : 1); }

} // namespace

RowMask writtenMask(const PackItem& it, const PackSettings& s, bool rotated) {
    RowMask m;
    m.y0 = 0;
    if (it.mesh.empty()) {
        m.rows.reserve(size_t(it.h) * 2);
        for (int y = 0; y < it.h; ++y) { m.rows.push_back(0); m.rows.push_back(it.w); }
    } else {
        atlas::meshRows(it.mesh, it.w, it.h, m.rows);
    }
    m = grow(m, tightGrow(it, s));
    return rotated ? rotateCw(m, it.h) : m;
}

TightBin::TightBin(int width, int height, int padding) : w_(width), h_(height), words_((width + 63) / 64), pad_(padding) {
    bits_.assign(size_t(words_) * size_t(std::max(0, height)), 0);
    run_.assign(size_t(std::max(0, height)), width);
}

int TightBin::nextRun(int row, int from, int len) const {
    const uint64_t* r = bits_.data() + size_t(row) * size_t(words_);
    auto bit = [&](int x) { return (r[x >> 6] >> (x & 63)) & 1; };
    // next bit with value `v` at or after x (w_ when none), a word at a time
    auto next = [&](int x, int v) {
        while (x < w_) {
            const int wi = x >> 6;
            uint64_t word = v ? r[wi] : ~r[wi];
            word &= ~uint64_t(0) << (x & 63);
            if (word) return std::min(w_, (wi << 6) + std::countr_zero(word));
            x = (wi + 1) << 6;
        }
        return w_;
    };
    int x = from;
    while (x < w_) {
        const int s = bit(x) ? next(x, 0) : x;
        if (s >= w_) return -1;
        const int e = next(s, 1);
        if (e - s >= len) return s;
        x = e;
    }
    return -1;
}

int TightBin::longestRun(int row) const {
    const uint64_t* r = bits_.data() + size_t(row) * size_t(words_);
    int best = 0, cur = 0;
    for (int wi = 0; wi < words_; ++wi) {
        const int n = std::min(64, w_ - wi * 64);
        const uint64_t v = r[wi];
        if (v == 0) { cur += n; continue; }
        for (int b = 0; b < n; ++b) {
            if (v >> b & 1) { best = std::max(best, cur); cur = 0; }
            else ++cur;
        }
    }
    return std::max(best, cur);
}

bool TightBin::lastSet(int row, int x0, int x1, int& last) const {
    const uint64_t* r = bits_.data() + size_t(row) * size_t(words_);
    for (int wi = (x1 - 1) >> 6; wi >= (x0 >> 6); --wi) {
        uint64_t v = r[wi];
        const int lo = wi << 6;
        if (x1 - lo < 64) v &= (uint64_t(1) << (x1 - lo)) - 1;
        if (x0 > lo) v &= ~((uint64_t(1) << (x0 - lo)) - 1);
        if (v) { last = lo + 63 - std::countl_zero(v); return true; }
    }
    return false;
}

bool TightBin::find(const RowMask& W, const RowMask& T, int& outX, int& outY, int maxBottom) const {
    int minA = 1 << 30, maxB = -(1 << 30), first = -1, lastRow = -1;
    for (int r = 0; r < W.count(); ++r)
        if (W.rows[size_t(r) * 2] < W.rows[size_t(r) * 2 + 1]) {
            minA = std::min(minA, W.rows[size_t(r) * 2]);
            maxB = std::max(maxB, W.rows[size_t(r) * 2 + 1]);
            if (first < 0) first = r;
            lastRow = r;
        }
    if (first < 0) return false;
    const int xmin = -minA, xmax = w_ - maxB, ymin = -(W.y0 + first);
    const int ymax = std::min(h_ - (W.y0 + lastRow + 1), maxBottom - 1 - (W.y0 + W.count()));
    if (xmin > xmax || ymin > ymax) return false;
    // rows by width, widest first: the first ones reject most positions
    std::vector<int> order;
    for (int r = first; r <= lastRow; ++r) if (W.rows[size_t(r) * 2] < W.rows[size_t(r) * 2 + 1]) order.push_back(r);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return W.rows[size_t(a) * 2 + 1] - W.rows[size_t(a) * 2] > W.rows[size_t(b) * 2 + 1] - W.rows[size_t(b) * 2]; });
    for (int y = ymin; y <= ymax; ++y) {
        bool full = false;   // a row of the shape longer than every free run of its page row
        for (size_t k = 0; k < order.size() && !full; ++k) {
            const int r = order[k];
            full = W.rows[size_t(r) * 2 + 1] - W.rows[size_t(r) * 2] > run_[size_t(y + W.y0 + r)];
        }
        if (full) continue;
        int x = xmin, hint = 0;
        while (x <= xmax) {
            bool hit = false;
            for (int k = 0; k < T.count() && !hit; ++k) {
                const int r = (hint + k) % T.count();
                const int py = y + T.y0 + r;
                if (py < 0 || py >= h_) continue;
                const int a = std::max(0, x + T.rows[size_t(r) * 2]), b = std::min(w_, x + T.rows[size_t(r) * 2 + 1]);
                int last;
                if (a < b && lastSet(py, a, b, last)) {
                    // past the obstacle, to the next free run that can hold this row (shortened by
                    // what the page edges may clip off the padding)
                    const int need = std::max(1, T.rows[size_t(r) * 2 + 1] - T.rows[size_t(r) * 2] - 2 * pad_);
                    const int s = nextRun(py, last + 1, need);
                    x = s < 0 ? xmax + 1 : std::max(last + 1, s) - T.rows[size_t(r) * 2];
                    hint = r;
                    hit = true;
                }
            }
            if (!hit) { outX = x; outY = y; return true; }
        }
    }
    return false;
}

void TightBin::place(const RowMask& W, int x, int y) {
    for (int r = 0; r < W.count(); ++r) {
        const int a = x + W.rows[size_t(r) * 2], b = x + W.rows[size_t(r) * 2 + 1], py = y + W.y0 + r;
        if (a >= b) continue;
        uint64_t* row = bits_.data() + size_t(py) * size_t(words_);
        for (int px = a; px < b; ++px) row[px >> 6] |= uint64_t(1) << (px & 63);
        run_[size_t(py)] = longestRun(py);
    }
}

// ------------------------------------------------------------------ pages
int footprint(int size, const PackSettings& s) {
    int f = size + 2 * s.extrude + s.padding;
    int a = std::max(1, s.align);
    return (f + a - 1) / a * a;
}

namespace {

struct Shape {            // tight packing: written texels and the padding test area, per orientation
    RowMask written[2], test[2];
    int boxW[2] = {0, 0}, boxH[2] = {0, 0};
};

struct Ctx {
    const std::vector<PackItem>& items;
    const PackSettings& s;
    std::vector<Shape> shapes;   // tight only
    long long beat = -1;         // tight, one page: only layouts smaller than this (the rectangle layout) count
};

void boxOf(const RowMask& m, int& w, int& h) {
    int lo = 1 << 30, hi = -(1 << 30), y0 = -1, y1 = -1;
    for (int r = 0; r < m.count(); ++r)
        if (m.rows[size_t(r) * 2] < m.rows[size_t(r) * 2 + 1]) {
            lo = std::min(lo, m.rows[size_t(r) * 2]); hi = std::max(hi, m.rows[size_t(r) * 2 + 1]);
            if (y0 < 0) y0 = r;
            y1 = r;
        }
    w = lo < hi ? hi - lo : 0;
    h = y0 < 0 ? 0 : y1 - y0 + 1;
}

void prepareShapes(Ctx& c) {
    if (!c.s.tight) return;
    c.shapes.resize(c.items.size());
    for (size_t i = 0; i < c.items.size(); ++i) {
        Shape& sh = c.shapes[i];
        for (int rot = 0; rot < (c.items[i].allowRotate ? 2 : 1); ++rot) {
            sh.written[rot] = writtenMask(c.items[i], c.s, rot != 0);
            sh.test[rot] = grow(sh.written[rot], c.s.padding);
            boxOf(sh.written[rot], sh.boxW[rot], sh.boxH[rot]);
        }
    }
}

bool fits(const Ctx& c, size_t i, int pageW, int pageH) {
    if (c.s.tight) {
        const Shape& sh = c.shapes[i];
        return (sh.boxW[0] <= pageW && sh.boxH[0] <= pageH) || (c.items[i].allowRotate && sh.boxW[1] <= pageW && sh.boxH[1] <= pageH);
    }
    int fw = footprint(c.items[i].w, c.s), fh = footprint(c.items[i].h, c.s);
    int bw = pageW + c.s.padding, bh = pageH + c.s.padding;
    return (fw <= bw && fh <= bh) || (c.items[i].allowRotate && fh <= bw && fw <= bh);
}

// One page being filled: rectangles (MaxRects over page + padding) or shapes (bit rows).
class Bin {
public:
    Bin(const Ctx& c, int w, int h) {
        if (c.s.tight) tight_.emplace(w, h, c.s.padding);
        else rects_.emplace(w + c.s.padding, h + c.s.padding);
    }
    bool insert(const Ctx& c, size_t i, Placement& p) {
        const PackItem& it = c.items[i];
        if (rects_) {
            PackRect r;
            bool rot = false;
            if (!rects_->insert(footprint(it.w, c.s), footprint(it.h, c.s), it.allowRotate, r, rot)) return false;
            p.x = r.x + c.s.extrude;
            p.y = r.y + c.s.extrude;
            p.rotated = rot;
            return true;
        }
        const Shape& sh = c.shapes[i];
        bool found = false;
        std::tuple<int, int> best{1 << 30, 1 << 30};
        for (int rot = 0; rot < (it.allowRotate ? 2 : 1); ++rot) {
            int x, y;
            if (!tight_->find(sh.written[rot], sh.test[rot], x, y, found ? std::get<0>(best) : 1 << 30)) continue;
            std::tuple<int, int> score{y + sh.written[rot].y0 + sh.written[rot].count(), x};   // lowest bottom edge, then leftmost
            if (score < best) { best = score; p.x = x; p.y = y; p.rotated = rot != 0; found = true; }
        }
        if (found) tight_->place(sh.written[p.rotated ? 1 : 0], p.x, p.y);
        return found;
    }

private:
    std::optional<MaxRects> rects_;
    std::optional<TightBin> tight_;
};

// Offline order: big first, then by area, then by path.
void sortForPacking(const Ctx& c, std::vector<size_t>& idx) {
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        const PackItem &A = c.items[a], &B = c.items[b];
        int ma = std::max(A.w, A.h), mb = std::max(B.w, B.h);
        if (ma != mb) return ma > mb;
        long long aa = 1LL * A.w * A.h, ab = 1LL * B.w * B.h;
        if (aa != ab) return aa > ab;
        return A.key < B.key;
    });
}

bool tryPack(const Ctx& c, std::vector<size_t> idx, int w, int h, std::vector<Placement>& out) {
    sortForPacking(c, idx);
    Bin bin(c, w, h);
    out.assign(c.items.size(), Placement{});
    for (size_t i : idx) if (!bin.insert(c, i, out[i])) return false;
    return true;
}

std::vector<int> pageSizes(const PackSettings& s) {
    std::vector<int> v;
    for (int x = std::max(4, s.minPage); x < s.maxPage; x *= 2) v.push_back(x);
    v.push_back(s.maxPage);
    return v;
}

// Smallest page that holds `idx`; returns false if even maxPage x maxPage fails.
// Tight pages: strip packing. For each candidate width the shapes are packed lowest-first into a
// page as tall as allowed; the page is then cut below the lowest shape. The smallest page wins
// (ties: the squarer one). Failing trials (the expensive kind for a bit-row search) never happen.
bool shrinkTight(const Ctx& c, const std::vector<size_t>& idx, int& outW, int& outH, std::vector<Placement>& out) {
    long long area = 0;
    int minW = 1;
    for (size_t i : idx) {
        area += c.shapes[i].written[0].area();
        const Shape& sh = c.shapes[i];
        minW = std::max(minW, c.items[i].allowRotate ? std::min(sh.boxW[0], sh.boxW[1]) : sh.boxW[0]);
    }
    auto roundSide = [&](int v) {
        v = std::max(v, std::max(4, c.s.minPage));
        if (c.s.pow2) { int p = 4; while (p < v) p *= 2; return p; }
        return (v + 3) / 4 * 4;
    };
    std::vector<int> widths;
    for (int w : pageSizes(c.s)) {
        widths.push_back(w);
        if (!c.s.pow2)   // between powers of two too: 5/8, 3/4, 7/8 of the next one
            for (int k : {5, 6, 7}) { int m = roundSide(w * 2 * k / 8); if (m > w && m < c.s.maxPage && m < w * 2) widths.push_back(m); }
    }
    std::sort(widths.begin(), widths.end());
    widths.erase(std::unique(widths.begin(), widths.end()), widths.end());
    long long best = c.beat;
    int bestW = 0, bestH = 0;
    std::vector<Placement> pl;
    // near-square pages only: a strip much wider or taller than sqrt(area) wastes its last row band
    const double side = std::sqrt(double(area));
    for (int w : widths) {
        if (w < minW || (w > minW && (w < side * 0.7 || w > side * 1.6))) continue;
        // no page of this width can beat the best: even a perfect fill needs area / w rows
        const long long lower = 1LL * w * roundSide(int(std::min<long long>(c.s.maxPage, (area + w - 1) / w)));
        if (best >= 0 && lower > best) continue;
        // a page taller than best / w cannot win: such a trial stops at the first shape below it
        const int limit = best >= 0 ? int(std::min<long long>(c.s.maxPage, best / w)) : c.s.maxPage;
        if (!tryPack(c, idx, w, limit, pl)) continue;
        int used = 1;
        for (size_t i : idx) {
            const RowMask& m = c.shapes[i].written[pl[i].rotated ? 1 : 0];
            used = std::max(used, pl[i].y + m.y0 + m.count());
        }
        const int h = roundSide(used);
        if (h > c.s.maxPage) continue;
        const long long a = 1LL * w * h;
        if (best < 0 || a < best || (a == best && bestW > 0 && std::max(w, h) < std::max(bestW, bestH))) {
            best = a; bestW = w; bestH = h; out = pl;
        }
    }
    if (bestW == 0) return false;
    outW = bestW; outH = bestH;
    return true;
}

bool shrink(const Ctx& c, const std::vector<size_t>& idx, int& outW, int& outH, std::vector<Placement>& out) {
    if (c.s.tight) return shrinkTight(c, idx, outW, outH, out);
    long long area = 0;
    int minW = 1, minH = 1;
    const int slack = c.s.tight ? 0 : c.s.padding;   // rect bins are page + padding wide
    for (size_t i : idx) {
        int a, b;
        if (c.s.tight) {
            area += c.shapes[i].written[0].area();   // written shapes never overlap
            a = c.shapes[i].boxW[0]; b = c.shapes[i].boxH[0];
        } else {
            area += 1LL * footprint(c.items[i].w, c.s) * footprint(c.items[i].h, c.s);
            a = footprint(c.items[i].w, c.s) - c.s.padding; b = footprint(c.items[i].h, c.s) - c.s.padding;
        }
        if (!c.items[i].allowRotate) { minW = std::max(minW, a); minH = std::max(minH, b); }
        else { minW = std::max(minW, std::min(a, b)); minH = std::max(minH, std::min(a, b)); }
    }
    std::vector<std::pair<int, int>> cand;
    auto sizes = pageSizes(c.s);
    for (int w : sizes)
        for (int h : sizes)
            if (w >= minW && h >= minH && 1LL * (w + slack) * (h + slack) >= area) cand.push_back({w, h});
    std::stable_sort(cand.begin(), cand.end(), [](auto a, auto b) {
        long long A = 1LL * a.first * a.second, B = 1LL * b.first * b.second;
        if (A != B) return A < B;
        int ma = std::max(a.first, a.second), mb = std::max(b.first, b.second);
        if (ma != mb) return ma < mb;
        return a.first > b.first; // wide before tall
    });
    bool found = false;
    for (auto [w, h] : cand)
        if (tryPack(c, idx, w, h, out)) { outW = w; outH = h; found = true; break; }
    if (!found) return false;
    if (!c.s.pow2) {
        // Multiples of 4: trim height, then width, below the power of two that fits.
        std::vector<Placement> tmp;
        auto reduce = [&](int& dim, int fixedOther, bool isH, int minDim) {
            int lo = std::max(4, (minDim + 3) / 4 * 4), hi = dim;
            while (lo < hi) {
                int mid = (lo + hi) / 2 / 4 * 4;
                if (mid < lo) mid = lo;
                bool ok = isH ? tryPack(c, idx, fixedOther, mid, tmp) : tryPack(c, idx, mid, fixedOther, tmp);
                if (ok) { hi = mid; } else { lo = mid + 4; }
            }
            dim = hi;
        };
        reduce(outH, outW, true, minH);
        reduce(outW, outH, false, minW);
        if (!tryPack(c, idx, outW, outH, out)) return false; // cannot happen: last size was verified
    }
    return true;
}

} // namespace

namespace {
PackedPages packImpl(const std::vector<PackItem>& items, const PackSettings& s, long long beat = -1) {
    PackedPages res;
    Ctx c{items, s, {}, -1};
    prepareShapes(c);
    res.placements.assign(items.size(), Placement{});
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].w <= 0 || items[i].h <= 0 || !fits(c, i, s.maxPage, s.maxPage)) {
            res.error = "'" + items[i].key + "' does not fit into a " + std::to_string(s.maxPage) + " page";
            return res;
        }
    if (items.empty()) { res.ok = true; return res; }

    std::vector<size_t> all(items.size());
    for (size_t i = 0; i < all.size(); ++i) all[i] = i;

    std::vector<std::vector<size_t>> groups;
    std::vector<Placement> tmp;
    if (tryPack(c, all, s.maxPage, s.maxPage, tmp)) {
        groups.push_back(all);
    } else {
        // Several pages: whole clusters first-fit into pages (online), so things drawn together
        // share a page; a cluster larger than a page is spread item by item.
        std::map<std::string, std::vector<size_t>> clusters;
        for (size_t i = 0; i < items.size(); ++i) clusters[items[i].cluster].push_back(i);
        std::vector<Bin> bins;
        auto fresh = [&]() { bins.emplace_back(c, s.maxPage, s.maxPage); groups.emplace_back(); };
        for (auto& [name, members] : clusters) {
            std::vector<size_t> m = members;
            sortForPacking(c, m);
            bool placed = false;
            for (size_t b = 0; b <= bins.size() && !placed; ++b) {
                if (b == bins.size()) fresh();
                Bin trial = bins[b];
                Placement p;
                bool ok = true;
                for (size_t i : m) if (!trial.insert(c, i, p)) { ok = false; break; }
                if (ok) { bins[b] = trial; groups[b].insert(groups[b].end(), m.begin(), m.end()); placed = true; }
                else if (b + 1 == bins.size() && groups[b].empty()) break; // cluster bigger than a page
            }
            if (placed) continue;
            if (!groups.empty() && groups.back().empty()) { bins.pop_back(); groups.pop_back(); }
            for (size_t i : m) {
                Placement p;
                bool ok = false;
                for (size_t b = 0; b < bins.size() && !ok; ++b)
                    if (bins[b].insert(c, i, p)) { groups[b].push_back(i); ok = true; }
                if (!ok) { fresh(); bins.back().insert(c, i, p); groups.back().push_back(i); }
            }
        }
    }

    if (groups.size() == 1) c.beat = beat;
    for (auto& g : groups) {
        int w = s.maxPage, h = s.maxPage;
        std::vector<Placement> pl;
        if (!shrink(c, g, w, h, pl)) {
            if (c.beat >= 0) { res.error = "tight: no layout smaller than the rectangles"; return res; }
            // Offline re-pack lost to the online layout: redo the online order at full size.
            Bin bin(c, s.maxPage, s.maxPage);
            pl.assign(items.size(), Placement{});
            for (size_t i : g)
                if (!bin.insert(c, i, pl[i])) { res.error = "internal: page re-pack failed"; return res; }
            w = h = s.maxPage;
        }
        int page = int(res.pages.size());
        res.pages.push_back({0, 0, w, h});
        for (size_t i : g) { res.placements[i] = pl[i]; res.placements[i].page = page; }
    }
    res.ok = true;
    return res;
}

} // namespace

PackedPages packRects(const std::vector<PackItem>& items, const PackSettings& s) {
    if (!s.tight) return packImpl(items, s);
    // rectangles one texel apart hold every mesh's written ring as well: a valid tight layout
    PackSettings r = s;
    r.tight = false;
    r.extrude = std::max(s.extrude, 1);
    PackedPages rects = packImpl(items, r);
    long long rectArea = 0;
    if (rects.ok) for (auto& pg : rects.pages) rectArea += 1LL * pg.w * pg.h;
    PackedPages tight = packImpl(items, s, rects.ok && rects.pages.size() == 1 ? rectArea : -1);
    if (!tight.ok) return rects;
    if (!rects.ok) return tight;
    auto area = [](const PackedPages& p) { long long a = 0; for (auto& pg : p.pages) a += 1LL * pg.w * pg.h; return a; };
    return area(tight) < area(rects) || (area(tight) == area(rects) && tight.pages.size() < rects.pages.size()) ? tight : rects;
}

} // namespace aiverse::atlas
