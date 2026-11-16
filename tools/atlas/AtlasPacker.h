// Rectangle packing for texture atlases (host tools only). Pure geometry: no images, no I/O.
//
// MaxRects with the best-short-side-fit rule. Every item reserves a footprint of
//   (w + 2*extrude + padding) x (h + 2*extrude + padding), rounded up to `align`,
// and its content starts at (x + extrude, y + extrude). Bins are (page + padding) wide so the
// trailing padding of items touching the right/bottom page edge costs nothing.
//
// packRects() splits items into pages so that items sharing a `cluster` (e.g. all frames of one
// unit, one faction's folder) stay on the same page whenever the cluster fits on one page, then
// shrinks every page to the smallest size (power of two or multiple of 4) that still holds it.
// Results depend only on the input (stable sorts, explicit tie-breaks): reproducible builds.
//
// Tight packing (PackSettings::tight): items are shapes, not rectangles. An item with a mesh
// (convex polygon, AtlasMesh.h) occupies only the texels its polygon touches, grown by
// max(extrude, 1) (what the build writes: content + extrusion ring); an item without one occupies
// its extruded rectangle. Shapes stay `padding` texels apart (Chebyshev), so an image can sit in the
// empty corner of another one's rectangle. Placement: lowest, then leftmost position where the
// shape fits (bit rows per page, skipping over occupied runs); pages: strip packing over candidate
// widths; `align` is ignored (no mip groups). The rectangle layout of the same items is computed
// too and wins when it needs less page area, so tight is never worse than rectangles.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace aiverse::atlas {

struct PackRect { int x = 0, y = 0, w = 0, h = 0; };

// Texels of a shape, row by row: row y0 + i covers [rows[2i], rows[2i+1]) (empty when equal).
// Coordinates are relative to the content origin (rotated content: of the turned box).
struct RowMask {
    int y0 = 0;
    std::vector<int> rows;
    int count() const { return int(rows.size() / 2); }
    long long area() const;
};

// Bit-row occupancy of one page for tight packing.
class TightBin {
public:
    TightBin(int width, int height, int padding = 0);
    // lowest, then leftmost (x, y) where `written` stays inside the page and `test` (written grown
    // by the padding) meets nothing placed
    bool find(const RowMask& written, const RowMask& test, int& x, int& y, int maxBottom = 1 << 30) const;   // bottom edge < maxBottom
    void place(const RowMask& written, int x, int y);

private:
    bool lastSet(int row, int x0, int x1, int& last) const;
    int nextRun(int row, int from, int len) const;   // start of the first free run >= len at or after `from`, or -1
    int longestRun(int row) const;
    int w_, h_, words_, pad_;
    std::vector<uint64_t> bits_;
    std::vector<int> run_;    // longest free run per row (quick reject)
};

class MaxRects {
public:
    MaxRects(int width, int height);
    // Finds a place for a w x h rect (or h x w when rotation is allowed and fits better).
    bool insert(int w, int h, bool allowRotate, PackRect& out, bool& rotated);
    int width() const { return w_; }
    int height() const { return h_; }

private:
    void place(const PackRect& r);
    int w_, h_;
    std::vector<PackRect> free_;
};

struct PackSettings {
    int maxPage = 4096;     // page width/height limit
    int padding = 2;        // empty texels between neighbouring (extruded) items
    int extrude = 1;        // edge texels repeated around each item (filtering seams)
    int align = 1;          // item origins/footprints aligned to this (4 = block/mip friendly)
    bool pow2 = true;       // page sizes: powers of two, else multiples of 4
    int minPage = 16;
    bool tight = false;     // pack shapes (meshes) instead of rectangles, see above
};

struct PackItem {
    int w = 0, h = 0;               // content size
    bool allowRotate = false;
    std::string cluster;            // items drawn together (same folder)
    std::string key;                // stable tie-break (path)
    std::vector<float> mesh;        // tight packing: convex polygon in content pixels (x y ...), empty = rectangle
};

// What a tight item writes into its page (content + extrusion), unrotated or turned clockwise.
RowMask writtenMask(const PackItem& item, const PackSettings& s, bool rotated);

struct Placement {
    int page = -1;
    int x = 0, y = 0;               // content origin in the page (after extrusion)
    bool rotated = false;           // content stored turned 90 degrees clockwise
};

struct PackedPages {
    std::vector<PackRect> pages;    // only w/h used
    std::vector<Placement> placements; // parallel to the input items
    std::string error;
    bool ok = false;
};

int footprint(int size, const PackSettings& s);
PackedPages packRects(const std::vector<PackItem>& items, const PackSettings& s);

} // namespace aiverse::atlas
