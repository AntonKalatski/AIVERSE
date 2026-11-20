#pragma once
#include "Geometry.h"
#include "Renderer3D.h"
#include "NativePresentationRetirement.h"
#include <map>

namespace aiverse::render {
class Renderer3D;
// Static presentation geometry, never collision or simulation objects. HLOD proxies merge
// compatible materials and use bounded-error cluster simplification. Sources stay available.
class GeometryScene {
public:
    struct Stats { uint32_t instances=0, groups=0, proxies=0, submitted=0, surfaceProxiesSubmitted=0; };
    void load(gfx::Device& dev, const Json& json, const FileReader& reader);
    // Stage a replacement scene independently, preserving the current scene
    // on validation/model/GPU upload errors. Publish only after success.
    void loadTransactional(gfx::Device& dev, const Json& json, const FileReader& reader);
    // Procedural/offline callers may supply immutable CPU models instead of files.
    // Borrowed image handles must outlive the scene. GPU geometry is owned by the scene.
    void loadPrepared(gfx::Device& dev,const Json& json,std::map<std::string,std::unique_ptr<Model>> models,size_t vertexBudget=2000000);
    // Full Device idle proof is required at final teardown. Live reload
    // instead moves old resident Models/HLOD proxies into the ordered queue.
    void shutdown(gfx::Device& dev);
    // Discard the current static presentation scene without waiting idle:
    // used on rules/project reload, not final Device teardown.
    void clear(gfx::Device& dev) {retireResident(dev);}
    size_t pendingGpuRetirements() const noexcept;
    // Bound retained GPU owners; a stalled/lost device must not permit an
    // unlimited series of new staging allocations.
    static constexpr size_t kMaxFailedStages=8;
    size_t pendingFailedStages() const noexcept {return failedStages_.size();}
    uint64_t rejectedFailedStageLoads() const noexcept {return rejectedFailedStageLoads_;}
    // Explicit host-controlled full-idle recovery (never from an open pass).
    // Does not retire the currently published scene or its normal queue.
    bool recoverFailedStagesAfterGpuIdle(gfx::Device& dev);
    // Render-owner-thread frame boundary only: poll both live and failed-stage
    // retirement queues, releasing stages only after GPU completion proof.
    bool collectRetired(gfx::Device& dev);
    void draw(Renderer3D& renderer);
    void setRuntimeOptions(bool hlod,bool geometry,bool surfaces,bool cache) { hlod_=hlod;geometryEnabled_=geometry;surfacesEnabled_=surfaces;cacheEnabled_=cache; }
    const Stats& stats() const { return stats_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    static void validate(const Json& json);
private:
    void retireResident(gfx::Device& dev);
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
    std::unique_ptr<Model> makeProxy(const std::vector<size_t>& ids,float& error);
    NativePresentationRetirement retirement_;
    // Failed transactions may still own GPU models after a refused idle.
    // Retain those retirement queues for a later safe collect/shutdown.
    std::vector<std::unique_ptr<GeometryScene>> failedStages_;
    uint64_t rejectedFailedStageLoads_=0;
    std::map<std::string,std::unique_ptr<Model>> models_;
    std::vector<Instance> instances_;
    std::vector<Group> groups_;
    std::vector<std::string> warnings_;
    Stats stats_;
    bool hlod_=false,geometryEnabled_=true,surfacesEnabled_=true,cacheEnabled_=true;
    float errorPixels_=2, maxPixels_=256;
    size_t proxyVertices_=0;
};
}
