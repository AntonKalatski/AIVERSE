// Universal deterministic scene hierarchy for ECS entities (RTS/FPS/native games).
// Standalone, no render matrices or floating point in the simulation.
// Existing Transform remains an independent world-space legacy component.
#pragma once
#include "CommonComponents.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse {
namespace hierarchy_detail {
inline AffineTransform localAffine(LocalTransform t) {
    const auto q=t.rotation;
    const auto xx=q.x*q.x,yy=q.y*q.y,zz=q.z*q.z;
    const auto xy=q.x*q.y,xz=q.x*q.z,yz=q.y*q.z;
    const auto wx=q.w*q.x,wy=q.w*q.y,wz=q.w*q.z;
    const auto two=2_fx;
    AffineTransform a;
    a.m={
        (1_fx-two*(yy+zz))*t.scale.x, two*(xy-wz)*t.scale.y, two*(xz+wy)*t.scale.z,t.position.x,
        two*(xy+wz)*t.scale.x,(1_fx-two*(xx+zz))*t.scale.y,two*(yz-wx)*t.scale.z,t.position.y,
        two*(xz-wy)*t.scale.x,two*(yz+wx)*t.scale.y,(1_fx-two*(xx+yy))*t.scale.z,t.position.z
    };
    return a;
}
inline AffineTransform multiplyAffine(const AffineTransform& a,const AffineTransform& b) {
    AffineTransform result;
    for(size_t row=0;row<3;++row)
        for(size_t col=0;col<4;++col) {
            Fixed value=col==3?a.at(row,3):Fixed{};
            for(size_t k=0;k<3;++k)value+=a.at(row,k)*b.at(k,col);
            result.m[row*4+col]=value;
        }
    return result;
}
inline Vec3 transformPoint(const AffineTransform& a,Vec3 p) {
    return {
        a.at(0,0)*p.x+a.at(0,1)*p.y+a.at(0,2)*p.z+a.at(0,3),
        a.at(1,0)*p.x+a.at(1,1)*p.y+a.at(1,2)*p.z+a.at(1,3),
        a.at(2,0)*p.x+a.at(2,1)*p.y+a.at(2,2)*p.z+a.at(2,3)
    };
}
inline Vec3 scale(Vec3 a,Vec3 b){return {a.x*b.x,a.y*b.y,a.z*b.z};}
inline Vec3 cross(Vec3 a,Vec3 b){
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
inline Rotation multiply(Rotation a,Rotation b){
    return {
        a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
        a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z
    };
}
// Assumes a unit quaternion; callers should normalize imported input once.
inline Vec3 rotate(Rotation q,Vec3 v) {
    Vec3 t=cross({q.x,q.y,q.z},v)*2_fx;
    return v+t*q.w+cross({q.x,q.y,q.z},t);
}
inline WorldTransform combine(const WorldTransform& parent,const LocalTransform& child){
    const auto affine=multiplyAffine(parent.affine,localAffine(child));
    return {
        {affine.at(0,3),affine.at(1,3),affine.at(2,3)},
        multiply(parent.rotation,child.rotation),
        scale(parent.scale,child.scale),
        affine
    };
}
inline WorldTransform root(LocalTransform local) {
    return {local.position,local.rotation,local.scale,localAffine(local)};
}
}

// Parent and LocalTransform are the authoritative components. WorldTransform
// is a derived cache, NOT serialized game state; rebuild after load/rollback.
// Structural mutations should go through SceneHierarchy to maintain indices.
class SceneHierarchy {
    struct Node {
        Entity entity=kNullEntity;
        Entity parent=kNullEntity;
        std::vector<uint32_t> children;
        bool dirty=true;
        bool subtreeDirty=true;
    };
    std::map<uint32_t,Node> nodes_; // index sorted for deterministic updates
    std::vector<uint32_t> roots_;
    std::set<uint32_t> pendingRoots_; // no full root scan on clean frames
    size_t lastUpdated_=0;
    size_t lastVisited_=0;
    static void erase(std::vector<uint32_t>& v,uint32_t index) {
        auto it=std::lower_bound(v.begin(),v.end(),index);
        if(it!=v.end()&&*it==index)v.erase(it);
    }
    static void insert(std::vector<uint32_t>& v,uint32_t index) {
        auto it=std::lower_bound(v.begin(),v.end(),index);
        if(it==v.end()||*it!=index)v.insert(it,index);
    }
    bool cycle(Entity child,Entity parent)const {
        Entity current=parent;
        for(size_t n=0;n<=nodes_.size();++n) {
            if(!current.valid())return false;
            if(current==child)return true;
            auto it=nodes_.find(current.index);
            if(it==nodes_.end()||it->second.entity!=current)return false;
            current=it->second.parent;
        }
        return true;
    }
public:
    size_t size()const{return nodes_.size();}
    size_t lastUpdated()const{return lastUpdated_;}
    size_t lastVisited()const{return lastVisited_;}
    bool contains(Entity e)const {
        auto it=nodes_.find(e.index);
        return e.valid()&&it!=nodes_.end()&&it->second.entity==e;
    }
    // Register an entity with default local transform, without a parent.
    bool attach(World& w,Entity entity,LocalTransform local={}) {
        if(!w.alive(entity)||contains(entity))return false;
        if(auto it=nodes_.find(entity.index);it!=nodes_.end())return false;
        w.add<LocalTransform>(entity,local);
        w.add<WorldTransform>(entity,hierarchy_detail::root(local));
        nodes_.emplace(entity.index,Node{entity,kNullEntity,{},true,true});
        insert(roots_,entity.index);
        pendingRoots_.insert(entity.index);
        return true;
    }
    // Keep local values when changing parent. No implicit floating-point
    // matrix inverse; preserve-world reparent is a separate authoring operation.
    bool setParent(World& w,Entity child,Entity parent=kNullEntity) {
        if(!contains(child)||!w.alive(child))return false;
        if(parent.valid()&&(!contains(parent)||!w.alive(parent)||cycle(child,parent)))return false;
        auto& node=nodes_.at(child.index);
        if(node.parent==parent)return true;
        if(node.parent.valid())erase(nodes_.at(node.parent.index).children,child.index);
        else {
            erase(roots_,child.index);
            pendingRoots_.erase(child.index);
        }
        node.parent=parent;
        if(parent.valid())insert(nodes_.at(parent.index).children,child.index);
        else insert(roots_,child.index);
        if(parent.valid())w.add<Parent>(child,Parent{parent});
        else w.remove<Parent>(child);
        // Must mark descendants even if this node was already dirty.
        invalidate(child);
        return true;
    }
    bool setLocal(World& w,Entity entity,LocalTransform local) {
        if(!contains(entity)||!w.alive(entity))return false;
        w.add<LocalTransform>(entity,local);
        invalidate(entity);
        return true;
    }
    void invalidate(Entity entity) {
        if(!contains(entity))return;
        // Every descendant must be recomputed when its ancestor changes.
        std::vector<uint32_t> stack{entity.index};
        while(!stack.empty()){
            uint32_t id=stack.back();stack.pop_back();
            auto it=nodes_.find(id);if(it==nodes_.end())continue;
            it->second.dirty=true;
            it->second.subtreeDirty=true;
            for(uint32_t child:it->second.children)stack.push_back(child);
        }
        // Propagate a cheap subtree marker upwards, so an edited grandchild
        // remains reachable through any number of otherwise-clean ancestors.
        Entity root=entity;
        Entity parent=nodes_.at(entity.index).parent;
        while(parent.valid()) {
            auto it=nodes_.find(parent.index);
            if(it==nodes_.end()||it->second.entity!=parent)break;
            it->second.subtreeDirty=true;
            root=parent;
            parent=it->second.parent;
        }
        pendingRoots_.insert(root.index);
    }
    // Detach direct children to scene roots. Destroying the Entity itself is
    // caller-owned; invoke before World::destroy()/flush().
    bool detach(World& w,Entity entity) {
        if(!contains(entity))return false;
        auto children=nodes_.at(entity.index).children;
        for(uint32_t child:children)setParent(w,nodes_.at(child).entity);
        auto parent=nodes_.at(entity.index).parent;
        if(parent.valid())erase(nodes_.at(parent.index).children,entity.index);
        else erase(roots_,entity.index);
        pendingRoots_.erase(entity.index);
        nodes_.erase(entity.index);
        if(w.alive(entity)){
            w.remove<Parent>(entity);
            w.remove<LocalTransform>(entity);
            w.remove<WorldTransform>(entity);
        }
        return true;
    }
    // Rebuild derived indices from ECS components after save/load/rollback.
    // Invalid/cyclic parent links become roots, preserving valid local data.
    void rebuild(World& w) {
        nodes_.clear();roots_.clear();pendingRoots_.clear();
        w.eachExistingConst<LocalTransform>([&](Entity e,const LocalTransform&){
            if(w.alive(e))nodes_.emplace(e.index,Node{e,kNullEntity,{},true,true});
        });
        for(auto& [id,node]:nodes_)roots_.push_back(id);
        std::vector<std::pair<Entity,Entity>> links;
        std::vector<Entity> invalid;
        w.eachExistingConst<Parent>([&](Entity e,const Parent& p){
            if(!contains(e))return;
            if(contains(p.value))links.push_back({e,p.value});
            else invalid.push_back(e);
        });
        // Repair invalid links outside the query iteration.
        for(Entity child:invalid)w.remove<Parent>(child);
        // Sparse-set swap-removals change dense iteration order. Resolve
        // conflicting/cyclic edges by canonical child index instead, so a
        // rollback produces the same graph regardless of pool history.
        std::sort(links.begin(),links.end(),[](const auto& lhs,const auto& rhs) {
            return lhs.first.index<rhs.first.index;
        });
        // Repair cyclic links as well; authoritative components and graph
        // must have identical parent relations after rebuild.
        for(auto [child,parent]:links) {
            if(cycle(child,parent)) {
                w.remove<Parent>(child);
                continue;
            }
            auto& node=nodes_.at(child.index);
            if(node.parent.valid())erase(nodes_.at(node.parent.index).children,child.index);
            else erase(roots_,child.index);
            node.parent=parent;
            insert(nodes_.at(parent.index).children,child.index);
        }
        pendingRoots_.insert(roots_.begin(),roots_.end());
        lastUpdated_=0;
        lastVisited_=0;
    }
    // Top-down iterative update. Clean branches are pruned; no recursion,
    // so arbitrarily deep nested scene hierarchies cannot overflow the stack.
    size_t update(World& w) {
        lastUpdated_=0;
        lastVisited_=0;
        if(pendingRoots_.empty())return 0;
        std::vector<uint32_t> stack;
        stack.reserve(pendingRoots_.size());
        for(auto it=pendingRoots_.rbegin();it!=pendingRoots_.rend();++it)
            stack.push_back(*it);
        pendingRoots_.clear();
        while(!stack.empty()){
            uint32_t index=stack.back();stack.pop_back();
            ++lastVisited_;
            auto it=nodes_.find(index);if(it==nodes_.end())continue;
            auto& node=it->second;
            if(!w.alive(node.entity))continue; // caller must detach stale nodes
            if(node.dirty){
                auto* local=w.tryGetConst<LocalTransform>(node.entity);
                if(local) {
                    WorldTransform result=hierarchy_detail::root(*local);
                    if(node.parent.valid()) {
                        auto* parent=w.tryGetConst<WorldTransform>(node.parent);
                        if(parent)result=hierarchy_detail::combine(*parent,*local);
                    }
                    w.add<WorldTransform>(node.entity,result);
                    ++lastUpdated_;
                }
                node.dirty=false;
            }
            node.subtreeDirty=false;
            for(auto child=node.children.rbegin();child!=node.children.rend();++child)
                if(nodes_.at(*child).subtreeDirty)stack.push_back(*child);
        }
        return lastUpdated_;
    }
};
} // namespace aiverse
