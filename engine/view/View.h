// AIVERSE View — genre-independent presentation scene.
// View owns ONLY rendered scene state. Gameplay modules adapt their entities
// into ViewObjects; the scene doesn't know what genre produced those objects.
#pragma once
#include <aiverselib/animation/MotionWeaveSourceBridge.h>
#include <aiverselib/animation/AnimationBudgetPolicy.h>
#include <aiverselib/motionweave/MotionWeave.h>
#include "engine/render/Renderer3D.h"
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace aiverse::view {

// RenderObject is a generic draw packet. All GPU models/materials/extra
// pointers remain host-owned and valid until submit() completes.
// The optional graph operates on a separate presentation ECS World.
struct Object {
    uint64_t id=0,generation=0;
    render::ModelDraw draw;
    std::shared_ptr<const render::AnimationGraph> graph;
    render::AnimationParameters parameters;
    lib::animation::AnimationSignificanceComponent significance;
    bool visible=true;
};
// Any host may submit geometry without assuming a Model or skeleton.
// Mesh ownership/lifetime remains external; this packet has no genre data.
struct MeshObject {
    const render::Mesh* mesh=nullptr;
    render::mat4 world;
    render::float4 color{1,1,1,1};
    uint32_t flags=0;
    float specular=.25f,emissive=0.f;
    bool visible=true;
};
struct Stats {
    lib::animation::AnimationBridgeStats source;
    lib::animation::MotionWeaveScheduleStats animation;
    lib::animation::AnimationBudgetStats budget;
    size_t visible=0,drawable=0,animated=0;
};
class View final {
public:
    explicit View(size_t animationWorkers=
            lib::animation::MotionWeaveWorkerPool::recommendedWorkers())
        :workerCount_(animationWorkers){}
    View(const View&)=delete;
    View& operator=(const View&)=delete;

    // Frame staging is a separate phase: material/geometry and GPU calls
    // never overlap worker animation updates. Hosts may use the same
    // generic scene for characters, buildings, props and cinematics.
    Stats update(std::span<const Object> objects,float dt) {
        stats_={};
        std::vector<lib::animation::AnimationSourceFrame> sources;
        sources.reserve(objects.size());
        for(const auto& obj:objects) {
            if(obj.visible)++stats_.visible;
            if(obj.draw.model && obj.visible)++stats_.drawable;
            if(!obj.graph || !obj.draw.model)continue;
            ++stats_.animated;
            lib::animation::AnimationSourceFrame input;
            input.sourceId=obj.id;
            input.generation=obj.generation;
            input.model=obj.draw.model;
            input.graph=obj.graph;
            input.inputs=obj.parameters;
            sources.push_back(std::move(input));
        }
        stats_.source=bridge_.sync(presentation_,sources);
        for(const auto& obj:objects) {
            if(!obj.graph || !obj.draw.model)continue;
            const Entity e=bridge_.find(obj.id);
            if(!presentation_.alive(e))continue;
            if(!presentation_.has<lib::animation::AnimationSignificanceComponent>(e))
                presentation_.add<lib::animation::AnimationSignificanceComponent>(e);
            auto& significance=
                presentation_.get<lib::animation::AnimationSignificanceComponent>(e);
            significance=obj.significance;
            significance.visible=obj.visible && obj.significance.visible;
        }
        stats_.budget=lib::animation::assignAnimationBudgets(presentation_,budgets_);
        if(stats_.animated>=minimumParallelActors_) {
            if(!scheduler_)scheduler_=std::make_unique<
                lib::animation::MotionWeaveScheduler>(workerCount_);
            stats_.animation=lib::animation::MotionWeave::update(
                presentation_,dt,*scheduler_);
        } else
            lib::animation::MotionWeave::update(presentation_,dt);
        return stats_;
    }

    // Call only AFTER update(), before borrowed ModelDraw storage expires.
    // The renderer does not interpret source identity, movement or genre.
    // One draw packet from any host. This is the same renderer pipeline
    // used for streamed, procedural, static and animated objects.
    void submitOne(render::Renderer3D& renderer,const Object& obj) const {
        if(!obj.visible || !obj.draw.model)return;
        render::ModelDraw draw=obj.draw;
        if(obj.graph) {
            const Entity e=bridge_.find(obj.id);
            if(presentation_.alive(e)) {
                const auto* pose=presentation_.tryGetConst<
                    lib::animation::AnimationPoseComponent>(e);
                if(pose && pose->valid) {
                    draw.pose=&pose->local;
                    draw.nodeWorld=&pose->matrices;
                }
            }
        }
        if(!draw.persistentId)draw.persistentId=obj.id;
        renderer.drawModel(draw);
    }
    void submit(render::Renderer3D& renderer,
                std::span<const Object> objects) const {
        for(const auto& obj:objects)submitOne(renderer,obj);
    }
    // Ordinary mesh/collision visualization, terrain tiles, gizmo backing
    // meshes and other plain draw packets use the SAME generic View API.
    void submitMesh(render::Renderer3D& renderer,
                    const MeshObject& object) const {
        if(!object.visible || !object.mesh)return;
        renderer.drawMesh(*object.mesh,object.world,object.color,
            object.flags,object.specular,object.emissive);
    }
    void submitMeshes(render::Renderer3D& renderer,
                      std::span<const MeshObject> objects) const {
        for(const auto& object:objects)submitMesh(renderer,object);
    }

    const Stats& stats() const noexcept{return stats_;}
    void setBudgetPolicy(lib::animation::AnimationBudgetPolicy policy) {
        budgets_=policy;
    }
    void setParallelThreshold(size_t count) {minimumParallelActors_=count;}
    Entity presentationEntity(uint64_t id) const {return bridge_.find(id);}
    const World& presentation() const noexcept{return presentation_;}
    // For controlled host-only effects such as montage slots. Never
    // snapshot this World or mutate it while update() is running.
    World& presentation() noexcept{return presentation_;}
    void clear() {
        bridge_.clear(presentation_);
        stats_={};
    }
private:
    World presentation_;
    lib::animation::MotionWeaveSourceBridge bridge_;
    std::unique_ptr<lib::animation::MotionWeaveScheduler> scheduler_;
    lib::animation::AnimationBudgetPolicy budgets_;
    size_t workerCount_=0,minimumParallelActors_=16;
    Stats stats_;
};
} // namespace aiverse::view

// Preferred user-facing type: aiverse::View.
// Legacy game::RtsView remains a compatibility adapter until all RTS-specific
// terrain/fog/unit event authoring is moved to the owning game module.
namespace aiverse {using View=view::View;}
