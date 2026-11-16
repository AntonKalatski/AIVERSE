// Texture atlases (atlas agent): convex meshes, tight packing, framed sprite sheets, and the
// sprite shapes the 2D renderer draws from the pages (tools/atlas/AtlasMesh.h, AtlasPacker.h,
// engine/atlas/AtlasFormat.h, engine/render/SpriteBatch.h).
#include "engine/render/SpriteBatch.h"
#include "tools/atlas/AtlasMesh.h"
#include "tools/atlas/AtlasPacker.h"

namespace {
// shape 0 disc, 1 triangle (top-left half), 2 diagonal band, 3 L, 4 full, 5 one pixel; transparent
// pixels stay 0 so a rebuilt image compares equal
aiverse::atlas::Image atlasShape(int w, int h, int shape, uint32_t seed) {
    aiverse::atlas::Image img;
    img.width = w; img.height = h;
    img.rgba.assign(size_t(w) * size_t(h) * 4, 0);
    uint32_t s = seed * 2654435761u + 7;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float fx = (x + 0.5f) / float(w) - 0.5f, fy = (y + 0.5f) / float(h) - 0.5f;
            bool in = shape == 0 ? fx * fx + fy * fy < 0.2f : shape == 1 ? x * h + y * w < w * h
                    : shape == 2 ? std::abs(x * h - y * w) < w * h / 5 : shape == 3 ? (x < w / 3 || y > 2 * h / 3)
                    : shape == 4 ? true : (x == w / 2 && y == h / 3);
            if (!in) continue;
            s = s * 1664525u + 1013904223u;
            uint8_t* p = img.px(x, y);
            p[0] = uint8_t(s >> 24); p[1] = uint8_t(x * 9 + seed); p[2] = uint8_t(y * 3); p[3] = uint8_t(100 + (s >> 26));
        }
    return img;
}

bool meshConvex(const std::vector<float>& m) {
    const size_t n = m.size() / 2;
    int sign = 0;
    for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n, k = (i + 2) % n;
        const double c = double(m[2 * j] - m[2 * i]) * double(m[2 * k + 1] - m[2 * j + 1]) - double(m[2 * j + 1] - m[2 * i + 1]) * double(m[2 * k] - m[2 * j]);
        if (c == 0) continue;
        const int sg = c > 0 ? 1 : -1;
        if (sign && sg != sign) return false;
        sign = sg;
    }
    return sign != 0;
}
} // namespace

TEST(atlas_mesh_is_convex_and_covers_every_visible_pixel) {
    using namespace aiverse::atlas;
    const int sizes[][2] = {{40, 40}, {64, 24}, {17, 53}, {9, 9}};
    int meshed = 0;
    for (auto& sz : sizes)
        for (int shape = 0; shape < 6; ++shape) {
            aiverse::atlas::Image img = atlasShape(sz[0], sz[1], shape, uint32_t(shape * 31 + sz[0]));
            int ox, oy;
            aiverse::atlas::Image t = img;   // as the build sees it: trimmed
            {
                int x0 = img.width, yy0 = img.height, x1 = -1, yy1 = -1;
                for (int y = 0; y < img.height; ++y) for (int x = 0; x < img.width; ++x)
                    if (img.px(x, y)[3]) { x0 = std::min(x0, x); yy0 = std::min(yy0, y); x1 = std::max(x1, x); yy1 = std::max(yy1, y); }
                ox = x0; oy = yy0;
                t.width = x1 - x0 + 1; t.height = yy1 - yy0 + 1;
                t.rgba.assign(size_t(t.width) * size_t(t.height) * 4, 0);
                for (int y = 0; y < t.height; ++y) std::memcpy(t.px(0, y), img.px(ox, oy + y), size_t(t.width) * 4);
            }
            for (int maxV : {4, 8, 16}) {
                std::vector<float> m = buildMesh(t, maxV);
                if (shape == 4 || shape == 5) { CHECK(m.empty()); continue; }   // nothing to save on a rectangle
                if (m.empty()) continue;
                ++meshed;
                CHECK(meshConvex(m));
                CHECK(int(m.size() / 2) <= maxV + 4);
                bool inside = true;
                for (size_t i = 0; i < m.size(); i += 2) inside &= m[i] >= 0 && m[i] <= t.width && m[i + 1] >= 0 && m[i + 1] <= t.height;
                CHECK(inside);
                CHECK(polygonArea(m) < 0.95 * double(t.width) * double(t.height));
                std::vector<int> rows;
                meshRows(m, t.width, t.height, rows);
                bool covered = true;
                for (int y = 0; y < t.height; ++y)
                    for (int x = 0; x < t.width; ++x)
                        if (t.px(x, y)[3] && !(x >= rows[size_t(y) * 2] && x < rows[size_t(y) * 2 + 1])) covered = false;
                CHECK(covered);
                // more corners never cover more
                if (maxV == 16) { std::vector<float> m4 = buildMesh(t, 4); if (!m4.empty()) CHECK(polygonArea(m) <= polygonArea(m4) + 0.5); }
            }
        }
    CHECK(meshed >= 20);
    // a disc: an octagon covers far less than its square
    std::vector<float> disc = buildMesh(atlasShape(64, 64, 0, 1), 8);
    CHECK(!disc.empty() && polygonArea(disc) < 0.86 * 64 * 64);
}

