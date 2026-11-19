// VIVID whole-human GPU retirement: model streams, images and actor buffers
// remain owned until a verified GPU completion token or successful idle drain.
// Never delete a Model before all GPU draws referencing its vertex stream end.
#pragma once
#include "HumanPreview.h"
#include "GpuActorStream.h"
#include "CpuActorStream.h"
#include "engine/render/AsyncResourceRetirementQueue.h"
#include <aiverselib/motionweave/MotionWeave.h>
#include <memory>
#include <utility>

namespace aiverse::vivid {
class HumanRetirementQueue final {
public:
    struct Resident {
        HumanPreviewAsset human;
        std::unique_ptr<GpuActorStream> actor;
        std::unique_ptr<CpuActorStream> cpuActor;
        std::unique_ptr<World> world;
        std::shared_ptr<const render::AnimationGraph> graph;
    };
    using Resource=std::unique_ptr<Resident>;
    using Queue=render::AsyncResourceRetirementQueue<Resource>;

    explicit HumanRetirementQueue(gfx::Device& device):device_(device){}
    HumanRetirementQueue(const HumanRetirementQueue&)=delete;
    HumanRetirementQueue& operator=(const HumanRetirementQueue&)=delete;

    // Reserve pending slot *before* the caller transfers old live owners.
    // On allocation failure no live Model/World/actor pointer has moved.
    Resident* enqueueEmpty() {
        auto value=std::make_unique<Resident>();
        auto* owned=value.get();
        queue_.enqueueFrom(value);
        return owned;
    }
    // Call collect() at the next frame boundary AFTER the previous endFrame.
    // Returns false for pending or failed tokens; never assumes completion.
    bool collect() {
        return queue_.collect(
            [&](){return device_.beginResourceRetirement();},
            [&](std::uint64_t token){return device_.resourceRetirementComplete(token);},
            [&](std::uint64_t token){
                if(device_.resourceRetirementStatus(token)==
                   gfx::ResourceRetirementStatus::Complete)
                    device_.endResourceRetirement(token);
                else device_.abandonRetirementTokenAfterIdle(token);
            },
            [&](){return device_.waitForResourceIdle();},
            [&](Resource& resource){release(resource);},
            [&](std::uint64_t token){
                return device_.resourceRetirementStatus(token)==
                    gfx::ResourceRetirementStatus::Failed;
            });
    }
    bool drain() {
        return queue_.drain(
            [&](){return device_.waitForResourceIdle();},
            [&](std::uint64_t token){
                device_.abandonRetirementTokenAfterIdle(token);
            },
            [&](Resource& resource){release(resource);});
    }
    std::size_t pending() const noexcept {return queue_.pending();}
    Queue::Stats stats() const noexcept {return queue_.stats();}
private:
    void release(Resource& resource) noexcept {
        if(!resource)return;
        // MotionWeave still borrows Model; release it before the Model's
        // native buffers/images are retired. The token proves GPU completion.
        resource->world.reset();
        resource->graph.reset();
        if(resource->actor) {
            resource->actor->releaseAfterGpuCompletion();
            resource->actor.reset();
        }
        if(resource->cpuActor) {
            resource->cpuActor->releaseAfterGpuCompletion();
            resource->cpuActor.reset();
        }
        if(resource->human.hair)
            render::releaseModel(device_,*resource->human.hair);
        if(resource->human.head)
            render::releaseModel(device_,*resource->human.head);
        resource.reset();
    }
    gfx::Device& device_;
    Queue queue_;
};
} // namespace aiverse::vivid
