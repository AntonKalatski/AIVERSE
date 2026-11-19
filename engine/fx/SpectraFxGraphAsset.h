// SpectraFX graph asset v1. Strict, versioned JSON authoring interchange.
// JSON is presentation data and cannot modify deterministic game state.
// Only public node fields and typed defaults are serialized, not runtime FX handles.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include "../data/Json.h"
#include "SpectraFxGraphRegistry.h"

namespace aiverse::fx {

struct SpectraFxGraphAsset {
    std::string name;
    SpectraFxGraph graph;
    SpectraFxGraphParameters defaults;
};

class SpectraFxGraphAssetCodec {
public:
    static constexpr int kVersion = 1;

    static const char* nodeName(SpectraFxNodeKind kind) {
        switch (kind) {
        case SpectraFxNodeKind::Input: return "input";
        case SpectraFxNodeKind::Scale: return "scale";
        case SpectraFxNodeKind::Tint: return "tint";
        case SpectraFxNodeKind::Emit: return "emit";
        case SpectraFxNodeKind::ParameterScale: return "parameterScale";
        case SpectraFxNodeKind::ParameterTint: return "parameterTint";
        case SpectraFxNodeKind::Offset: return "offset";
        case SpectraFxNodeKind::Gate: return "gate";
        case SpectraFxNodeKind::EventFilter: return "eventFilter";
        }
        return "";
    }

    static bool read(const Json& json, SpectraFxGraphAsset& out, std::string* error = nullptr) {
        const auto fail = [&](std::string message) {
            if (error) *error = std::move(message);
            return false;
        };
        if (!json.isObject()) return fail("SpectraFX asset must be an object");
        if (!checkKeys(json, {"version","name","nodes","defaults"}))
            return fail("SpectraFX asset has unknown field");
        if (!json["version"].isNumber() || json["version"].integer(-1) != kVersion)
            return fail("SpectraFX asset version must be 1");
        if (!json["name"].isString() || json["name"].str().empty() ||
            json["name"].str().size() > 128) return fail("SpectraFX invalid graph name");
        const Json& list = json["nodes"];
        if (!list.isArray() || list.size() == 0 || list.size() > SpectraFxGraph::kMaxNodes)
            return fail("SpectraFX invalid node list");
        SpectraFxGraphAsset temporary;
        temporary.name = json["name"].str();
        std::vector<SpectraFxNode> nodes;
        nodes.reserve(list.size());
        for (size_t i=0; i<list.size(); ++i) {
            const Json& item = list[i];
            if (!item.isObject() || !checkKeys(item, {"kind","source","factor","tint","cueId","parameter","offset"}))
                return fail("SpectraFX invalid node object at " + std::to_string(i));
            if (!item["kind"].isString()) return fail("SpectraFX node kind missing");
            SpectraFxNode node;
            if (!kindFromName(item["kind"].str(), node.kind))
                return fail("SpectraFX unknown node kind at " + std::to_string(i));
            if (item.has("source")) {
                if (!uint32Field(item["source"], node.source))
                    return fail("SpectraFX source index invalid");
            }
            if (item.has("cueId") && !uint32Field(item["cueId"],node.cueId))
                return fail("SpectraFX cue ID invalid");
            if (item.has("factor") && !number(item["factor"],node.factor))
                return fail("SpectraFX factor invalid");
            if (item.has("tint") && !vector4(item["tint"],node.tint))
                return fail("SpectraFX tint invalid");
            if (item.has("offset") && !vector3(item["offset"],node.offset))
                return fail("SpectraFX offset invalid");
            if (item.has("parameter")) {
                if (!item["parameter"].isString())
                    return fail("SpectraFX parameter must be a string");
                node.parameter = item["parameter"].str();
            }
            nodes.push_back(std::move(node));
        }
        temporary.graph.setNodes(std::move(nodes));
        if (!temporary.graph.validate(error)) return false;
        const Json& defaults = json["defaults"];
        if (!defaults.isNull()) {
            if (!defaults.isObject() || !checkKeys(defaults, {"scalars","colors","flags"}))
                return fail("SpectraFX invalid defaults");
            const Json& scalars = defaults["scalars"];
            if (!scalars.isNull()) {
                if (!scalars.isObject()) return fail("SpectraFX scalars must be object");
                for (const auto& pair : scalars.members()) {
                    float v = 0;
                    if (!key(pair.first) || !number(pair.second,v) || v < 0 || v > 1000)
                        return fail("SpectraFX scalar default invalid: " + pair.first);
                    temporary.defaults.setScalar(pair.first,v);
                }
            }
            const Json& colors = defaults["colors"];
            if (!colors.isNull()) {
                if (!colors.isObject()) return fail("SpectraFX colors must be object");
                for (const auto& pair : colors.members()) {
                    float4 v{};
                    if (!key(pair.first) || !vector4(pair.second,v) || !validColor(v))
                        return fail("SpectraFX color default invalid: " + pair.first);
                    temporary.defaults.setColor(pair.first,v);
                }
            }
            const Json& flags = defaults["flags"];
            if (!flags.isNull()) {
                if (!flags.isObject()) return fail("SpectraFX flags must be object");
                for (const auto& pair : flags.members()) {
                    if (!key(pair.first) || pair.second.type() != Json::Type::Bool)
                        return fail("SpectraFX flag default invalid: " + pair.first);
                    temporary.defaults.setFlag(pair.first,pair.second.boolean());
                }
            }
        }
        out = std::move(temporary); // atomic: malformed assets never alter caller's state
        if (error) error->clear();
        return true;
    }

