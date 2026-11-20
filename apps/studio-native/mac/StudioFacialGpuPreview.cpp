#include "StudioFacialGpuPreview.h"
#include "apps/studio-native/StudioContentDirectory.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace aiverse::studio {
namespace fs=std::filesystem;

bool StudioFacialGpuPreview::readProjectFile(
        const fs::path& projectRoot,const std::string& key,
        std::string& bytes) {
    bytes.clear();
    if(key.empty() || fs::path(key).is_absolute())return false;
    const fs::path content=fs::weakly_canonical(
        studioContentRoot(projectRoot));
    const fs::path requested=fs::path(key).lexically_normal();
    const fs::path relative=requested.lexically_relative("content");
    if(relative.empty() || relative.is_absolute())return false;
    for(const auto& part:relative)
        if(part.empty() || part=="." || part=="..")return false;
    fs::path cursor=content;
    for(const auto& part:relative) {
        cursor/=part;
        std::error_code ec;
        const auto status=fs::symlink_status(cursor,ec);
        if(ec || fs::is_symlink(status))return false;
    }
    std::error_code ec;
    if(!fs::is_regular_file(cursor,ec) || ec)return false;
    const auto len=fs::file_size(cursor,ec);
    if(ec || len>128u*1024u*1024u)return false;
    std::ifstream input(cursor,std::ios::binary);
    if(!input)return false;
    bytes.assign(std::istreambuf_iterator<char>(input),
                 std::istreambuf_iterator<char>());
    return bool(input) || input.eof();
}

bool StudioFacialGpuPreview::clear(gfx::Device& device) noexcept {
    // Never free old GPU resources through a new device at the same address.
    if(model_ && !gpuLease_.validFor(device)) {
        (void)model_.release();
        facial_.clear();
        animator_.setModel(nullptr);
        pose_.clear();
        nodeWorld_.clear();
        return false;
    }
    if(model_) {
        try {
            if(!device.waitForResourceIdle()) {
                (void)model_.release(); // quarantine GPU resources on failed fence
                facial_.clear();
                animator_.setModel(nullptr);
                pose_.clear();
                nodeWorld_.clear();
                return false;
            }
            render::releaseModel(device,*model_);
            model_.reset();
        }catch(...) {
            (void)model_.release();
            facial_.clear();
            animator_.setModel(nullptr);
            pose_.clear();
            nodeWorld_.clear();
            return false;
        }
    }
    facial_.clear();
    animator_.setModel(nullptr);
    pose_.clear();
    nodeWorld_.clear();
    guid_.clear();
    projectRoot_.clear();
    revision_=0;
    rejectedRevision_=0;
    rejectedGuid_.clear();
    rejectedProject_.clear();
    rejectedError_.clear();
    return true;
}

