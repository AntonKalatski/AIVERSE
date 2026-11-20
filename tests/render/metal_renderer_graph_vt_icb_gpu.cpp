// Experimental, native macOS production Shader Graph + Renderer3D Metal ICB acceptance.
// This is deliberately distinct from metal_icb_graph_gpu.mm's minimal MSL ABI fixture.
// The opt-in CTest must compile an authored JSON graph through render::graph::compile,
// render the same two material regions through the real Renderer3D direct and GPU-ICB
// paths, and prove both pixel parity and actual mixed-graph GPU ICB submissions.
#include "engine/render/Renderer3D.h"
#include "engine/render/graph/ShaderGraph.h"
#include "engine/platform/Platform.h"
#include "engine/gfx/VirtualTextureFileStreamer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace aiverse;

namespace {
constexpr int kWidth=192, kHeight=128;
bool failed=false, finished=false;
void check(bool condition,const char* what) {
    if (!condition) { failed=true; std::fprintf(stderr,"FAIL: %s\n",what); }
}
bool optIn(const char* name) {
    const char* value=std::getenv(name);
    return value && std::string(value)=="1";
}
render::graph::Program compileAuthoredGraph() {
    // Four real, connected sampleTexture nodes; tex16 is sampled, not merely
    // declared. This forces Renderer3D's production MSL injection code.
    const Json json=Json::parse(R"JSON({
      "version":1,"domain":"material","nodes":[
        {"id":"uv","op":"uv"},
        {"id":"t0","op":"sampleTexture","inputs":["uv"],"texture":"fixtures/0.png"},
        {"id":"t1","op":"sampleTexture","inputs":["uv"],"texture":"fixtures/1.png"},
        {"id":"t2","op":"sampleTexture","inputs":["uv"],"texture":"fixtures/2.png"},
        {"id":"t3","op":"sampleTexture","inputs":["uv"],"texture":"fixtures/3.png"},
        {"id":"a","op":"add","inputs":["t0","t1"]},
        {"id":"b","op":"add","inputs":["t2","t3"]},
        {"id":"sum","op":"add","inputs":["a","b"]},
        {"id":"quarter","op":"constant","value":0.08},
        {"id":"colour","op":"multiply","inputs":["sum","quarter"]},
        {"id":"vt","op":"sampleVirtualTexture","inputs":["uv"]},
        {"id":"vtWeight","op":"constant","value":0.65},
        {"id":"vtColour","op":"multiply","inputs":["vt","vtWeight"]},
        {"id":"final","op":"add","inputs":["colour","vtColour"]}
      ],"outputs":{"color":"final"}
    })JSON");
    return render::graph::compile(json,"material");
}
std::unique_ptr<render::Model> makeScene(const std::shared_ptr<const render::graph::Program>& graph) {
    auto m=std::make_unique<render::Model>();
    m->path="metal-renderer-graph-icb-fixture";
    m->batching.autoLod=false;
    // 8 separate source images: the same 4-sampler compiled graph uses
    // independent textures on each material, not shared static bindings.
    const std::array<std::array<uint8_t,4>,8> colors{{
        {{160,25,15,255}},{{120,35,35,255}},
        {{150,30,55,255}},{{170,20,35,255}},
        {{20,140,55,255}},{{25,160,60,255}},
        {{40,145,50,255}},{{35,155,70,255}}
    }};
    for (size_t i=0;i<colors.size();++i) {
        render::Image image;
        image.name="graph-source-"+std::to_string(i);
        image.width=image.height=2;
        image.repeat=true;
        image.nearest=false; // graph source slot16 requires repeat/linear
        for(int pixel=0;pixel<4;++pixel)
            image.rgba.insert(image.rgba.end(),colors[i].begin(),colors[i].end());
        m->images.push_back(std::move(image));
    }
    m->materials.resize(2);
    for(int side=0;side<2;++side) {
        auto& material=m->materials[size_t(side)];
        material.shaderGraph=graph;
        material.name=side?"vt-right":"vt-left";
        material.shading=render::ShadingModel::Unlit;
        material.doubleSided=true;
        material.alpha=render::AlphaMode::Opaque;
        material.castShadow=false;
        material.baseColor=side?render::float4{.8f,1,1,1}:render::float4{1,1,1,1};
        for(int slot=0;slot<4;++slot)material.graphTextures[slot]=side*4+slot;
    }
    render::MeshDef mesh;
    mesh.name="split-material-quads";
    for(int side=0;side<2;++side) {
        const float x=side ? .8f : -.8f;
        const uint32_t firstVertex=uint32_t(m->vertices.size());
        for(const auto& local:std::array<render::float3,4>{{
                {-0.72f,-0.82f,0.f},{0.72f,-0.82f,0.f},
                {-0.72f,0.82f,0.f},{0.72f,0.82f,0.f}}}) {
            render::ModelVertex v;
            v.pos={x+local.x,local.y,0};
            v.nrm={0,0,1};
            v.uv={(local.x+0.72f)/1.44f,(local.y+0.82f)/1.64f};
            m->vertices.push_back(v);
        }
        const uint32_t firstIndex=uint32_t(m->indices.size());
        m->indices.insert(m->indices.end(),{
            firstVertex,firstVertex+1,firstVertex+2,
            firstVertex+1,firstVertex+3,firstVertex+2
        });
        render::Primitive primitive;
        primitive.material=side;
        primitive.firstIndex=firstIndex;
        primitive.indexCount=6;
        primitive.firstVertex=firstVertex;
        primitive.vertexCount=4;
        primitive.boundsMin={x-.72f,-.82f,0};
        primitive.boundsMax={x+.72f,.82f,0};
        mesh.primitives.push_back(primitive);
    }
    mesh.boundsMin={-1.52f,-.82f,0};
    mesh.boundsMax={1.52f,.82f,0};
    m->meshes.push_back(std::move(mesh));
    render::Node node;
    node.mesh=0;
    node.name="two-material-fixture";
    m->nodes.push_back(std::move(node));
    m->finalize();
    return m;
}
bool differentRegions(const std::vector<uint8_t>& rgba) {
    if (rgba.size()!=size_t(kWidth*kHeight*4)) return false;
    // The two interior regions must actually be rendered and must differ
    // chromatically. A blank frame or both draws inheriting one texture
    // cannot pass merely by being identical across modes.
    auto average=[&](int x0,int x1) {
        std::array<double,3> sum{};
        for(int y=kHeight/3;y<2*kHeight/3;++y)
            for(int x=x0;x<x1;++x)
                for(int channel=0;channel<3;++channel)
                    sum[size_t(channel)]+=rgba[(size_t(y*kWidth+x)*4)+size_t(channel)];
        const double count=double((2*kHeight/3-kHeight/3)*(x1-x0));
        for(auto& v:sum)v/=count;
        return sum;
    };
    const auto left=average(kWidth/5,2*kWidth/5);
    const auto right=average(3*kWidth/5,4*kWidth/5);
    const double delta=std::abs(left[0]-right[0])+
                       std::abs(left[1]-right[1])+
                       std::abs(left[2]-right[2]);
    std::printf("Renderer3D Shader Graph regions: left=(%.1f,%.1f,%.1f) "
                "right=(%.1f,%.1f,%.1f) delta=%.1f\n",
                left[0],left[1],left[2],right[0],right[1],right[2],delta);
    return delta>35.0; // fail closed if one/both quads were culled/blank
}
void compareCaptured(const std::vector<uint8_t>& ordinary,
                     const std::vector<uint8_t>& icb,const char* phase) {
    check(ordinary.size()==size_t(kWidth*kHeight*4) &&
          ordinary.size()==icb.size(),"two valid full-screen capture buffers");
    if (ordinary.size()!=icb.size() || ordinary.empty()) return;
    check(differentRegions(ordinary),"compiled Shader Graph draws distinct regions");
    check(differentRegions(icb),"GPU ICB draws distinct graph-source regions");
    size_t bad=0;int maxDelta=0;
    // Allow only two 8-bit levels for shader/UNORM rounding.
    for(size_t i=0;i<ordinary.size();++i) {
        const int diff=std::abs(int(ordinary[i])-int(icb[i]));
        maxDelta=std::max(maxDelta,diff);
        if(diff>2)++bad;
    }
    std::printf("Renderer3D graph CPU/ICB pixels: bad_channels=%zu max_delta=%d\n",
                bad,maxDelta);
    check(bad==0,"full production graph Renderer3D CPU/ICB capture parity");
    if(!failed)std::printf("PASS: Shader Graph+VT %s Renderer3D CPU/ICB pixels\n",phase);
}

