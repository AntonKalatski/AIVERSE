// Experimental, native macOS production Shader Graph + Renderer3D Metal ICB acceptance.
// This is deliberately distinct from metal_icb_graph_gpu.mm's minimal MSL ABI fixture.
// The opt-in CTest must compile an authored JSON graph through render::graph::compile,
// render the same two material regions through the real Renderer3D direct and GPU-ICB
// paths, and prove both pixel parity and actual mixed-graph GPU ICB submissions.
#include "engine/render/Renderer3D.h"
#include "engine/render/graph/ShaderGraph.h"
#include "engine/platform/Platform.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
        {"id":"quarter","op":"constant","value":0.25},
        {"id":"colour","op":"multiply","inputs":["sum","quarter"]}
      ],"outputs":{"color":"colour"}
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
        material.name=side?"right":"left";
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
                     const std::vector<uint8_t>& icb) {
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
    if(!failed)std::puts("PASS: production Renderer3D compiled Shader Graph 4-source mixed Metal GPU ICB pixels");
}
} // namespace

int aiverseMain(int,char**) {
    for(const char* flag:{"AIVERSE_EXPERIMENTAL_METAL_ICB",
                          "AIVERSE_GPU_ENCODED_ICB",
                          "AIVERSE_GPU_AUTHORED_DRAWS",
                          "AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR",
                          "AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR",
                          "AIVERSE_EXPERIMENTAL_ICB_SHADER_GRAPH"})
        if (!optIn(flag)) {
            std::fprintf(stderr,"FAIL: explicit opt-in missing: %s\n",flag);
            return 2;
        }
    std::shared_ptr<const render::graph::Program> graph;
    std::unique_ptr<render::Model> model;
    try {
        graph=std::make_shared<render::graph::Program>(compileAuthoredGraph());
        check(graph->textures.size()==4 && graph->vertexMsl.empty() &&
              graph->vertexGlsl.empty(),
              "real graph compiler emitted fragment-only four-source material");
        if(failed)return 1;
        model=makeScene(graph);
    } catch(const std::exception& e) {
        std::fprintf(stderr,"FAIL: compiled Shader Graph: %s\n",e.what());
        return 2;
    }

    render::Renderer3D renderer;
    gfx::Device* device=nullptr;
    bool rendererReady=false, modelReady=false;
    int frame=0;
    bool requestedOrdinary=false,requestedIcb=false;
    bool ordinaryReady=false,icbReady=false;
    bool observedMixedGpuIcb=false;
    std::vector<uint8_t> ordinaryPixels,icbPixels;

    platform::AppConfig cfg;
    cfg.title="AIVERSE experimental Renderer3D Shader Graph GPU ICB";
    cfg.width=kWidth;cfg.height=kHeight;cfg.highDpi=false;
    platform::AppCallbacks callbacks;
    callbacks.init=[&](gfx::Device& d) {
        device=&d;
        check(d.info().backend==gfx::Backend::Metal && d.supportsGpuEncodedIcb(),
              "real Metal GPU-encoded ICB hardware and explicit opt-in");
        if(failed)return;
        rendererReady=renderer.init(d);
        check(rendererReady,"Renderer3D initialized with production RHI");
        if(!rendererReady)return;
        renderer.setShadowQuality(render::ShadowQuality::Off);
        renderer.setMaterialQuality(render::MaterialQuality::High);
        renderer.setModelMultiDrawEnabled(true);
        renderer.setModelInstancingEnabled(true);
        renderer.setProfiling(true);
        renderer.lighting.tonemap=false;
        renderer.lighting.fogDensity=0;
        modelReady=render::uploadModel(d,*model);
        check(modelReady,"Model upload created geometry and all graph textures");
        if(modelReady)
            for(const auto& image:model->images)
                check(image.tex.valid(),"native Metal source texture allocated");
    };
    callbacks.frame=[&](gfx::Device& d,double) {
        if(failed || finished || frame>=90) {
            if(frame>=90 && !finished)check(false,"renderer graph capture deadline exceeded");
            platform::requestQuit();return;
        }
        const bool gpu=frame>=6;
        renderer.setGpuAuthoredDrawEnabled(gpu);
        render::RTSCamera camera;
        camera.target={0,0,0};
        camera.distance=4.0f;
        camera.pitch=0.10f;
        camera.yaw=0.0f;
        renderer.beginScene(camera,kWidth,kHeight);
        render::ModelDraw draw;
        draw.model=model.get();
        draw.flags=render::kDrawStatic;
        renderer.drawModel(draw);
        gfx::PassDesc pass;
        renderer.render(pass);
        if(!renderer.modelError().empty()) {
            std::fprintf(stderr,"FAIL: renderer shader/pipeline: %s\n",
                         renderer.modelError().c_str());
            failed=true;
        }
        if (gpu) {
            const auto stats=renderer.gpuModelDrawStats();
            if(stats.shaderGraphIcbSubmissions>0 &&
               stats.mixedGraphSourceSubmissions>0 &&
               stats.perCommandPbrSubmissions>0 &&
               stats.gpuEncodedCommands>=2 &&
               stats.gpuIcbExecuteFailed==0)
                observedMixedGpuIcb=true;
            if(frame==12) {
                std::printf("GPU ICB graph stats: eligible=%u candidates=%u prepared=%u "
                            "submitted=%u mixed=%u gpu_commands=%u rejected=%u errors=%u\n",
                            stats.eligible,stats.gpuIcbCandidates,stats.gpuIcbPrepared,
                            stats.shaderGraphIcbSubmissions,stats.mixedGraphSourceSubmissions,
                            stats.gpuEncodedCommands,stats.gpuIcbRejected,stats.gpuIcbExecuteFailed);
            }
        }
        if(frame==3 && !requestedOrdinary) {
            requestedOrdinary=true;
            d.captureNextFrame([&](int w,int h,std::vector<uint8_t> pixels){
                check(w==kWidth && h==kHeight,"ordinary Renderer3D capture extent");
                ordinaryPixels=std::move(pixels);ordinaryReady=true;
            });
        }
        if(frame==13 && !requestedIcb) {
            requestedIcb=true;
            d.captureNextFrame([&](int w,int h,std::vector<uint8_t> pixels){
                check(w==kWidth && h==kHeight,"mixed GPU ICB capture extent");
                icbPixels=std::move(pixels);icbReady=true;
            });
        }
        d.endPass();
        ++frame;
        if(frame>=16 && ordinaryReady && icbReady) {
            check(observedMixedGpuIcb,
                  "production Renderer3D submitted heterogeneous Shader Graph GPU ICB");
            if(!failed)compareCaptured(ordinaryPixels,icbPixels);
            finished=true;
            platform::requestQuit();
        }
    };
    callbacks.shutdown=[&] {
        if(device) {
            if(modelReady)render::releaseModel(*device,*model);
            if(rendererReady)renderer.shutdown();
        }
    };
    platform::run(cfg,callbacks);
    check(finished,"both production renderer captures completed");
    return failed?1:0;
}
