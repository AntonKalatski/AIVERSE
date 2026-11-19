#include "GeometryScene.h"
#include "Renderer3D.h"
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
#include "StrataModelAdapter.h"
#include "StrataView.h"
#include "StrataGpuScene.h"
#include "StrataGpuCulling.h"
#include "StrataRenderRouting.h"
#include "StrataPortableRender.h"
#include "StrataStreamingCoordinator.h"
#endif
#include <algorithm>
#include <filesystem>
#include <numeric>
#include <set>
#include <tuple>
#include <utility>

namespace aiverse::render {
namespace {
float3 vector(const Json& j,float3 fallback){return j.isNull()?fallback:float3{float(j[0].toDouble()),float(j[1].toDouble()),float(j[2].toDouble())};}
// Cross-model merging is safe for untextured materials with identical render properties.
bool sameUntextured(const Material& a,const Material& b){
    auto key=[](const Material& m){return std::make_tuple(m.shading,m.baseColor.x,m.baseColor.y,m.baseColor.z,m.baseColor.w,
        m.metallic,m.roughness,m.normalScale,m.occlusionStrength,m.emissive.x,m.emissive.y,m.emissive.z,m.emissiveStrength,
        m.alpha,m.alphaCutoff,m.doubleSided,m.vertexColors,m.castShadow,m.receiveShadow,m.transparent,m.team,m.teamStrength,
        m.uvOffset.x,m.uvOffset.y,m.uvScale.x,m.uvScale.y,m.uvRotation,m.fixedLight.x,m.fixedLight.y,m.fixedLight.z,m.fixedLight.w,m.fixedShade.x,m.fixedShade.y);};
    auto plain=[](const Material& m){return !m.shaderGraph&&m.baseTex<0&&m.mrTex<0&&m.normalTex<0&&m.occlusionTex<0&&m.emissiveTex<0&&m.teamTex<0;};
    return plain(a)&&plain(b)&&key(a)==key(b);
}
bool sameBoundMaterial(Material a,Material b,const std::vector<Image>& images){
    const int ai[]={a.baseTex,a.mrTex,a.normalTex,a.occlusionTex,a.emissiveTex,a.teamTex};
    const int bi[]={b.baseTex,b.mrTex,b.normalTex,b.occlusionTex,b.emissiveTex,b.teamTex};
    for(int k=0;k<6;++k){if((ai[k]<0)!=(bi[k]<0))return false;if(ai[k]>=0){const auto x=images[size_t(ai[k])].tex,y=images[size_t(bi[k])].tex;if(!x.valid()||!(x==y))return false;}}
    a.baseTex=a.mrTex=a.normalTex=a.occlusionTex=a.emissiveTex=a.teamTex=-1;
    b.baseTex=b.mrTex=b.normalTex=b.occlusionTex=b.emissiveTex=b.teamTex=-1;
    return sameUntextured(a,b);
}
float scaleBound(const mat4& m){float sum=0;for(int r=0;r<3;++r)for(int c=0;c<3;++c)sum+=m.at(r,c)*m.at(r,c);return std::sqrt(sum);}
}
void GeometryScene::shutdown(gfx::Device& dev){
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    // Renderer3D has consumed the previous draw epoch before scene unload.
    // Release alias borrowers BEFORE their index sidecars / source Models.
    strataPortableDrawModels_.clear();
    for (auto& [source, staged] : strataPortableSources_)
        if (staged) staged->shutdown(dev);
    strataPortableSources_.clear();
    // The original RHI buffers are already released through their owner;
    // only now return cross-world reservations to the device ledger.
    for(const auto& [source,token]:strataGlobalIndexLeases_)
        strata::releaseDeviceIndexBytes(dev,token);
    strataGlobalIndexLeases_.clear();
    strata::clearIdleDeviceIndexBudget(dev);
    if (strataGpuCulling_) strataGpuCulling_->shutdown(dev);
    if (strataGpuMetadata_) strataGpuMetadata_->shutdown(dev);
#endif
    strataGpuCulling_.reset();
    strataGpuMetadata_.reset();
    strataGpuDevice_ = nullptr;
    strataPortableDevice_=nullptr;
    strataStreamingFrameSerial_=UINT64_MAX;
    strataStreamingFrameBudgetLeft_=0;
    strataStreamingSourceCursor_=0;
    strataStaging_.reset();for(auto& g:groups_)if(g.proxy)releaseModel(dev,*g.proxy);for(auto& [k,m]:models_)if(m)releaseModel(dev,*m);models_.clear();instances_.clear();groups_.clear();warnings_.clear();stats_={};proxyVertices_=0;}
bool GeometryScene::strataStagingSupported() const {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    return true;
#else
    return false;
#endif
}

gfx::BufferHandle GeometryScene::strataGpuMetadataBuffer(const std::string& sourceKey) const {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if (!strataGpuMetadata_) return {};
    const auto it = models_.find(sourceKey);
    return it == models_.end() || !it->second ? gfx::BufferHandle{} :
           strataGpuMetadata_->find(sourceKey, *it->second);
#else
    (void)sourceKey;
    return {};
#endif
}

bool GeometryScene::collectStrataGpuInstances(
    const std::string& sourceKey,
    std::vector<strata::GpuSceneInstance>& out) const {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if (!strataStaging_) return false;
    const auto modelIt = models_.find(sourceKey);
    const auto* asset = strataStaging_->find(sourceKey);
    if (modelIt == models_.end() || !modelIt->second || !asset ||
        asset->primitives.empty()) return false;
    const Model& source = *modelIt->second;
    if (source.bindWorld.size() != source.nodes.size() ||
        source.order.empty()) return false;
    std::vector<strata::GpuSceneInstance> instances;
    uint32_t token = 1;
    for (const Instance& sceneInstance : instances_) {
        if (sceneInstance.model != &source) continue;
        for (const int nodeId : source.order) {
            if (nodeId < 0 || size_t(nodeId) >= source.nodes.size())
                return false;
            const Node& node = source.nodes[size_t(nodeId)];
            if (node.mesh < 0) continue;
            if (size_t(node.mesh) >= source.meshes.size()) return false;
            const mat4 world = sceneInstance.world *
                               source.bindWorld[size_t(nodeId)];
            for (size_t p = 0; p < asset->primitives.size(); ++p) {
                if (asset->primitives[p].mesh != uint32_t(node.mesh)) continue;
                if (instances.size() >= 4096) return false;
                instances.push_back(strata::makeGpuSceneInstance(
                    world, uint32_t(p), token++));
            }
        }
    }
    if (instances.empty()) return false;
    out = std::move(instances);
    return true;
#else
    (void)sourceKey; (void)out;
    return false;
#endif
}

bool GeometryScene::requestStrataGpuReadback(
    gfx::Device& device, const strata::GpuSelectionSubmission& submission,
    std::function<void(strata::GpuSelectionReadback)> callback) {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if (!strataGpuCullingEnabled_ || !strataGpuCulling_ ||
        strataGpuDevice_ != &device || !callback) return false;
    return strataGpuCulling_->requestReadback(device, submission,
                                            std::move(callback));
#else
    (void)device; (void)submission; (void)callback;
    return false;
#endif
}

bool GeometryScene::dispatchStrataGpuView(
    gfx::Device& device, const std::string& sourceKey,
    const strata::GpuPassConfig& pass, uint64_t frameSerial,
    strata::GpuSelectionSubmission& out) {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if (!strataGpuCullingEnabled_ || !strataStagingEnabled_ ||
        !strataGpuMetadataEnabled_ || !strataGpuMetadata_ ||
        strataGpuDevice_ != &device) return false;
    const auto metadata = strataGpuMetadataBuffer(sourceKey);
    if (!metadata.valid()) return false;
    std::vector<strata::GpuSceneInstance> instances;
    if (!collectStrataGpuInstances(sourceKey, instances)) return false;
    if (!strataGpuCulling_)
        strataGpuCulling_ = std::make_shared<strata::GpuClusterCulling>();
    return strataGpuCulling_->dispatch(device, metadata, instances,
                                      pass, frameSerial, out);
#else
    (void)device; (void)sourceKey; (void)pass; (void)frameSerial; (void)out;
    return false;
#endif
}

bool GeometryScene::strataGpuIndexBinding(const std::string& sourceKey,
                                           StrataIndexBinding& out) const {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if (!strataGpuMetadata_) return false;
    const auto it = models_.find(sourceKey);
    if (it == models_.end() || !it->second) return false;
    strata::GpuMetadataScene::IndexBinding binding;
    if (!strataGpuMetadata_->indexBinding(sourceKey, *it->second, binding))
        return false;
    out = {binding.buffer, binding.firstIndex, binding.indexCount,
           binding.baseVertex};
    return true;
#else
    (void)sourceKey; (void)out;
    return false;
#endif
}

const strata::ModelAsset* GeometryScene::strataAsset(const std::string& sourceKey) const {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    return strataStaging_ ? strataStaging_->find(sourceKey) : nullptr;
#else
    (void)sourceKey;
    return nullptr;
#endif
}

bool GeometryScene::prepareStrataRenderCandidate(
    const std::string& sourceKey, uint32_t mesh, uint32_t primitive,
    strata::RenderPass pass, const strata::ViewConfig& view,
    strata::RenderCandidate& out, std::string* error) const {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if (!strataStagingEnabled_ || !strataGpuMetadataEnabled_ ||
        !strataStaging_ || !strataGpuMetadata_) {
        if (error) *error = "Strata CPU/GPU staging is disabled";
        return false;
    }
    const auto it = models_.find(sourceKey);
    const auto* asset = strataStaging_->find(sourceKey);
    if (it == models_.end() || !it->second || !asset) {
        if (error) *error = "Strata source is not staged";
        return false;
    }
    return strata::prepareRenderCandidate(*it->second, *asset,
        *strataGpuMetadata_, sourceKey, mesh, primitive, pass,
        view, out, error);
#else
    (void)sourceKey; (void)mesh; (void)primitive; (void)pass;
    (void)view; (void)out;
    if (error) *error = "Strata is disabled at build time";
    return false;
#endif
}

bool GeometryScene::planStrataView(const std::string& sourceKey,
                                   uint32_t mesh, uint32_t primitive,
                                   const strata::ViewConfig& config,
                                   strata::ViewPlan& out,
                                   std::string* error) const {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    const auto* model = strataAsset(sourceKey);
    if (!model) {
        if (error) *error = "Strata source is not staged";
        return false;
    }
    for (const auto& p : model->primitives)
        if (p.mesh == mesh && p.primitive == primitive)
            return strata::planView(p.geometry, config, out, error);
    if (error) *error = "Strata primitive is not staged";
    return false;
#else
    (void)sourceKey; (void)mesh; (void)primitive; (void)config; (void)out;
    if (error) *error = "Strata support is disabled at build time";
    return false;
#endif
}

void GeometryScene::load(gfx::Device& dev,const Json& json,const FileReader& read){loadImpl(dev,json,read,{},2000000);}
void GeometryScene::loadPrepared(gfx::Device& dev,const Json& json,std::map<std::string,std::unique_ptr<Model>> models,size_t vertexBudget){if(!vertexBudget||vertexBudget>16000000)throw std::runtime_error("geometry scene: prepared vertex budget must be 1..16000000");loadImpl(dev,json,{},std::move(models),vertexBudget);}
void GeometryScene::loadImpl(gfx::Device& dev,const Json& json,const FileReader& read,std::map<std::string,std::unique_ptr<Model>> models,size_t vertexBudget){
    validate(json);shutdown(dev);strataPortableDevice_=&dev;models_=std::move(models);geometryEnabled_=surfacesEnabled_=cacheEnabled_=true;
    hlod_=json["hlod"].getBool("enabled");errorPixels_=float(json["hlod"]["errorPixels"].toDouble(2));maxPixels_=float(json["hlod"]["maxPixels"].toDouble(256));
    try{
        size_t vertices=0;
        for(const auto& [name,m]:models_){
            if(!m||m->vertices.empty()||m->indices.empty()||!m->skins.empty()||!m->clips.empty())throw std::runtime_error("geometry scene: prepared models must be nonempty rigid CPU meshes");
            if(m->vb.valid()||m->ib.valid())throw std::runtime_error("geometry scene: prepared geometry must not be uploaded");
            for(const auto& mesh:m->meshes)for(const auto& p:mesh.primitives)if(p.morphTargets)throw std::runtime_error("geometry scene: morph source unsupported");
            vertices+=m->vertices.size();
        }
        if(vertices>vertexBudget)throw std::runtime_error("geometry scene: exceeds source vertex budget");
        for(auto& i:json["instances"].items()){
            const auto file=i.getStr("model");
            if(!models_.count(file)){
                if(!read)throw std::runtime_error("geometry scene: missing prepared source "+file);
                auto m=loadModel(file,read,&warnings_);
                if(!m->skins.empty()||!m->clips.empty())throw std::runtime_error("geometry scene: animated model '"+file+"' must use ordinary entities");
                for(auto& mesh:m->meshes)for(auto& p:mesh.primitives)if(p.morphTargets)throw std::runtime_error("geometry scene: morph model must use ordinary entities");
                vertices+=m->vertices.size();if(vertices>vertexBudget)throw std::runtime_error("geometry scene: exceeds source vertex budget");
                if(m->vertices.empty()||m->indices.empty())throw std::runtime_error("geometry scene: source has no triangles: "+file);
                models_[file]=std::move(m);
            }
            Instance in;in.model=models_.at(file).get();in.policy=parseGeometryPolicy(i["geometry"]);
            auto rot=vector(i["rotation"],{});constexpr float deg=.0174532925199433f;
            in.world=mat4::translate(vector(i["position"],{}))*mat4::rotateY(rot.y*deg)*mat4::rotateX(rot.x*deg)*mat4::rotateZ(rot.z*deg)*mat4::scale(vector(i["scale"],{1,1,1}));
            if(i.has("proxy")){
                const auto proxyFile=i["proxy"].getStr("model");
                if(!models_.count(proxyFile)){
                    if(!read)throw std::runtime_error("geometry scene: missing prepared proxy "+proxyFile);
                    auto proxy=loadModel(proxyFile,read,&warnings_);
                    vertices+=proxy->vertices.size();if(vertices>vertexBudget)throw std::runtime_error("geometry scene: exceeds source vertex budget");
                    if(proxy->vertices.empty()||proxy->indices.empty()||!proxy->skins.empty()||!proxy->clips.empty())throw std::runtime_error("geometry scene: proxy must be a nonempty rigid model");
                    for(auto& mesh:proxy->meshes)for(auto& p:mesh.primitives)if(p.morphTargets)throw std::runtime_error("geometry scene: morph proxy unsupported");
                    models_[proxyFile]=std::move(proxy);
                }
                in.surfaceProxy=models_.at(proxyFile).get();in.proxyPixels=float(i["proxy"]["maxPixels"].toDouble(96));
                ++stats_.proxies;
            }
            instances_.push_back(in);
        }
        for(auto& [file,m]:models_)if(hlod_||std::any_of(instances_.begin(),instances_.end(),[&](const Instance& i){return i.model==m.get()&&(i.policy.clusters||i.policy.shadowClusters||i.policy.occluder);}))prepareGeometry(*m);
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
        if (strataPortableRenderingEnabled_) {
            // Crucial ordering: ordinary uploadModel() may generate LOD
            // meshes, change packed vertex attributes and rebuild batches.
            // Complete that preparation first, then stage Strata from the
            // EXACT source vertex/index stream that the RHI will upload.
            // This leaves legacy material/shadow/LOD metadata intact.
            for (auto& [key, model] : models_)
                if (model && !model->uploadPrepared) {
                    prepareModelUpload(*model);
                    using Policy=Model::Batching::GeometryPoolPolicy;
                    if (model->batching.geometryPoolPolicy==Policy::Inherit)
                        model->batching.geometryPoolPolicy=
                            Policy::AllowRigidLods;
                }
        }
        if (strataStagingEnabled_ || strataPortableRenderingEnabled_) {
            // Build once for each *source* model before uploadModel may release
            // CPU vertex streams. Surface/HLOD proxies retain legacy ownership
            // and are never duplicated here. This path does NOT draw Strata.
            strataStaging_ = std::make_shared<strata::SceneStaging>();
            if (strataPortableRenderingEnabled_)
                strataStaging_->setAllowPreparedRootLods(true);
            std::set<const Model*> usedSources;
            for (const auto& instance : instances_)
                usedSources.insert(instance.model);
            for (const auto& [file, model] : models_)
                if (model && usedSources.count(model.get()))
                    strataStaging_->stage(file, *model);
            const auto& st = strataStaging_->stats();
            stats_.strataSources = st.stagedSources;
            stats_.strataPrimitives = st.stagedPrimitives;
            stats_.strataSkippedSources = st.skippedSources;
            stats_.strataSkippedPrimitives = st.skippedPrimitives;
            stats_.strataFailures = st.failedSources;
            stats_.strataNodes = st.clusterNodes;
            stats_.strataMeshlets = st.meshlets;
            stats_.strataIndexBytes = st.indexBytes;
            for (const auto& warning : strataStaging_->warnings())
                warnings_.push_back(warning);
        }
#endif
        if(hlod_&&!instances_.empty()){std::vector<size_t> ids(instances_.size());std::iota(ids.begin(),ids.end(),0);buildGroup(dev,std::move(ids));}
        for(auto& [k,m]:models_)if(!uploadModel(dev,*m)||!m->vb.valid()||!m->ib.valid())throw std::runtime_error("geometry scene: source GPU upload failed");
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
        if (strataPortableRenderingEnabled_ && strataStaging_) {
            // Budget zero preserves the original Stage 14 eager path. A
            // scene budget (even with no per-source cap) enables streaming.
            // Root reservations are admitted BEFORE any GPU index upload:
            // models that cannot fit their roots stay intact on Legacy.
            std::map<std::string,size_t> streamingBudgets;
            const bool streamingRequested = strataStreamingIndexBudgetBytes_ ||
                                            strataStreamingSceneBudgetBytes_ ||
                                            strataGlobalIndexBudgetBytes_;
            const bool globalBudgetConfigured = !streamingRequested ||
                (strata::configureDeviceIndexBudget(
                    dev,uint64_t(strataGlobalIndexBudgetBytes_)) &&
                 strata::configureDeviceFrameUploadBudget(
                    dev,uint64_t(strataGlobalFrameUploadBudgetBytes_)));
            if(!globalBudgetConfigured)
                warnings_.push_back(
                    "STRATA global device budget conflicts with an active world");
            if (streamingRequested) {
                std::vector<strata::SceneIndexBudgetRequest> candidates;
                constexpr uint64_t kSourceMaximum=64ull*1024ull*1024ull;
                for (const auto& [key,model]:models_) {
                    if (!model) continue;
                    const auto* asset=strataStaging_->find(key);
                    if (!asset || asset->primitives.empty() ||
                        !asset->skipped.empty()) continue;
                    uint64_t roots=0, all=0;
                    for(const auto& entry:asset->primitives){
                        const auto& g=entry.geometry;
                        all+=uint64_t(g.indices.size())*sizeof(uint32_t);
                        for(uint32_t root:g.roots)
                            if(root<g.nodes.size())
                                roots+=uint64_t(g.nodes[root].indexCount)*
                                       sizeof(uint32_t);
                    }
                    uint64_t cap=std::min(all,kSourceMaximum);
                    if(strataStreamingIndexBudgetBytes_)
                        cap=std::min(cap,uint64_t(strataStreamingIndexBudgetBytes_));
                    if(!roots || roots>cap) continue;
                    candidates.push_back({key,roots,cap});
                }
                for(const auto& grant:strata::apportionSceneIndexBudget(
                        std::move(candidates),strataStreamingSceneBudgetBytes_))
                    if(grant.bytes)
                        streamingBudgets[grant.key]=size_t(grant.bytes);
            }
            // CPU-selected raster fallback works without compute or MDI.
            // Only index data is streamed; source VB/material ownership
            // remains with the single original Renderer3D Model.
            for (const auto& [key, model] : models_) {
                if (!model) continue;
                const auto* asset = strataStaging_->find(key);
                if (!asset) { ++stats_.strataPortableDeclined; continue; }
                if(!globalBudgetConfigured){
                    ++stats_.strataPortableDeclined;
                    ++stats_.strataGlobalBudgetDeclined;
                    continue;
                }
                size_t sourceBudget=0;
                if(streamingRequested){
                    const auto granted=streamingBudgets.find(key);
                    if(granted==streamingBudgets.end()){
                        ++stats_.strataPortableDeclined;
                        ++stats_.strataStreamingBudgetDeclined;
                        continue; // source remains fully valid Legacy
                    }
                    sourceBudget=granted->second;
                }
                // Reserve the maximum arena allocation BEFORE loading a
                // source. All worlds on this device compete for one ledger.
                // If the remaining cap allows only roots, clamp this source
                // rather than dropping it when a safe coarse LOD fits.
                uint64_t leaseToken=0;
                if(streamingRequested){
                    uint64_t roots=0;
                    for(const auto& entry:asset->primitives)
                        for(uint32_t root:entry.geometry.roots)
                            roots+=uint64_t(entry.geometry.nodes[root].indexCount)*4u;
                    const auto current=strata::deviceIndexBudgetStats(dev);
                    if(current.limitBytes){
                        const uint64_t spare=current.reservedBytes<=current.limitBytes
                            ? current.limitBytes-current.reservedBytes:0;
                        if(spare<roots){
                            ++stats_.strataPortableDeclined;
                            ++stats_.strataGlobalBudgetDeclined;
                            continue;
                        }
                        sourceBudget=size_t(std::min<uint64_t>(sourceBudget,spare));
                    }
                    if(!strata::reserveDeviceIndexBytes(dev,sourceBudget,leaseToken)){
                        ++stats_.strataPortableDeclined;
                        ++stats_.strataGlobalBudgetDeclined;
                        continue;
                    }
                    strataGlobalIndexLeases_[model.get()]=leaseToken;
                }
                std::string error;
                std::shared_ptr<strata::DiskIndexPages> diskPages;
                const bool diskApplicable =
                    !strataDiskPageDirectory_.empty() && sourceBudget &&
                    strataIncrementalIndexStreamingEnabled_ &&
                    dev.canUpdateSharedBuffers() &&
                    (dev.info().backend==gfx::Backend::Metal ||
                     dev.info().backend==gfx::Backend::Vulkan);
                if(diskApplicable){
                    diskPages=std::make_shared<strata::DiskIndexPages>();
                    // Source key is only hashed to a safe archive filename;
                    // it is never interpreted as a filesystem subpath.
                    const auto filename=strata::DiskIndexPages::
                        archiveFilename(key);
                    const auto path=(std::filesystem::path(
                        strataDiskPageDirectory_)/filename).string();
                    if(!diskPages->open(path,*asset,sourceBudget,512,&error)){
                        ++stats_.strataPortableDeclined;
                        ++stats_.strataDiskArchiveDeclined;
                        if(leaseToken){
                            strata::releaseDeviceIndexBytes(dev,leaseToken);
                            strataGlobalIndexLeases_.erase(model.get());
                        }
                        warnings_.push_back("STRATA disk archive '"+key+"': "+error);
                        continue; // original Model remains Legacy
                    }
                }
                auto source=std::make_shared<strata::PortableSource>();
                if (!source->stage(dev, *model, *asset, &error,
                                   sourceBudget,
                                   strataIncrementalIndexStreamingEnabled_,
                                   strataStreamingUploadBudgetBytes_,
                                   std::move(diskPages))) {
                    ++stats_.strataPortableDeclined;
                    if(leaseToken){
                        strata::releaseDeviceIndexBytes(dev,leaseToken);
                        strataGlobalIndexLeases_.erase(model.get());
                    }
                    if (!error.empty())
                        warnings_.push_back("portable Strata source " + key +
                                            ": " + error);
                    continue;
                }
                if(source->streamingEnabled()){
                    ++stats_.strataStreamedSources;
                    if(const auto* status=source->residencyStats()){
                        stats_.strataStreamingIndexBytes+=status->activeBytes;
                        stats_.strataStreamingArenaBytes+=status->arenaBytes;
                        if(status->incremental)++stats_.strataIncrementalSources;
                        if(diskApplicable){
                            ++stats_.strataDiskArchiveSources;
                            stats_.strataDiskBytesRead+=status->diskBytesRead;
                        }
                    }
                }
                strataPortableSources_.emplace(model.get(), std::move(source));
                ++stats_.strataPortableSources;
                // Disk manifest was matched against the entire source,
                // and all pinned roots have been checked and GPU-uploaded.
                // Only the portable disk path is allowed to drop its
                // redundant STRATA CPU index arrays. Diagnostic/metadata
                // paths still require raw indices and must retain them.
                const bool canReleaseCpu = diskApplicable &&
                    strataDropArchivedCpuIndicesEnabled_ &&
                    !strataStagingEnabled_ && !strataGpuMetadataEnabled_ &&
                    !strataGpuCullingEnabled_ && !strataRenderPreviewEnabled_;
                if(canReleaseCpu){
                    const size_t freed=strataStaging_->
                        discardArchivedCpuIndices(key);
                    stats_.strataArchivedCpuIndexBytesFreed+=freed;
                    stats_.strataArchivedCpuIndexSources+=freed!=0;
                }else if(diskApplicable && strataDropArchivedCpuIndicesEnabled_){
                    warnings_.push_back("STRATA kept CPU index payload for diagnostic source "+key);
                }
            }
        }
        {
            const auto shared=strata::deviceIndexBudgetStats(dev);
            stats_.strataDeviceReservedIndexBytes=shared.reservedBytes;
            stats_.strataDeviceIndexLeases=shared.activeLeases;
            stats_.strataDeviceFrameUploadedBytes=shared.currentFrameUploadedBytes;
            stats_.strataDeviceFrameUploadRemainingBytes=shared.frameUploadRemainingBytes;
            const auto io=strata::sharedDiskIoStats();
            stats_.strataSharedIoWorkers=io.workers;
            stats_.strataSharedIoQueuedJobs=io.queuedJobs;
            stats_.strataSharedIoActiveJobs=io.activeJobs;
            stats_.strataSharedCpuReadyBytes=io.readyBytes;
            stats_.strataSharedCpuReadyBudgetBytes=io.readyLimitBytes;
        }
        if (strataGpuMetadataEnabled_ && strataStaging_) {
            strataGpuDevice_ = &dev;
            // Optional GPU metadata only; source Model remains owner of
            // GlobalGeometryPool VB/IB and legacy Renderer3D draws unchanged.
            // Failure to allocate metadata must never abort the scene.
            strataGpuMetadata_ = std::make_shared<strata::GpuMetadataScene>();
            for (const auto& [key, model] : models_)
                if (model)
                    if (const auto* asset = strataStaging_->find(key))
                        strataGpuMetadata_->stage(dev, key, *model, *asset);
            const auto& gpuStats = strataGpuMetadata_->stats();
            stats_.strataGpuMetadataSources = gpuStats.sources;
            stats_.strataGpuMetadataDeclined = gpuStats.declined;
            stats_.strataGpuMetadataFailures = gpuStats.failed;
            stats_.strataGpuMetadataBytes = gpuStats.bytes;
            stats_.strataGpuIndexedSources = gpuStats.indexedSources;
            stats_.strataGpuIndexBytes = gpuStats.indexBytes;
            stats_.strataGpuIndexDeclined = gpuStats.indexDeclined;
            stats_.strataGpuIndexFailures = gpuStats.indexFailed;
            for (const auto& warning : strataGpuMetadata_->warnings())
                warnings_.push_back(warning);
        }
#endif
        for(auto& in:instances_){
            in.cache.prepare(*in.model,in.world);
            if(in.surfaceProxy)in.proxyCache.prepare(*in.surfaceProxy,in.world);
            in.min={1e30f,1e30f,1e30f};in.max={-1e30f,-1e30f,-1e30f};
            for(int c=0;c<8;++c){auto a=in.model->boundsMin,b=in.model->boundsMax;auto p=in.world.transformPoint({c&1?b.x:a.x,c&2?b.y:a.y,c&4?b.z:a.z});in.min=min3(in.min,p);in.max=max3(in.max,p);}
        }
        for(auto& g:groups_)if(g.proxy)g.cache.prepare(*g.proxy,mat4{});
        stats_.instances=uint32_t(instances_.size());stats_.groups=uint32_t(groups_.size());
    }catch(...){shutdown(dev);throw;}
}
std::unique_ptr<Model> GeometryScene::makeProxy(const std::vector<size_t>& ids,float& error){
    // Explicit baked surfaces own their switch threshold. Do not bypass it with a parent proxy.
    for(auto id:ids)if(instances_[id].surfaceProxy)return {};
    // Transparent/masked/graph materials retain their original independent draws and sorting.
    for(auto id:ids)for(auto& m:instances_[id].model->materials)if(m.alpha!=AlphaMode::Opaque||m.transparent||m.shaderGraph)return {};
    auto out=std::make_unique<Model>();out->path="geometry:hlod";
    out->meshes.emplace_back();Node node;node.mesh=0;out->nodes.push_back(node);
    std::map<std::pair<const Model*,int>,int> materials;
    std::map<const Model*,int> images;
    for(auto id:ids){const auto& in=instances_[id];const Model& m=*in.model;
        if(proxyVertices_+out->vertices.size()+m.vertices.size()>2000000)return {};
        if(!images.count(&m)){images[&m]=int(out->images.size());out->images.insert(out->images.end(),m.images.begin(),m.images.end());}
        for(int n:m.order){const auto& nd=m.nodes[size_t(n)];if(nd.mesh<0)continue;mat4 xf=in.world*m.bindWorld[size_t(n)],normal=xf.normalMatrix();
            size_t pi=0;for(int mi=0;mi<nd.mesh;++mi)pi+=m.meshes[size_t(mi)].primitives.size();
            for(const auto& p:m.meshes[size_t(nd.mesh)].primitives){
                const auto key=std::make_pair(&m,p.material);
                if(!materials.count(key)){auto mat=m.materials[size_t(p.material)];int index=-1;
                    for(auto field:{&mat.baseTex,&mat.mrTex,&mat.normalTex,&mat.occlusionTex,&mat.emissiveTex,&mat.teamTex})if(*field>=0)*field+=images[&m];
                    for(size_t k=0;k<out->materials.size();++k)if(sameBoundMaterial(mat,out->materials[k],out->images)){index=int(k);break;}
                    if(index<0){index=int(out->materials.size());out->materials.push_back(mat);}
                    materials[key]=index;}
                // Build temporary per-material index lists after all transforms below.
                const int material=materials[key];
                std::map<uint32_t,uint32_t> remap;
                auto emit=[&](uint32_t first,uint32_t count){for(uint32_t k=0;k<count;++k){uint32_t source=m.indices[first+k];
                    if(!remap.count(source)){auto v=m.vertices[source];v.pos=xf.transformPoint(v.pos);v.nrm=normalize(normal.transformDir(v.nrm));auto t=normalize(xf.transformDir({v.tangent.x,v.tangent.y,v.tangent.z}));v.tangent={t.x,t.y,t.z,v.tangent.w};remap[source]=uint32_t(out->vertices.size());out->vertices.push_back(v);}
                    out->indices.push_back(remap[source]);
                }};
                const auto first=uint32_t(out->indices.size());
                const auto& clusters=m.geometry->primitives[pi++];
                if(clusters.empty())emit(p.firstIndex,p.indexCount);
                else for(auto& c:clusters){emit(c.coarseFirst,c.coarseCount);error=std::max(error,c.error*scaleBound(xf));}
                // Each segment is tagged; regrouped by material at the end.
                Primitive segment;segment.material=material;segment.firstIndex=first;segment.indexCount=uint32_t(out->indices.size())-first;
                out->meshes[0].primitives.push_back(segment);
            }
        }
    }
    std::vector<uint32_t> indices;std::vector<Primitive> primitives;
    for(size_t material=0;material<out->materials.size();++material){Primitive p;p.material=int(material);p.firstIndex=uint32_t(indices.size());
        for(auto& segment:out->meshes[0].primitives)if(segment.material==int(material))indices.insert(indices.end(),out->indices.begin()+segment.firstIndex,out->indices.begin()+segment.firstIndex+segment.indexCount);
        p.indexCount=uint32_t(indices.size())-p.firstIndex;p.vertexCount=uint32_t(out->vertices.size());if(p.indexCount)primitives.push_back(p);}
    // Existing instancing already combines identical primitives in batches of 32.
    std::map<std::pair<const Model*,const Primitive*>,size_t> batches;size_t sourceIndices=0;
    for(auto id:ids){const auto* model=instances_[id].model;for(int n:model->order){const auto& node=model->nodes[size_t(n)];if(node.mesh<0)continue;
        for(const auto& p:model->meshes[size_t(node.mesh)].primitives){++batches[{model,&p}];sourceIndices+=p.indexCount;}}}
    size_t sourceDraws=0;for(auto [key,count]:batches)sourceDraws+=(count+31)/32;
    if(primitives.size()>=sourceDraws && indices.size()>=sourceIndices)return {};
    out->indices=std::move(indices);out->meshes[0].primitives=std::move(primitives);out->finalize();proxyVertices_+=out->vertices.size();return out;
}
int GeometryScene::buildGroup(gfx::Device& dev,std::vector<size_t> ids){
    const int index=int(groups_.size());groups_.emplace_back();
    float3 mn{1e30f,1e30f,1e30f},mx{-1e30f,-1e30f,-1e30f};
    for(auto id:ids){const auto& in=instances_[id];for(int c=0;c<8;++c){auto a=in.model->boundsMin,b=in.model->boundsMax;auto p=in.world.transformPoint({c&1?b.x:a.x,c&2?b.y:a.y,c&4?b.z:a.z});mn=min3(mn,p);mx=max3(mx,p);}}
    groups_[size_t(index)].min=mn;groups_[size_t(index)].max=mx;
    if(ids.size()>8){const auto e=mx-mn;const int axis=e.x>=e.z?0:2;std::stable_sort(ids.begin(),ids.end(),[&](size_t a,size_t b){auto x=instances_[a].world.transformPoint({}),y=instances_[b].world.transformPoint({});return axis==0?x.x<y.x:x.z<y.z;});
        auto middle=ids.begin()+ids.size()/2;const int l=buildGroup(dev,{ids.begin(),middle}),r=buildGroup(dev,{middle,ids.end()});groups_[size_t(index)].left=l;groups_[size_t(index)].right=r;
    }else groups_[size_t(index)].instances=ids;
    auto& group=groups_[size_t(index)];
    group.occlusion=std::all_of(ids.begin(),ids.end(),[&](size_t id){return instances_[id].policy.occlusion;});
    if(ids.size()>1){group.proxy=makeProxy(ids,group.error);if(group.proxy){if(!uploadModel(dev,*group.proxy)||!group.proxy->vb.valid()||!group.proxy->ib.valid())throw std::runtime_error("geometry scene: proxy GPU upload failed");++stats_.proxies;}}
    return index;
}
void GeometryScene::drawGroup(Renderer3D& r,int index){auto& g=groups_[size_t(index)];
    // Retain offscreen source draws so their shadows/reflections remain available.
    const float extent=length(g.max-g.min);
    const bool far=geometryErrorPixels(r.viewProj(),g.min,g.max,extent,r.sceneWidth(),r.sceneHeight(),r.clipDepthZeroToOne())<=maxPixels_;
    if(g.proxy&&far&&geometryErrorPixels(r.viewProj(),g.min,g.max,g.error,r.sceneWidth(),r.sceneHeight(),r.clipDepthZeroToOne())<=errorPixels_){ModelDraw d;d.model=g.proxy.get();d.staticCache=cacheEnabled_?&g.cache:nullptr;d.geometry.occlusion=geometryEnabled_&&g.occlusion;r.drawModel(d);++stats_.submitted;return;}
    if(g.left>=0){drawGroup(r,g.left);drawGroup(r,g.right);return;}
    for(auto id:g.instances)drawInstance(r,instances_[id]);
}
void GeometryScene::updateStrataStreamingBeforeDraw(Renderer3D& r){
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if(!strataPortableRenderingEnabled_ ||
       !(strataStreamingIndexBudgetBytes_ || strataStreamingSceneBudgetBytes_) ||
       strataPortableSources_.empty() ||
       r.sceneWidth()<=0 || r.sceneHeight()<=0)return;
    std::vector<strata::PortablePassView> views;
    views.push_back({r.viewProj(),r.sceneWidth(),r.sceneHeight(),
                     r.clipDepthZeroToOne(),errorPixels_});
    if(r.shadowQuality()!=ShadowQuality::Off && r.shadowMapSize()>0)
        views.push_back({r.lightViewProj(),r.shadowMapSize(),
                         r.shadowMapSize(),r.clipDepthZeroToOne(),errorPixels_});
    const bool forceFinest=r.lights.reflectionsWanted();
    std::map<const Model*,std::vector<strata::IndexDemand>> requests;
    std::set<const Model*> failed;
    // Preflight ALL instances before submitting ANY Renderer3D ModelDraw.
    // One shared source buffer generation covers the complete draw epoch,
    // including independent main, shadow and reflection passes.
    for(const auto& instance:instances_){
        const auto it=strataPortableSources_.find(instance.model);
        if(it==strataPortableSources_.end() || !it->second ||
           !it->second->streamingEnabled() ||
           failed.count(instance.model))continue;
        auto& list=requests[instance.model];
        if(!it->second->collectResidencyDemands(
               instance.world,views,forceFinest,list)){
            failed.insert(instance.model);
            list.clear(); // keep roots; don't promote a partial view plan
        }
    }
    stats_.strataStreamingIndexBytes=0;
    stats_.strataStreamingEvictions=0;
    stats_.strataStreamingArenaBytes=0;
    stats_.strataStreamingUploadedBytes=0;
    stats_.strataStreamingUploadDeferrals=0;
    stats_.strataStreamingFenceDeferrals=0;
    stats_.strataDiskBytesRead=0;
    stats_.strataDiskReadFailures=0;
    stats_.strataDiskPending=0;
    stats_.strataDiskReadyBytes=0;
    stats_.strataStreamingGpuTransferBytes=0;
    const size_t frameCap=strataStreamingFrameUploadBudgetBytes_;
    if(frameCap && strataPortableDevice_){
        const uint64_t serial=strataPortableDevice_->frameSerial();
        if(!serial || strataStreamingFrameSerial_!=serial){
            strataStreamingFrameSerial_=serial;
            strataStreamingFrameBudgetLeft_=frameCap;
        }
    }
    // Model keys are stable across runs. Rotate the first source once per
    // prepass so one expensive source cannot permanently monopolize the
    // frame-level GPU bandwidth allowance.
    std::vector<const Model*> ordered;
    for(const auto& [key,instance]:models_){
        if(!instance)continue;
        const auto it=strataPortableSources_.find(instance.get());
        if(it!=strataPortableSources_.end() && it->second &&
           it->second->streamingEnabled())
            ordered.push_back(instance.get());
    }
    if(!ordered.empty()){
        const size_t start=size_t(strataStreamingSourceCursor_++%ordered.size());
        for(size_t i=0;i<ordered.size();++i){
            const Model* model=ordered[(start+i)%ordered.size()];
            auto& source=strataPortableSources_.at(model);
            std::string error;
            const auto* before=source->residencyStats();
            const uint64_t beforeTransfer=before ? before->gpuTransferBytes : 0;
            const bool incremental=before && before->incremental;
            size_t allowance=frameCap && incremental
                ? strataStreamingFrameBudgetLeft_ : SIZE_MAX;
            const uint64_t serial=strataPortableDevice_ ?
                strataPortableDevice_->frameSerial() : 0;
            if(incremental && strataPortableDevice_){
                const uint64_t shared=strata::deviceFrameUploadAllowance(
                    *strataPortableDevice_,serial);
                allowance=std::min<uint64_t>(allowance,shared);
            }
            if(failed.count(model) ||
               !source->updateResidency(requests[model],&error,allowance))
                ++stats_.strataStreamingPrepareFailures;
            if(const auto* status=source->residencyStats()){
                const uint64_t copied=status->gpuTransferBytes>=beforeTransfer
                    ? status->gpuTransferBytes-beforeTransfer : 0;
                if(frameCap && incremental){
                    strataStreamingFrameBudgetLeft_=
                        copied>=strataStreamingFrameBudgetLeft_
                            ? 0 : strataStreamingFrameBudgetLeft_-size_t(copied);
                }
                if(incremental && strataPortableDevice_)
                    strata::accountDeviceFrameUpload(
                        *strataPortableDevice_,serial,copied);
                stats_.strataStreamingIndexBytes+=status->activeBytes;
                stats_.strataStreamingEvictions+=status->evictions;
                stats_.strataStreamingArenaBytes+=status->arenaBytes;
                stats_.strataStreamingUploadedBytes+=status->uploadedBytes;
                stats_.strataStreamingGpuTransferBytes+=status->gpuTransferBytes;
                stats_.strataStreamingUploadDeferrals+=status->uploadBudgetDeferrals;
                stats_.strataStreamingFenceDeferrals+=status->fenceReuseDeferrals;
                stats_.strataDiskBytesRead+=status->diskBytesRead;
                stats_.strataDiskReadFailures+=status->diskFailures;
                stats_.strataDiskPending+=status->diskPending;
                stats_.strataDiskReadyBytes+=status->diskReadyBytes;
            }
        }
    }
    if(strataPortableDevice_){
        const auto shared=strata::deviceIndexBudgetStats(*strataPortableDevice_);
        stats_.strataDeviceReservedIndexBytes=shared.reservedBytes;
        stats_.strataDeviceIndexLeases=shared.activeLeases;
        stats_.strataDeviceFrameUploadedBytes=shared.currentFrameUploadedBytes;
        stats_.strataDeviceFrameUploadRemainingBytes=shared.frameUploadRemainingBytes;
    }
    const auto io=strata::sharedDiskIoStats();
    stats_.strataSharedIoWorkers=io.workers;
    stats_.strataSharedIoQueuedJobs=io.queuedJobs;
    stats_.strataSharedIoActiveJobs=io.activeJobs;
    stats_.strataSharedCpuReadyBytes=io.readyBytes;
    stats_.strataSharedCpuReadyBudgetBytes=io.readyLimitBytes;
#else
    (void)r;
#endif
}
void GeometryScene::draw(Renderer3D& r){
    stats_.submitted=0;stats_.surfaceProxiesSubmitted=0;
    updateStrataStreamingBeforeDraw(r);
    if(hlod_&&!groups_.empty())drawGroup(r,0);
    else for(auto& i:instances_)drawInstance(r,i);
}
void GeometryScene::drawInstance(Renderer3D& r,const Instance& i){
    ModelDraw d;d.model=i.model;d.world=i.world;d.geometry=geometryEnabled_?i.policy:GeometryPolicy{};d.staticCache=cacheEnabled_?&i.cache:nullptr;
    if(i.surfaceProxy&&surfacesEnabled_){
        const auto mn=i.min,mx=i.max;
        if(geometryErrorPixels(r.viewProj(),mn,mx,length(mx-mn),r.sceneWidth(),r.sceneHeight(),r.clipDepthZeroToOne())<=i.proxyPixels){
            ++stats_.surfaceProxiesSubmitted;d.model=i.surfaceProxy;d.staticCache=cacheEnabled_?&i.proxyCache:nullptr;d.geometry.clusters=false;d.geometry.shadowClusters=false;d.geometry.occluder=false;
        }
    }
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    // No duplicate geometry: preview only; always execute the exact legacy
    // draw below. Source proxies and HLOD substitutes are intentionally
    // excluded from this diagnostic path. Source key is borrowed only for
    // the synchronous preview call; no Renderer3D packet stores it.
    if (strataRenderPreviewEnabled_ && strataStagingEnabled_ &&
        strataGpuMetadataEnabled_ && strataStaging_ &&
        strataGpuMetadata_ && d.model == i.model) {
        for (const auto& [key, model] : models_) {
            if (model.get() != d.model) continue;
            const auto* prepared = strataStaging_->find(key);
            if (prepared && strataGpuMetadata_->find(key, *d.model).valid())
                r.previewStrataModel(d, *prepared, *strataGpuMetadata_, key);
            break;
        }
    }
#endif
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if (strataPortableRenderingEnabled_ && d.model == i.model &&
        !strataPortableSources_.empty()) {
        const auto staged=strataPortableSources_.find(d.model);
        if (staged != strataPortableSources_.end() && staged->second &&
            r.sceneWidth() > 0 && r.sceneHeight() > 0) {
            std::vector<strata::PortablePassView> views;
            views.push_back({r.viewProj(), r.sceneWidth(), r.sceneHeight(),
                             r.clipDepthZeroToOne(), errorPixels_});
            if (r.shadowQuality() != ShadowQuality::Off &&
                r.shadowMapSize() > 0)
                views.push_back({r.lightViewProj(), r.shadowMapSize(),
                                 r.shadowMapSize(), r.clipDepthZeroToOne(),
                                 errorPixels_});
            // A planar reflection camera is chosen after drawModel(), by
            // renderReflection(). To guarantee no missing reflected detail,
            // use the finest resident frontier whenever water reflections
            // might consume this ModelDraw in the current frame.
            const bool forceFinest = r.lights.reflectionsWanted();
            auto& slot=strataPortableDrawModels_[&i];
            if (!slot) slot=std::make_unique<Model>();
            std::string error;
            if (staged->second->prepareModel(i.world, views, forceFinest,
                                              *slot, &error)) {
                d.model=slot.get();
                d.flags |= kDrawStatic; // eligible rigid Strata, Vulkan MDI may batch it
                d.staticCache=nullptr;
                d.persistentId=0; // no stale original static-render packets
                // The actual draw is STILL authored only by Renderer3D.
                // Do not draw the original source after a successful switch.
                ++stats_.strataPortableDraws;
                stats_.strataPortableClusters+=
                    slot->meshes.empty() ? 0u :
                    uint64_t(std::accumulate(
                        slot->meshes.begin(),slot->meshes.end(),size_t(0),
                        [](size_t n,const MeshDef& mesh) {
                            return n+mesh.primitives.size();
                        }));
            }else{
                ++stats_.strataPortableFallbacks;
                // Failure is atomic: output was preserved, and source model
                // always remains the only drawn object in this epoch.
            }
        }
    }
#endif
    r.drawModel(d);++stats_.submitted;
}

}
