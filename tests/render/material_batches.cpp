// Real-backend A/B: the same model/material copies, camera and shadow map, with batching off/on.
#include "engine/render/Renderer3D.h"
#include "engine/render/ModelMerge.h"
#include "engine/render/GpuMaterialState.h"
#include "engine/platform/Platform.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <algorithm>
#include <chrono>
#include <vector>
using namespace aiverse;
// GPU-free invariants, including shared vertices and material overrides.
static bool unitChecks() {
    using namespace render;
    int checks=0;
    auto check=[&](bool ok){++checks;if(!ok)std::fprintf(stderr,"material palette check %d failed\n",checks);return ok;};
    Model m; m.materials.resize(3); m.vertices.resize(3); m.indices={0,1,2,0,1,2,0,1,2};
    m.materials[0].baseColor={.137f,.271f,.819f,1};
    m.materials[1].metallic=.731f; m.materials[1].roughness=.213f;
    m.materials[2].baseTex=1;
    MeshDef mesh;
    for(int i=0;i<3;++i){Primitive p;p.firstIndex=uint32_t(i*3);p.indexCount=3;p.material=i;mesh.primitives.push_back(p);}
    m.meshes.push_back(mesh);
    const auto indices=m.indices;
    bool ok=check(prepareMaterialBatches(m)==2);
    ok&=check(m.meshes[0].materialBatches.size()==1);
    const auto& batch=m.meshes[0].materialBatches[0];
    ok&=check(batch.primitive.indexCount==6 && batch.parts==std::vector<size_t>{0,1});
    ok&=check(m.vertices.size()==6 && std::equal(indices.begin(),indices.end(),m.indices.begin()));
    for(uint32_t i=0;i<6;++i)ok&=check(m.vertices[m.indices[batch.primitive.firstIndex+i]].weights[0]==i/3);
    ok&=check(prepareMaterialBatches(m)==0);
    auto mats=m.materials;
    mats[0].baseColor.x=.999123f;mats[1].roughness=.456789f;
    ok&=check(materialBatchCompatible(m.meshes[0],batch,mats));
    mats[1].normalTex=0;ok&=check(!materialBatchCompatible(m.meshes[0],batch,mats));mats=m.materials;
    mats[1].alpha=AlphaMode::Blend;ok&=check(!materialBatchCompatible(m.meshes[0],batch,mats));mats=m.materials;
    mats[1].receiveShadow=false;ok&=check(!materialBatchCompatible(m.meshes[0],batch,mats));mats=m.materials;
    mats.resize(kMaxMaterialPalette+1);ok&=check(!materialBatchCompatible(m.meshes[0],batch,mats));
    ok&=check(m.materials[0].baseColor.x==.137f && m.materials[1].roughness==.213f);
    // A frame-scoped renderer material-group cache must not turn pointer
    // equality into an accidental GPU-state equality shortcut. Identical
    // copies across models merge, while changes to uniforms or sampled
    // texture handles do not.
    Model aModel, bModel;
    Material original, copy;
    original.baseColor={.2f,.4f,.8f,1.f};
    original.emissive={.02f,.04f,.08f};
    copy=original;
    ok&=check(equivalentGpuMaterial(aModel,original,bModel,copy) &&
              gpuMaterialStateHash(aModel,original)==gpuMaterialStateHash(bModel,copy));
    copy.emissive.x+=.1f;
    ok&=check(!equivalentGpuMaterial(aModel,original,bModel,copy));
    copy=original;
    aModel.images.resize(1); bModel.images.resize(1);
    aModel.images[0].tex={123}; bModel.images[0].tex={456};
    original.baseTex=copy.baseTex=0;
    ok&=check(!equivalentGpuMaterial(aModel,original,bModel,copy));
    bModel.images[0].tex=aModel.images[0].tex;
    ok&=check(equivalentGpuMaterial(aModel,original,bModel,copy) &&
              gpuMaterialStateHash(aModel,original)==gpuMaterialStateHash(bModel,copy));
    // Indirect material palettes may vary ONLY baseColour/metal/roughness;
    // texture identity, emissive and render state must still match exactly.
    Material variation=copy;
    variation.baseColor={.91f,.10f,.18f,1};
    variation.metallic=.67f; variation.roughness=.23f;
    ok&=check(equivalentGpuPaletteState(aModel,original,bModel,variation) &&
              gpuPaletteStateHash(aModel,original)==gpuPaletteStateHash(bModel,variation) &&
              !equivalentGpuMaterial(aModel,original,bModel,variation));
    variation.emissive.y += .01f;
    ok&=check(!equivalentGpuPaletteState(aModel,original,bModel,variation));
    variation=copy;variation.alpha=AlphaMode::Blend;
    ok&=check(!equivalentGpuPaletteState(aModel,original,bModel,variation));
    variation=copy;
    bModel.images[0].tex={789};
    ok&=check(!equivalentGpuPaletteState(aModel,original,bModel,variation));
    bModel.images[0].tex=aModel.images[0].tex;
    auto invalid=[&](int mode){
        Model x;x.vertices.resize(3);x.indices=indices;x.materials.resize(3);x.meshes.push_back(mesh);
        if(mode==0)x.meshes[0].primitives[0].morphTargets=1;
        if(mode==1)x.skins.resize(1);
        if(mode==2)x.variants.push_back("variant");
        if(mode==3)x.materials.resize(kMaxMaterialPalette+1);
        return prepareMaterialBatches(x)==0;
    };
    for(int i=0;i<4;++i)ok&=check(invalid(i));
    Model lod;lod.vertices.resize(3);lod.indices=indices;lod.materials.resize(3);lod.meshes={mesh,mesh};
    lod.meshes[0].lods.push_back({1,20});
    ok&=check(prepareMaterialBatches(lod)==6 && lod.meshes[0].materialBatches.size()==1 && lod.meshes[1].materialBatches.size()==1);
    std::printf("material palette CPU checks=%d result=%s\n",checks,ok?"passed":"FAILED");
    return ok;
}

