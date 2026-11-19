// aiverse_modelview: look at models the way the game renders them (materials, shadows, skinning,
// clips, variants, team colours, quality tiers). For artists checking an export and for CI
// screenshots of the model pipeline.
//
//   aiverse_modelview a.glb b.gltf@Walk c.obj [--clip Walk] [--time 0.4] [--variant winter] [--baked]
//                 [--team 0.9,0.2,0.2] [--quality low|medium|high] [--height 1.5] [--native] [--distance 6]
//                 [--yaw 20] [--pitch 30] [--spacing 2.2]
//                 [--lod-report] [--no-lod] [--lod-fade 0.15] [--shadows off|low|medium|high]
//   file@Clip plays that clip on that instance. Keys: Space next clip, Q quality, V variant,
//   P pause, T team colour; drag to orbit, wheel to zoom. AIVERSE_SCREENSHOT works as in the client.
//
//   aiverse_modelview --game templates/showcase [--types footman,catapult] [--start 4] [--fixed-dt 0.016]
//   gallery: every type of the game through the game's own view (view blocks, overrides,
//   procedural animation), cycling idle -> move -> attack -> death -> respawn.
#include "../../engine/platform/Platform.h"
#include "../../engine/render/Animation.h"
#include "../../engine/render/ImGuiLayer.h"
#include "../../engine/render/ModelMerge.h"
#include "../../engine/render/Renderer3D.h"
#include "../../engine/render/ModelAssetProvider.h"
#include "../../engine/render/FailedModelUpload.h"
#include "../../engine/view/View.h"
#include "../../engine/view/ImportedAttachmentAuthoring.h"
#include "imgui.h"
#include "Gallery.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <set>
#include <stdexcept>

using namespace aiverse;
using namespace aiverse::render;

namespace {

using Imported = ::aiverse::view::ImportedAttachmentAuthoring;
struct AttachmentSpec {
    std::string modelSource;
    std::string socketName;
    bool enabled=true;
};
struct Instance {
    Model* model = nullptr;
    std::string path;
    Animator anim;
    Pose pose;
    std::vector<mat4> world;
    int clip = -1, variant = -1;
    mat4 fix;
    ::aiverse::view::AppearanceAssetRegistry::Snapshot publishedAppearance;
    Imported::Compiled compiledAttachments;
    bool attachmentsReady=false;
    std::string lastAttachmentError;
};

// All parent Animator objects and their borrowed Model* stay with the
// provider that created them until a GPU completion proof permits retirement.
struct RetiredNativeScene {
    std::unique_ptr<ModelAssetProvider> provider;
    std::unique_ptr<::aiverse::view::AppearanceAssetRegistry> appearances;
    std::vector<Instance> instances;
};

struct Viewer {
    std::vector<std::pair<std::string, std::string>> files;   // path, clip
    std::string clipAll, variantName;
    bool lodReport = false, noLod = false;
    float lodFadeWidth=.15f;
    ShadowQuality shadowQuality=ShadowQuality::High;
    bool native = false;
    bool merge = true;   // static batching of parts that share a material (--no-merge: as authored)
    bool providerMode=false; // --provider uses the genre-free owner for static raw files
    float seek = -1, height = 0, spacing = 2.2f, distance = 0, yawDeg = 25, pitchDeg = 32;
    float3 team{0.92f * 0.92f, 0.22f * 0.22f, 0.20f * 0.20f};
    MaterialQuality quality = MaterialQuality::High;
    bool qualitySet = false, paused = false, baked = false;
    std::string gameDir;
    std::vector<std::string> types;
    float start = 0, fixedDt = 0;
    std::unique_ptr<modelview::Gallery> gallery;

    gfx::Device* dev = nullptr;
    Renderer3D r3d;
    ImGuiLayer gui;
    RTSCamera cam;
    std::map<std::string, std::unique_ptr<Model>> models;
    std::unique_ptr<ModelAssetProvider> nativeModels=
        std::make_unique<ModelAssetProvider>();
    std::unique_ptr<::aiverse::view::AppearanceAssetRegistry> attachmentsRegistry=
        std::make_unique<::aiverse::view::AppearanceAssetRegistry>();
    std::vector<std::unique_ptr<RetiredNativeScene>> retiredNativeScenes;
    static constexpr size_t kRetiredNativeLimit=8;
    bool reimportRequested=false, watchModelFiles=false;
    double watchElapsed=0.;
    std::map<std::string,std::filesystem::file_time_type> sourceStamps;
    std::string nativeReloadError;
    ::aiverse::View attachmentView; // compose borrowed precomputed poses only
    std::vector<AttachmentSpec> attachmentSpecs;
    std::string attachmentConfigPath;
    char newAttachmentModel[512]{},newAttachmentSocket[256]{};
    bool attachmentPreview=false,attachmentsDirty=false;
    std::map<std::string, std::string> mergeInfo;   // path -> "parts 47 -> 14" or why not merged
    std::vector<Instance> inst;
    std::vector<std::string> log;
    Mesh ground;
    double time = 0;
    int variant = -1, teamIdx = 0;
    bool dragging = false;
    float lastX = 0, lastY = 0;