TEST(atlas_tight_packing_keeps_shapes_apart_and_saves_area) {
    using namespace aiverse::atlas;
    std::vector<PackItem> items;
    for (int i = 0; i < 70; ++i) {
        const int w = 14 + (i * 37) % 50, h = 12 + (i * 23) % 44, shape = i % 5 == 4 ? 4 : i % 4;
        PackItem it;
        it.w = w; it.h = h;
        it.allowRotate = i % 3 == 0;
        it.cluster = "c" + std::to_string(i / 10);
        it.key = "k" + std::to_string(1000 + i);
        it.mesh = buildMesh(atlasShape(w, h, shape, uint32_t(i)), 8);
        items.push_back(it);
    }
    for (int pass = 0; pass < 2; ++pass) {
        PackSettings s;
        s.maxPage = pass == 0 ? 1024 : 160;   // pass 1: several pages
        s.padding = 2;
        s.extrude = 1;
        s.pow2 = false;
        PackSettings t = s;
        t.tight = true;
        PackedPages rect = packRects(items, s), tight = packRects(items, t), again = packRects(items, t);
        CHECK(rect.ok && tight.ok && again.ok);
        if (!rect.ok || !tight.ok) continue;
        bool same = tight.pages.size() == again.pages.size();
        for (size_t i = 0; same && i < items.size(); ++i)
            same = tight.placements[i].x == again.placements[i].x && tight.placements[i].y == again.placements[i].y &&
                   tight.placements[i].page == again.placements[i].page && tight.placements[i].rotated == again.placements[i].rotated;
        CHECK(same);   // reproducible
        long long rectArea = 0, tightArea = 0;
        for (auto& p : rect.pages) rectArea += 1LL * p.w * p.h;
        for (auto& p : tight.pages) tightArea += 1LL * p.w * p.h;
        if (pass == 0) { CHECK(tight.pages.size() == 1); CHECK(tightArea < rectArea); }
        else { CHECK(tight.pages.size() > 1); CHECK(tight.pages.size() <= rect.pages.size()); }
        // written shapes: inside their page, never closer than the padding to another one
        std::vector<std::vector<int>> owner(tight.pages.size());
        for (size_t p = 0; p < tight.pages.size(); ++p) owner[p].assign(size_t(tight.pages[p].w) * size_t(tight.pages[p].h), -1);
        bool inside = true, apart = true, rotOk = true;
        for (size_t i = 0; i < items.size(); ++i) {
            const Placement& pl = tight.placements[i];
            rotOk &= !pl.rotated || items[i].allowRotate;
            const PackRect& pg = tight.pages[size_t(pl.page)];
            RowMask m = writtenMask(items[i], t, pl.rotated);
            for (int r = 0; r < m.count(); ++r)
                for (int x = m.rows[size_t(r) * 2]; x < m.rows[size_t(r) * 2 + 1]; ++x) {
                    const int px = pl.x + x, py = pl.y + m.y0 + r;
                    if (px < 0 || py < 0 || px >= pg.w || py >= pg.h) { inside = false; continue; }
                    for (int dy = -t.padding; dy <= t.padding; ++dy)
                        for (int dx = -t.padding; dx <= t.padding; ++dx) {
                            const int qx = px + dx, qy = py + dy;
                            if (qx < 0 || qy < 0 || qx >= pg.w || qy >= pg.h) continue;
                            const int o = owner[size_t(pl.page)][size_t(qy) * size_t(pg.w) + size_t(qx)];
                            if (o >= 0 && o != int(i)) apart = false;
                        }
                    owner[size_t(pl.page)][size_t(py) * size_t(pg.w) + size_t(px)] = int(i);
                }
        }
        CHECK(inside);
        CHECK(apart);
        CHECK(rotOk);
    }
}

