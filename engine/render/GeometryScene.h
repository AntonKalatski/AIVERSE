#pragma once
#include "Geometry.h"
#include "Renderer3D.h"
#include <map>
#include <memory>
#include <functional>
#include <string>

namespace aiverse::render {
namespace strata { struct ModelAsset; class SceneStaging; class GpuMetadataScene; struct ViewConfig; struct ViewPlan;
    struct GpuSceneInstance; struct GpuPassConfig; struct GpuSelectionSubmission;
    struct GpuSelectionReadback; class GpuClusterCulling;
    class PortableSource;
    struct RenderCandidate; enum class RenderPass : uint8_t; }
class Renderer3D;
// Static presentation geometry, never collision or simulation objects. HLOD proxies merge
// compatible materials and use bounded-error cluster simplification. Sources stay available.
class GeometryScene {
public:
    struct Stats { uint32_t instances=0, groups=0, proxies=0, submitted=0, surfaceProxiesSubmitted=0;
        uint32_t strataSources=0, strataPrimitives=0, strataSkippedSources=0,
                 strataSkippedPrimitives=0, strataFailures=0;
        uint64_t strataNodes=0, strataMeshlets=0, strataIndexBytes=0;
        uint32_t strataGpuMetadataSources=0, strataGpuMetadataDeclined=0,
                 strataGpuMetadataFailures=0;
        uint64_t strataGpuMetadataBytes=0;
        uint32_t strataGpuIndexedSources=0, strataGpuIndexDeclined=0,
                 strataGpuIndexFailures=0;
        uint64_t strataGpuIndexBytes=0;
        // Cross-backend CPU-selected Strata indexed rendering (opt-in).
        uint32_t strataPortableSources=0, strataPortableDeclined=0;
        uint64_t strataPortableDraws=0, strataPortableFallbacks=0;
        uint64_t strataPortableClusters=0;
        uint32_t strataStreamedSources=0;
        uint64_t strataStreamingIndexBytes=0, strataStreamingEvictions=0;
        uint64_t strataStreamingPrepareFailures=0;
        uint32_t strataStreamingBudgetDeclined=0;
    };
    void load(gfx::Device& dev, const Json& json, const FileReader& reader);
    // Procedural/offline callers may supply immutable CPU models instead of files.
    // Borrowed image handles must outlive the scene. GPU geometry is owned by the scene.
    void loadPrepared(gfx::Device& dev,const Json& json,std::map<std::string,std::unique_ptr<Model>> models,size_t vertexBudget=2000000);
    void shutdown(gfx::Device& dev);
    void draw(Renderer3D& renderer);
    void setRuntimeOptions(bool hlod,bool geometry,bool surfaces,bool cache) { hlod_=hlod;geometryEnabled_=geometry;surfacesEnabled_=surfaces;cacheEnabled_=cache; }
    // Opt-in staging only: prepare auxiliary CPU Strata assets at load time,
    // but keep the existing GeometryScene/Renderer3D draw path authoritative.
    // Must be configured before load/loadPrepared. Defaults OFF.
    void setStrataStagingEnabled(bool value) { strataStagingEnabled_ = value; }
    bool strataStagingEnabled() const { return strataStagingEnabled_; }
    // Experimental metadata-only upload after ordinary Model GPU upload.
    // Requires CPU staging and an existing shared geometry-pool allocation;
    // default OFF. Does not upload Strata index data or change draw calls.
    void setStrataGpuMetadataEnabled(bool enabled) { strataGpuMetadataEnabled_ = enabled; }
    bool strataGpuMetadataEnabled() const { return strataGpuMetadataEnabled_; }
    // Diagnostic GPU culling: caller must execute outside render passes and
    // never consume returned ranges as draw calls until GPU/CPU parity is proven.
    // Requires BOTH other Strata staging flags. Default OFF.
    void setStrataGpuCullingEnabled(bool on) { strataGpuCullingEnabled_ = on; }
    bool strataGpuCullingEnabled() const { return strataGpuCullingEnabled_; }
    // Renderer3D diagnostic-only preview. Legacy ModelDraw is always sent
    // unchanged; no Strata indirect or direct draw is performed. Default OFF.
    void setStrataRenderPreviewEnabled(bool on) { strataRenderPreviewEnabled_ = on; }
    bool strataRenderPreviewEnabled() const { return strataRenderPreviewEnabled_; }
    // Optional actual Strata raster route for rigid opaque sources:
    // CPU selects one conservative frontier for main/sun/reflection, then
    // renders it with THE EXISTING Renderer3D on Metal, Vulkan and GLES3.
    // Source GPU vertex data stays shared; the auxiliary index-only stream
    // is pooled when supported, dedicated on GLES3. Default OFF.
    // Scene must be reloaded after changing this option.
    void setStrataPortableRenderingEnabled(bool on) { strataPortableRenderingEnabled_=on; }
    bool strataPortableRenderingEnabled() const { return strataPortableRenderingEnabled_; }
    // Positive budget enables bounded physical index-only GPU residency per
    // source, instead of eager full Strata sidecars. Must set before load.
    // Zero means classic fully resident mode; this does not affect source VB.
    void setStrataStreamingIndexBudgetBytes(size_t bytes) {
        strataStreamingIndexBudgetBytes_=bytes;
    }
    size_t strataStreamingIndexBudgetBytes() const {
        return strataStreamingIndexBudgetBytes_;
    }
    // Optional aggregate cap on ACTIVE Strata index storage in one scene.
    // Streams share this cap via deterministic load-time apportionment.
    // Scene-level limit=0 means no aggregate cap; per-source limit still applies.
    void setStrataStreamingSceneBudgetBytes(size_t bytes) {
        strataStreamingSceneBudgetBytes_=bytes;
    }
    size_t strataStreamingSceneBudgetBytes() const {
        return strataStreamingSceneBudgetBytes_;
    }
    bool collectStrataGpuInstances(const std::string& sourceKey,
                                   std::vector<strata::GpuSceneInstance>& out) const;
    bool dispatchStrataGpuView(gfx::Device& device, const std::string& sourceKey,
                               const strata::GpuPassConfig& pass,
                               uint64_t frameSerial,
                               strata::GpuSelectionSubmission& out);
    // Explicit diagnostic readback; callback runs on the device owner thread
    // in a later frame. Do not let test observers mutate scene lifetime here.
    bool requestStrataGpuReadback(gfx::Device& device,
        const strata::GpuSelectionSubmission& submission,
        std::function<void(strata::GpuSelectionReadback)> callback);
    gfx::BufferHandle strataGpuMetadataBuffer(const std::string& sourceKey) const;
    // Device-resident auxiliary Strata indices from the EXISTING shared page.
    // NOT a draw submission; model/node transforms, materials and index
    // ownership remain with Renderer3D and GlobalGeometryPool.
    struct StrataIndexBinding {
        gfx::BufferHandle buffer;
        uint32_t firstIndex=0, indexCount=0;
        int32_t baseVertex=0;
        bool valid() const { return buffer.valid() && indexCount>0; }
    };
    bool strataGpuIndexBinding(const std::string& sourceKey,
                               StrataIndexBinding& out) const;
    bool strataStagingSupported() const;
    const strata::ModelAsset* strataAsset(const std::string& sourceKey) const;
    // Explicit, read-only CPU reference plan for one source primitive.
    // Caller supplies the correct pass-specific node-to-clip transform.
    // Neither this method nor its result submits GPU draw calls.
    bool planStrataView(const std::string& sourceKey, uint32_t mesh,
                        uint32_t primitive, const strata::ViewConfig& config,
                        strata::ViewPlan& out, std::string* error = nullptr) const;
    // Preflight only: prepares selected auxiliary index spans for a single
    // primitive/pass but DOES NOT draw, replace ModelDraw or commit a route.
    // Renderer3D remains the only authority for pass/material/instance data.
    bool prepareStrataRenderCandidate(const std::string& sourceKey,
        uint32_t mesh, uint32_t primitive, strata::RenderPass pass,
        const strata::ViewConfig& view, strata::RenderCandidate& out,
        std::string* error = nullptr) const;
    const Stats& stats() const { return stats_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    static void validate(const Json& json);
private:
    void loadImpl(gfx::Device& dev,const Json& json,const FileReader& reader,std::map<std::string,std::unique_ptr<Model>> models,size_t vertexBudget);
    struct Instance { Model* model=nullptr; mat4 world; GeometryPolicy policy; Model* surfaceProxy=nullptr; float proxyPixels=96; StaticModelCache cache,proxyCache; float3 min,max; };
    struct Group {
        std::vector<size_t> instances;
        int left=-1,right=-1;
        std::unique_ptr<Model> proxy;
        float3 min,max;
        float error=0;
        StaticModelCache cache;
        bool occlusion=false;
    };
    int buildGroup(gfx::Device& dev, std::vector<size_t> ids);
    void drawGroup(Renderer3D& renderer,int group);
    void drawInstance(Renderer3D& renderer,const Instance& instance);
    void updateStrataStreamingBeforeDraw(Renderer3D& renderer);
    std::unique_ptr<Model> makeProxy(const std::vector<size_t>& ids,float& error);
    std::map<std::string,std::unique_ptr<Model>> models_;
    std::shared_ptr<strata::SceneStaging> strataStaging_; // null unless explicitly staged
    std::shared_ptr<strata::GpuMetadataScene> strataGpuMetadata_;
    std::shared_ptr<strata::GpuClusterCulling> strataGpuCulling_;
    // Sources live until scene shutdown; aliases live until the following
    // draw epoch, after Renderer3D has consumed all ModelDraw pointers.
    std::map<const Model*, std::shared_ptr<strata::PortableSource>> strataPortableSources_;
    std::map<const Instance*, std::unique_ptr<Model>> strataPortableDrawModels_;
    gfx::Device* strataGpuDevice_ = nullptr; // borrowed owner-thread validation
    std::vector<Instance> instances_;
    std::vector<Group> groups_;
    std::vector<std::string> warnings_;
    Stats stats_;
    bool hlod_=false,geometryEnabled_=true,surfacesEnabled_=true,cacheEnabled_=true;
    bool strataStagingEnabled_=false;
    bool strataGpuMetadataEnabled_=false;
    bool strataGpuCullingEnabled_=false;
    bool strataRenderPreviewEnabled_=false;
    bool strataPortableRenderingEnabled_=false;
    size_t strataStreamingIndexBudgetBytes_=0;
    size_t strataStreamingSceneBudgetBytes_=0;
    float errorPixels_=2, maxPixels_=256;
    size_t proxyVertices_=0;
};
}