    static bool readFile(const std::string& p, std::string& out) {
        std::ifstream f(p, std::ios::binary);
        if (f) { std::stringstream ss; ss << f.rdbuf(); out = ss.str(); return true; }
        return platform::readContentFile(p, out);
    }

    void init(gfx::Device& d) {
        dev = &d;
        loadAttachmentConfig();
        // This attachment preview uses ONE real ModelAssetProvider for both
        // source rig and imported children. The original comparison/gallery
        // paths remain unchanged unless the user opts into attachments.
        if(!attachmentSpecs.empty() || !attachmentConfigPath.empty()) {
            if(!gameDir.empty())
                throw std::invalid_argument(
                    "attachment preview takes model files, not --game gallery");
            attachmentPreview=true;
            providerMode=true;
            merge=false;
            baked=false;
        }
        r3d.init(d);
        r3d.setProfiling(lodReport);
        r3d.setModelLodEnabled(!noLod);
        r3d.setShadowQuality(shadowQuality);
        r3d.setModelLodFadeWidth(lodFadeWidth);
        r3d.setMaterialQuality(qualitySet ? quality : Renderer3D::defaultMaterialQualityFor(d, platform::isTouchDevice()));
        gui.init(d, "engine/fonts/ui.ttf", 15.0f);
        MeshBuilder gb;
        gb.color(rgba(120, 132, 110)).box({0, -0.05f, 0}, {60, 0.1f, 60});
        ground = gb.upload(d);
        time = start;
        if (!gameDir.empty()) {
            gallery = std::make_unique<modelview::Gallery>();
            std::string err;
            if (!gallery->init(d, gameDir, types, err)) { log.push_back(err); gallery.reset(); }
            else for (auto& w : gallery->warnings()) log.push_back(w);
            if (gallery) {
                cam.target = gallery->center();
                cam.distance = distance > 0 ? distance : gallery->extent() * 0.75f;
                cam.yaw = yawDeg * 3.14159265f / 180.0f;
                cam.pitch = pitchDeg * 3.14159265f / 180.0f;
                cam.minDistance = 1;
                for (auto& l : log) std::printf("%s\n", l.c_str());
                return;
            }
        }
        // The standalone native provider deliberately keeps source geometry
        // untouched. Existing merged/animated comparisons retain their
        // historic loader and all of its authoring/animation semantics.
        if(providerMode)nativeModels->setReader(readFile);
        float x = -spacing * float(files.size() - 1) * 0.5f, maxH = 1;
        for (auto& [path, clip] : files) {
            Model* resolved=nullptr;
            if(providerMode && !merge && !baked &&
               ((clip.empty() && clipAll.empty()) || attachmentPreview)) {
                resolved=nativeModels->load(d,path);
                if(resolved) {
                    for(auto& mt:resolved->materials) {
                        std::string n=mt.name;
                        for(auto& ch:n)
                            ch=char(std::tolower(static_cast<unsigned char>(ch)));
                        if(n.find("team")!=std::string::npos &&
                           mt.team==TeamMode::None)mt.team=TeamMode::Tint;
                    }
                    log.push_back(path+": generic native source provider");
                }
            }
            auto it = models.find(path);
            if(!resolved && it == models.end()) {
                // Create map storage before any GPU upload; a later
                // allocation failure must never destroy GPU-live Models.
                it = models.try_emplace(path).first;
                std::unique_ptr<Model> m;
                bool uploadStarted=false;
                try {
                    std::vector<std::string> warn;
                    m = loadModel(path, readFile, &warn);
                    if(!m)throw std::runtime_error("model import returned no data");
                    for (auto& w : warn) log.push_back(w);
                    if (merge) {
                        const auto mr = mergeStaticPrimitives(*m);
                        mergeInfo[path] = mr.merged ? "parts " + std::to_string(mr.primitivesBefore) + " -> " + std::to_string(mr.primitivesAfter) +
                                                          (mr.shadowBatched ? ", shadow 1 draw for " + std::to_string(mr.shadowBatched) : "")
                                                    : "parts not merged: " + mr.reason;
                        log.push_back(path + ": " + mergeInfo[path]);
                    }
                    uploadStarted=true;
                    if(!uploadModel(d, *m))
                        throw std::runtime_error("GPU Model upload returned false");
                // as the game view does by default: materials named "*team*" take the team colour
                for (auto& mt : m->materials) {
                    std::string n = mt.name;
                    for (auto& ch : n) ch = char(std::tolower(static_cast<unsigned char>(ch)));
                    if (n.find("team") != std::string::npos && mt.team == TeamMode::None) mt.team = TeamMode::Tint;
                }
                    char buf[256];
                    std::snprintf(buf, sizeof buf, "%s: %zu nodes, %zu meshes, %zu materials, %zu textures, %zu skins, %zu clips",
                                  path.c_str(), m->nodes.size(), m->meshes.size(), m->materials.size(), m->images.size(), m->skins.size(), m->clips.size());
                    log.push_back(buf);
                    for (auto& c : m->clips) log.push_back("  clip '" + c.name + "' " + std::to_string(c.duration) + " s");
                    for (auto& im : m->images) if (im.gpuCompressed) log.push_back("  texture '" + im.name + "' stays GPU-compressed");
                    if (m->baked.tex.valid() || bakeAnimations(d, *m)) log.push_back("  clips baked: " + std::to_string(m->baked.rows) + " rows at " + std::to_string(int(m->baked.fps)) + " fps");
                    for (auto& v : m->variants) log.push_back("  variant '" + v + "'");
                } catch (const std::exception& e) {
                    if(uploadStarted) {
                        const auto status=abortFailedModelUpload(d,m);
                        if(status==FailedModelUploadDisposition::OrphanedWithoutGpuProof)
                            log.push_back(path+": GPU completion unavailable; partial Model quarantined");
                    } else m.reset();
                    log.push_back(path+": "+e.what());
                }
                it->second = std::move(m);
            }
            Instance in;
            in.model = resolved ? resolved : it->second.get();
            in.path = path;
            if (in.model) {
                Model& m = *in.model;
                float3 mn = m.boundsMin, mx = m.boundsMax;
                float s = height > 0 && mx.y > mn.y ? height / (mx.y - mn.y) : 1.0f;
                // formats with their own units (voxels, Warcraft, Generals, Collada in cm) would put the
                // camera inside the model: frame anything far from game size at 2 units (--native keeps it)
                const float big = std::max({mx.x - mn.x, mx.y - mn.y, mx.z - mn.z});
                if (height <= 0 && !native && big > 1e-6f && (big > 6.0f || big < 0.2f)) {
                    s = 2.0f / big;
                    log.push_back(path + ": shown at " + std::to_string(s) + "x (native size " + std::to_string(big) + " units; --native to keep it)");
                }
                in.fix = mat4::translate({x, 0, 0}) * mat4::scale({s, s, s}) * mat4::translate({-(mn.x + mx.x) * 0.5f, -mn.y, -(mn.z + mx.z) * 0.5f});
                maxH = std::max(maxH, (mx.y - mn.y) * s);
                in.anim.setModel(&m);
                std::string want = clip.empty() ? clipAll : clip;
                in.clip = want.empty() ? (m.clips.empty() ? -1 : 0) : m.findClip(want);
                if (!want.empty() && in.clip < 0) log.push_back(path + ": no clip '" + want + "'");
                play(in);
                if (!variantName.empty()) in.variant = m.findVariant(variantName);
            }
            inst.push_back(std::move(in));
            x += spacing;
        }
        if(attachmentPreview)rebuildAttachments();
        for (auto& l : log) std::printf("%s\n", l.c_str());
        cam.target = {0, maxH * 0.45f, 0};
        cam.distance = distance > 0 ? distance : std::max(4.0f, spacing * float(files.size()) * 1.1f + maxH);
        cam.yaw = yawDeg * 3.14159265f / 180.0f;
        cam.pitch = pitchDeg * 3.14159265f / 180.0f;
        cam.minDistance = 1;
    }

