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

bool StudioFacialGpuPreview::clear(
        gfx::Device& device,render::Renderer3D& renderer) noexcept {
    // Detach CPU readers before the shared owner drains GPU retirements.
    appearanceOnly_=false;
    facial_.clear();
    animator_.setModel(nullptr);
    pose_.clear();
    nodeWorld_.clear();
    model_=nullptr;hair_=nullptr;hairKey_.clear();creatorRig_=vivid::RigAuthoring{};
    guid_.clear();projectRoot_.clear();modelKey_.clear();
    pendingSource_.clear();pendingProject_.clear();pendingGuid_.clear();
    pendingRevision_=revision_=rejectedRevision_=0;
    rejectedGuid_.clear();rejectedProject_.clear();rejectedError_.clear();
    readerProject_.clear();
    if(!provider_)return true;
    try {
        provider_->stopAsync();
        if(!gpuLease_.validFor(device))
            throw std::runtime_error("Facial Model GPU epoch expired");
        for(const auto& [key,model]:provider_->resources().models()) {
            (void)key;
            if(model)renderer.invalidateModelCachesFor(model.get());
        }
        provider_->shutdown(device);
        provider_.reset();
        return true;
    }catch(...) {
        // The shared cache still owns partial/resident GPU resources. Do
        // not destroy its CPU ownership when completion cannot be proven.
        (void)provider_.release();
        return false;
    }
}