bool StudioFacialGpuPreview::sync(
        gfx::Device& device,const std::string& projectRoot,
        const StudioFacialPreviewBinding& binding,std::string& error) {
    error.clear();
    if(!gpuLease_.validFor(device)) {
        error="Facial Model GPU epoch or owner thread mismatch";
        return false;
    }
    if(!binding.bound()) {
        if(!clear(device))error="Facial GPU retirement pending device completion";
        return error.empty();
    }
    // Rebind only when the immutable project/asset/rig generation changes.
    // Sliders and IK targets alter live session controls, not GPU ownership.
    if(model_ && revision_==binding.revision() &&
       guid_==binding.guid() && projectRoot_==projectRoot)
        return true;
    // Bad external GLBs/rigs are not re-imported 60 times/sec while the
    // Studio viewport is visible. Explicit Bind increments the revision.
    if(rejectedRevision_==binding.revision() &&
       rejectedGuid_==binding.guid() &&
       rejectedProject_==projectRoot && !rejectedError_.empty()) {
        error=rejectedError_;
        return false;
    }
    if(model_ && !device.waitForResourceIdle()) {
        error="Cannot switch facial model: previous GPU frame is still active";
        return false;
    }
    // A failed GPU upload can leave partial VB/IB/texture handles. Keep the
    // candidate alive through catch and retire behind a real device barrier.
    std::unique_ptr<render::Model> candidate;
    bool gpuUploadStarted=false;
    try {
        auto read=[root=fs::path(projectRoot)](
                const std::string& key,std::string& data) {
            return readProjectFile(root,key,data);
        };
        candidate=render::loadModel(
            "content/"+binding.path(),read);
        if(!candidate)throw std::runtime_error("Model import returned no Model");
        if(candidate->nodes.empty() || candidate->vertices.empty())
            throw std::runtime_error("Imported facial Model has no geometry");
        // Respect the imported asset's normal autoLod policy. The shared
        // Stage 186 generator now supports unskinned semantic morphs with
        // source texture aliasing. Authored/skinned LODs keep importer policy.
        lib::animation::FacialPreviewSession newSession;
        lib::animation::FacialPreviewSession::Vectors ikTargets;
        for(const auto& [name,v]:binding.vectors())
            ikTargets.emplace(name,render::float3{v[0],v[1],v[2]});
        if(!newSession.configure(*candidate,binding.document(),
                                 binding.scalars(),ikTargets,error))
            throw std::runtime_error(error);
        // Source validation completed before publishing any GPU Model.
        // Preparation and upload remain on the owning native device thread.
        render::prepareModelUpload(*candidate);
        gpuUploadStarted=true;
        if(!render::uploadModel(device,*candidate,true))
            throw std::runtime_error("GPU facial Model upload failed");
        // Previous owner is now completion-proven, so it can retire safely.
        if(model_) {
            render::releaseModel(device,*model_);
            model_.reset();
        }
        model_=std::move(candidate);
        facial_=std::move(newSession);
        animator_.setModel(model_.get());
        if(!model_->clips.empty()) {
            render::Animator::Play play;
            play.restart=true;
            play.fade=0.f;
            animator_.play(0,0,play);
        }
        projectRoot_=projectRoot;
        guid_=binding.guid();
        revision_=binding.revision();
        rejectedRevision_=0;
        rejectedGuid_.clear();
        rejectedProject_.clear();
        rejectedError_.clear();
        return true;
    }catch(const std::exception& ex) {
        error=ex.what();
        rejectedRevision_=binding.revision();
        rejectedGuid_=binding.guid();
        rejectedProject_=projectRoot;
        if(candidate && gpuUploadStarted) {
            // A candidate is not published until ALL GPU allocations succeed.
            // Never let CPU destructor release possibly submitted GPU objects.
            try {
                if(device.waitForResourceIdle())
                    render::releaseModel(device,*candidate);
                else {
                    (void)candidate.release();
                    error+=" (partial GPU Model quarantined: no completion proof)";
                }
            }catch(...) {
                (void)candidate.release();
                error+=" (partial GPU Model quarantined: cleanup failed)";
            }
        }
        rejectedError_=error;
        return false;
    }
}

bool StudioFacialGpuPreview::submit(
        render::Renderer3D& renderer,
        const StudioFacialPreviewBinding& binding,
        float dt,std::string& error) {
    error.clear();
    if(!gpuLease_.device() ||
       !gpuLease_.validFor(*gpuLease_.device())) {
        error="Facial GPU submission outside current device epoch";
        return false;
    }
    if(!ready() || !binding.bound() || binding.revision()!=revision_) {
        error="Facial render has no matching GPU Model generation";
        return false;
    }
    for(const auto& [name,value]:binding.scalars())
        if(!facial_.scalar(name,value)) {
            error="Invalid scalar facial input: "+name;
            return false;
        }
    for(const auto& [name,value]:binding.vectors())
        if(!facial_.vector(name,
                render::float3{value[0],value[1],value[2]})) {
            error="Invalid IK vector input: "+name;
            return false;
        }
    const float frameDt=std::clamp(dt,0.f,0.1f);
    animator_.update(frameDt);
    animator_.evaluate(pose_); // one parent writer, bind/clip pose once
    if(!facial_.apply(*model_,pose_,frameDt,error))
        return false; // FacialControlRig is transactional on failure
    render::worldMatrices(*model_,pose_,nodeWorld_);
    const auto a=model_->boundsMin,b=model_->boundsMax;
    const float dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z;
    const float extent=std::max({dx,dy,dz,0.0001f});
    const float scale=2.f/extent;
    const render::float3 center{(a.x+b.x)*.5f,(a.y+b.y)*.5f,(a.z+b.z)*.5f};
    render::ModelDraw draw;
    draw.model=model_.get();
    draw.pose=&pose_;
    draw.nodeWorld=&nodeWorld_;
    draw.world=render::mat4::scale({scale,scale,scale}) *
               render::mat4::translate({-center.x,-center.y,-center.z});
    renderer.drawModel(draw); // same native Renderer3D, no fake browser face
    return true;
}

} // namespace aiverse::studio