    // ModelView is an actual file-backed authoring host. It never stores
    // permanent read leases in its compiled bundles: only the provider owns
    // Models and the draw-scoped Prepared object holds temporary leases.
    void loadAttachmentConfig() {
        if(attachmentConfigPath.empty())return;
        std::string contents;
        if(!readFile(attachmentConfigPath,contents))return; // new file
        try {
            const auto authored=Json::parse(contents);
            if(!authored["attachments"].isArray())
                throw std::invalid_argument("attachment config requires an attachments array");
            std::vector<AttachmentSpec> fromFile;
            for(const auto& entry:authored["attachments"].items()) {
                const auto child=entry.getStr("model");
                const auto socket=entry.getStr("socket");
                if(child.empty() || socket.empty())
                    throw std::invalid_argument("attachment needs model and named socket");
                fromFile.push_back({child,socket,entry.getBool("enabled",true)});
            }
            fromFile.insert(fromFile.end(),attachmentSpecs.begin(),attachmentSpecs.end());
            attachmentSpecs=std::move(fromFile);
        }catch(const std::exception& e) {
            log.push_back(std::string("attachment config rejected: ")+e.what());
        }
    }

    void saveAttachmentConfig() {
        if(attachmentConfigPath.empty())
            attachmentConfigPath=files.empty()?"attachments.json":
                files.front().first+".attachments.json";
        auto root=Json::object();
        auto items=Json::array();
        for(const auto& entry:attachmentSpecs) {
            auto item=Json::object();
            item.set("model",Json::string(entry.modelSource));
            item.set("socket",Json::string(entry.socketName));
            item.set("enabled",Json::fromBool(entry.enabled));
            items.push(item);
        }
        root.set("attachments",items);
        std::ofstream file(attachmentConfigPath,std::ios::binary|std::ios::trunc);
        if(!file || !(file<<root.dump(2)))
            log.push_back("failed to save attachment config: "+attachmentConfigPath);
        else log.push_back("saved attachment config: "+attachmentConfigPath);
    }