    static bool readText(const std::string& text, SpectraFxGraphAsset& out,
                         std::string* error = nullptr) {
        try { return read(Json::parse(text),out,error); }
        catch (const JsonError& e) {
            if (error) *error = e.what();
            return false;
        }
    }

    static Json write(const SpectraFxGraphAsset& asset) {
        Json root=Json::object();
        root.set("version",Json::number(int64_t(kVersion)));
        root.set("name",Json::string(asset.name));
        Json list=Json::array();
        for (const auto& node : asset.graph.nodes()) {
            Json j=Json::object();
            j.set("kind",Json::string(nodeName(node.kind)));
            if (node.kind != SpectraFxNodeKind::Input)
                j.set("source",Json::number(int64_t(node.source)));
            switch (node.kind) {
            case SpectraFxNodeKind::Scale: j.set("factor",Json::number(double(node.factor),6)); break;
            case SpectraFxNodeKind::Tint: j.set("tint",write4(node.tint)); break;
            case SpectraFxNodeKind::Offset: j.set("offset",write3(node.offset)); break;
            case SpectraFxNodeKind::Emit:
            case SpectraFxNodeKind::EventFilter: j.set("cueId",Json::number(int64_t(node.cueId))); break;
            case SpectraFxNodeKind::ParameterScale:
            case SpectraFxNodeKind::ParameterTint:
            case SpectraFxNodeKind::Gate: j.set("parameter",Json::string(node.parameter)); break;
            case SpectraFxNodeKind::Input: break;
            }
            list.push(std::move(j));
        }
        root.set("nodes",std::move(list));
        Json defaults=Json::object(),scalars=Json::object(),colors=Json::object(),flags=Json::object();
        // Stable sorted keys give reproducible authored output.
        std::vector<std::string> names;
        for (const auto& p : asset.defaults.scalars) names.push_back(p.first);
        std::sort(names.begin(),names.end());
        for (const auto& n : names) scalars.set(n,Json::number(double(asset.defaults.scalars.at(n)),6));
        names.clear();
        for (const auto& p : asset.defaults.colors) names.push_back(p.first);
        std::sort(names.begin(),names.end());
        for (const auto& n : names) colors.set(n,write4(asset.defaults.colors.at(n)));
        names.clear();
        for (const auto& p : asset.defaults.flags) names.push_back(p.first);
        std::sort(names.begin(),names.end());
        for (const auto& n : names) flags.set(n,Json::fromBool(asset.defaults.flags.at(n)));
        defaults.set("scalars",std::move(scalars));
        defaults.set("colors",std::move(colors));
        defaults.set("flags",std::move(flags));
        root.set("defaults",std::move(defaults));
        return root;
    }

    static bool install(SpectraFxGraphRegistry& registry, const Json& json,
                        std::string* error = nullptr) {
        SpectraFxGraphAsset parsed;
        if (!read(json,parsed,error)) return false;
        return registry.define(std::move(parsed.name),std::move(parsed.graph),
                               std::move(parsed.defaults),error);
    }

private:
    static bool checkKeys(const Json& obj, std::initializer_list<const char*> keys) {
        for (const auto& entry : obj.members()) {
            bool found=false;
            for (const char* k : keys) if (entry.first == k) { found=true; break; }
            if (!found) return false;
        }
        return true;
    }
    static bool key(const std::string& name) { return !name.empty() && name.size()<=128; }
    static bool kindFromName(const std::string& name,SpectraFxNodeKind& out) {
        for (auto k : {SpectraFxNodeKind::Input,SpectraFxNodeKind::Scale,
                       SpectraFxNodeKind::Tint,SpectraFxNodeKind::Emit,
                       SpectraFxNodeKind::ParameterScale,SpectraFxNodeKind::ParameterTint,
                       SpectraFxNodeKind::Offset,SpectraFxNodeKind::Gate,
                       SpectraFxNodeKind::EventFilter})
            if (name == nodeName(k)) { out=k; return true; }
        return false;
    }
    static bool uint32Field(const Json& v,uint32_t& out) {
        if (!v.isNumber() || v.numberLiteral().find_first_of(".eE-") != std::string::npos) return false;
        const int64_t n=v.integer(-1);
        if (n<0 || uint64_t(n)>UINT32_MAX) return false;
        out=uint32_t(n);
        return true;
    }
    static bool number(const Json& v,float& out) {
        if (!v.isNumber()) return false;
        const double n=v.toDouble();
        if (!std::isfinite(n) || n < -10000000 || n > 10000000) return false;
        out=static_cast<float>(n); return true;
    }
    static bool vector3(const Json& v,float3& out) {
        return v.isArray() && v.size()==3 && number(v[0],out.x) &&
               number(v[1],out.y) && number(v[2],out.z);
    }
    static bool vector4(const Json& v,float4& out) {
        return v.isArray() && v.size()==4 && number(v[0],out.x) &&
               number(v[1],out.y) && number(v[2],out.z) && number(v[3],out.w);
    }
    static bool validColor(float4 v) {
        return v.x>=0 && v.y>=0 && v.z>=0 && v.w>=0 &&
               v.x<=8 && v.y<=8 && v.z<=8 && v.w<=8;
    }
    static Json write3(float3 v) {
        Json a=Json::array();
        a.push(Json::number(double(v.x),6)); a.push(Json::number(double(v.y),6));
        a.push(Json::number(double(v.z),6)); return a;
    }
    static Json write4(float4 v) {
        Json a=write3({v.x,v.y,v.z}); a.push(Json::number(double(v.w),6)); return a;
    }
};
} // namespace aiverse::fx
