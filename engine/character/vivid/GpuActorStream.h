// VIVID per-character GPU morph presentation owner.
// One instance per rendered character; no shared writable output buffers.
// MotionWeave remains authoritative for skeleton pose and graph scheduling.
#pragma once

#include "FaceShapeBinding.h"
#include "GpuMorphMeshStream.h"
#include "engine/render/ModelVertexStreamOverride.h"
#include "engine/render/Renderer3D.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace aiverse::vivid {

class GpuActorStream final {
public:
    GpuActorStream() = default;
    ~GpuActorStream() = default;
    GpuActorStream(const GpuActorStream&) = delete;
    GpuActorStream& operator=(const GpuActorStream&) = delete;

    // Call on the gfx device thread, outside render/compute passes and prior
    // to submitting any draws referencing this instance. All inputs must
    // describe the same full, model-relative 60B stream. The uploaded model
    // and gfx device must outlive this owner.
    // Reinitialization requires shutdownAfterIdle() first.
    bool initialize(gfx::Device& device, const render::Model& uploadedModel,
                    const SparseMorphLibrary& library,
                    std::span<const render::ModelVertex> original);

    // Exactly ONE update per instance per gfx frameSerial() (Metal only).
    // Call after gfx::Device::beginFrame, before opening render passes.
    // Both GPU computes are submitted in order, without any CPU readback.
    // The caller still invokes MotionWeave::update for body pose separately.
    bool dispatch(const FaceFrame& face, const FaceShapeBinding& mapping,
                  float influence = 1.0f, bool normalizeDirections = true);

    // Transactional: returns false and leaves draw untouched unless this
    // actor's compute stream was produced in the CURRENT gfx frame.
    // Host fills draw.nodeWorld from MotionWeave's pose independently.
    bool attach(render::ModelDraw& draw) const noexcept;

    // Only a diagnostic. Between frames; stalls for GPU -> CPU copy.
    bool download(std::span<render::ModelVertex> vertices) const;
    // Call at a frame boundary AFTER endFrame; never inside an active frame.
    // Waits for all submitted work before destroying GPU buffers.
    // On a failed idle barrier, retains resources, returning false.
    bool shutdownAfterIdle();
    bool ready() const noexcept { return device_ && morph_.ready() && mesh_.ready(); }
    std::size_t vertexCount() const noexcept { return mesh_.vertexCount(); }
    std::size_t targetCount() const noexcept { return morph_.targetCount(); }
    std::uint64_t dispatchedFrame() const noexcept { return lastCompletedFrame_; }
    gfx::BufferHandle vertexBuffer() const noexcept { return mesh_.vertexBuffer(); }
    const std::string& lastError() const noexcept { return error_; }

private:
    friend class GpuActorRetirementQueue;
    // Called only by the GPU completion-token owner after a Complete token,
    // or by shutdownAfterIdle after an explicit successful GPU idle barrier.
    void releaseAfterGpuCompletion() noexcept;
    // GPU members have ordinary nonblocking destructors; hosts MUST complete
    // shutdownAfterIdle() while the Device is alive, not just destroy this
    // object during scene shutdown with active/in-flight commands.
    gfx::Device* device_ = nullptr;
    const render::Model* model_ = nullptr;
    GpuMorphDeformer morph_;
    GpuMorphMeshStream mesh_;
    std::vector<float> weights_;
    std::uint64_t lastAttemptedFrame_ = 0;
    std::uint64_t lastCompletedFrame_ = 0;
    std::string error_;
};

} // namespace aiverse::vivid