    // Compile an entire attachment configuration in any provider generation.
    // Both normal layout edits and on-disk Model hot-reimport call this same
    // genre-neutral API. No live instance is mutated before all entries pass.
    bool buildAttachmentLayout(
            ModelAssetProvider& provider,
            ::aiverse::view::AppearanceAssetRegistry& registry,
            std::vector<Instance>& actors,std::string& error) {
        std::vector<Imported::Compiled> staged;
        std::vector<::aiverse::view::AppearanceAssetRegistry::Snapshot> parents;
        staged.reserve(actors.size());
        parents.reserve(actors.size());
        std::map<std::string,::aiverse::view::AppearanceAssetRegistry::Snapshot> children;
        try {
            // Import every real child Model before taking any source read
            // lease: the cache cannot evict/replace under active readers.
            for(const auto& entry:attachmentSpecs) {
                if(!entry.enabled)continue;
                if(entry.modelSource.empty() || entry.socketName.empty())
                    throw std::invalid_argument(
                        "attachment needs a Model file and named socket");
                if(children.count(entry.modelSource))continue;
                ::aiverse::view::AppearanceAssetRegistry::Snapshot published;
                ::aiverse::view::AppearanceAssetRegistry::Request request;
                if(!Imported::importChild(
                        provider,*dev,registry,
                        "modelview/child/"+entry.modelSource,
                        entry.modelSource,std::move(request),published,{},&error))
                    throw std::runtime_error(error);
                children.emplace(entry.modelSource,std::move(published));
            }
            for(size_t i=0;i<actors.size();++i) {
                auto& actor=actors[i];
                if(!actor.model)
                    throw std::invalid_argument("parent Model is unavailable");
                auto lease=provider.resources().acquireModelRead(actor.model);
                if(!lease)
                    throw std::invalid_argument(
                        "parent Model must belong to the same provider");
                ::aiverse::view::AppearanceAssetRegistry::Request req;
                req.model=actor.model;
                req.sources.push_back(actor.path);
                const auto key="modelview/source/"+std::to_string(i);
                if(!registry.publish(key,req,{}, {},&error))
                    throw std::runtime_error(error);
                auto parent=registry.find(key);
                std::vector<Imported::ChildRequest> requested;
                requested.reserve(attachmentSpecs.size());
                for(const auto& entry:attachmentSpecs) {
                    if(!entry.enabled)continue;
                    Imported::ChildRequest child;
                    child.appearance=children.at(entry.modelSource);
                    child.socketName=entry.socketName;
                    requested.push_back(std::move(child));
                }
                Imported::Compiled compiled;
                if(!Imported::compile(parent,provider.resources(),
                                      requested,compiled,&error))
                    throw std::runtime_error(error);
                staged.push_back(std::move(compiled));
                parents.push_back(std::move(parent));
            }
            for(size_t i=0;i<actors.size();++i) {
                actors[i].compiledAttachments=std::move(staged[i]);
                actors[i].publishedAppearance=std::move(parents[i]);
                actors[i].attachmentsReady=true;
                actors[i].lastAttachmentError.clear();
            }
            return true;
        }catch(const std::exception& e) {
            error=e.what();
            return false;
        }
    }

    bool rebuildAttachments() {
        if(!dev || !attachmentPreview || gallery)return false;
        std::string error;
        if(buildAttachmentLayout(*nativeModels,*attachmentsRegistry,inst,error)) {
            log.push_back("attachment layout compiled for "+
                          std::to_string(inst.size())+" source Models");
            return true;
        }
        log.push_back("attachment update rejected; last good retained: "+error);
        return false;
    }

    // A provider generation cannot be freed until its LAST queued draw
    // is GPU-complete. Both old Animators and every published appearance
    // snapshot stay with the old provider while completion is uncertain.
    void deferNativeScene(std::unique_ptr<RetiredNativeScene> scene) noexcept {
        if(!scene)return;
        try {retiredNativeScenes.push_back(std::move(scene));}
        catch(...) {
            // No proof of completed GPU work: leaking this provider is safer
            // than its destructor releasing potentially GPU-live buffers.
            (void)scene.release();
        }
    }

    // The owner thread calls this only at a frame boundary, never while a
    // Renderer3D draw uses Prepared ModelReadLease or an Animator Model*.
    // The cap is advisory; a failed GPU idle proof keeps old resources.
    bool collectRetiredNativeScenes(bool force=false) noexcept {
        if(!dev || retiredNativeScenes.empty())return true;
        if(!force && retiredNativeScenes.size()<kRetiredNativeLimit)
            return false;
        try {
            if(!dev->waitForResourceIdle()) {
                nativeReloadError="old Model providers retained: GPU idle unavailable";
                return false;
            }
            for(auto& retired:retiredNativeScenes) {
                if(!retired)continue;
                try {
                    retired->instances.clear();
                    retired->appearances.reset();
                    if(retired->provider)
                        retired->provider->shutdown(*dev);
                    retired.reset();
                }catch(...) {
                    // A provider with failed shutdown still owns GPU models:
                    // never permit its destructor to destroy those objects.
                    (void)retired.release();
                }
            }
            retiredNativeScenes.clear();
            return true;
        }catch(...) {
            nativeReloadError="GPU retirement failed; old providers kept";
            return false;
        }
    }

