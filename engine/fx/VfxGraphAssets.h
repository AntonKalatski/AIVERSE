// AIVERSE versioned VFX graph asset registry and CPU backend hot reload.
// No genre assumptions; exposed parameters are instance variants of a graph.
// Owns metadata only: ParticleSystem owns compiled runtime effects/handles.
#pragma once

#include "VfxGraph.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aiverse::fx {

struct VfxGraphLoadResult {
    int effect = -1;
    bool recompiled = false;
    bool fallback = false; // invalid reload kept previous good compiled effect
    std::string runtimeName;
    std::vector<std::string> errors;
};

class VfxGraphAssets {
public:
    using FileReader = std::function<bool(const std::string& path, std::string& text)>;

    VfxGraphAssets(ParticleSystem& particles, FileReader reader)
        : ps_(particles), reader_(std::move(reader)) {}

    // Content path: fx/graphs/<safe-name>.vfx.json. Optional exposed-parameter
    // overrides create independent variants. Re-read on call to support editor
    // live refresh; unchanged content returns the previous compiled ID.
    VfxGraphLoadResult load(const std::string& name, const Json& overrides = Json::object()) {
        VfxGraphLoadResult result;
        if (!vfxgraph_detail::nameOk(name)) {
            result.errors.push_back("vfx asset: invalid name (path traversal denied)");
            return result;
        }
        if (!overrides.isObject()) {
            result.errors.push_back("vfx asset: overrides must be an object");
            return result;
        }
        const std::string params = overrides.dump(-1);
        const std::string variantKey = name + "#" + params;
        const uint64_t variantHash = hash(params);
        const std::string runtimeName = "graph:" + name + ":" + hex(variantHash);
        result.runtimeName = runtimeName;
        std::string file;
        if (!reader_ || !reader_("fx/graphs/" + name + ".vfx.json", file)) {
            result.errors.push_back("vfx asset: cannot read fx/graphs/" + name + ".vfx.json");
            auto old = compiled_.find(variantKey);
            if (old != compiled_.end()) {
                result.effect = old->second.id;
                result.fallback = true;
            }
            return result;
        }
        const uint64_t fileHash = hash(file);
        auto old = compiled_.find(variantKey);
        if (old != compiled_.end() && old->second.sourceHash == fileHash) {
            result.effect = old->second.id;
            return result;
        }

        Json json;
        try { json = Json::parse(file); }
        catch (const std::exception& e) {
            result.errors.push_back(std::string("vfx asset: invalid JSON: ") + e.what());
            if (old != compiled_.end()) {
                result.effect = old->second.id;
                result.fallback = true;
            }
            return result;
        }
        EffectDef definition;
        if (!compileVfxGraph(json, overrides, definition, result.errors)) {
            if (old != compiled_.end()) {
                result.effect = old->second.id;
                result.fallback = true;
            }
            return result;
        }
        const int id = ps_.define(runtimeName, definition);
        if (id < 0) {
            result.errors.push_back("vfx asset: compiled effect rejected by CPU backend");
            if (old != compiled_.end()) {
                result.effect = old->second.id;
                result.fallback = true;
            }
            return result;
        }
        compiled_[variantKey] = {fileHash, id};
        result.effect = id;
        result.recompiled = true;
        return result;
    }

    // Force next load to recompile. Does not destroy the active runtime effect
    // until a valid replacement has been compiled and defined.
    void invalidate() { compiled_.clear(); }
    void invalidate(const std::string& name) {
        const std::string prefix = name + "#";
        for (auto it = compiled_.begin(); it != compiled_.end();)
            if (it->first.compare(0, prefix.size(), prefix) == 0)
                it = compiled_.erase(it);
            else ++it;
    }
    size_t variants() const { return compiled_.size(); }

private:
    struct Entry { uint64_t sourceHash; int id; };
    static uint64_t hash(const std::string& value) {
        uint64_t h = 14695981039346656037ull;
        for (unsigned char c : value) {
            h ^= c;
            h *= 1099511628211ull;
        }
        return h;
    }
    static std::string hex(uint64_t value) {
        static constexpr char chars[] = "0123456789abcdef";
        std::string out(16, '0');
        for (size_t i = 16; i-- > 0;) {
            out[i] = chars[value & 0xF];
            value >>= 4;
        }
        return out;
    }

    ParticleSystem& ps_;
    FileReader reader_;
    std::unordered_map<std::string, Entry> compiled_;
};

} // namespace aiverse::fx
