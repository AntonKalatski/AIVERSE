// AIVERSE LENS: execute an ECS CameraRenderPlan into screen and managed RTs.
// The host owns Device::beginFrame/endFrame, the renderer, and scene data;
// this layer owns no gameplay state and never modifies the ECS World.
// A renderer pass must not sample its own output; all camera-produced RT reads
// must be declared in CameraDependenciesComponent for DAG validation.
#pragma once
#include "CameraRenderScheduler.h"
#include "RenderTargetPool.h"
#include "Renderer3D.h"
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

namespace aiverse::render {

enum class CameraExecutionIssue : uint8_t {
    MissingInput, TargetUnavailable, InvalidFramebuffer, SharedPostProcess
};
struct CameraExecutionDiagnostic {
    Entity entity{};
    CameraExecutionIssue reason=CameraExecutionIssue::MissingInput;
    uint64_t targetAssetKey=0;
};
struct CameraExecutionResult {
    CameraRenderPlan plan;
    std::vector<CameraExecutionDiagnostic> diagnostics;
    size_t submitted=0, skipped=0;
};

class CameraRenderPipeline {
public:
    CameraRenderPipeline(gfx::Device& device, Renderer3D& renderer,
                         CameraRenderScheduler& scheduler)
        : device_(device), renderer_(renderer), scheduler_(scheduler) {
        targets_.init(device);
    }
    CameraRenderTargetPool& targets() { return targets_; }
    const CameraRenderTargetPool& targets() const { return targets_; }

    template<class Submit>
    CameraExecutionResult execute(const World& world,uint64_t frame,double seconds,
                                   Submit&& submit,Vec3 renderOrigin={}) {
        return executeWithPost(world,frame,seconds,std::forward<Submit>(submit),
            [](const EcsCameraView&)->PostProcess* {return nullptr;},renderOrigin);
    }

    // A post provider must return a *separate persistent PostProcess* per
    // camera. Shared PostFX instances are rejected within the same frame to
    // prevent TAA/exposure history contamination. The host owns those objects.
    template<class Submit,class PostProvider>
    CameraExecutionResult executeWithPost(const World& world,uint64_t frame,
             double seconds,Submit&& submit,PostProvider&& postFor,
             Vec3 renderOrigin={}) {
        CameraExecutionResult result;
        targets_.collectCompleted();
        result.plan=scheduler_.buildPlan(world,frame,seconds,renderOrigin);
        std::unordered_set<PostProcess*> usedPost;
        std::unordered_set<uint64_t> failedTargets;
        for(const auto& view: result.plan.jobs) {
            bool inputsReady=true;
            uint64_t missing=0;
            const auto& inputs=view.dependencies;
            for(size_t j=0;j<inputs.count;++j) {
                const uint64_t key=inputs.reads[j];
                if(failedTargets.count(key) || !targets_.color(key).valid()) {
                    inputsReady=false; missing=key; break;
                }
            }
            if (!inputsReady) {
                ++result.skipped;
                result.diagnostics.push_back({view.entity,CameraExecutionIssue::MissingInput,missing});
                if(view.output.kind==CameraOutputKind::Texture)
                    failedTargets.insert(view.output.targetAssetKey);
                continue;
            }
            int width=device_.framebufferWidth(),height=device_.framebufferHeight();
            gfx::PassDesc pass;
            CameraRenderTargetPool::Target const* target=nullptr;
            if(view.output.kind==CameraOutputKind::Texture) {
                if(view.output.width) width=int(view.output.width);
                if(view.output.height) height=int(view.output.height);
                target=targets_.acquire(view.output.targetAssetKey,width,height,
                                         view.output.hdr,view.output.depth);
                if (!target) {
                    ++result.skipped;
                    failedTargets.insert(view.output.targetAssetKey);
                    result.diagnostics.push_back({view.entity,CameraExecutionIssue::TargetUnavailable,
                                                   view.output.targetAssetKey});
                    continue;
                }
                pass.colorTarget=target->color;
                pass.depthTarget=target->depth;
            }
            if(width<=0 || height<=0) {
                ++result.skipped;
                result.diagnostics.push_back({view.entity,CameraExecutionIssue::InvalidFramebuffer,
                                               view.output.targetAssetKey});
                if(target)failedTargets.insert(view.output.targetAssetKey);
                continue;
            }
            PostProcess* post=postFor(view);
            if(post && post->active() && !usedPost.insert(post).second) {
                // Explicit opt-out is safer than silently sharing temporal
                // accumulation between distinct ECS camera entities.
                post=nullptr;
                result.diagnostics.push_back({view.entity,CameraExecutionIssue::SharedPostProcess});
            }
            renderer_.beginScene(view.camera,width,height);
            submit(view,renderer_); // renderer must be initialized by the host
            renderer_.render(pass,post);
            renderer_.resolvePostFx(); // no-op without an active post chain
            device_.endPass();
            if(target)targets_.markReady(view.output.targetAssetKey);
            scheduler_.acknowledge(view.entity,view.output,frame,seconds,true);
            ++result.submitted;
        }
        return result;
    }
private:
    gfx::Device& device_;
    Renderer3D& renderer_;
    CameraRenderScheduler& scheduler_;
    CameraRenderTargetPool targets_;
};

} // namespace aiverse::render
