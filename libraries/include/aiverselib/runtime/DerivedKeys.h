#pragma once
// derived-keys: caches for derived lookups of native modules that keep per-entity values in string-keyed maps
// (trigger variables "<prefix><id><suffix>", NativeState values). A key string or a node pointer is a pure
// function of its inputs and of the map's own erase / insert counters: never saved, hashed or rolled back, so a
// cache can only remove work (string building, map lookups), never change results. Not thread-safe; keep one
// cache per thread / owner (a function-local `thread_local` works).
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace aiverse::lib::runtime {

// Dense ids (indices into a population): one string per id, built on first use.
struct DenseKeys {
    std::string prefix, suffix;
    std::vector<std::string> keys;
    explicit DenseKeys(std::string_view p, std::string_view s = {}) : prefix(p), suffix(s) {}
    const std::string& operator()(std::size_t id) {
        if (id >= keys.size()) keys.resize(id + 1);
        auto& k = keys[id];
        if (k.empty()) { k.reserve(prefix.size() + suffix.size() + 20); k.append(prefix).append(std::to_string(id)).append(suffix); }
        return k;
    }
};

// Sparse ids (packed entity handles). Bounded: dropped whole when it grows past `limit` (lookups only, the map
// is never iterated, so its order does not matter).
struct SparseKeys {
    std::string prefix, suffix;
    std::size_t limit = 65536;
    std::unordered_map<std::uint64_t, std::string> keys;
    explicit SparseKeys(std::string_view p, std::string_view s = {}, std::size_t cap = 65536) : prefix(p), suffix(s), limit(cap) {}
    const std::string& operator()(std::uint64_t id) {
        if (keys.size() > limit) keys.clear();
        auto [it, fresh] = keys.try_emplace(id);
        if (fresh) { auto& k = it->second; k.reserve(prefix.size() + suffix.size() + 20); k.append(prefix).append(std::to_string(id)).append(suffix); }
        return it->second;
    }
};

// Node pointers of N fields per id into a node-based map (std::map and the like: pointers stay valid until the
// node is erased). The map must expose erasures() and inserts() counters: any erasure (or rebuild / load, which
// must bump erasures) drops the whole cache; an insertion re-checks the fields that were missing. Maps without
// those counters: use find() directly.
template <std::size_t N>
struct NodeCache {
    struct Entry { std::array<void*, N> p{}; unsigned known = 0; std::uint64_t inserts = 0; };
    const void* map = nullptr;
    std::uint64_t stamp = 0;
    std::vector<Entry> entries;
    static_assert(N <= 32, "one bit per field");

    template <class Vars, class KeyOf>
    auto lookup(Vars& v, std::size_t id, KeyOf&& keyOf) {
        using Value = typename std::remove_reference_t<Vars>::mapped_type;
        std::array<Value*, N> out{};
        if (map != static_cast<const void*>(&v) || stamp != v.erasures()) { map = &v; stamp = v.erasures(); entries.clear(); }
        if (id >= entries.size()) entries.resize(id + 1);
        auto& e = entries[id];
        const bool fresh = e.inserts == v.inserts();
        for (std::size_t f = 0; f < N; ++f) {
            const unsigned bit = 1u << f;
            if (!(e.known & bit) || (!e.p[f] && !fresh)) {
                auto i = v.find(keyOf(f));
                e.p[f] = i == v.end() ? nullptr : static_cast<void*>(&i->second);
                e.known |= bit;
            }
            out[f] = static_cast<Value*>(e.p[f]);
        }
        e.inserts = v.inserts();
        return out;
    }
    void clear() { map = nullptr; stamp = 0; entries.clear(); }
};

} // namespace aiverse::lib::runtime
