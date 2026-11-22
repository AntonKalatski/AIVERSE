#include "StrataCooked.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <type_traits>

namespace aiverse::render::strata {
namespace {

constexpr char kMagic[8] = {'S', 'T', 'R', 'A', 'T', 'A', 'C', 'K'};
constexpr uint32_t kVersion = 1;
// Bumped whenever the builder's output for the same input may change.
constexpr uint64_t kBuilderEpoch = 0x5354524154410004ull; // 4: + up to 16 levels

struct Hasher {
    uint64_t h = 0x9e3779b97f4a7c15ull;
    void u64(uint64_t v) {
        v *= 0xbf58476d1ce4e5b9ull; v ^= v >> 31; v *= 0x94d049bb133111ebull;
        h = (h ^ v) * 0x100000001b3ull; h ^= h >> 29;
    }
    void f(float x) { uint32_t b = 0; std::memcpy(&b, &x, 4); u64(b); }
    void words(const void* data, size_t bytes) {
        const auto* p = static_cast<const unsigned char*>(data);
        size_t i = 0;
        for (; i + 8 <= bytes; i += 8) { uint64_t w; std::memcpy(&w, p + i, 8); u64(w); }
        uint64_t tail = 0; std::memcpy(&tail, p + i, bytes - i); u64(tail ^ (uint64_t(bytes) << 56));
    }
};

class Writer {
public:
    std::string out;
    template <class T> void pod(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        out.append(reinterpret_cast<const char*>(&v), sizeof(T));
    }
    template <class T> void vec(const std::vector<T>& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        pod(uint64_t(v.size()));
        if (!v.empty()) out.append(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(T));
    }
    void str(const std::string& s) { pod(uint64_t(s.size())); out += s; }
};

class Reader {
public:
    const std::string& in;
    size_t at = 0;
    bool ok = true;
    explicit Reader(const std::string& s) : in(s) {}
    template <class T> bool pod(T& v) {
        if (!ok || in.size() - at < sizeof(T)) return ok = false;
        std::memcpy(&v, in.data() + at, sizeof(T)); at += sizeof(T); return true;
    }
    template <class T> bool vec(std::vector<T>& v, uint64_t limit) {
        uint64_t n = 0;
        if (!pod(n) || n > limit || (in.size() - at) / sizeof(T) < n) return ok = false;
        v.resize(size_t(n));
        if (n) std::memcpy(v.data(), in.data() + at, size_t(n) * sizeof(T));
        at += size_t(n) * sizeof(T);
        return true;
    }
    bool str(std::string& s) {
        uint64_t n = 0;
        if (!pod(n) || n > 4096 || in.size() - at < n) return ok = false;
        s.assign(in.data() + at, size_t(n)); at += size_t(n); return true;
    }
};

// ClusterNode carries a bool; store it field by field (no padding bytes).
struct PackedNode {
    uint32_t firstChild, childCount, firstIndex, indexCount, materialGroup;
    float geometricError;
    uint32_t resident;
};
static_assert(sizeof(PackedNode) == 28);
static_assert(std::is_trivially_copyable_v<ClusterBounds> && sizeof(ClusterBounds) == 24);
static_assert(std::is_trivially_copyable_v<Meshlet> && sizeof(Meshlet) == 20);

bool fail(std::string* e, const char* why) { if (e) *e = why; return false; }

} // namespace

uint64_t cookSignature(const Model& model, const BuildOptions& o,
                       bool allowPreparedRootLods, bool tolerate) {
    Hasher h;
    h.u64(kBuilderEpoch);
    h.u64(o.maxVertices); h.u64(o.maxTriangles); h.u64(o.childrenPerNode); h.u64(o.maxLevels);
    h.f(o.targetRatio); h.f(o.relativeError);
    h.u64(allowPreparedRootLods); h.u64(tolerate);
    h.u64(model.vertices.size()); h.u64(model.indices.size());
    h.u64(model.meshes.size()); h.u64(model.nodes.size());
    for (const auto& n : model.nodes) h.u64(uint64_t(int64_t(n.mesh)));
    for (const auto& m : model.meshes) {
        h.u64(m.primitives.size());
        for (const auto& p : m.primitives) {
            h.u64(p.firstIndex); h.u64(p.indexCount); h.u64(p.firstVertex); h.u64(p.vertexCount);
            h.u64(uint64_t(int64_t(p.material))); h.u64(uint64_t(p.morphTargets)); h.u64(p.jointPalette.size());
        }
    }
    for (const auto& mat : model.materials) h.u64(uint64_t(mat.alpha));
    if (!model.indices.empty()) h.words(model.indices.data(), model.indices.size() * sizeof(uint32_t));
    // Imported positions/normals/uvs only: generated tangents may differ in
    // the last bit between compilers and must not invalidate a cook.
    std::vector<float> block;
    block.reserve(8 * 4096);
    for (size_t i = 0; i < model.vertices.size(); ++i) {
        const auto& v = model.vertices[i];
        block.insert(block.end(), {v.pos.x, v.pos.y, v.pos.z, v.nrm.x, v.nrm.y, v.nrm.z, v.uv.x, v.uv.y});
        if (block.size() >= 8 * 4096 || i + 1 == model.vertices.size()) {
            h.words(block.data(), block.size() * sizeof(float));
            block.clear();
        }
    }
    return h.h;
}

std::string cookedFilename(const std::string& sourceKey) {
    uint64_t h = 14695981039346656037ull;
    for (unsigned char ch : sourceKey) { h ^= ch; h *= 1099511628211ull; }
    std::ostringstream r;
    r << "strata-" << std::hex << std::setw(16) << std::setfill('0') << h << ".stratack";
    return r.str();
}

std::string encodeCookedAsset(const ModelAsset& asset, uint64_t signature) {
    Writer w;
    w.out.append(kMagic, 8);
    w.pod(kVersion);
    w.pod(signature);
    w.pod(uint32_t(asset.sourceSeamsCertified));
    w.pod(asset.sharedGeometricBoundarySegments);
    w.pod(uint64_t(asset.skipped.size()));
    for (const auto& s : asset.skipped) { w.pod(s.mesh); w.pod(s.primitive); w.str(s.reason); }
    w.pod(uint64_t(asset.primitives.size()));
    std::vector<PackedNode> nodes;
    for (const auto& p : asset.primitives) {
        w.pod(p.mesh); w.pod(p.primitive); w.pod(p.material);
        const auto& g = p.geometry;
        nodes.clear();
        for (const auto& n : g.nodes)
            nodes.push_back({n.firstChild, n.childCount, n.firstIndex, n.indexCount,
                             n.materialGroup, n.geometricError, uint32_t(n.resident)});
        w.vec(nodes); w.vec(g.bounds); w.vec(g.roots);
        w.pod(uint32_t(g.seamCertified)); w.pod(g.seamRejectedParents);
        w.vec(g.indices); w.pod(g.archivedIndexCount);
        w.vec(g.meshlets); w.vec(g.meshletVertices); w.vec(g.meshletTriangles);
    }
    w.out.append(kMagic, 8); // trailer: detects truncation
    return std::move(w.out);
}

bool decodeCookedAsset(const std::string& bytes, uint64_t signature,
                       ModelAsset& out, std::string* error) {
    if (bytes.size() < 32 || std::memcmp(bytes.data(), kMagic, 8) != 0 ||
        std::memcmp(bytes.data() + bytes.size() - 8, kMagic, 8) != 0)
        return fail(error, "not a STRATA cooked asset or truncated");
    Reader r(bytes);
    r.at = 8;
    uint32_t version = 0, certified = 0;
    uint64_t sig = 0, count = 0;
    if (!r.pod(version) || version != kVersion) return fail(error, "unsupported STRATA cooked version");
    if (!r.pod(sig) || sig != signature) return fail(error, "STRATA cooked asset is stale (source or builder changed)");
    ModelAsset next;
    r.pod(certified); next.sourceSeamsCertified = certified != 0;
    r.pod(next.sharedGeometricBoundarySegments);
    if (!r.pod(count) || count > 65536) return fail(error, "corrupt STRATA cooked skip list");
    for (uint64_t i = 0; i < count && r.ok; ++i) {
        ModelPrimitiveSkip s; r.pod(s.mesh); r.pod(s.primitive); r.str(s.reason);
        next.skipped.push_back(std::move(s));
    }
    if (!r.pod(count) || count > 65536) return fail(error, "corrupt STRATA cooked primitive list");
    constexpr uint64_t kBig = 1ull << 31;
    std::vector<PackedNode> nodes;
    for (uint64_t i = 0; i < count && r.ok; ++i) {
        ModelPrimitiveAsset p;
        r.pod(p.mesh); r.pod(p.primitive); r.pod(p.material);
        auto& g = p.geometry;
        r.vec(nodes, kBig);
        g.nodes.resize(nodes.size());
        for (size_t k = 0; k < nodes.size(); ++k) {
            const auto& n = nodes[k];
            auto& d = g.nodes[k];
            d.firstChild = n.firstChild; d.childCount = n.childCount; d.firstIndex = n.firstIndex;
            d.indexCount = n.indexCount; d.materialGroup = n.materialGroup;
            d.geometricError = n.geometricError; d.resident = n.resident != 0;
        }
        r.vec(g.bounds, kBig); r.vec(g.roots, kBig);
        uint32_t seam = 0; r.pod(seam); g.seamCertified = seam != 0; r.pod(g.seamRejectedParents);
        r.vec(g.indices, kBig); r.pod(g.archivedIndexCount);
        r.vec(g.meshlets, kBig); r.vec(g.meshletVertices, kBig); r.vec(g.meshletTriangles, kBig);
        if (r.ok && (g.bounds.size() != g.nodes.size() || !validate(g.nodes, g.roots)))
            return fail(error, "STRATA cooked hierarchy is invalid");
        next.primitives.push_back(std::move(p));
    }
    if (!r.ok || r.at + 8 != bytes.size()) return fail(error, "corrupt STRATA cooked asset");
    out = std::move(next);
    return true;
}

bool writeCookedAsset(const std::string& path, const ModelAsset& asset,
                      uint64_t signature, std::string* error) {
    const std::string bytes = encodeCookedAsset(asset, signature);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    const std::string temp = path + ".tmp";
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) return fail(error, "cannot create STRATA cooked file");
    const bool wrote = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (std::fclose(f) != 0 || !wrote) { std::filesystem::remove(temp, ec); return fail(error, "cannot write STRATA cooked file"); }
    std::filesystem::rename(temp, path, ec);
    if (ec) { std::filesystem::remove(temp, ec); return fail(error, "cannot publish STRATA cooked file"); }
    return true;
}

} // namespace aiverse::render::strata
