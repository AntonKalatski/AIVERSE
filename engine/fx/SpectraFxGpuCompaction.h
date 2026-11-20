// SpectraFX experimental Metal GPU live-slot compaction.
// A compute pass packs the indices of non-transparent particles into a
// texture and produces one indexed draw command with GPU-authored instance
// count. No GPU->CPU readback. A regular instanced draw remains the fallback.
// Owner must release() before gfx::Device teardown.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include "../gfx/Gfx.h"
#include "SpectraFxGpuEvolution.h"

namespace aiverse::fx {

inline constexpr const char* kSpectraFxCompactMetal = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct DrawArgs {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int baseVertex;
    uint firstInstance;
};
struct Params {
    uint count;
    uint width;
    uint mode;
    uint padding;
};

kernel void cs_main(uint tid [[thread_position_in_grid]],
                    device atomic_uint* count [[buffer(0)]],
                    device DrawArgs* command [[buffer(1)]],
                    constant Params& params [[buffer(8)]],
                    texture2d<float, access::read> positions [[texture(0)]],
                    texture2d<float, access::write> denseIndices [[texture(1)]]) {
    if (params.mode == 0u) {
        if (tid >= params.count) return;
        const float4 pos = positions.read(uint2(tid % params.width,
                                                tid / params.width));
        if (pos.w <= 0.0f) return;
        const uint dense = atomic_fetch_add_explicit(
            &count[0], 1u, memory_order_relaxed);
        if (dense < params.count) {
            // All uint32 slot indices <=65535 round-trip exactly in float32.
            denseIndices.write(float4(float(tid), 0.0f, 0.0f, 1.0f),
                               uint2(dense % params.width, dense / params.width));
        }
    } else if (tid == 0u) {
        DrawArgs args;
        args.indexCount = 6u;
        args.instanceCount = min(atomic_load_explicit(
            &count[0], memory_order_relaxed), params.count);
        args.firstIndex = 0u;
        args.baseVertex = 0;
        args.firstInstance = 0u;
        command[0] = args;
    }
}
)MSL";

class SpectraFxGpuCompaction {
public:
    static constexpr size_t kBufferedFrames = 3;

    SpectraFxGpuCompaction() = default;
    SpectraFxGpuCompaction(const SpectraFxGpuCompaction&) = delete;
    SpectraFxGpuCompaction& operator=(const SpectraFxGpuCompaction&) = delete;
    // Explicit device-lifetime rule; destructor never calls a dead RHI object.
    ~SpectraFxGpuCompaction() = default;

    bool init(gfx::Device& device, uint32_t capacity,
              gfx::ComputeHandle sharedCompute={}) {
        release();
        error_.clear();
        // Only Metal implements drawIndexedGpuIndirect of GPU-authored
        // BufferType::Storage in the currently inspected AIVERSE RHI.
        if (device.info().backend != gfx::Backend::Metal ||
            !device.supportsCompute() || !device.frameSerial() ||
            !device.supports(gfx::PixelFormat::RGBA32F) ||
            !capacity || capacity > SpectraFxGpuEvolution::kMaxParticles) {
            error_ = "SpectraFX GPU indirect compaction requires Metal and RGBA32F";
            return false;
        }
        device_ = &device;
        capacity_ = capacity;
        ownsCompute_=!sharedCompute.valid();
        compute_=sharedCompute.valid()?sharedCompute:
            device.createCompute({kSpectraFxCompactMetal,nullptr,
                                  "SpectraFX GPU live-slot compact"});
        if (!compute_.valid()) {
            error_ = device.lastError();
            release();
            return false;
        }

        gfx::TextureDesc texture{};
        texture.width = int(SpectraFxGpuEvolution::kTextureWidth);
        texture.height = int((capacity +
            SpectraFxGpuEvolution::kTextureWidth - 1) /
            SpectraFxGpuEvolution::kTextureWidth);
        texture.format = gfx::PixelFormat::RGBA32F;
        texture.filter = gfx::Filter::Nearest;
        texture.storage = true;
        texture.label = "SpectraFX packed particle indices";

        const uint32_t zero = 0;
        const gfx::DrawIndexedIndirectCommand initial{6, 0, 0, 0, 0};
        for (auto& frame : frames_) {
            frame.indices = device.createTexture(texture);
            // Metal's GPU-authored indirect API accepts a Storage buffer,
            // not the CPU-authored BufferType::Indirect type.
            frame.counter = device.createBuffer(
                {gfx::BufferType::Storage, gfx::Usage::Dynamic,
                 sizeof(zero), &zero, "SpectraFX live particle count"});
            frame.command = device.createBuffer(
                {gfx::BufferType::Storage, gfx::Usage::Immutable,
                 sizeof(initial), &initial, "SpectraFX GPU indirect arguments"});
            if (!frame.indices.valid() || !frame.counter.valid() ||
                !frame.command.valid()) {
                error_ = "SpectraFX GPU compaction resource allocation failed";
                release();
                return false;
            }
        }
        return true;
    }

