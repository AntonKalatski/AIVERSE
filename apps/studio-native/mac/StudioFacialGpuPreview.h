// Native Studio facial preview owns one real imported Model and ONE Animator.
// Reuses MotionWeave FacialPreviewSession for expression and IK postprocessing.
// Only called on the Metal gfx::Device owner thread, never from browser code.
#pragma once
#include "apps/studio-native/StudioFacialPreviewBinding.h"
#include "engine/render/Animation.h"
#include "engine/render/Renderer3D.h"
#include <aiverselib/animation/FacialPreviewSession.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace aiverse::studio {

class StudioFacialGpuPreview final {
public:
    StudioFacialGpuPreview()=default;
    StudioFacialGpuPreview(const StudioFacialGpuPreview&)=delete;
    StudioFacialGpuPreview& operator=(const StudioFacialGpuPreview&)=delete;
    // Discard GPU resources only after device completion; on failure leave
    // the Model quarantined instead of destroying in-flight handles.
    bool clear(gfx::Device& device) noexcept;
    bool sync(gfx::Device& device,const std::string& projectRoot,
              const StudioFacialPreviewBinding& binding,std::string& error);
    bool submit(render::Renderer3D& renderer,
                const StudioFacialPreviewBinding& binding,
                float dt,std::string& error);
    const render::Model* model() const noexcept {return model_.get();}
    bool ready() const noexcept {return bool(model_) && facial_.ready();}
    uint64_t sourceRevision() const noexcept {return revision_;}
private:
    static bool readProjectFile(const std::filesystem::path& projectRoot,
                                const std::string& key,std::string& bytes);
    std::unique_ptr<render::Model> model_;
    lib::animation::FacialPreviewSession facial_;
    render::Animator animator_; // ONLY parent pose evaluator in this preview
    render::Pose pose_;
    std::vector<render::mat4> nodeWorld_;
    std::string projectRoot_,guid_;
    uint64_t revision_=0;
    uint64_t rejectedRevision_=0;
    std::string rejectedGuid_,rejectedProject_,rejectedError_;
};

} // namespace aiverse::studio
