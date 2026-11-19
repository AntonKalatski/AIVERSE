// VIVID genre-independent skin weight authoring.
// Edits refer to model-global vertex IDs and *named* joints, never transient
// palette slots. Validate all owning primitive palettes before publication.
#pragma once
#include "engine/render/Model.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::vivid {
class SkinWeightAuthoring final {
public:
    struct Edit {
        std::uint32_t vertex=0;
        std::string joint;   // exact Model::Node name
        float weight=0.0f;   // requested share of 255 normalized bytes
    };
    struct Result {
        std::size_t verticesChanged=0;
        std::size_t primitivesValidated=0;
    };
    bool set(std::uint32_t vertex,std::string joint,float weight) {
        if(joint.empty()||joint.size()>128 ||
           joint.find_first_of("\n\r")!=std::string::npos ||
           !std::isfinite(weight)||weight<0||weight>1 ||
           edits_.size()>=65536 && !edits_.count(vertex))
            return false;
        edits_[vertex]={vertex,std::move(joint),weight};
        return true;
    }
    bool erase(std::uint32_t vertex) {return edits_.erase(vertex)!=0;}
    void clear() noexcept {edits_.clear();}
    const std::map<std::uint32_t,Edit>& edits() const noexcept {return edits_;}

    Json toJson() const {
        Json root=Json::object();
        root.set("version",Json::number(std::int64_t(1)));
        Json entries=Json::array();
        for(const auto& [id,edit]:edits_) {
            Json item=Json::object();
            item.set("vertex",Json::number(std::int64_t(id)));
            item.set("joint",Json::string(edit.joint));
            item.set("weight",Json::number(double(edit.weight),9));
            entries.push(std::move(item));
        }
        root.set("edits",std::move(entries));
        return root;
    }
    static SkinWeightAuthoring fromJson(const Json& root) {
        if(!root.isObject()||root["version"].integer(-1)!=1 ||
           !root["edits"].isArray()||root["edits"].size()>65536)
            throw std::invalid_argument("VIVID skin weights: invalid authoring schema");
        SkinWeightAuthoring authoring;
        for(const auto& e:root["edits"].items()) {
            if(!e.isObject()||!e["vertex"].isNumber()||
               !e["joint"].isString()||!e["weight"].isNumber())
                throw std::invalid_argument("VIVID skin weights: malformed edit");
            const auto id=e["vertex"].integer(-1);
            if(id<0||id>std::numeric_limits<std::uint32_t>::max() ||
               authoring.edits_.count(std::uint32_t(id)) ||
               !authoring.set(std::uint32_t(id),e["joint"].str(),
                              float(e["weight"].toDouble())))
                throw std::invalid_argument("VIVID skin weights: invalid or repeated edit");
        }
        return authoring;
    }