TEST(atlas_tight_build_rebuilds_images_through_their_meshes) {
    using namespace aiverse::atlas;
    std::vector<SourceImage> in;
    std::map<std::string, aiverse::atlas::Image> orig;
    for (int i = 0; i < 24; ++i) {
        const std::string p = "units/u" + std::to_string(i % 3) + "/s" + std::to_string(i) + ".png";
        // shapes inside a transparent margin: trim + mesh both matter
        aiverse::atlas::Image shape = atlasShape(20 + (i * 7) % 30, 18 + (i * 11) % 26, i % 4, uint32_t(i + 3));
        aiverse::atlas::Image img;
        img.width = shape.width + 6; img.height = shape.height + 4;
        img.rgba.assign(size_t(img.width) * size_t(img.height) * 4, 0);
        for (int y = 0; y < shape.height; ++y) std::memcpy(img.px(3, 1 + y), shape.px(0, y), size_t(shape.width) * 4);
        in.push_back(atlasSrc(p, img));
        orig[p] = img;
    }
    AtlasOutput loose, tight;
    CHECK(buildAtlas(in, atlasCfg(R"({"atlas": {"maxPage": 512, "files": {"units/*": {"rotate": true}}}})"), loose));
    CHECK(buildAtlas(in, atlasCfg(R"({"atlas": {"maxPage": 512, "files": {"units/*": {"tight": true, "rotate": true, "meshVertices": 8}}}})"), tight));
    uint64_t looseArea = 0, tightArea = 0;
    for (auto& p : loose.manifest.pages()) looseArea += uint64_t(p.width) * uint64_t(p.height);
    for (auto& p : tight.manifest.pages()) tightArea += uint64_t(p.width) * uint64_t(p.height);
    CHECK(tightArea < looseArea);
    int meshes = 0;
    bool back = true;
    for (auto& [path, img] : orig) {
        const Region* r = tight.manifest.find(path);
        CHECK(r != nullptr);
        if (!r) continue;
        meshes += !r->mesh.empty();
        aiverse::atlas::Image out;
        back &= extractRegion(tight.pageImages[size_t(r->page)], *r, out) && sameImage(out, img);
    }
    CHECK(back);
    CHECK(meshes >= 12);
    CHECK(tight.manifest.pages()[0].group.find("tight") != std::string::npos);
    // meshes survive the manifest round trip (1/16 pixel grid)
    Manifest m;
    CHECK(m.parse(tight.manifest.serialize()));
    CHECK(m.serialize() == tight.manifest.serialize());
    bool sameMesh = true;
    for (auto& [path, r] : tight.manifest.regions()) sameMesh &= m.find(path) && m.find(path)->mesh == r.mesh;
    CHECK(sameMesh);
    // a mesh vertex outside its content is a readable error
    std::string text = tight.manifest.serialize();
    aiverse::Json j = aiverse::Json::parse(text);
    aiverse::Json meshesJ = aiverse::Json::object();
    meshesJ.set(orig.begin()->first, aiverse::Json::parse("[0, 0, 99999, 0, 0, 16]"));
    j.set("meshes", meshesJ);
    std::string err;
    CHECK(!m.parse(j.dump(), &err) && err.find("mesh") != std::string::npos);
}

TEST(atlas_framed_sheets_split_into_trimmed_frames) {
    using namespace aiverse;
    // 4 x 2 frames of 32 x 32, each a different shape at a different place in its cell
    atlas::Image sheet;
    sheet.width = 128; sheet.height = 64;
    sheet.rgba.assign(size_t(128 * 64 * 4), 0);
    for (int f = 0; f < 8; ++f) {
        atlas::Image s = atlasShape(14 + f * 2, 12 + f, f % 4, uint32_t(f + 40));
        const int cx = (f % 4) * 32 + (f * 3) % 8, cy = (f / 4) * 32 + (f * 5) % 9;
        for (int y = 0; y < s.height; ++y) std::memcpy(sheet.px(cx, cy + y), s.px(0, y), size_t(s.width) * 4);
    }
    auto build = [&](const char* rules, const char* atlasJson, atlas::AtlasStats& st, atlas::Manifest& m, std::unique_ptr<pack::Pack>& p, std::string& bytes) {
        pack::PackWriter w;
        CHECK(w.addData("rules.json", rules));
        CHECK(w.addData("sprites/walk.png", atlas::encodePng(sheet)));
        CHECK(w.addData("sprites/other.png", atlas::encodePng(atlasShape(30, 20, 0, 9))));
        CHECK(atlas::applyAtlas(w, atlas::AtlasConfig::fromGame(Json::parse(atlasJson), "linux"), st));
        CHECK(w.writeToString(bytes, pack::WriteOptions{}));
        p = pack::Pack::open(pack::memorySource(bytes));
        std::string text;
        CHECK(p && p->read(atlas::kManifestPath, text) && m.parse(text));
    };
    const char* rules = R"({"types": {"a": {"sprite": {"sheet": "sprites/walk.png", "frame": [32, 32]}}}})";
    atlas::AtlasStats st;
    atlas::Manifest m;
    std::unique_ptr<pack::Pack> p;
    std::string bytes;
    build(rules, R"({"atlas": {"files": {"sprites/*": {"frames": true, "tight": true, "rotate": true}}}})", st, m, p, bytes);
    const atlas::Sheet* sh = m.findSheet("sprites/walk.png");
    CHECK(sh && sh->cols == 4 && sh->rows == 2 && sh->frameW() == 32 && sh->frameH() == 32);
    CHECK(m.find("sprites/walk.png") == nullptr && st.atlasedSheets == 1);
    CHECK(p && !p->contains("sprites/walk.png"));
    bool trimmed = true;
    for (int f = 0; f < 8; ++f) { const atlas::Region* r = m.frame("sprites/walk.png", f); trimmed &= r && r->w < 32 && r->sourceW == 32; }
    CHECK(trimmed);
    atlas::ImageSource src([&](const std::string& path, std::string& out) { return p->read(path, out); }, atlas::decodeImageFile);
    atlas::Image back;
    CHECK(src.atlased("sprites/walk.png") && src.load("sprites/walk.png", back) && sameImage(back, sheet));   // whole sheet rebuilt

    // the renderer's view: every logical pixel of every frame, through spriteShape, nearest sampling
    std::map<int, atlas::Image> pages;
    for (size_t i = 0; i < m.pages().size(); ++i) {
        std::string pb;
        CHECK(p->read(m.pages()[i].file, pb) && atlas::loadPageImage(pb, atlas::decodeImageFile, pages[int(i)]));
    }
    render::AtlasSprite sprite;
    sprite.cols = sh->cols; sprite.rows = sh->rows;
    for (int f = 0; f < 8; ++f) {
        const atlas::Region& r = *m.frame("sprites/walk.png", f);
        sprite.frames.push_back({{}, r, m.pages()[size_t(r.page)].width, m.pages()[size_t(r.page)].height});
    }
    render::TextureRef ref;
    ref.width = 128; ref.height = 64; ref.sprite = &sprite;
    bool drawnRight = true, nothingMissing = true;
    for (int f = 0; f < 8; ++f) {
        const int col = f % 4, row = f / 4;
        for (int mirror = 0; mirror < 2; ++mirror) {
            render::float4 uv{col * 0.25f, row * 0.5f, (col + 1) * 0.25f, (row + 1) * 0.5f};
            if (mirror) std::swap(uv.x, uv.z);
            render::SpriteShape shape;
            CHECK(render::spriteShape(ref, uv, shape));
            const atlas::Image& pg = pages[sprite.frames[size_t(f)].region.page];
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) {
                    // rect parameters of the logical pixel's centre
                    const float lu = (col * 32 + x + 0.5f) / 128.0f, lv = (row * 32 + y + 0.5f) / 64.0f;
                    const render::float2 ab{(lu - uv.x) / (uv.z - uv.x), (lv - uv.y) / (uv.w - uv.y)};
                    bool in = true;   // inside the convex shape (either winding)
                    int sg = 0;
                    for (int k = 0; k < shape.count && in; ++k) {
                        const render::float2 a = shape.ab[k], b = shape.ab[(k + 1) % shape.count];
                        const float c = (b.x - a.x) * (ab.y - a.y) - (b.y - a.y) * (ab.x - a.x);
                        if (std::fabs(c) < 1e-9f) continue;
                        const int s = c > 0 ? 1 : -1;
                        if (sg && s != sg) in = false;
                        sg = s;
                    }
                    const uint8_t* want = sheet.px(col * 32 + x, row * 32 + y);
                    if (!in) { nothingMissing &= want[3] == 0; continue; }
                    const render::float2 t = shape.uv(ab);
                    const int px = std::clamp(int(std::floor(t.x * float(pg.width))), 0, pg.width - 1), py = std::clamp(int(std::floor(t.y * float(pg.height))), 0, pg.height - 1);
                    const uint8_t* got = pg.px(px, py);
                    if (want[3] != 0) drawnRight &= std::memcmp(want, got, 4) == 0;
                }
        }
    }
    CHECK(drawnRight);      // what is drawn samples the right texels (trim, rotation, mesh, mirror)
    CHECK(nothingMissing);  // nothing visible lies outside the drawn shape

    // a sub-rect of a whole image clips the shape to the rect
    const atlas::Region* other = m.find("sprites/other.png");
    CHECK(other && !other->plain());
    if (other) {
        render::AtlasSprite one;
        one.frames.push_back({{}, *other, m.pages()[size_t(other->page)].width, m.pages()[size_t(other->page)].height});
        render::TextureRef o;
        o.width = 30; o.height = 20; o.sprite = &one;
        render::SpriteShape half;
        CHECK(render::spriteShape(o, {0, 0, 0.5f, 1}, half));
        bool within = half.count >= 3;
        for (int k = 0; k < half.count; ++k) within &= half.ab[k].x >= -1e-5f && half.ab[k].x <= 1 + 1e-5f && half.ab[k].y >= -1e-5f && half.ab[k].y <= 1 + 1e-5f;
        CHECK(within);
    }
    // plain textures: the whole rect, linear uv as before
    render::TextureRef plainRef;
    plainRef.width = 64; plainRef.height = 64; plainRef.u0 = 0.25f; plainRef.u1 = 0.75f;
    render::SpriteShape full;
    CHECK(render::spriteShape(plainRef, {0, 0, 1, 1}, full) && full.count == 4);
    CHECK(std::fabs(full.uv({1, 1}).x - 0.75f) < 1e-6f && std::fabs(full.uv({0, 0}).x - 0.25f) < 1e-6f);

    // a sheet used with two frame sizes is kept whole (and untrimmed)
    atlas::AtlasStats st2;
    atlas::Manifest m2;
    std::unique_ptr<pack::Pack> p2;
    std::string bytes2;
    build(R"({"types": {"a": {"sprite": {"sheet": "sprites/walk.png", "frame": [32, 32]}}, "b": {"sprite": {"sheet": "sprites/walk.png", "frame": [64, 32]}}}})",
          R"({"atlas": {"files": {"sprites/*": {"frames": true, "mesh": true}}}})", st2, m2, p2, bytes2);
    const atlas::Region* whole = m2.find("sprites/walk.png");
    CHECK(!m2.findSheet("sprites/walk.png") && whole && whole->plain());
    bool warned = false;
    for (auto& w : st2.warnings) warned |= w.find("sprites/walk.png") != std::string::npos;
    CHECK(warned);
}
