// Experimental bounded Metal compute GI for small, immutable triangle scenes.
// Independent opt-in backend; NEVER auto-enables in GiDynamicRuntime.
// Caller must invoke dispatch inside beginFrame/endFrame and OUTSIDE render passes.
// GPU jobs write the RGBA16F dynamic-only atlas. No GPU readback or CPU coefficients.
// In a real app, completion/failure must be paired with a GPU frame fence before
// claiming scheduler tickets complete; this prototype does NOT acknowledge them.
#pragma once
#include "GiGpuSmallSceneShaders.h"
#include "GiChunkedGeometry.h"
#include "GiDynamicProbeTexture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <vector>

namespace aiverse::render {

class GiGpuSmallScene {
public:
    static constexpr size_t kMaxTriangles = 256;
    static constexpr size_t kMaxLights = 16;
    static constexpr size_t kMaxJobs = 32;
    static constexpr int kMaxSamples = 64;

    struct DispatchResult {
        bool submitted = false;
        uint32_t probes = 0, samples = 0, triangles = 0, lights = 0;
        uint64_t primaryRayBudget = 0, maximumTriangleTests = 0;
    };
    struct GpuTri { float4 a{}, b{}, c{}, albedo{}; };
    struct GpuLamp { float4 posRadius{}, colorIntensity{}; };
    struct GpuJob { uint32_t index = 0; float x = 0, y = 0, z = 0; };
    struct Uniforms {
        uint32_t triangles = 0, lights = 0, jobs = 0, samples = 0;
        float traceDistance = 0, pad0 = 0, pad1 = 0, pad2 = 0;
    };
    static_assert(sizeof(GpuTri) == 64 && sizeof(GpuLamp) == 32 &&
                  sizeof(GpuJob) == 16 && sizeof(Uniforms) == 32);

    ~GiGpuSmallScene() { shutdown(); }
    GiGpuSmallScene() = default;
    GiGpuSmallScene(const GiGpuSmallScene&) = delete;
    GiGpuSmallScene& operator=(const GiGpuSmallScene&) = delete;

    bool initialize(gfx::Device& device, size_t probeCount) {
        shutdown();
        // This shader is MSL only. Other RHI backends always take the CPU fallback.
        if (device.info().backend != gfx::Backend::Metal || !device.supportsCompute() ||
            !device.supports(gfx::PixelFormat::RGBA16F))
            return false;
        if (!atlas_.initialize(device,probeCount,true)) return false;
        const auto compute = device.createCompute(
            {gi_gpu::kMetalSmallScene,nullptr,"GI bounded triangle transport"});
        if (!compute.valid()) { atlas_.release(); return false; }
        dev_ = &device;
        compute_ = compute;
        // One dummy element is allocated to satisfy Metal argument-buffer validation
        // even for a scene with no lights/triangles.
        triBuffer_ = allocate(sizeof(GpuTri),nullptr);
        lampBuffer_ = allocate(sizeof(GpuLamp),nullptr);
        jobBuffer_ = allocate(kMaxJobs*sizeof(GpuJob),nullptr);
        if (!triBuffer_.valid() || !lampBuffer_.valid() || !jobBuffer_.valid()) {
            shutdown();
            return false;
        }
        return true;
    }

    void shutdown() {
        if (dev_) {
            if (triBuffer_.valid()) dev_->destroyBuffer(triBuffer_);
            if (lampBuffer_.valid()) dev_->destroyBuffer(lampBuffer_);
            if (jobBuffer_.valid()) dev_->destroyBuffer(jobBuffer_);
            if (compute_.valid()) dev_->destroyCompute(compute_);
        }
        triBuffer_={};
        lampBuffer_={};
        jobBuffer_={};
        compute_={};
        atlas_.release();
        dev_=nullptr;
        triangleCount_=0;
        lightCount_=0;
        geometryRevision_=0;
    }

    bool ready() const { return dev_ && compute_.valid() && atlas_.texture().valid(); }
    const GiDynamicProbeTexture& atlas() const { return atlas_; }
    uint64_t geometryRevision() const { return geometryRevision_; }
    size_t triangleCount() const { return triangleCount_; }
    size_t lightCount() const { return lightCount_; }

    // Publish only when a new scene geometry revision becomes visible.
    // This prototype flattens a bounded scene once. A world-scale GI system must
    // upload hierarchical GPU BVH/SDF chunks rather than these O(T) ray loops.
    bool setGeometry(const GiChunkedGeometrySnapshot& snapshot, uint64_t revision) {
        if (!ready()) return false;
        if (revision == geometryRevision_) return true;
        if (snapshot.triangleCount()>kMaxTriangles || !dev_->canUpdateSharedBuffers())
            return false;
        std::vector<GpuTri> triangles;
        triangles.reserve(snapshot.triangleCount());
        snapshot.forEachTriangle([&](const StaticShadowGeometry::Triangle& t,float3 albedo) {
            triangles.push_back({{t.a.x,t.a.y,t.a.z,0},
                                 {t.b.x,t.b.y,t.b.z,0},
                                 {t.c.x,t.c.y,t.c.z,0},
                                 {albedo.x,albedo.y,albedo.z,0}});
        });
        const auto next=allocate(std::max<size_t>(1,triangles.size())*sizeof(GpuTri),
                                 triangles.empty()?nullptr:triangles.data());
        if (!next.valid()) return false;
        dev_->destroyBuffer(triBuffer_);
        triBuffer_=next;
        triangleCount_=triangles.size();
        geometryRevision_=revision;
        return true;
    }

