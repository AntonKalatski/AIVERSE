#pragma once
// Experimental Metal GPU-authored indexed draw arguments.
// A compute pass writes a 20-byte indexed command per eligible opaque model.
// Render pass consumes the generated arguments with no CPU readback.
// This is NOT multi-draw: each eligible model still has one API draw submission.
#include "engine/gfx/Gfx.h"
#include "Math3D.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace aiverse::render {

struct GpuPrimitiveDrawRecord {
    float world[16] = {};
    float boundsMin[4] = {};
    float boundsMax[4] = {};
    gfx::DrawIndexedIndirectCommand command{};
    uint32_t enabled = 0;
    uint32_t padding[2] = {};
};
static_assert(sizeof(GpuPrimitiveDrawRecord) == 128,
    "GpuPrimitiveDrawRecord must match Metal compute layout");

class GpuSceneDrawCommands {
public:
    struct UploadStats { uint32_t records = 0, ranges = 0; size_t bytes = 0; };
    GpuSceneDrawCommands() = default;
    GpuSceneDrawCommands(const GpuSceneDrawCommands&) = delete;
    GpuSceneDrawCommands& operator=(const GpuSceneDrawCommands&) = delete;

    bool prepare(gfx::Device& device, const std::vector<GpuPrimitiveDrawRecord>& records,
                 const mat4& viewProjection, uint64_t frameSerial) {
        if (device.info().backend != gfx::Backend::Metal ||
            !device.supportsCompute() || !frameSerial || records.empty() ||
            records.size() > UINT32_MAX) return false;
        if (!pipeline_.valid()) {
            pipeline_ = device.createCompute({kMetalSource, nullptr,
                                              "GPU indexed draw command generator"});
            if (!pipeline_.valid()) return false;
        }
        Slot& slot = slots_[frameSerial % slots_.size()];
        const size_t inputBytes = records.size() * sizeof(GpuPrimitiveDrawRecord);
        const size_t outputBytes = records.size() * sizeof(gfx::DrawIndexedIndirectCommand);
        const bool resizedInput = !slot.input.valid() || slot.inputCapacity < inputBytes;
        if (!allocate(device, slot.input, slot.inputCapacity, inputBytes,
                      "GPU draw record input") ||
            !allocate(device, slot.output, slot.outputCapacity, outputBytes,
                      "GPU authored draw arguments")) return false;
        // A recycled frame-slot retains its last CPU/GPU records. Upload only
        // changed consecutive ranges; camera changes still rerun compute.
        // Keep the comparison independent per frame slot for in-flight safety.
        slot.upload = {};
        if (resizedInput) slot.lastRecords.clear();
        const size_t oldCount = slot.lastRecords.size();
        slot.lastRecords.resize(records.size());
        size_t begin = records.size();
        auto flush = [&](size_t end) {
            if (begin == records.size()) return;
            const size_t length = end - begin;
            const size_t bytes = length * sizeof(GpuPrimitiveDrawRecord);
            device.updateBuffer(slot.input, records.data() + begin, bytes,
                                begin * sizeof(GpuPrimitiveDrawRecord));
            slot.upload.ranges++;
            slot.upload.records += uint32_t(length);
            slot.upload.bytes += bytes;
            begin = records.size();
        };
        for (size_t i = 0; i < records.size(); ++i) {
            const bool dirty = i >= oldCount ||
                std::memcmp(&slot.lastRecords[i], &records[i],
                            sizeof(GpuPrimitiveDrawRecord)) != 0;
            if (dirty) {
                slot.lastRecords[i] = records[i];
                if (begin == records.size()) begin = i;
            } else flush(i);
        }
        flush(records.size());
        if (!device.beginCompute(pipeline_)) return false;
        device.setComputeBuffer(0, slot.input);
        device.setComputeBuffer(1, slot.output);
        struct alignas(16) Uniforms {
            float viewProjection[16];
            uint32_t count = 0;
            uint32_t padding[3] = {};
        } params{};
        std::memcpy(params.viewProjection, viewProjection.m, sizeof(params.viewProjection));
        params.count = uint32_t(records.size());
        device.setComputeUniforms(&params, sizeof(params));
        device.dispatch(params.count);
        device.endCompute();
        slot.commandCount = params.count;
        return true;
    }