    // Replace the ENTIRE ModelView source generation, never a Model* alone:
    // the parent Animator, its named clip phase, the child source snapshots
    // and compiled sockets are all rebound against new CPU Model objects.
    bool reimportModelFiles() {
        if(!dev || !attachmentPreview || gallery || !nativeModels)
            return false;
        auto candidate=std::make_unique<RetiredNativeScene>();
        candidate->provider=std::make_unique<ModelAssetProvider>(readFile);
        candidate->appearances=
            std::make_unique<::aiverse::view::AppearanceAssetRegistry>();
        std::string error;
        try {
            if(nativeModels->resources().activeModelReaders()!=0)
                throw std::logic_error(
                    "source reload requested while old Model readers are active");
            candidate->instances.reserve(inst.size());
            for(const auto& old:inst) {
                if(!old.model)
                    throw std::runtime_error(
                        "cannot reload an instance without a valid old Model");
                Model* replacement=candidate->provider->load(*dev,old.path);
                if(!replacement)
                    throw std::runtime_error(
                        "on-disk Model reload failed: "+old.path);
                Instance next;
                next.model=replacement;
                next.path=old.path;
                next.fix=old.fix; // keep user-authored world placement
                next.variant=variantName.empty()
                    ?old.variant:replacement->findVariant(variantName);
                const int oldClip=old.clip;
                if(oldClip>=0 && size_t(oldClip)<old.model->clips.size()) {
                    const auto& previous=old.model->clips[size_t(oldClip)];
                    next.clip=replacement->findClip(previous.name);
                } else next.clip=-1;
                next.anim.setModel(replacement);
                if(next.clip>=0) {
                    Animator::Play playing;
                    playing.fade=0.f;
                    playing.restart=true;
                    next.anim.play(0,next.clip,playing);
                    const auto oldClipIndex=old.clip;
                    if(oldClipIndex>=0 &&
                       size_t(oldClipIndex)<old.model->clips.size()) {
                        const float oldDuration=
                            old.model->clips[size_t(oldClipIndex)].duration;
                        const float newDuration=
                            replacement->clips[size_t(next.clip)].duration;
                        if(std::isfinite(oldDuration) && oldDuration>0.f &&
                           std::isfinite(newDuration) && newDuration>0.f) {
                            const float phase=std::clamp(
                                old.anim.time(0)/oldDuration,0.f,0.999999f);
                            (void)next.anim.seek(0,phase*newDuration);
                        }
                    }
                }
                next.anim.evaluate(next.pose);
                worldMatrices(*replacement,next.pose,next.world);
                candidate->instances.push_back(std::move(next));
            }
            if(!buildAttachmentLayout(*candidate->provider,
                                      *candidate->appearances,
                                      candidate->instances,error))
                throw std::runtime_error("compiled attachment reload: "+error);
            // Allocate the old-session owner BEFORE the atomic move/commit.
            auto retired=std::make_unique<RetiredNativeScene>();
            retired->provider=std::move(nativeModels);
            retired->appearances=std::move(attachmentsRegistry);
            retired->instances=std::move(inst);
            nativeModels=std::move(candidate->provider);
            attachmentsRegistry=std::move(candidate->appearances);
            inst=std::move(candidate->instances);
            deferNativeScene(std::move(retired));
            nativeReloadError.clear();
            log.push_back("reimported "+
                          std::to_string(inst.size())+
                          " ModelView sources and socket attachments from disk");
            return true;
        }catch(const std::exception& e) {
            nativeReloadError=e.what();
            log.push_back("on-disk reimport rejected; last-good retained: "+
                          nativeReloadError);
            // Partially GPU-uploaded scratch Models remain owned until the
            // same fenced retirement boundary as any old visible source.
            deferNativeScene(std::move(candidate));
            return false;
        }
    }

    // Poll on-disk file revisions only when explicitly opted in. An edit
    // schedules ONE complete staged import at the next owner frame boundary,
    // never from a file-watcher worker or Renderer3D submission callback.
    void pollModelSourceChanges(double seconds) {
        if(!attachmentPreview || !watchModelFiles || gallery)return;
        watchElapsed+=std::max(0.,seconds);
        if(watchElapsed<1.)return;
        watchElapsed=0.;
        std::set<std::string> observed;
        for(const auto& actor:inst)observed.insert(actor.path);
        for(const auto& child:attachmentSpecs)
            if(child.enabled)observed.insert(child.modelSource);
        bool changed=false;
        for(const auto& path:observed) {
            std::error_code ec;
            const auto stamp=std::filesystem::last_write_time(path,ec);
            if(ec)continue; // assets from non-filesystem providers are not watched
            const auto [it,inserted]=sourceStamps.try_emplace(path,stamp);
            if(!inserted && it->second!=stamp) {
                it->second=stamp;
                changed=true;
            }
        }
        for(auto it=sourceStamps.begin();it!=sourceStamps.end();)
            if(!observed.count(it->first))it=sourceStamps.erase(it);
            else ++it;
        if(changed)reimportRequested=true;
    }