    // Call after SpectraFxGpuEvolution::step, inside beginFrame/endFrame and
    // OUTSIDE all render passes. The Metal RHI's 3-frame semaphore protects
    // resources when frameSerial()%3 is reused. Separate dispatches encode
    // count/indices first, followed by final GPU indirect arguments.
    bool prepare(const SpectraFxGpuEvolution& particles) {
        if (!ready() || !particles.ready() ||
            particles.count() != capacity_ ||
            !device_->frameSerial()) return false;
        const uint64_t serial = device_->frameSerial();
        Slot& frame = frames_[serial % frames_.size()];
        frame.preparedSerial = 0;
        frame.sourceTexture = {};
        if (!device_->canUpdateSharedBuffers()) {
            error_ = "SpectraFX GPU compaction called in a render/compute encoder";
            return false;
        }
        const uint32_t zero = 0;
        device_->updateBuffer(frame.counter, &zero, sizeof(zero));

        struct alignas(16) Params {
            uint32_t count, width, mode, padding;
        } params{capacity_, SpectraFxGpuEvolution::kTextureWidth, 0, 0};
        for (uint32_t mode = 0; mode != 2; ++mode) {
            params.mode = mode;
            if (!device_->beginCompute(compute_)) {
                error_ = "SpectraFX GPU compaction compute encoder unavailable";
                return false;
            }
            device_->setComputeBuffer(0, frame.counter);
            device_->setComputeBuffer(1, frame.command);
            device_->setComputeTexture(0, particles.positionTexture());
            device_->setComputeTexture(1, frame.indices);
            device_->setComputeUniforms(&params, sizeof(params));
            device_->dispatch(mode == 0 ? capacity_ : 1u);
            device_->endCompute();
        }
        frame.preparedSerial = serial;
        frame.sourceTexture = particles.positionTexture();
        error_.clear();
        return true;
    }

    bool preparedFor(const SpectraFxGpuEvolution& particles) const {
        if (!ready() || !particles.ready() || !device_->frameSerial() ||
            particles.count() != capacity_) return false;
        const Slot& frame = frames_[device_->frameSerial() % frames_.size()];
        return frame.preparedSerial == device_->frameSerial() &&
               frame.sourceTexture == particles.positionTexture();
    }

    gfx::TextureHandle denseIndexTexture() const {
        if (!ready() || !device_->frameSerial()) return {};
        return frames_[device_->frameSerial() % frames_.size()].indices;
    }
    gfx::BufferHandle indirectArguments() const {
        if (!ready() || !device_->frameSerial()) return {};
        return frames_[device_->frameSerial() % frames_.size()].command;
    }
    bool ready() const {
        return device_ && compute_.valid() && capacity_ != 0;
    }
    uint64_t nominalGpuBytes() const {
        if(!ready()) return 0;
        const uint64_t cells=uint64_t(SpectraFxGpuEvolution::kTextureWidth)*
            ((uint64_t(capacity_)+SpectraFxGpuEvolution::kTextureWidth-1)/
             SpectraFxGpuEvolution::kTextureWidth);
        return kBufferedFrames*(cells*sizeof(render::float4) +
                sizeof(uint32_t)+sizeof(gfx::DrawIndexedIndirectCommand));
    }
    uint32_t capacity() const { return capacity_; }
    const std::string& lastError() const { return error_; }

    void release() {
        if (device_) {
            for (auto& frame : frames_) {
                if (frame.indices.valid()) device_->destroyTexture(frame.indices);
                if (frame.counter.valid()) device_->destroyBuffer(frame.counter);
                if (frame.command.valid()) device_->destroyBuffer(frame.command);
            }
            if (ownsCompute_ && compute_.valid()) device_->destroyCompute(compute_);
        }
        for (auto& frame : frames_) frame = {};
        device_ = nullptr;
        compute_ = {};
        capacity_ = 0;
        ownsCompute_=false;
    }

private:
    struct Slot {
        gfx::TextureHandle indices{};
        gfx::BufferHandle counter{};
        gfx::BufferHandle command{};
        gfx::TextureHandle sourceTexture{};
        uint64_t preparedSerial = 0;
    };
    gfx::Device* device_ = nullptr;
    gfx::ComputeHandle compute_{};
    bool ownsCompute_=false;
    std::array<Slot, kBufferedFrames> frames_{};
    uint32_t capacity_ = 0;
    std::string error_;
};

} // namespace aiverse::fx