    UploadStats uploadStats(uint64_t frameSerial) const {
        return slots_[frameSerial % slots_.size()].upload;
    }
    gfx::BufferHandle output(uint64_t frameSerial) const {
        return slots_[frameSerial % slots_.size()].output;
    }
    void release(gfx::Device& device) {
        for (Slot& slot : slots_) {
            if (slot.input.valid()) device.destroyBuffer(slot.input);
            if (slot.output.valid()) device.destroyBuffer(slot.output);
            slot = {};
        }
        if (pipeline_.valid()) device.destroyCompute(pipeline_);
        pipeline_ = {};
    }
private:
    struct Slot {
        gfx::BufferHandle input{}, output{};
        size_t inputCapacity = 0, outputCapacity = 0;
        uint32_t commandCount = 0;
        std::vector<GpuPrimitiveDrawRecord> lastRecords;
        UploadStats upload{};
    };
    std::array<Slot, 3> slots_{};
    gfx::ComputeHandle pipeline_{};

    static bool allocate(gfx::Device& device, gfx::BufferHandle& handle,
                         size_t& capacity, size_t required, const char* label) {
        if (handle.valid() && capacity >= required) return true;
        gfx::BufferDesc desc{};
        desc.type = gfx::BufferType::Storage;
        desc.usage = gfx::Usage::Immutable;
        desc.size = required;
        desc.label = label;
        const gfx::BufferHandle replacement = device.createBuffer(desc);
        if (!replacement.valid()) return false;
        if (handle.valid()) device.destroyBuffer(handle);
        handle = replacement;
        capacity = required;
        return true;
    }

    static constexpr const char* kMetalSource = R"MSL(
#include <metal_stdlib>
using namespace metal;
struct DrawArgs {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int baseVertex;
    uint firstInstance;
};
struct alignas(16) Record {
    float4 c0, c1, c2, c3;
    float4 lo, hi;
    DrawArgs draw;
    uint enabled;
    uint2 padding;
};
struct Params {
    float4x4 viewProjection;
    uint count;
    uint pad0, pad1, pad2;
};
kernel void cs_main(device const Record* records [[buffer(0)]],
                    device DrawArgs* output [[buffer(1)]],
                    constant Params& params [[buffer(8)]],
                    uint idx [[thread_position_in_grid]]) {
    if (idx >= params.count) return;
    const Record r = records[idx];
    DrawArgs args = r.draw;
    if (r.enabled == 0 || args.indexCount == 0) {
        args.instanceCount = 0;
        output[idx] = args;
        return;
    }
    const float4x4 clip = params.viewProjection *
        float4x4(r.c0, r.c1, r.c2, r.c3);
    uint allOutside = 63;
    for (uint corner = 0; corner < 8; ++corner) {
        const float3 p = float3(
            (corner & 1) ? r.hi.x : r.lo.x,
            (corner & 2) ? r.hi.y : r.lo.y,
            (corner & 4) ? r.hi.z : r.lo.z);
        const float4 v = clip * float4(p, 1.0);
        uint outside = 0;
        if (v.x < -v.w) outside |= 1;
        if (v.x > v.w) outside |= 2;
        if (v.y < -v.w) outside |= 4;
        if (v.y > v.w) outside |= 8;
        if (v.z < 0.0) outside |= 16;
        if (v.z > v.w) outside |= 32;
        allOutside &= outside;
    }
    args.instanceCount = (allOutside == 0) ? args.instanceCount : 0;
    output[idx] = args;
}
)MSL";
};

} // namespace aiverse::render
