// TerraWeave tree trunks -> optional Box3D static capsules (AIVERSE::physics).
// Same TreeTrunkCollider list as the SpatialWorld publisher; one static
// capsule per trunk, grouped per streamed tile. Call on the physics scene
// owner thread between physics::Scene::step() calls.
#pragma once
#include "engine/world/TerraWeaveTreeColliders.h"
#include "engine/physics/Scene.h"
#include <algorithm>
#include <map>
#include <span>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {

inline physics::BodyDesc makeTreeTrunkBox3DBody(const TreeTrunkCollider& c,uint32_t userData=0){
    if(c.radiusCm<=0 || c.heightCm<=0)
        throw std::invalid_argument("TerraWeave Box3D trunk shape");
    if(c.xCm<-10000000 || c.xCm>10000000 || c.zCm<-10000000 || c.zCm>10000000)
        throw std::out_of_range("TerraWeave Box3D trunk outside physics scene");
    physics::BodyDesc body;
    body.motion=physics::Motion::Static;
    body.shape=physics::ShapeKind::Capsule;
    body.category=physics::kCategoryStatic;
    body.userData=userData;
    const float r=float(c.radiusCm)*0.01f, h=float(c.heightCm)*0.01f;
    body.radius=r;
    body.halfHeight=std::max(0.01f,(h-2.f*r)*0.5f);
    body.position={float(c.xCm)*0.01f,float(c.baseYCm)*0.01f+h*0.5f,float(c.zCm)*0.01f};
    body.friction=0.8f;
    return body;
}

class TreeTrunkBox3DPublisher {
public:
    size_t bodyCount()const{
        size_t n=0; for(const auto& [c,v]:tiles_){(void)c;n+=v.size();} return n;
    }
    const std::vector<physics::BodyHandle>* bodies(world::Cell cell)const{
        const auto it=tiles_.find(cell); return it==tiles_.end()?nullptr:&it->second;
    }
    // New bodies are created before the old ones are destroyed; on failure
    // the created ones are rolled back and the old tile stays intact.
    bool publish(physics::Scene& scene,world::Cell cell,
                 std::span<const TreeTrunkCollider> trunks,uint32_t userData=0){
        if(trunks.size()>8192)throw std::length_error("TerraWeave Box3D trunk tile budget");
        std::vector<physics::BodyHandle> created;
        created.reserve(trunks.size());
        for(const auto& t:trunks){
            const auto handle=scene.create(makeTreeTrunkBox3DBody(t,userData));
            if(handle==physics::kNoBody){
                for(auto h:created)scene.destroy(h);
                return false;
            }
            created.push_back(handle);
        }
        auto& slot=tiles_[cell];
        for(auto h:slot)if(scene.valid(h))scene.destroy(h);
        slot=std::move(created);
        return true;
    }
    void unload(physics::Scene& scene,world::Cell cell){
        const auto it=tiles_.find(cell);
        if(it==tiles_.end())return;
        for(auto h:it->second)if(scene.valid(h))scene.destroy(h);
        tiles_.erase(it);
    }
    void shutdown(physics::Scene& scene){
        while(!tiles_.empty())unload(scene,tiles_.begin()->first);
    }
private:
    std::map<world::Cell,std::vector<physics::BodyHandle>> tiles_;
};

} // namespace aiverse::terraweave