    void play(Instance& in) {
        if (!in.model || in.clip < 0) return;
        Animator::Play p;
        p.fade = 0;
        p.restart = true;
        in.anim.play(0, in.clip, p);
        if (seek >= 0) in.anim.update(seek);
    }

    void frame(gfx::Device& d, double dt) {
        if (fixedDt > 0) dt = fixedDt;
        if (!paused && seek < 0) time += dt;
        // Explicit Device-owner frame boundary. Gallery may draw a portrait
        // and scene within ONE RHI frame; neither draw is a safe retirement
        // hook. Poll model and paint token queues before any GPU pass.
        if(gallery)gallery->collectRetiredGpuResources();
        if(nativeModels->resources().pendingRetirements()!=0 ||
           nativeModels->pendingRolledBackImages()!=0)
            nativeModels->collectRetired(d);
        pollModelSourceChanges(dt);
        // All staged on-disk imports and old-provider cleanup happen on
        // this owner boundary, before d.beginFrame or any model draw.
        if(reimportRequested) {
            reimportRequested=false;
            (void)reimportModelFiles();
        }
        (void)collectRetiredNativeScenes();
        // All UI attachment edits are applied at the next OWNER frame
        // boundary, before any Renderer3D GPU submission and after prior
        // draw-scoped ModelReadLeases have been released.
        if(attachmentsDirty) {
            (void)rebuildAttachments();
            attachmentsDirty=false;
        }
        d.beginFrame(platform::framebufferWidth(), platform::framebufferHeight());
        gui.newFrame(dt);
        gfx::TextureHandle portrait = gallery ? gallery->portrait(time) : gfx::TextureHandle{}; // before the scene pass
        r3d.beginScene(cam, d.framebufferWidth(), d.framebufferHeight());
        if (gallery) { gallery->update(time); gallery->draw(r3d, time); }
        else r3d.drawMesh(ground, mat4{}, {1, 1, 1, 1}, kDrawNoCastShadow, 0.05f);
        for (auto& in : inst) {
            if (!in.model) continue;
            if (!paused && seek < 0) in.anim.update(float(dt));
            in.anim.evaluate(in.pose);
            worldMatrices(*in.model, in.pose, in.world);
            ModelDraw md;
            md.model = in.model;
            md.world = in.fix;
            md.nodeWorld = &in.world;
            md.pose = &in.pose;
            md.team = team;
            md.variant = variant >= 0 ? variant : in.variant;
            BakedSample bs;
            if (baked && in.model->baked.tex.valid()) { // skin from the baked clip texture, as crowds do
                const auto b = in.anim.baseSample();
                bs = {b.clipA, b.clipB, b.timeA, b.timeB, b.w, b.loopA, b.loopB};
                md.baked = &bs;
            }
            // Existing host Animator is the only writer: generic View
            // consumes its precomputed pose and composes imported children.
            // Keep owner-thread ModelReadLeases until submitOne returns.
            bool presented=false;
            if(attachmentPreview && in.attachmentsReady) {
                Imported::Prepared prepared;
                std::string error;
                if(Imported::prepare(
                        in.compiledAttachments,nativeModels->resources(),
                        in.fix,nullptr,prepared,
                        Imported::AnimationOwner::HostPrecomputed,&error)) {
                    auto& draw=prepared.object.draw;
                    draw.pose=md.pose;
                    draw.nodeWorld=md.nodeWorld;
                    draw.baked=md.baked;
                    draw.variant=md.variant;
                    draw.team=md.team;
                    // Stable per-viewer-instance visual draw identity; the
                    // parent asset's registry key identifies a RESOURCE,
                    // not every visible instance using that resource.
                    prepared.object.id=uint64_t(&in-inst.data())+1;
                    attachmentView.submitOne(r3d,prepared.object);
                    presented=true;
                    in.lastAttachmentError.clear();
                } else if(error!=in.lastAttachmentError) {
                    in.lastAttachmentError=error;
                    log.push_back(in.path+": attachment packet: "+error);
                }
            }
            if(!presented)
                ::aiverse::View::submitModel(r3d,md);
        }
        gfx::PassDesc pass;
        pass.clearColor[0] = 0.55f; pass.clearColor[1] = 0.66f; pass.clearColor[2] = 0.78f;
        r3d.render(pass);
        ImGui::SetNextWindowPos(ImVec2(8, 8));
        ImGui::SetNextWindowBgAlpha(0.55f);
        ImGui::Begin("info", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
        static const char* q[] = {"low", "medium", "high"};
        ImGui::Text("materials: %s   draw calls: %u", q[int(r3d.materialQuality())], r3d.drawCalls());
        for (auto& in : inst)
            if (in.model) {
                ImGui::Text("%s  %s", in.path.c_str(), in.clip >= 0 ? in.model->clips[size_t(in.clip)].name.c_str() : "(bind pose)");
                auto mi = mergeInfo.find(in.path);
                if (mi != mergeInfo.end()) ImGui::Text("  %s", mi->second.c_str());
            }
        if (portrait.valid()) {
            const bool flip = d.info().renderTargetFlipY;
            ImGui::Image(ImTextureRef(ImTextureID(portrait.id)), ImVec2(128, 128), ImVec2(0, flip ? 0.f : 1.f), ImVec2(1, flip ? 1.f : 0.f));
        }
        if (gallery) for (auto& ev : gallery->recentEvents()) ImGui::TextColored(ImVec4(1, 0.5f, 0.85f, 1), "event: %s", ev.c_str());
        if (!r3d.modelError().empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "%s", r3d.modelError().c_str());
        ImGui::End();
        if(attachmentPreview) {
            ImGui::Begin("Attachments");
            ImGui::TextUnformatted("Real imported child Models on named sockets");
            ImGui::TextUnformatted("Changes apply next frame; models remain provider-owned");
            for(size_t i=0;i<attachmentSpecs.size();++i) {
                ImGui::PushID(int(i));
                auto& spec=attachmentSpecs[i];
                if(ImGui::Checkbox("##enabled",&spec.enabled))
                    attachmentsDirty=true;
                ImGui::SameLine();
                ImGui::Text("%s @ %s",spec.modelSource.c_str(),spec.socketName.c_str());
                ImGui::SameLine();
                if(ImGui::SmallButton("Detach")) {
                    attachmentSpecs.erase(attachmentSpecs.begin()+i);
                    attachmentsDirty=true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            ImGui::InputText("Child model file",newAttachmentModel,
                             sizeof(newAttachmentModel));
            ImGui::InputText("Socket name",newAttachmentSocket,
                             sizeof(newAttachmentSocket));
            if(ImGui::Button("Attach") && newAttachmentModel[0] &&
               newAttachmentSocket[0]) {
                attachmentSpecs.push_back(
                    {newAttachmentModel,newAttachmentSocket,true});
                newAttachmentModel[0]=newAttachmentSocket[0]='\0';
                attachmentsDirty=true;
            }
            ImGui::SameLine();
            if(ImGui::Button("Rebuild"))attachmentsDirty=true;
            ImGui::SameLine();
            if(ImGui::Button("Reload model files"))reimportRequested=true;
            ImGui::Checkbox("Watch source files",&watchModelFiles);
            if(ImGui::Button("Save"))saveAttachmentConfig();
            ImGui::Text("Retired provider generations: %zu",
                        retiredNativeScenes.size());
            if(!attachmentConfigPath.empty())
                ImGui::TextWrapped("Config: %s",attachmentConfigPath.c_str());
            if(!nativeReloadError.empty())
                ImGui::TextWrapped("Reload: %s",nativeReloadError.c_str());
            if(!log.empty())
                ImGui::TextWrapped("%s",log.back().c_str());
            ImGui::End();
        }
        gui.render();
        d.endPass();
        d.endFrame();
    }

    void event(const platform::Event& e) {
        if (gui.handleEvent(e)) return;
        using platform::EventType;
        if (e.type == EventType::MouseDown) { dragging = true; lastX = e.x; lastY = e.y; }
        else if (e.type == EventType::MouseUp) dragging = false;
        else if (e.type == EventType::MouseMove && dragging) {
            cam.yaw -= (e.x - lastX) * 0.01f;
            cam.pitch = std::clamp(cam.pitch + (e.y - lastY) * 0.01f, 0.05f, 1.5f);
            lastX = e.x; lastY = e.y;
        } else if (e.type == EventType::MouseWheel) cam.distance = std::clamp(cam.distance * (e.wheelY > 0 ? 0.9f : 1.1f), 1.0f, 200.0f);
        else if (e.type == EventType::KeyDown) {
            using platform::Key;
            if (e.key == Key::Space) for (auto& in : inst) if (in.model && !in.model->clips.empty()) { in.clip = (in.clip + 1) % int(in.model->clips.size()); play(in); }
            if (e.key == Key::Q) r3d.setMaterialQuality(MaterialQuality((int(r3d.materialQuality()) + 1) % 3));
            if (e.key == Key::V) variant = variant + 1 >= 4 ? -1 : variant + 1;
            if (e.key == Key::P) paused = !paused;
            if (e.key == Key::T) {
                static const float3 teams[] = {{0.85f, 0.05f, 0.04f}, {0.04f, 0.2f, 0.9f}, {0.06f, 0.64f, 0.09f}, {0.94f, 0.64f, 0.02f}};
                teamIdx = (teamIdx + 1) % 4; team = teams[teamIdx];
            }
        }
    }

    void shutdown() {
        if(!dev)return;
        // ModelView draws raw Models, a Gallery scene and an offscreen
        // portrait through the SAME Device. They can all be GPU-visible
        // after the final frame. One explicit idle proof must precede
        // destruction of any native Model, texture or renderer resources.
        if(!dev->waitForResourceIdle()) {
            std::fprintf(stderr,
                "aiverse_modelview: GPU shutdown barrier failed; resources left alive\n");
            return;
        }
        for (auto& [p, m] : models) if (m) releaseModel(*dev, *m);
        // Old Animators and published snapshots must be destroyed BEFORE
        // their owning ModelProvider releases Model CPU/GPU storage.
        inst.clear();
        attachmentsRegistry.reset();
        if(nativeModels) {
            nativeModels->shutdown(*dev);
            nativeModels.reset();
        }
        (void)collectRetiredNativeScenes(true);
        // If even full-idle shutdown could not complete, retain old GPU
        // owners rather than letting unique_ptr destruct GPU-visible Models.
        for(auto& retired:retiredNativeScenes)(void)retired.release();
        retiredNativeScenes.clear();
        if (gallery) gallery->shutdown(*dev);
        dev->destroyBuffer(ground.vb); dev->destroyBuffer(ground.ib);
        gui.shutdown();
        if(lodReport){const auto& p=r3d.profile();std::printf("LOD_REPORT nodes=%u transitions=%u mainTriangles=%u shadowTriangles=%u\n",p.lodNodes,p.lodTransitionNodes,p.triangles[1],p.triangles[0]);}
        r3d.shutdown();
    }
};

Viewer* g_viewer = nullptr;

float3 parse3(const std::string& s) {
    float3 v;
    std::sscanf(s.c_str(), "%f,%f,%f", &v.x, &v.y, &v.z);
    return {v.x * v.x, v.y * v.y, v.z * v.z}; // given like player colours (~sRGB)
}

} // namespace

int aiverseMain(int argc, char** argv) {
    static Viewer v;
    g_viewer = &v;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--clip") v.clipAll = next();
        else if (a == "--game") v.gameDir = next();
        else if (a == "--types") {
            std::stringstream ss(next());
            for (std::string t; std::getline(ss, t, ',');) v.types.push_back(t);
        }
        else if (a == "--start") v.start = std::stof(next());
        else if (a == "--lod-report") v.lodReport = true;
        else if (a == "--no-lod") v.noLod = true;
        else if (a == "--lod-fade") v.lodFadeWidth = std::stof(next());
        else if (a == "--shadows") {
            auto q=next();v.shadowQuality=q=="off"?ShadowQuality::Off:q=="low"?ShadowQuality::Low:q=="medium"?ShadowQuality::Medium:ShadowQuality::High;
        }
        else if (a == "--baked") v.baked = true;
        else if (a == "--fixed-dt") v.fixedDt = std::stof(next());
        else if (a == "--time") v.seek = std::stof(next());
        else if (a == "--variant") v.variantName = next();
        else if (a == "--team") v.team = parse3(next());
        else if (a == "--height") v.height = std::stof(next());
        else if (a == "--spacing") v.spacing = std::stof(next());
        else if (a == "--distance") v.distance = std::stof(next());
        else if (a == "--native") v.native = true;
        else if (a == "--no-merge") v.merge = false;
        else if (a == "--provider") v.providerMode = true;
        else if (a == "--watch-model-files")v.watchModelFiles=true;
        else if (a == "--attachments-file") {
            v.attachmentConfigPath=next();
            if(v.attachmentConfigPath.empty()) {
                std::fprintf(stderr,"--attachments-file requires a JSON path\n");
                return 2;
            }
        }
        else if (a == "--attach") {
            auto authored=next();
            const auto at=authored.rfind('@');
            if(at==std::string::npos || at==0 || at+1>=authored.size()) {
                std::fprintf(stderr,
                    "--attach expects imported-child.glb@NamedSocket\n");
                return 2;
            }
            v.attachmentSpecs.push_back({
                authored.substr(0,at),authored.substr(at+1),true});
        }
        else if (a == "--yaw") v.yawDeg = std::stof(next());
        else if (a == "--pitch") v.pitchDeg = std::stof(next());
        else if (a == "--quality") {
            std::string q = next();
            v.quality = q == "low" ? MaterialQuality::Low : q == "medium" ? MaterialQuality::Medium : MaterialQuality::High;
            v.qualitySet = true;
        } else {
            size_t at = a.find('@');
            v.files.push_back({a.substr(0, at), at == std::string::npos ? std::string() : a.substr(at + 1)});
        }
    }
    if (v.files.empty() && v.gameDir.empty()) {
        std::printf("usage: aiverse_modelview model.glb [model2.gltf@Clip ...] [--clip C] [--time T] [--variant V] [--quality low|medium|high] [--no-merge] [--provider]\n"
                    "       aiverse_modelview parent.glb@Walk --attach child.glb@Hand.R [--attachments-file file.json]\n"
                    "       aiverse_modelview --game templates/<id> [--types a,b] [--start T] [--fixed-dt S]\n");
        return 1;
    }
    platform::AppConfig cfg;
    cfg.title = "AIVERSE model viewer";
    cfg.width = 1280; cfg.height = 720;
    platform::AppCallbacks cb;
    cb.init = [](gfx::Device& d) { g_viewer->init(d); };
    cb.frame = [](gfx::Device& d, double dt) { g_viewer->frame(d, dt); };
    cb.event = [](const platform::Event& e) { g_viewer->event(e); };
    cb.shutdown = [] { g_viewer->shutdown(); };
    return platform::run(cfg, cb);
}
