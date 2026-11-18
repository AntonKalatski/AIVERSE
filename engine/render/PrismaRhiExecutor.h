#pragma once
// PRISMA Stage 13: opt-in host adapter from the compiled graph to gfx::Device.
// This is NOT a replacement for Prisma::resolve() and never runs implicitly.
// Device APIs are called on the owning graphics thread only.
#include "PrismaGraphResourcePool.h"
#include "../gfx/Gfx.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::render {

inline bool prismaRhiFormat(PrismaGraphFormat in, gfx::PixelFormat& out) {
    switch (in) {
        case PrismaGraphFormat::RGBA8: out = gfx::PixelFormat::RGBA8; return true;
        case PrismaGraphFormat::RGBA16F: out = gfx::PixelFormat::RGBA16F; return true;
        case PrismaGraphFormat::Depth32F: out = gfx::PixelFormat::Depth; return true;
        // The current RHI has no independent RG16F/R32F render-target formats.
        case PrismaGraphFormat::RG16F:
        case PrismaGraphFormat::R32F: return false;
    }
    return false;
}

struct PrismaRhiExecution {
    bool success = false;
    size_t passes = 0;
    std::string error;
};

class PrismaRhiExecutor {
public:
    explicit PrismaRhiExecutor(gfx::Device& device) : device_(device) {}
    PrismaRhiExecutor(const PrismaRhiExecutor&) = delete;
    PrismaRhiExecutor& operator=(const PrismaRhiExecutor&) = delete;

    // Host must call reset() after the GPU has finished referencing pooled targets.
    // For a camera cut, call invalidateHistory() before the next frame instead.
    void invalidateHistory() { pool_.invalidateHistory(); }
    void reset() { pool_.clear([this](gfx::TextureHandle h) { device_.destroyTexture(h); }); }
    const PrismaGraphResourcePool<gfx::TextureHandle>& pool() const { return pool_; }

    // External resource handles must be indexed by graph resource ID.
    // Raster: bool(Device&, passId, DispatchView&) INSIDE an open pass.
    // The host binds its own pipeline, textures and uniforms. The pass has
    // one color output, no implicit clearing and optional depth attachment.
    // Compute: bool(Device&, passId, DispatchView&) OUTSIDE any render pass.
    // Caller must not call beginPass/endPass inside either callback.
    // Copy passes use Device::copyColor and require one source and destination.
    // This method MUST be called outside any currently open RHI pass.
    // It does not present, wait for GPU fences or alter authoritative ECS.
    template <typename RasterFn, typename ComputeFn>
    PrismaRhiExecution execute(const PrismaRenderGraph& graph,
                              const PrismaGraphPlan& plan,
                              const std::vector<gfx::TextureHandle>& external,
                              RasterFn&& raster, ComputeFn&& compute,
                              gfx::TextureHandle rasterDepth = {}) {
        PrismaRhiExecution result;
        if (!plan.valid || plan.resources.size() != graph.resources().size() ||
            plan.order.size() != graph.passes().size() ||
            external.size() != graph.resources().size()) {
            result.error = "PRISMA RHI: incompatible graph or external bindings";
            return result;
        }
        // Preflight ALL requested backends/formats and pass targets before
        // allocating anything or emitting GPU commands.
        for (size_t i = 0; i < graph.resources().size(); ++i) {
            const auto& res = graph.resources()[i];
            gfx::PixelFormat format{};
            if (!prismaRhiFormat(res.format, format) ||
                (!res.external && !device_.supports(format))) {
                result.error = "PRISMA RHI: unsupported format for " + res.name;
                return result;
            }
            if (res.external && !external[i].valid()) {
                result.error = "PRISMA RHI: missing external " + res.name;
                return result;
            }
        }
        for (const auto& pass : graph.passes()) {
            if (pass.kind == PrismaGraphPassKind::Copy &&
                (pass.reads.size() != 1 || pass.writes.size() != 1)) {
                result.error = "PRISMA RHI: Copy requires exactly one read and write";
                return result;
            }
            if (pass.kind == PrismaGraphPassKind::Compute && !device_.supportsCompute()) {
                result.error = "PRISMA RHI: compute unsupported on device";
                return result;
            }
            if (pass.kind == PrismaGraphPassKind::Raster) {
                if (pass.writes.size() != 1) {
                    result.error = "PRISMA RHI: raster currently requires one color output";
                    return result;
                }
                const auto& output = graph.resources()[pass.writes.front()];
                if (output.format == PrismaGraphFormat::Depth32F) {
                    result.error = "PRISMA RHI: depth-only raster not implemented";
                    return result;
                }
            }
        }
        auto prepared = pool_.prepare(graph, plan,
            [this](PrismaGraphFormat format, uint32_t w, uint32_t h) {
                gfx::PixelFormat native{};
                if (!prismaRhiFormat(format, native) || !device_.supports(native))
                    return gfx::TextureHandle{};
                gfx::TextureDesc desc;
                desc.width = static_cast<int>(w);
                desc.height = static_cast<int>(h);
                desc.format = native;
                desc.renderTarget = true;
                desc.filter = native == gfx::PixelFormat::Depth ?
                    gfx::Filter::Nearest : gfx::Filter::Linear;
                desc.label = "PRISMA graph transient";
                return device_.createTexture(desc);
            },
            [this](gfx::TextureHandle handle) { device_.destroyTexture(handle); });
        if (!prepared.success) {
            result.error = prepared.error;
            return result;
        }
        PrismaGraphBindings<gfx::TextureHandle> bindings;
        bindings.external = external;
        bindings.slots = std::move(prepared.slots);
        auto dispatched = dispatchPrismaGraph(graph, plan, bindings,
            [&](const PrismaGraphPass& pass, PrismaGraphId id,
                const PrismaGraphDispatchView<gfx::TextureHandle>& view) {
                if (pass.kind == PrismaGraphPassKind::Copy) {
                    return device_.copyColor(view.resource(pass.reads.front()),
                                             view.resource(pass.writes.front()));
                }
                if (pass.kind == PrismaGraphPassKind::Compute)
                    return compute(device_, id, view);
                gfx::PassDesc desc;
                desc.colorTarget = view.resource(pass.writes.front());
                desc.clearColorEnabled = false; // shader must fully write target
                desc.clearDepthEnabled = false;
                // Metal may require a depth attachment. The host supplies one
                // sized for the output, or leaves invalid where backend permits.
                desc.depthTarget = rasterDepth;
                device_.beginPass(desc);
                const bool ok = raster(device_, id, view);
                device_.endPass();
                return ok;
            });
        result.success = dispatched.success;
        result.passes = dispatched.executedPasses;
        result.error = std::move(dispatched.error);
        return result;
    }

private:
    gfx::Device& device_;
    PrismaGraphResourcePool<gfx::TextureHandle> pool_;
};

} // namespace aiverse::render