// Deterministic 16-primitive fixture with one shader/material and independent
// node transforms. MDI and the fallback must render byte-identical images.
static std::unique_ptr<render::Model> indirectFixture(bool variedMaterials = false) {
    using namespace render;
    auto m = std::make_unique<Model>();
    m->path = "indirect-fixture";
    m->materials.resize(variedMaterials ? 3 : 1);
    m->materials[0].doubleSided = true;
    m->materials[0].baseColor = {.72f, .48f, .19f, 1.f};
    if (variedMaterials) {
        m->materials[1] = m->materials[0];
        m->materials[1].baseColor = {.13f, .74f, .28f, 1.f};
        m->materials[1].metallic = .65f;
        m->materials[1].roughness = .35f;
        m->materials[2] = m->materials[0];
        m->materials[2].baseColor = {.15f, .25f, .85f, 1.f};
        m->materials[2].roughness = .18f;
    }
    for (int i = 0; i < 16; ++i) {
        const uint32_t start = uint32_t(m->vertices.size());
        for (float3 pos : {float3{-.3f, 0, 0}, float3{.3f, 0, 0}, float3{0, .75f, 0}}) {
            ModelVertex v;
            v.pos = pos;
            v.nrm = {0, 0, 1};
            m->vertices.push_back(v);
        }
        m->indices.insert(m->indices.end(), {start, start + 1, start + 2});
        Primitive p;
        p.material = variedMaterials ? i % 3 : 0;
        p.firstIndex = uint32_t(m->indices.size() - 3);
        p.indexCount = 3;
        p.firstVertex = start;
        p.vertexCount = 3;
        MeshDef mesh;
        mesh.name = "primitive_" + std::to_string(i);
        mesh.primitives.push_back(p);
        Node node;
        node.mesh = int(m->meshes.size());
        node.name = mesh.name;
        node.t = {float(i % 4) * 1.5f - 2.25f, float(i / 4) * .9f, 0};
        m->meshes.push_back(std::move(mesh));
        m->nodes.push_back(std::move(node));
    }
    m->finalize();
    return m;
}

