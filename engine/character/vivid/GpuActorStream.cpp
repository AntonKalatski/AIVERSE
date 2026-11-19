#include "GpuActorStream.h"

#include <limits>
#include <vector>

namespace aiverse::vivid {

bool GpuActorStream::initialize(gfx::Device& device,
                                const render::Model& uploadedModel,
                                const SparseMorphLibrary& library,
                                std::span<const render::ModelVertex> original) {
    if (device_ || morph_.ready() || mesh_.ready()) {
        error_ = "VIVID actor: release existing GPU resources at an idle boundary before reinitializing";
        return false;
    }
    error_.clear();
    if (device.info().backend != gfx::Backend::Metal ||
        !device.supportsCompute() || !device.gpuHealthy()) {
        error_ = "VIVID actor: currently requires healthy Metal compute";
        return false;
    }
    if (original.empty() || original.size() != library.vertices() ||
        original.size() > std::numeric_limits<std::uint32_t>::max() ||
        render::modelVertexOverrideStatus(uploadedModel,uploadedModel.vb,
                    static_cast<std::uint32_t>(original.size())) !=
                    render::ModelVertexOverrideStatus::Ready) {
        error_ = "VIVID actor: full dedicated model-relative 60-byte vertex stream required";
        return false;
    }

    std::vector<MorphVertex> base(original.size());
    for(std::size_t i=0; i<original.size(); ++i) {
        const auto& source=original[i];
        base[i].position={source.pos.x,source.pos.y,source.pos.z};
        base[i].normal={source.nrm.x,source.nrm.y,source.nrm.z};
        base[i].tangent={source.tangent.x,source.tangent.y,source.tangent.z};
    }
    if(!morph_.initialize(device,library,base)) {
        error_ = "VIVID actor: morph GPU initialization failed: " + morph_.lastError();
        return false;
    }
    if(!mesh_.initialize(device,original)) {
        error_ = "VIVID actor: model vertex packing initialization failed: " +
                 mesh_.lastError();
        morph_.release(); // no frame has referenced these new resources
        return false;
    }

    device_=&device;
    model_=&uploadedModel;
    weights_.assign(library.targets(),0.0f);
    lastAttemptedFrame_=lastCompletedFrame_=0;
    return true;
}

bool GpuActorStream::dispatch(const FaceFrame& face,
                              const FaceShapeBinding& mapping,
                              float influence, bool normalizeDirections) {
    if(!ready() || !model_ || !device_->gpuHealthy()) {
        error_ = "VIVID actor: not initialized or graphics device unhealthy";
        return false;
    }
    const auto serial=device_->frameSerial();
    if(!serial || serial==lastAttemptedFrame_) {
        error_ = "VIVID actor: one compute submission per actor per live Metal frame";
        return false;
    }
    if(mapping.targets()!=weights_.size() || !mapping.evaluate(face,weights_,influence)) {
        error_ = "VIVID actor: invalid semantic face/shape mapping";
        return false;
    }

    // Record the attempt before submitting either GPU compute. If an encoder
    // or allocation fails, retry only on a later frame to prevent partially
    // overwritten output being mistaken for a valid visible character.
    lastAttemptedFrame_=serial;
    if(!morph_.dispatch(weights_,normalizeDirections)) {
        error_ = "VIVID actor: sparse morph dispatch failed: " + morph_.lastError();
        return false;
    }
    if(!mesh_.dispatch(morph_.outputBuffer(),morph_.vertexCount())) {
        error_ = "VIVID actor: model vertex dispatch failed: " + mesh_.lastError();
        return false;
    }
    lastCompletedFrame_=serial;
    error_.clear();
    return true;
}

bool GpuActorStream::attach(render::ModelDraw& draw) const noexcept {
    if(!ready() || !model_ || draw.model!=model_ || draw.vertexOverride.valid() ||
       draw.vertexOverrideCount!=0 || !lastCompletedFrame_ ||
       device_->frameSerial()!=lastCompletedFrame_ ||
       mesh_.vertexCount()>std::numeric_limits<std::uint32_t>::max() ||
       render::modelVertexOverrideStatus(*model_,mesh_.vertexBuffer(),
            static_cast<std::uint32_t>(mesh_.vertexCount())) !=
            render::ModelVertexOverrideStatus::Ready)
        return false;
    draw.vertexOverride=mesh_.vertexBuffer();
    draw.vertexOverrideCount=static_cast<std::uint32_t>(mesh_.vertexCount());
    return true;
}

bool GpuActorStream::download(std::span<render::ModelVertex> vertices) const {
    return ready() && mesh_.download(vertices);
}

bool GpuActorStream::shutdownAfterIdle() {
    if(!device_) return true;
    // Caller must have ended the frame. Metal's barrier does not flush
    // uncommitted cmd_ frames, therefore never call during a frame.
    if(!device_->waitForResourceIdle()) {
        error_="VIVID actor: GPU idle barrier failed; cannot retire in-flight actor buffers";
        return false;
    }
    releaseAfterGpuCompletion();
    return true;
}

void GpuActorStream::releaseAfterGpuCompletion() noexcept {
    mesh_.release();
    morph_.release();
    device_=nullptr;
    model_=nullptr;
    weights_.clear();
    lastAttemptedFrame_=lastCompletedFrame_=0;
    error_.clear();
}

} // namespace aiverse::vivid