// Only synthetic, trusted 4x4 raw RGBA8 pages live in this test's own
// temporary directory. No assetpack path and no production game files.
struct DiskFixture {
    std::filesystem::path root;
    bool ready=false;
    DiskFixture() {
        std::error_code error;
        const auto temp=std::filesystem::temp_directory_path(error);
        if(error)return;
        root=temp/("aiverse-renderer-vt-icb-"+
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ready=std::filesystem::create_directory(root,error) && !error;
    }
    ~DiskFixture() {
        if(ready) {
            std::error_code error;
            std::filesystem::remove_all(root,error);
        }
    }
    bool write(int domain,gfx::VirtualPageKey key,std::array<uint8_t,4> color) const {
        if(!ready || domain<0 || domain>1)return false;
        const auto directory=root/(domain==0?"left":"right");
        const auto file=directory/gfx::VirtualTextureDiskQueue::relativePath(key);
        std::error_code error;
        std::filesystem::create_directories(file.parent_path(),error);
        if(error)return false;
        std::array<uint8_t,4*4*4> bytes{};
        for(size_t i=0;i<bytes.size();i+=4)
            std::copy(color.begin(),color.end(),bytes.begin()+std::ptrdiff_t(i));
        std::ofstream out(file,std::ios::binary|std::ios::trunc);
        if(!out)return false;
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  std::streamsize(bytes.size()));
        out.close();
        return bool(out);
    }
};
constexpr gfx::VirtualPageKey coarseKey(int side) {return {2,0,0,side};}
constexpr gfx::VirtualPageKey fineKey(int side) {return {0,1,1,side};}
constexpr gfx::VirtualPageKey replacementKey(int side) {return {0,2,2,side};}
bool differentCaptures(const std::vector<uint8_t>& before,
                       const std::vector<uint8_t>& after,const char* why) {
    if(before.size()!=after.size() || before.empty()) {
        check(false,"VT transition produced incompatible readback buffers");
        return false;
    }
    size_t changed=0;
    for(size_t i=0;i<before.size();++i)
        if(std::abs(int(before[i])-int(after[i]))>2)++changed;
    std::printf("VT page transition %s changed_channels=%zu\n",why,changed);
    check(changed>64,"VT page transition must visibly change production Shader Graph pixels");
    return changed>64;
}
enum class CapturePhase {
    WaitCoarse,CoarseDirect,CoarseIcb,
    WaitFine,FineDirect,FineIcb,
    WaitReplacement,ReplacementDirect,ReplacementIcb,Done
};
bool waiting(CapturePhase p) {
    return p==CapturePhase::WaitCoarse || p==CapturePhase::WaitFine ||
           p==CapturePhase::WaitReplacement;
}
bool gpuPhase(CapturePhase p) {
    return p==CapturePhase::CoarseIcb || p==CapturePhase::FineIcb ||
           p==CapturePhase::ReplacementIcb;
}
int phaseIndex(CapturePhase p) {
    return (p==CapturePhase::CoarseDirect || p==CapturePhase::CoarseIcb) ? 0 :
           (p==CapturePhase::FineDirect || p==CapturePhase::FineIcb) ? 1 : 2;
}
const char* phaseName(int stage) {
    return stage==0?"pinned coarse":stage==1?"fine detail":"LRU recycled";
}
} // namespace