    // Requires pre-upload private Model. Multi-node and multi-primitive
    // ownership is legal ONLY when all references to an edited vertex
    // resolve identical skin/joint palette slots. A shared seam with
    // conflicting palettes must first be split by a mesh authoring tool.
    // All writes are delayed until every edit is independently validated.
    Result apply(render::Model& model) const {
        if(model.vb.valid() || model.ib.valid() || model.geometryPoolPage ||
           model.geometry || model.vertices.empty())
            throw std::invalid_argument("VIVID skin weights: requires private pre-upload CPU mesh");
        struct Context {
            std::size_t skin=0;
            std::vector<std::uint16_t> palette;
        };
        std::map<std::uint32_t,Context> contexts;
        Result stats;
        for(const auto& node:model.nodes) {
            if(node.mesh<0 || node.skin<0)continue;
            if(std::size_t(node.mesh)>=model.meshes.size() ||
               std::size_t(node.skin)>=model.skins.size())
                throw std::invalid_argument("VIVID skin weights: invalid model mesh/skin reference");
            const auto skinId=std::size_t(node.skin);
            const auto& skin=model.skins[skinId];
            for(const auto& primitive:model.meshes[std::size_t(node.mesh)].primitives) {
                const auto start=std::size_t(primitive.firstIndex);
                if(start>model.indices.size() ||
                   primitive.indexCount>model.indices.size()-start)
                    throw std::invalid_argument("VIVID skin weights: invalid primitive range");
                if(primitive.jointPalette.empty())continue;
                ++stats.primitivesValidated;
                for(std::size_t i=start;i<start+primitive.indexCount;++i) {
                    const auto id=model.indices[i];
                    if(!edits_.count(id))continue;
                    if(id>=model.vertices.size())
                        throw std::invalid_argument("VIVID skin weights: vertex index out of bounds");
                    for(const auto j:primitive.jointPalette)
                        if(j>=skin.joints.size() ||
                           skin.joints[j]<0 ||
                           std::size_t(skin.joints[j])>=model.nodes.size())
                            throw std::invalid_argument("VIVID skin weights: bad palette joint");
                    auto [it,inserted]=contexts.try_emplace(id,Context{
                        skinId,primitive.jointPalette});
                    if(!inserted && (it->second.skin!=skinId ||
                                     it->second.palette!=primitive.jointPalette))
                        throw std::invalid_argument(
                            "VIVID skin weights: shared vertex has conflicting palettes; split seam first");
                }
            }
        }
        std::vector<std::pair<std::uint32_t,render::ModelVertex>> staged;
        staged.reserve(edits_.size());
        for(const auto& [vertex,edit]:edits_) {
            if(vertex>=model.vertices.size() || !contexts.count(vertex))
                throw std::invalid_argument("VIVID skin weights: edited vertex is not skinned");
            const auto& ctx=contexts.at(vertex);
            const auto& skin=model.skins[ctx.skin];
            const int jointNode=model.findNode(edit.joint);
            if(jointNode<0)
                throw std::invalid_argument("VIVID skin weights: unknown named joint");
            int destination=-1;
            for(std::size_t slot=0;slot<ctx.palette.size();++slot)
                if(skin.joints[ctx.palette[slot]]==jointNode) {
                    if(slot>255 || destination>=0)
                        throw std::invalid_argument("VIVID skin weights: ambiguous palette joint");
                    destination=int(slot);
                }
            if(destination<0)
                throw std::invalid_argument("VIVID skin weights: selected bone not in primitive palette");
            const auto& source=model.vertices[vertex];
            std::map<int,double> values;
            for(std::size_t i=0;i<4;++i) {
                if(!source.weights[i])continue;
                const int slot=source.joints[i];
                if(slot<0||std::size_t(slot)>=ctx.palette.size())
                    throw std::invalid_argument("VIVID skin weights: source influence outside palette");
                values[slot]+=source.weights[i]/255.0;
            }
            if(values.empty())
                throw std::invalid_argument("VIVID skin weights: source weights empty");
            const double requested=edit.weight;
            const double oldShare=values.count(destination)?values[destination]:0.0;
            double others=0;
            for(const auto& [slot,value]:values)if(slot!=destination)others+=value;
            for(auto& [slot,value]:values)
                if(slot!=destination)value=(others>0)
                    ? value*(1.0-requested)/others : 0.0;
            if(others==0 && requested<1.0)
                throw std::invalid_argument(
                    "VIVID skin weights: no other joint to absorb remaining weight");
            (void)oldShare;
            values[destination]=requested;
            std::vector<std::pair<int,double>> sorted;
            for(const auto& [slot,value]:values)if(value>0)sorted.push_back({slot,value});
            std::sort(sorted.begin(),sorted.end(),
                [](const auto& a,const auto& b) {
                    return a.second!=b.second ? a.second>b.second : a.first<b.first;
                });
            if(sorted.size()>4)sorted.resize(4);
            double total=0;
            for(const auto& pair:sorted)total+=pair.second;
            if(total<=0)throw std::invalid_argument("VIVID skin weights: no remaining influence");
            struct Quant {int slot;int bytes;double remainder;};
            std::vector<Quant> quant;
            int allocated=0;
            for(const auto& [slot,value]:sorted) {
                const double exact=value*255.0/total;
                const int floorValue=int(std::floor(exact));
                quant.push_back({slot,floorValue,exact-floorValue});
                allocated+=floorValue;
            }
            while(allocated<255) {
                auto best=std::max_element(quant.begin(),quant.end(),
                    [](const Quant& a,const Quant& b) {
                        return a.remainder!=b.remainder
                            ? a.remainder<b.remainder : a.slot>b.slot;
                    });
                ++best->bytes;best->remainder=-1; ++allocated;
            }
            render::ModelVertex next=source;
            for(int i=0;i<4;++i) {next.joints[i]=0;next.weights[i]=0;}
            for(std::size_t i=0;i<quant.size();++i) {
                next.joints[i]=std::uint8_t(quant[i].slot);
                next.weights[i]=std::uint8_t(quant[i].bytes);
            }
            staged.push_back({vertex,next});
        }
        // Zero-allocation publication after complete preflight.
        for(const auto& [vertex,updated]:staged)model.vertices[vertex]=updated;
        stats.verticesChanged=staged.size();
        return stats;
    }
private:
    std::map<std::uint32_t,Edit> edits_;
};
} // namespace aiverse::vivid