bool StudioFacialGpuPreview::sync(
        gfx::Device& device,render::Renderer3D& renderer,
        const std::string& projectRoot,
        const StudioFacialPreviewBinding& binding,std::string& error) {
    error.clear();
    if(!gpuLease_.validFor(device)) {
        error="Facial Model GPU epoch or owner thread mismatch";
        return false;
    }
    if(!binding.bound()) {
        if(!clear(device,renderer))error="Facial GPU retirement pending device completion";
        return error.empty();
    }
    try {
        // Identical relative texture paths in different projects must never
        // reuse the previous project's decoded/GPU texture cache.
        if(provider_ && !readerProject_.empty() && readerProject_!=projectRoot)
            if(!clear(device,renderer))
                throw std::runtime_error("Previous project GPU retirement failed");
        if(!provider_) {
            provider_=std::make_unique<render::ModelAssetProvider>();
            provider_->setGpuDeviceLease(gpuLease_);
            provider_->setCpuMaxQueued(1);
        }
        // Poll before beginFrame and before acquiring the render read lease.
        provider_->collectRetired(device);
        if(model_ && revision_==binding.revision() &&
           guid_==binding.guid() && projectRoot_==projectRoot)return true;
        if(rejectedRevision_==binding.revision() &&
           rejectedGuid_==binding.guid() && rejectedProject_==projectRoot &&
           !rejectedError_.empty()) {
            error=rejectedError_;
            return false;
        }
        if(binding.creatorMode()) {
            // Digital Human Creator: regenerate the procedural VIVID human
            // on this owner thread from the validated document copy, then
            // publish head + groom cards under fresh generation keys. The
            // previous generation stays drawable until publication succeeds.
            if(!pendingSource_.empty()){provider_->cancelAsync(pendingSource_);pendingSource_.clear();}
            vivid::HumanCreatorWorkbench creator;std::string loadError;
            if(!creator.loadDocument(binding.creatorDocument(),&loadError))
                throw std::runtime_error("Human Creator: "+loadError);
            auto built=creator.buildModels(vivid::GroomQuality::Medium);
            const int previewBase=vivid::appendHumanPreviewClips(*built.head);
            if(provider_->publicationBackpressured()) {
                error="Human Creator publication waiting for GPU retirement";
                return false;
            }
            const auto serial=std::to_string(++publicationSerial_);
            auto* head=provider_->publishPrepared(device,"studio-creator-head/"+serial,std::move(built.head));
            render::Model* hair=built.hair?provider_->publishPrepared(device,"studio-creator-hair/"+serial,std::move(built.hair)):nullptr;
            std::vector<std::string> retired;
            if(!modelKey_.empty())retired.push_back(modelKey_);
            if(!hairKey_.empty())retired.push_back(hairKey_);
            if(!retired.empty())
                provider_->resources().invalidateSources(device,retired,[&](const auto& affected) {
                    for(const auto& item:affected)
                        if(item.model)renderer.invalidateModelCachesFor(item.model);
                    animator_.setModel(nullptr);
                    facial_.clear();
                });
            appearanceOnly_=true;facial_=lib::animation::FacialPreviewSession{};
            model_=head;hair_=hair;
            modelKey_="studio-creator-head/"+serial;hairKey_=hair?"studio-creator-hair/"+serial:std::string();
            creatorRig_=creator.rigCorrections();
            animator_.setModel(model_);
            previewClipBase_=previewBase;activePreviewClip_=-1;
            projectRoot_=projectRoot;guid_=binding.guid();revision_=binding.revision();
            pendingRevision_=0;pendingGuid_.clear();pendingProject_.clear();
            rejectedRevision_=0;rejectedGuid_.clear();rejectedProject_.clear();rejectedError_.clear();
            return true;
        }
        if(!hairKey_.empty()) {
            // Leaving Creator mode: the groom generation is not part of an
            // imported character; retire it with the next publication.
            provider_->resources().invalidateSources(device,{hairKey_},[&](const auto& affected) {
                for(const auto& item:affected)
                    if(item.model)renderer.invalidateModelCachesFor(item.model);
            });
            hairKey_.clear();hair_=nullptr;creatorRig_=vivid::RigAuthoring{};
        }
        const std::string source="content/"+binding.path();
        if(pendingRevision_!=binding.revision() ||
           pendingGuid_!=binding.guid() || pendingProject_!=projectRoot) {
            if(!pendingSource_.empty())provider_->cancelAsync(pendingSource_);
            render::ModelAssetProvider::Prepared discarded;
            while(provider_->takePrepared(discarded)) {}
            if(readerProject_!=projectRoot) {
                // Rare project switch joins the old reader's CPU work.
                // Ordinary generation changes only invalidate its result.
                provider_->setReader([root=fs::path(projectRoot)](
                        const std::string& key,std::string& bytes) {
                    return readProjectFile(root,key,bytes);
                });
                readerProject_=projectRoot;
            }
            if(!provider_->requestAsync(source,render::ModelLoadPriority::Interactive)) {
                error="Facial model import waiting for resource capacity";
                return false;
            }
            pendingSource_=source;
            pendingRevision_=binding.revision();
            pendingGuid_=binding.guid();
            pendingProject_=projectRoot;
        }
        render::ModelAssetProvider::Prepared prepared;
        if(provider_->publicationBackpressured()) {
            error="Facial model publication waiting for GPU retirement";
            return false;
        }
        if(!provider_->takePrepared(prepared)) {
            error="Loading facial model";
            return false;
        }
        if(prepared.source!=pendingSource_) {
            error="Loading facial model";
            return false; // stale different-source result never gets published
        }
        if(!prepared.model)
            throw std::runtime_error(prepared.warnings.empty()
                ?"Model import returned no Model":prepared.warnings.back());
        if(prepared.model->nodes.empty() || prepared.model->vertices.empty())
            throw std::runtime_error("Imported facial Model has no geometry");
        lib::animation::FacialPreviewSession newSession;
        lib::animation::FacialPreviewSession::Vectors ikTargets;
        for(const auto& [name,v]:binding.vectors())
            ikTargets.emplace(name,render::float3{v[0],v[1],v[2]});
        if(binding.face())vivid::applyHumanFaceShape(*prepared.model,*binding.face());
        if(binding.appearance())vivid::applyHumanAppearance(*prepared.model,*binding.appearance());
        if(!binding.appearanceOnly() && !newSession.configure(*prepared.model,binding.document(),
                                 binding.scalars(),ikTargets,error))
            throw std::runtime_error(error);
        const int previewBase=vivid::appendHumanPreviewClips(*prepared.model);
        // CPU decoding/preparation is complete; validate the rig BEFORE any
        // GPU publication. Fresh owner keys preserve last-good on failure.
        const auto key="studio-facial-generation/"+std::to_string(++publicationSerial_);
        auto* published=provider_->publishPrepared(device,key,std::move(prepared.model));
        if(!modelKey_.empty()) {
            // Generation keys identify publications, not CPU source paths;
            // retire them without adding synthetic queue revision entries.
            provider_->resources().invalidateSources(device,{modelKey_},[&](const auto& affected) {
                for(const auto& item:affected)
                    if(item.model)renderer.invalidateModelCachesFor(item.model);
                animator_.setModel(nullptr);
                facial_.clear();
            });
        }
        appearanceOnly_=binding.appearanceOnly();
        model_=published;
        modelKey_=key;
        facial_=std::move(newSession);
        animator_.setModel(model_);
        previewClipBase_=previewBase;activePreviewClip_=-1;
        projectRoot_=projectRoot;guid_=binding.guid();revision_=binding.revision();
        pendingRevision_=0;pendingSource_.clear();pendingGuid_.clear();pendingProject_.clear();
        rejectedRevision_=0;rejectedGuid_.clear();rejectedProject_.clear();rejectedError_.clear();
        return true;
    }catch(const std::exception& ex) {
        error=ex.what();
        rejectedRevision_=binding.revision();
        rejectedGuid_=binding.guid();rejectedProject_=projectRoot;rejectedError_=error;
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
    if(!provider_ || provider_->resources().activeModelReaders()==0) {
        error="Facial render requires a frame Model read lease";
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
    const auto& animation=binding.previewAnimation;
    const int requestedClip=previewClipBase_+animation.poseIndex()*2+(animation.faceIdle?1:0);
    if(activePreviewClip_!=requestedClip){vivid::playHumanPreview(animator_,requestedClip,animation.playing);activePreviewClip_=requestedClip;}
    animator_.setSpeed(0,animation.speed);
    animator_.update(animation.playing?frameDt:0.f);
    animator_.evaluate(pose_); // one parent writer, bind/clip pose once
    if(!appearanceOnly_ && !facial_.apply(*model_,pose_,frameDt,error))
        return false; // FacialControlRig is transactional on failure
    render::worldMatrices(*model_,pose_,nodeWorld_);
    if(binding.creatorMode() && !creatorRig_.edits().empty()) {
        // Presentation-only rig correction overlay (Creator section 8).
        std::vector<render::mat4> corrected(nodeWorld_.size());
        if(!creatorRig_.apply(*model_,nodeWorld_,corrected)) {
            error="Human Creator rig correction rejected (unknown node or singular pose)";
            return false;
        }
        nodeWorld_=std::move(corrected);
    }
    if(binding.appearance()) {
        try {vivid::applyHumanBody(*model_,*binding.appearance(),nodeWorld_);}
        catch(const std::exception& ex){error=ex.what();return false;}
    }
    const auto a=model_->boundsMin,b=model_->boundsMax;
    const float dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z;
    const float extent=std::max({dx,dy,dz,0.0001f});
    const float scale=2.f/extent;
    const render::float3 center{(a.x+b.x)*.5f,(a.y+b.y)*.5f,(a.z+b.z)*.5f};
    render::ModelDraw draw;
    draw.model=model_;
    draw.pose=&pose_;
    draw.nodeWorld=&nodeWorld_;
    draw.world=render::mat4::scale({scale,scale,scale}) *
               render::mat4::translate({-center.x,-center.y,-center.z});
    renderer.drawModel(draw); // same native Renderer3D, no fake browser face
    if(binding.creatorMode() && hair_) {
        // Groom cards are authored in head space at bind pose; drawn with
        // the head's framing. Follow-the-scalp simulation stays in
        // tools/vivid-creator (dynamic stream), not in this static preview.
        render::ModelDraw hairDraw;
        hairDraw.model=hair_;
        hairDraw.world=draw.world;
        renderer.drawModel(hairDraw);
    }
    return true;
}

} // namespace aiverse::studio