int aiverseMain(int argc, char** argv) {
    if (!unitChecks()) return 1;
    if (argc == 2 && std::string(argv[1]) == "--unit") return 0;
    if (argc != 3) { std::fprintf(stderr, "usage: material_batches MODEL OUTPUT_PREFIX\n"); return 2; }
    auto read = [](const std::string& path, std::string& bytes) {
        std::ifstream f(path, std::ios::binary); if (!f) return false;
        bytes.assign(std::istreambuf_iterator<char>(f), {}); return true;
    };
    if (std::string(argv[1]) == "--audit") {
        size_t models=0, before=0, after=0, addedIndices=0, addedVertices=0;
        for (const auto& entry : std::filesystem::directory_iterator(argv[2])) {
            if (entry.path().extension() != ".glb") continue;
            auto m=render::loadModel(entry.path().string(), read);
            const size_t ni=m->indices.size(), nv=m->vertices.size();
            render::prepareMaterialBatches(*m);
            addedIndices+=m->indices.size()-ni; addedVertices+=m->vertices.size()-nv;
            for (const auto& n:m->nodes) if(n.mesh>=0) {
                const auto& mesh=m->meshes[size_t(n.mesh)];
                before+=mesh.primitives.size();after+=mesh.primitives.size();
                for(const auto& b:mesh.materialBatches)after-=b.parts.size()-1;
            }
            ++models;
        }
        std::printf("models=%zu opaque_before=%zu opaque_after=%zu added_indices=%zu added_vertices=%zu\n",models,before,after,addedIndices,addedVertices);
        return models && after<before ? 0 : 1;
    }
    const std::string scenario = std::getenv("AIVERSE_PALETTE_TEST_CASE") ? std::getenv("AIVERSE_PALETTE_TEST_CASE") : "default";
    // Cold-vs-warm packet screenshot parity uses the same self-contained
    // varied-material 16-rigid-part fixture as the indirect palette test.
    auto model = scenario == "indirect" || scenario == "indirect_palette" ||
                 scenario == "static_packets"
        ? indirectFixture(scenario != "indirect") : render::loadModel(argv[1],read);
    const std::string prefix = argv[2];
    const bool reflection=std::getenv("AIVERSE_SHADOW_TEST_REFLECTION")!=nullptr;
    render::Renderer3D renderer; render::Mesh floor; gfx::Device* device = nullptr;
    std::unique_ptr<render::Model> secondModel;
    std::vector<render::Material> materials;
    std::vector<std::vector<render::MaterialBatch>> batches;
    std::vector<uint8_t> before;
    uint32_t shadowBefore=0, opaqueBefore=0; int frame=0; bool failed=false, finished=false;
    bool packetBuilt=false; // the warm hit must reuse a previously built packet
    // Opt-in synchronous CPU measurements; never treat these as GPU timing.
    const bool cpuTiming = std::getenv("AIVERSE_BATCH_CPU_TIMING") &&
        std::string(std::getenv("AIVERSE_BATCH_CPU_TIMING")) == "1";
    std::vector<double> cpuRenderBefore, cpuRenderAfter;
    // Benchmark only: independent runs for CPU MDI and GPU ICB. The latter
    // may legitimately retain CPU MDI for a different render pass.
    const std::string benchmarkMode = std::getenv("AIVERSE_BATCH_BENCH_MODE")
        ? std::getenv("AIVERSE_BATCH_BENCH_MODE") : "legacy";
    if (benchmarkMode != "legacy" && benchmarkMode != "cpu_mdi" &&
        benchmarkMode != "gpu_icb") {
        std::fprintf(stderr, "Invalid AIVERSE_BATCH_BENCH_MODE: %s\n",
                     benchmarkMode.c_str());
        return 2;
    }
    platform::AppConfig cfg; cfg.title="Material palette A/B"; cfg.width=960; cfg.height=640; cfg.highDpi=false;
    platform::AppCallbacks cb;
    cb.init=[&](gfx::Device& d) {
        device=&d; if (!renderer.init(d) || !render::uploadModel(d,*model)) { failed=true; return; }
        if (scenario == "indirect") {
            secondModel = indirectFixture();
            if (!render::uploadModel(d, *secondModel)) { failed=true; return; }
            if (!model->geometryPoolPage || !secondModel->geometryPoolPage ||
                !(model->vb == secondModel->vb)) { failed=true; return; }
        }
        renderer.setProfiling(true); renderer.setShadowQuality(render::ShadowQuality::High);
        renderer.lighting.fogDensity=0;
        if (const char* q=std::getenv("AIVERSE_PALETTE_TEST_QUALITY")) renderer.setMaterialQuality(render::MaterialQuality(std::atoi(q)));
        renderer.lights.setQuality(render::LightQuality::High);
        floor=render::MeshBuilder().box({0,-.1f,0},{60,.2f,60}).upload(d);
        materials=model->materials; // Match RtsView's per-type material-copy path.
        if (scenario=="overrides") for(size_t i=0;i<materials.size();++i) {
            materials[i].baseColor.x*=.731234f; materials[i].metallic=float(i%3)*.27913f;
            materials[i].roughness=.1213f+float(i%4)*.174321f;
        }
        if (scenario=="fallback") for(size_t i=0;i<materials.size();++i) materials[i].uvOffset.x=float(i)*.137f;
        if (scenario=="lod1" || scenario=="lod2") for(auto& n:model->nodes) if(n.mesh>=0) {
            const auto& lods=model->meshes[size_t(n.mesh)].lods;
            if(!lods.empty()) n.mesh=lods[scenario=="lod2"?lods.size()-1:0].mesh;
        }
        for (const auto& mesh:model->meshes) batches.push_back(mesh.materialBatches);
    };
    cb.frame=[&](gfx::Device& d,double) {
        if(failed||finished||frame>40){platform::requestQuit();return;}
        const bool batch=frame>=15;
        // In cpu_mdi mode explicitly exclude GPU-authored draws, regardless
        // of process flags. In gpu_icb mode the original real GPU ICB path is
        // allowed, with separate CPU MDI counters for any remaining passes.
        if (benchmarkMode != "legacy")
            renderer.setGpuAuthoredDrawEnabled(batch && benchmarkMode == "gpu_icb");
        renderer.setModelMultiDrawEnabled(scenario == "static_packets"
            ? false : ((scenario != "indirect" &&
                        scenario != "indirect_palette") || batch));
        // Keep CPU packet primitive source pointers stable for the lifetime of
        // the fixture. Other scenarios deliberately toggle material batches.
        if (scenario != "static_packets")
            for(size_t i=0;i<model->meshes.size();++i)
                model->meshes[i].materialBatches=(batch && scenario != "indirect" &&
                                                   scenario != "indirect_palette")
                    ? batches[i] : std::vector<render::MaterialBatch>{};
        render::RTSCamera cam; cam.target={0,2,0};cam.distance=std::getenv("AIVERSE_SHADOW_TEST_DISTANCE")?float(std::atof(std::getenv("AIVERSE_SHADOW_TEST_DISTANCE"))):14;cam.pitch=.85f;cam.yaw=.45f;
        renderer.beginScene(cam,960,640);
        renderer.drawMesh(floor,{}, {.6f,.6f,.6f,1});
        if(reflection) renderer.drawMesh(floor,render::mat4::translate({0,.2f,0}),{.1f,.2f,.3f,.6f},render::kDrawNoCastShadow,.9f);
        render::ModelDraw draw;draw.model=model.get();draw.materials=&materials;
        // This is also a GPU-authored draw fixture. The real renderer only
        // puts static (or explicitly allowed rigid) instances into the
        // compute-generated ICB path. Without this flag a green test would
        // exercise CPU MDI only, regardless of AIVERSE_GPU_ENCODED_ICB.
        if (scenario == "indirect" || scenario == "static_packets")
            draw.flags |= render::kDrawStatic;
        if (scenario == "static_packets") {
            draw.persistentId = 0x12345;
            // Frames 0..14 are genuinely uncached; the second image is
            // captured after 8 warm cache replays with identical geometry.
            draw.staticPacketRevision = batch ? 1 : 0;
        }
        const auto center=(model->boundsMin+model->boundsMax)*.5f;
        draw.world=render::mat4::scale({.25f,.25f,.25f})*render::mat4::translate({-center.x,-model->boundsMin.y,-center.z});
        if (scenario=="effects") {draw.dissolve=.18f;draw.outline={1,.2f,0,.025f};}
        if (scenario=="transition") for(size_t i=0;i<model->nodes.size();++i) {
            const auto& n=model->nodes[i];if(n.mesh<0)continue;
            auto& mesh=model->meshes[size_t(n.mesh)];if(mesh.lods.empty())continue;
            const auto xf=draw.world*model->bindWorld[i];const float error=render::length(mesh.boundsMax-mesh.boundsMin);
            float pixels=render::geometryErrorPixels(renderer.viewProj()*xf,mesh.boundsMin,mesh.boundsMax,error,960,640,d.info().clipDepthZeroToOne);
            pixels=std::max(pixels,render::geometryErrorPixels(renderer.lightViewProj()*xf,mesh.boundsMin,mesh.boundsMax,error,renderer.shadowMapSize(),renderer.shadowMapSize(),d.info().clipDepthZeroToOne));
            mesh.lods.resize(1);mesh.lods[0].maxPixels=pixels;renderer.setModelLodFadeWidth(.15f);
        }
        renderer.drawModel(draw);
        if (scenario == "indirect") {
            render::ModelDraw other = draw;
            other.model = secondModel.get();
            other.materials = &secondModel->materials; // distinct Material* with identical shader/parameters
            other.world = render::mat4::translate({4.f, 0, 0}) * draw.world;
            renderer.drawModel(other);
        }
        if(scenario=="instanced"){draw.world=render::mat4::translate({3,0,0})*draw.world;renderer.drawModel(draw);}
        gfx::PassDesc pass;
        const auto startCpu = std::chrono::steady_clock::now();
        renderer.render(pass);
        const auto endCpu = std::chrono::steady_clock::now();
        // Discard pipeline warmup and the transition frame; no readback in sample.
        if (cpuTiming && ((frame >= 3 && frame <= 8) ||
                          (frame >= 18 && frame <= 23))) {
            const double ms = std::chrono::duration<double, std::milli>(
                endCpu - startCpu).count();
            (batch ? cpuRenderAfter : cpuRenderBefore).push_back(ms);
        }
        if (scenario == "static_packets" && frame == 15) {
            const auto stats = renderer.staticRenderPacketStats();
            packetBuilt = stats.rebuilds == 1 && stats.hits == 0 &&
                          stats.live == 1 && stats.retainedBytes > 0;
            if (!packetBuilt) {
                std::fprintf(stderr,
                    "static packet fixture failed to build a cold packet "
                    "(rebuilt=%llu hits=%llu live=%zu bytes=%zu)\n",
                    (unsigned long long)stats.rebuilds,
                    (unsigned long long)stats.hits,
                    stats.live, stats.retainedBytes);
                failed = true;
            }
        }
        if (cpuTiming && frame == 23) {
            const auto report = [](const char* phase, std::vector<double> samples) {
                std::sort(samples.begin(), samples.end());
                if (samples.empty()) return;
                const size_t n = samples.size();
                const double median = (n % 2) ? samples[n / 2]
                    : (samples[n / 2 - 1] + samples[n / 2]) * 0.5;
                const size_t p95Index = (95 * n + 99) / 100 - 1;
                std::printf("CPU render benchmark %s: samples=%zu median_ms=%.4f p95_ms=%.4f min_ms=%.4f max_ms=%.4f\n",
                    phase, n, median, samples[p95Index], samples.front(), samples.back());
            };
            report("direct", cpuRenderBefore);
            report(benchmarkMode == "cpu_mdi" ? "cpu_mdi" :
                   benchmarkMode == "gpu_icb" ? "gpu_icb_mixed" :
                   "batched", cpuRenderAfter);
            std::printf("CPU render benchmark: synchronous renderer.render() only; not GPU time or FPS\n");
        }
        if(frame==8||frame==23){
            const auto& p=renderer.profile();
            std::printf("batch=%d shadow=%u opaque=%u total=%u error=%s\n",batch,p.draws[0],p.draws[1],renderer.drawCalls(),renderer.modelError().c_str());
            if(!batch){shadowBefore=p.draws[0];opaqueBefore=p.draws[1];}
            else if (scenario == "static_packets") {
                const auto stats = renderer.staticRenderPacketStats();
                std::printf("static packet A/B: hits=%llu rebuilds=%llu declines=%llu bytes=%zu\n",
                    (unsigned long long)stats.hits,
                    (unsigned long long)stats.rebuilds,
                    (unsigned long long)stats.declined,
                    stats.retainedBytes);
                if (!packetBuilt || stats.hits != 1 || stats.rebuilds != 0 ||
                    stats.live != 1 || stats.retainedBytes == 0 ||
                    p.draws[0] != shadowBefore || p.draws[1] != opaqueBefore)
                    failed = true;
            }
            else if (scenario == "indirect_palette") {
                const bool supported = d.supportsMultiDrawIndirect();
                const bool requested = std::getenv("AIVERSE_MDI_INSTANCE_PALETTE") &&
                    std::string(std::getenv("AIVERSE_MDI_INSTANCE_PALETTE")) == "1";
                const auto& stats = renderer.modelStats();
                std::printf("indirect palette: supported=%d requested=%d calls %u -> %u, palette submissions=%llu commands=%llu\n",
                            int(supported), int(requested), opaqueBefore, p.draws[1],
                            (unsigned long long)stats.mdiPaletteSubmissions,
                            (unsigned long long)stats.mdiPaletteCommands);
                if (supported && requested) {
                    if (p.draws[1] >= opaqueBefore || !stats.mdiPaletteSubmissions ||
                        stats.mdiPaletteCommands < 16) failed = true;
                } else if (p.draws[1] != opaqueBefore) failed = true;
                if (p.draws[0] != shadowBefore) failed = true;
            }
            else if (scenario == "indirect") {
                const bool supported = d.supportsMultiDrawIndirect();
                // Traditional Metal ICB accepts depth-only/vertex-buffer shaders,
                // but PBR colour fragment shaders with direct textures cannot
                // become ICB pipelines without converting textures to argument
                // buffers. Validate the actual shadow + colour submission total.
                const bool expectGpu = std::getenv("AIVERSE_EXPECT_GPU_ICB") &&
                                        std::string(std::getenv("AIVERSE_EXPECT_GPU_ICB")) == "1";
                const auto gpuDraw = renderer.gpuModelDrawStats();
                const uint64_t indirectCommands = renderer.modelStats().indirectCommands;
                if (benchmarkMode == "cpu_mdi" &&
                    (gpuDraw.gpuEncodedSubmissions || gpuDraw.gpuEncodedCommands ||
                     !renderer.modelStats().multiDrawSubmissions)) {
                    std::fprintf(stderr,
                        "CPU MDI benchmark isolation failed: gpu=%u/%u cpu=%llu\n",
                        gpuDraw.gpuEncodedSubmissions, gpuDraw.gpuEncodedCommands,
                        (unsigned long long)renderer.modelStats().multiDrawSubmissions);
                    failed = true;
                }
                if (benchmarkMode == "gpu_icb" &&
                    (!gpuDraw.gpuEncodedSubmissions || !gpuDraw.gpuEncodedCommands)) {
                    std::fprintf(stderr, "GPU ICB benchmark did not submit GPU commands\n");
                    failed = true;
                }
                // Separate GPU-compute-authored ICB from CPU-encoded MDI.
                // The two paths can both contribute to a single model frame
                // (e.g. different shadow/PBR passes), so the aggregate CPU
                // indirect counters alone cannot establish GPU ICB execution.
                std::printf("GPU-encoded ICB: supported=%d required=%d submissions=%u commands=%u\n",
                            int(d.supportsGpuEncodedIcb()), int(expectGpu),
                            gpuDraw.gpuEncodedSubmissions, gpuDraw.gpuEncodedCommands);
                // Report eligibility/preflight separately from actual GPU execution.
                // Nonzero candidates alone are never evidence of GPU-authored draws.
                std::printf("GPU ICB diagnostics: candidates=%u prepared=%u rejected=%u "
                            "executeFailed=%u preflightGroups=%u\n",
                            gpuDraw.gpuIcbCandidates, gpuDraw.gpuIcbPrepared,
                            gpuDraw.gpuIcbRejected, gpuDraw.gpuIcbExecuteFailed,
                            gpuDraw.preflightGroups);
                // CPU-encoded MDI and GPU-encoded ICB account for separate
                // command counters. Do not reject a successful GPU ICB draw
                // merely because the CPU indirect-command counter is zero.
                if (expectGpu && !d.supportsGpuEncodedIcb()) {
                    std::fprintf(stderr,
                        "[metal-gpu-icb] GPU-encoded ICB is unavailable on the CI device; cannot claim GPU validation\n");
                    failed = true;
                }
                if (expectGpu && d.supportsGpuEncodedIcb()) {
                    if (!gpuDraw.gpuEncodedSubmissions ||
                        !gpuDraw.gpuEncodedCommands ||
                        uint64_t(p.draws[0]) + p.draws[1] >=
                            uint64_t(shadowBefore) + opaqueBefore) {
                        std::fprintf(stderr,
                            "[metal-gpu-icb] expected actual GPU submissions, got submissions=%u commands=%u shadow=%u opaque=%u\n",
                            gpuDraw.gpuEncodedSubmissions, gpuDraw.gpuEncodedCommands,
                            p.draws[0], p.draws[1]);
                        failed = true;
                    }
                } else if (supported) {
                    if (uint64_t(p.draws[0]) + p.draws[1] >=
                            uint64_t(shadowBefore) + opaqueBefore ||
                        indirectCommands < 8) failed = true;
                } else if (p.draws[1] != opaqueBefore ||
                           p.draws[0] != shadowBefore) {
                    failed = true;
                }
                const bool demandPbr = std::getenv("AIVERSE_EXPECT_PBR_ICB") &&
                                       std::string(std::getenv("AIVERSE_EXPECT_PBR_ICB")) == "1";
                if (demandPbr && supported && p.draws[1] >= opaqueBefore) {
                    std::fprintf(stderr, "[metal-icb] PBR colour pass did not enter indirect rendering\n");
                    failed = true;
                }
                std::printf("PBR colour indirect active=%d (before %u, after %u)\n",
                            int(p.draws[1] < opaqueBefore), opaqueBefore, p.draws[1]);
                std::printf("CPU MDI: supported=%d submissions=%llu commands=%llu\n",
                            int(supported),
                            (unsigned long long)renderer.modelStats().multiDrawSubmissions,
                            (unsigned long long)renderer.modelStats().indirectCommands);
            }
            else if(p.draws[0]!=shadowBefore || (scenario=="fallback"?p.draws[1]!=opaqueBefore:p.draws[1]>=opaqueBefore)) failed=true;
            if(scenario=="transition") {std::printf("lod_transition_nodes=%u\n",p.lodTransitionNodes);failed|=p.lodTransitionNodes==0;}
            d.captureNextFrame([&,batch](int w,int h,std::vector<uint8_t> px){
                if(w!=960||h!=640||px.size()!=size_t(w*h*4)){failed=true;return;}
                std::ofstream f(prefix+(batch?"-after.ppm":"-before.ppm"),std::ios::binary);f<<"P6\n"<<w<<" "<<h<<"\n255\n";
                for(size_t i=0;i<px.size();i+=4) f.write(reinterpret_cast<const char*>(px.data()+i),3);
                if(!f)failed=true;
                if(!batch)before=std::move(px);
                else {size_t changed=0;for(size_t i=0;i<px.size();++i)changed+=i>=before.size()||px[i]!=before[i];
                    std::printf("changed_channels=%zu\n",changed);
                    if (scenario == "indirect" && changed) {
                        size_t changedPixels=0, beforeBright=0, afterBright=0;
                        uint8_t beforeMin=255, beforeMax=0, afterMin=255, afterMax=0;
                        for (size_t j=0;j+3<px.size();j+=4) {
                            bool differs=false;
                            for (int k=0;k<3;++k) {
                                const uint8_t a=before[j+size_t(k)],b=px[j+size_t(k)];
                                differs |= a!=b;
                                beforeMin=std::min(beforeMin,a);beforeMax=std::max(beforeMax,a);
                                afterMin=std::min(afterMin,b);afterMax=std::max(afterMax,b);
                                beforeBright+=a;afterBright+=b;
                            }
                            changedPixels+=differs;
                        }
                        std::printf("indirect mismatch: pixels=%zu beforeRGB=%u..%u afterRGB=%u..%u beforeSum=%zu afterSum=%zu\n",
                                    changedPixels,unsigned(beforeMin),unsigned(beforeMax),
                                    unsigned(afterMin),unsigned(afterMax),beforeBright,afterBright);
                        for (size_t i=0;i<5;++i) {
                            const size_t pos=(i==0?0:i==1?px.size()/8:i==2?px.size()/2:i==3?px.size()*3/4:px.size()-4)&~size_t(3);
                            std::printf("sample[%zu] (%u,%u,%u) -> (%u,%u,%u)\n",i,
                                unsigned(before[pos]),unsigned(before[pos+1]),unsigned(before[pos+2]),
                                unsigned(px[pos]),unsigned(px[pos+1]),unsigned(px[pos+2]));
                        }
                    }
                    failed|=changed!=0;finished=true;}
            });
        }
        failed|=!renderer.modelError().empty();d.endPass();++frame;
    };
    cb.shutdown=[&]{device->destroyBuffer(floor.vb);device->destroyBuffer(floor.ib);if(secondModel)render::releaseModel(*device,*secondModel);render::releaseModel(*device,*model);renderer.shutdown();};
    platform::run(cfg,cb);return failed||!finished?1:0;
}