int aiverseMain(int,char**) {
    for(const char* flag:{"AIVERSE_EXPERIMENTAL_METAL_ICB",
                          "AIVERSE_GPU_ENCODED_ICB",
                          "AIVERSE_GPU_AUTHORED_DRAWS",
                          "AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR",
                          "AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR",
                          "AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_VT",
                          "AIVERSE_EXPERIMENTAL_ICB_MIXED_VT",
                          "AIVERSE_EXPERIMENTAL_ICB_SHADER_GRAPH"})
        if(!optIn(flag)) {
            std::fprintf(stderr,"FAIL: missing experimental opt-in %s\n",flag);
            return 2;
        }
    auto graph=std::shared_ptr<const render::graph::Program>{};
    auto model=std::unique_ptr<render::Model>{};
    try {
        graph=std::make_shared<render::graph::Program>(compileAuthoredGraph());
        check(graph->textures.size()==4 && graph->usesVirtualTexture &&
              graph->vertexMsl.empty() && graph->vertexGlsl.empty(),
              "production compiler emitted four-source fragment VT graph");
        if(failed)return 1;
        model=makeScene(graph);
    }catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL: authored VT Shader Graph compile: %s\n",error.what());
        return 2;
    }
    DiskFixture files;
    check(files.ready,"isolated VT archive directory created");
    if(!files.ready)return 1;
    for(int side=0;side<2;++side) {
        check(files.write(side,coarseKey(side),side==0?
              std::array<uint8_t,4>{45,75,120,255}:
              std::array<uint8_t,4>{80,60,35,255}),"pinned coarse archive file");
        check(files.write(side,fineKey(side),side==0?
              std::array<uint8_t,4>{215,20,25,255}:
              std::array<uint8_t,4>{25,85,225,255}),"fine detail archive file");
        check(files.write(side,replacementKey(side),side==0?
              std::array<uint8_t,4>{25,210,40,255}:
              std::array<uint8_t,4>{200,30,170,255}),"LRU replacement archive file");
    }
    if(failed)return 1;

    render::Renderer3D renderer;
    gfx::Device* device=nullptr;
    bool rendererReady=false,modelReady=false;
    std::array<std::unique_ptr<gfx::VirtualTextureFileStreamer>,2> streaming;
    std::array<std::array<std::vector<uint8_t>,3>,2> images;
    std::array<bool,3> directReady{},icbReady{},directRequested{},icbRequested{},icbSubmitted{};
    CapturePhase phase=CapturePhase::WaitCoarse;
    size_t frames=0,stableFrames=0;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(40);

    platform::AppConfig config;
    config.title="AIVERSE production Shader Graph+disk VT+Metal ICB";
    config.width=kWidth;config.height=kHeight;config.highDpi=false;
    platform::AppCallbacks callbacks;
    callbacks.init=[&](gfx::Device& d) {
        device=&d;
        check(d.info().backend==gfx::Backend::Metal && d.supportsGpuEncodedIcb(),
              "real native Metal GPU-encoded ICB supported");
        if(failed)return;
        rendererReady=renderer.init(d);
        check(rendererReady,"production Renderer3D initialized");
        if(!rendererReady)return;
        renderer.setShadowQuality(render::ShadowQuality::Off);
        renderer.setMaterialQuality(render::MaterialQuality::High);
        renderer.setModelInstancingEnabled(true);
        renderer.setModelMultiDrawEnabled(true);
        renderer.setProfiling(true);
        renderer.lighting.tonemap=false;
        renderer.lighting.fogDensity=0;
        modelReady=render::uploadModel(d,*model);
        check(modelReady,"native model and four Shader Graph source textures uploaded");
        if(!modelReady)return;
        for(const auto& image:model->images)
            check(image.tex.valid(),"graph texture on GPU");

        for(int side=0;side<2;++side) {
            gfx::TextureStreamConfig limits;
            limits.gpuBudgetBytes=4096;
            limits.stagingBudgetBytes=256;
            limits.uploadBytesPerFrame=128;
            limits.uploadsPerFrame=2;
            streaming[size_t(side)]=
                std::make_unique<gfx::VirtualTextureFileStreamer>(d,limits);
            gfx::VirtualTextureStreamConfig atlas;
            atlas.tilesX=atlas.tilesY=4;
            atlas.mipLevels=3;atlas.virtualLayers=2;
            atlas.physicalPages=2;atlas.physicalPageSize=4;
            atlas.physicalFormat=gfx::PixelFormat::RGBA8;
            atlas.pageTableBytesPerFlush=64;
            gfx::VirtualDiskPageConfig disk;
            disk.assetRoot=files.root/(side==0?"left":"right");
            disk.tilesX=disk.tilesY=4;disk.mipLevels=3;
            disk.virtualLayers=2;disk.pageSize=4;
            disk.maxOutstanding=8;disk.maxReadyBytes=256;
            const bool ok=streaming[size_t(side)]->initialize(atlas,disk,2);
            check(ok,"separate native disk-backed VT atlas initialized");
            if(!ok) {
                std::fprintf(stderr,"FAIL: VT init: %s\n",
                             streaming[size_t(side)]->lastError().c_str());
                return;
            }
        }
        check(streaming[0]->physicalAtlas()!=streaming[1]->physicalAtlas() &&
              streaming[0]->pageTableTexture()!=streaming[1]->pageTableTexture(),
              "two separate native atlas and page-table identities");
        renderer.setModelVirtualTextureResolver(
            [&](const render::Model&,const render::Material& material,
                render::ModelVirtualTextureBinding& binding) {
                const int side=material.name=="vt-left"?0:
                    material.name=="vt-right"?1:-1;
                if(side<0 || !streaming[size_t(side)] ||
                   !streaming[size_t(side)]->ready())return false;
                auto& stream=*streaming[size_t(side)];
                binding.physicalAtlas=stream.physicalAtlas();
                binding.pageTable=stream.pageTableTexture();
                binding.layer=side; // different virtual layer in each archive
                binding.gutterPixels=0;
                binding.addressU=side==0?gfx::SamplerAddressMode::Repeat:
                                         gfx::SamplerAddressMode::MirrorRepeat;
                binding.addressV=gfx::SamplerAddressMode::ClampToEdge;
                return binding.valid();
            });
        for(int side=0;side<2;++side) {
            const auto ticket=streaming[size_t(side)]->requestPage(coarseKey(side),true);
            check(ticket.valid(),"coarse fallback request queued from disk");
        }
    };
    callbacks.frame=[&](gfx::Device& d,double) {
        if(failed || finished || frames>=320 ||
           std::chrono::steady_clock::now()>=deadline) {
            if(!failed && !finished)check(false,"VT+Shader Graph ICB test timed out");
            platform::requestQuit();return;
        }
        ++frames;
        for(int side=0;side<2;++side) {
            if(!streaming[size_t(side)]) {
                check(false,"VT streamer not initialized");
                break;
            }
            auto& stream=*streaming[size_t(side)];
            stream.pump(2,[&](gfx::VirtualDiskPageTicket,
                             gfx::VirtualPageKey key,bool ok,
                             const std::string& why) {
                if(!ok) {
                    std::fprintf(stderr,"FAIL: VT disk-to-GPU page publication "
                                        "mip=%d x=%d y=%d layer=%d: %s\n",
                                 key.mip,key.x,key.y,key.layer,why.c_str());
                    failed=true;
                }
            });
            if(!stream.ready())check(false,"disk-backed VT atlas remains healthy");
        }
        if(failed) {platform::requestQuit();return;}
        if(waiting(phase)) {
            const bool bothReady=[&] {
                for(int side=0;side<2;++side) {
                    const auto& stream=*streaming[size_t(side)];
                    const auto key=phase==CapturePhase::WaitCoarse?coarseKey(side):
                                    phase==CapturePhase::WaitFine?fineKey(side):
                                    replacementKey(side);
                    if(!stream.exactResidentPage(key))return false;
                }
                return true;
            }();
            if(bothReady) {
                if(phase==CapturePhase::WaitReplacement)
                    for(int side=0;side<2;++side) {
                        const auto& stream=*streaming[size_t(side)];
                        check(stream.exactResidentPage(coarseKey(side)) &&
                              !stream.exactResidentPage(fineKey(side)) &&
                              stream.stats().pages.evictions>=1,
                              "LRU slot reuse evicted fine while keeping pinned coarse");
                    }
                phase=phase==CapturePhase::WaitCoarse?CapturePhase::CoarseDirect:
                      phase==CapturePhase::WaitFine?CapturePhase::FineDirect:
                                                    CapturePhase::ReplacementDirect;
                stableFrames=0;
            }
        } else if(phase!=CapturePhase::Done) {
            const int index=phaseIndex(phase);
            if(!gpuPhase(phase) && directReady[size_t(index)]) {
                phase=CapturePhase(int(phase)+1); // adjacent Direct -> Icb
                stableFrames=0;
            }
        }
        const bool gpu=gpuPhase(phase);
        renderer.setGpuAuthoredDrawEnabled(gpu);
        render::RTSCamera camera;
        camera.target={0,0,0};camera.distance=4.0f;
        camera.pitch=0.10f;camera.yaw=0.0f;
        renderer.beginScene(camera,kWidth,kHeight);
        render::ModelDraw modelDraw;
        modelDraw.model=model.get();modelDraw.flags=render::kDrawStatic;
        renderer.drawModel(modelDraw);
        gfx::PassDesc pass;
        renderer.render(pass);
        if(!renderer.modelError().empty()) {
            std::fprintf(stderr,"FAIL: production Shader Graph VT pipeline: %s\n",
                         renderer.modelError().c_str());
            failed=true;
        }
        if(phase!=CapturePhase::Done && !waiting(phase)) {
            const int index=phaseIndex(phase);
            const auto stats=renderer.gpuModelDrawStats();
            if(gpu && stats.shaderGraphIcbSubmissions>=1 &&
               stats.mixedGraphSourceSubmissions>=1 &&
               stats.perCommandVtExecutions>=1 &&
               stats.mixedVtExecutions>=1 &&
               stats.gpuEncodedCommands>=2 &&
               stats.gpuIcbExecuteFailed==0)
                icbSubmitted[size_t(index)]=true;
            if(gpu && stableFrames==2)std::printf(
                "VT+graph GPU ICB %s: eligible=%u prepared=%u graph=%u "
                "mixedGraph=%u vt=%u mixedVt=%u commands=%u rejected=%u errors=%u\n",
                phaseName(index),stats.eligible,stats.gpuIcbPrepared,
                stats.shaderGraphIcbSubmissions,stats.mixedGraphSourceSubmissions,
                stats.perCommandVtExecutions,stats.mixedVtExecutions,
                stats.gpuEncodedCommands,stats.gpuIcbRejected,
                stats.gpuIcbExecuteFailed);
            ++stableFrames;
            if(stableFrames>=3 && !gpu && !directRequested[size_t(index)]) {
                directRequested[size_t(index)]=true;
                d.captureNextFrame([&,index](int w,int h,std::vector<uint8_t> data) {
                    if(w!=kWidth || h!=kHeight)failed=true;
                    images[0][size_t(index)]=std::move(data);
                    directReady[size_t(index)]=true;
                });
            }
            if(stableFrames>=3 && gpu && !icbRequested[size_t(index)]) {
                icbRequested[size_t(index)]=true;
                d.captureNextFrame([&,index](int w,int h,std::vector<uint8_t> data) {
                    if(w!=kWidth || h!=kHeight)failed=true;
                    images[1][size_t(index)]=std::move(data);
                    icbReady[size_t(index)]=true;
                });
            }
        }
        d.endPass();
        if(phase!=CapturePhase::Done && !waiting(phase) && gpu) {
            const int stage=phaseIndex(phase);
            if(icbReady[size_t(stage)]) {
                check(icbSubmitted[size_t(stage)],
                      "actual graph ICB with distinct VT atlases executed");
                compareCaptured(images[0][size_t(stage)],
                                images[1][size_t(stage)],phaseName(stage));
                if(stage>0)
                    differentCaptures(images[0][size_t(stage-1)],
                                      images[0][size_t(stage)],phaseName(stage));
                if(!failed) {
                    if(stage==0 || stage==1) {
                        for(int side=0;side<2;++side) {
                            const auto key=stage==0?fineKey(side):replacementKey(side);
                            auto ticket=streaming[size_t(side)]->requestPage(key);
                            check(ticket.valid(),"next page transition queued on disk");
                        }
                        phase=stage==0?CapturePhase::WaitFine:
                                       CapturePhase::WaitReplacement;
                        stableFrames=0;
                    } else {
                        std::puts("PASS: production Renderer3D Shader Graph+disk VT "
                                  "mixed Metal GPU ICB coarse/fine/LRU pixel parity");
                        phase=CapturePhase::Done;
                        finished=true;
                        platform::requestQuit();
                    }
                }
            }
        }
    };
    callbacks.shutdown=[&] {
        if(device) {
            renderer.clearModelVirtualTextureResolver();
            if(rendererReady)renderer.shutdown();
            if(modelReady)render::releaseModel(*device,*model);
            for(auto& streamer:streaming)
                if(streamer)streamer->shutdown();
        }
    };
    platform::run(config,callbacks);
    check(finished,"all three disk VT/Shader Graph GPU ICB image phases completed");
    return failed?1:0;
}