    bool setLights(std::vector<GiMovingLight> lights) {
        if (!ready() || lights.size()>kMaxLights || !dev_->canUpdateSharedBuffers())
            return false;
        std::sort(lights.begin(),lights.end(),
                  [](const auto& a,const auto& b){return a.key<b.key;});
        std::vector<GpuLamp> packed;
        packed.reserve(lights.size());
        uint64_t last=0;
        bool haveLast=false;
        for (const auto& l:lights) {
            if ((haveLast && l.key==last) || !finite(l.position) || !finite(l.color) ||
                l.color.x<0 || l.color.y<0 || l.color.z<0 ||
                !std::isfinite(l.radius) || l.radius<=0 ||
                !std::isfinite(l.intensity) || l.intensity<0)
                return false;
            packed.push_back({{l.position.x,l.position.y,l.position.z,l.radius},
                              {l.color.x,l.color.y,l.color.z,l.intensity}});
            haveLast=true;
            last=l.key;
        }
        const auto next=allocate(std::max<size_t>(1,packed.size())*sizeof(GpuLamp),
                                 packed.empty()?nullptr:packed.data());
        if (!next.valid()) return false;
        dev_->destroyBuffer(lampBuffer_);
        lampBuffer_=next;
        lightCount_=packed.size();
        return true;
    }

    DispatchResult dispatch(const std::vector<GiProbeTicket>& jobs,
                            uint32_t samples, float traceDistance) {
        DispatchResult result;
        if (!ready() || jobs.empty() || jobs.size()>kMaxJobs ||
            samples<8 || samples>kMaxSamples || !std::isfinite(traceDistance) ||
            traceDistance<=0 || traceDistance>1000 || !dev_->canUpdateSharedBuffers())
            return result;

        std::vector<GpuJob> packed;
        packed.reserve(jobs.size());
        std::set<uint32_t> unique;
        for (const auto& t:jobs) {
            if (t.index>=atlas_.probeCount() || !finite(t.position) ||
                !unique.insert(t.index).second)
                return result;
            packed.push_back({t.index,t.position.x,t.position.y,t.position.z});
        }
        dev_->updateBuffer(jobBuffer_,packed.data(),packed.size()*sizeof(GpuJob));
        // With zero geometry the kernel overwrites requested probes with zero RGB
        // (the cache is initialized to zero; old light must not survive).
        if (!dev_->beginCompute(compute_)) return result;
        dev_->setComputeBuffer(0,triBuffer_);
        dev_->setComputeBuffer(1,lampBuffer_);
        dev_->setComputeBuffer(2,jobBuffer_);
        dev_->setComputeTexture(0,atlas_.texture());
        Uniforms params;
        params.triangles=uint32_t(triangleCount_);
        params.lights=uint32_t(lightCount_);
        params.jobs=uint32_t(packed.size());
        params.samples=samples;
        params.traceDistance=traceDistance;
        dev_->setComputeUniforms(&params,sizeof(params));
        dev_->dispatch(params.jobs);
        dev_->endCompute();
        result.submitted=true;
        result.probes=params.jobs;
        result.samples=params.samples;
        result.triangles=params.triangles;
        result.lights=params.lights;
        result.primaryRayBudget=uint64_t(params.jobs)*params.samples;
        result.maximumTriangleTests=result.primaryRayBudget*
            uint64_t(params.triangles)*(uint64_t(params.lights)+1u);
        return result;
    }

private:
    static bool finite(float3 x) {
        return std::isfinite(x.x) && std::isfinite(x.y) && std::isfinite(x.z);
    }
    gfx::BufferHandle allocate(size_t size,const void* data) const {
        gfx::BufferDesc desc;
        desc.type=gfx::BufferType::Storage;
        desc.usage=gfx::Usage::Immutable;
        desc.size=size;
        desc.data=data;
        desc.label="GI bounded compute input";
        return dev_->createBuffer(desc);
    }

    gfx::Device* dev_=nullptr;
    gfx::ComputeHandle compute_{};
    gfx::BufferHandle triBuffer_{},lampBuffer_{},jobBuffer_{};
    GiDynamicProbeTexture atlas_;
    size_t triangleCount_=0,lightCount_=0;
    uint64_t geometryRevision_=0;
};

} // namespace aiverse::render
