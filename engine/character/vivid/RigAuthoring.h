// VIVID non-destructive facial rig correction layer.
// Authored offsets apply after MotionWeave, never rewriting imported binds.
#pragma once
#include "engine/render/Model.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace aiverse::vivid {
struct RigTranslationEdit {
    std::string node;
    render::float3 local{};
};
class RigAuthoring {
public:
    // Edits are named so they survive stable node-index reordering on import.
    bool setTranslation(const std::string& name,render::float3 translation) {
        if(name.empty()||name.size()>128 ||
           name.find_first_of("\n\r\0",0)!=std::string::npos ||
           !finite(translation) ||
           std::fabs(translation.x)>1 || std::fabs(translation.y)>1 ||
           std::fabs(translation.z)>1)return false;
        auto next=edits_;
        if(translation.x==0&&translation.y==0&&translation.z==0)
            next.erase(name);
        else {
            if(next.size()>=128 && next.find(name)==next.end())return false;
            next[name]=translation;
        }
        if(next==edits_)return true;
        if(undo_.size()==kHistory)undo_.erase(undo_.begin());
        undo_.push_back(edits_);edits_=std::move(next);redo_.clear();
        return true;
    }
    bool undo() {
        if(undo_.empty())return false;
        if(redo_.size()==kHistory)redo_.erase(redo_.begin());
        redo_.push_back(edits_);edits_=std::move(undo_.back());
        undo_.pop_back();return true;
    }
    bool redo() {
        if(redo_.empty())return false;
        if(undo_.size()==kHistory)undo_.erase(undo_.begin());
        undo_.push_back(edits_);edits_=std::move(redo_.back());
        redo_.pop_back();return true;
    }
    bool canUndo() const noexcept{return !undo_.empty();}
    bool canRedo() const noexcept{return !redo_.empty();}
    const std::map<std::string,render::float3>& edits() const noexcept {
        return edits_;
    }
    // Stage a hierarchy-aware world-pose overlay. Translation is relative
    // to the selected node's coordinate frame. Every descendant receives
    // the exact same world-space correction, keeping attachments coherent.
    bool apply(const render::Model& model,
               std::span<const render::mat4> input,
               std::span<render::mat4> output) const {
        if(input.size()!=model.nodes.size() ||
           output.size()!=input.size() ||
           output.data()==input.data())return false;
        for(const auto& m:input)
            for(float value:m.m)if(!std::isfinite(value))return false;
        std::vector<render::mat4> staged(input.begin(),input.end());
        for(const auto& [name,offset]:edits_) {
            const int index=model.findNode(name);
            if(index<0||!finite(offset))return false;
            const auto& original=staged[std::size_t(index)];
            // Reject singular transforms instead of using the engine
            // mat4::inverse() identity fallback.
            render::float3 a{original.at(0,0),original.at(1,0),original.at(2,0)};
            render::float3 b{original.at(0,1),original.at(1,1),original.at(2,1)};
            render::float3 c{original.at(0,2),original.at(1,2),original.at(2,2)};
            if(std::fabs(render::dot(a,render::cross(b,c)))<1.e-8f)return false;
            const auto correction=original*
                render::mat4::translate(offset)*original.inverse();
            for(std::size_t j=0;j<model.nodes.size();++j) {
                int cursor=int(j);
                for(std::size_t depth=0;depth<model.nodes.size() && cursor>=0;++depth) {
                    if(cursor==index) {
                        staged[j]=correction*staged[j];
                        break;
                    }
                    if(cursor>=int(model.nodes.size()))return false;
                    cursor=model.nodes[std::size_t(cursor)].parent;
                }
            }
        }
        std::copy(staged.begin(),staged.end(),output.begin());
        return true;
    }
private:
    static bool finite(render::float3 p) {
        return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
    }
    static constexpr std::size_t kHistory=32;
    std::map<std::string,render::float3> edits_;
    std::vector<std::map<std::string,render::float3>> undo_,redo_;
};
} // namespace aiverse::vivid
