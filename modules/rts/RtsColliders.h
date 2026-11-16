// Static colliders: map entities whose type has "collider": true do not become units. They are circles
// (move.radius, the type's layer) in a bucket grid built once when the map is set up, so thousands of
// bollards, benches and flower beds cost nothing until something moves next to them. Read by local
// avoidance (static ORCA obstacles, no stepping into one), separation (push out), top-down heroes and
// vehicles (solid like any body that never moves).
//
// Derived from the map: rebuilt by setup, never saved or hashed (the map's data hash covers it).
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include "../../engine/core/Fixed.h"

namespace aiverse::game {

struct StaticColliders {
    struct Item { Vec2 p; Fixed r; uint8_t layer = 0; };
    static constexpr int kBucket = 2;   // cells per bucket side
    std::vector<Item> items;            // map order
    Fixed maxR{};
    int w = 0, h = 0;                   // buckets
    std::vector<int32_t> head, next;

    bool empty() const { return items.empty(); }
    void clear() { items.clear(); head.clear(); next.clear(); maxR = Fixed{}; w = h = 0; }
    void add(Vec2 p, Fixed r, uint8_t layer) { items.push_back({p, r, layer}); maxR = fxMax(maxR, r); }

    void build(int cellsW, int cellsH) {
        w = std::max(1, (cellsW + kBucket - 1) / kBucket);
        h = std::max(1, (cellsH + kBucket - 1) / kBucket);
        head.assign(size_t(w) * size_t(h), -1);
        next.assign(items.size(), -1);
        for (size_t i = items.size(); i-- > 0;) {   // each bucket lists its items in ascending map order
            const size_t b = bucket(items[i].p);
            next[i] = head[b];
            head[b] = int32_t(i);
        }
    }
    size_t bucket(Vec2 p) const {
        const int x = std::clamp(int(p.x.floorInt()) / kBucket, 0, w - 1), y = std::clamp(int(p.y.floorInt()) / kBucket, 0, h - 1);
        return size_t(y) * size_t(w) + size_t(x);
    }
    // Every collider of `layer` whose circle comes within `reach` of `c`: fn(index, item). Deterministic order
    // (buckets row by row, map order inside a bucket).
    template<class F> void query(Vec2 c, Fixed reach, uint8_t layer, F&& fn) const {
        if (items.empty()) return;
        const Fixed span = reach + maxR;
        const int x0 = std::clamp(int((c.x - span).floorInt()) / kBucket, 0, w - 1), x1 = std::clamp(int((c.x + span).floorInt()) / kBucket, 0, w - 1);
        const int y0 = std::clamp(int((c.y - span).floorInt()) / kBucket, 0, h - 1), y1 = std::clamp(int((c.y + span).floorInt()) / kBucket, 0, h - 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                for (int32_t i = head[size_t(y) * size_t(w) + size_t(x)]; i >= 0; i = next[size_t(i)]) {
                    const Item& it = items[size_t(i)];
                    if (!(it.layer & layer)) continue;
                    const Fixed d = reach + it.r;
                    if ((it.p - c).lengthSq() > d * d) continue;
                    fn(size_t(i), it);
                }
    }
    // Push needed to move a circle (c, r) out of every collider it overlaps (summed, not capped).
    Vec2 pushOut(Vec2 c, Fixed r, uint8_t layer, Fixed deadZone = Fixed{}) const {
        Vec2 acc{};
        query(c, r, layer, [&](size_t, const Item& it) {
            Vec2 d = c - it.p;
            Fixed len = d.length();
            const Fixed overlap = r + it.r - len - deadZone;
            if (overlap.raw <= 0) return;
            if (len.raw == 0) { d = {Fixed::fromInt(1), Fixed{}}; len = Fixed::fromInt(1); }
            acc += d * (overlap / len);
        });
        return acc;
    }
    // Does the circle (c, r) overlap a collider?
    bool overlaps(Vec2 c, Fixed r, uint8_t layer) const {
        bool any = false;
        query(c, r, layer, [&](size_t, const Item& it) { const Fixed d = r + it.r; if ((it.p - c).lengthSq() < d * d) any = true; });
        return any;
    }
};

} // namespace aiverse::game
