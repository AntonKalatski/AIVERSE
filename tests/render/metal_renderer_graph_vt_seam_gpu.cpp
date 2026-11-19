// Isolated, experimental Metal Shader Graph+disk VT missing-page and UV wrap seam acceptance.
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
    m->path="metal-renderer-graph-vt-wrap-fixture";
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
        material.uvScale={0.0001f,0.0001f};
        material.uvOffset={-0.125f,0.375f};
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
        root=temp/("aiverse-renderer-vt-seam-"+
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

constexpr gfx::VirtualPageKey coarse(int side) {return {2,0,0,side};}
constexpr gfx::VirtualPageKey missing(int side) {return {0,2,1,side};}
constexpr gfx::VirtualPageKey finePageKey(int side) {return {0,3,1,side};}
using Pixel=std::array<uint8_t,4>;

void validateTable(gfx::VirtualTextureFileStreamer& stream,int side,
                   Pixel want,const char* label) {
    const Pixel actual=stream.encodedPageTable(3,1,side);
    if(actual!=want) {
        std::fprintf(stderr,"FAIL: %s table side=%d want=[%u,%u,%u,%u] "
                "got=[%u,%u,%u,%u]\n",label,side,
                unsigned(want[0]),unsigned(want[1]),unsigned(want[2]),unsigned(want[3]),
                unsigned(actual[0]),unsigned(actual[1]),unsigned(actual[2]),unsigned(actual[3]));
        failed=true;
    }
}
size_t changedChannels(const std::vector<uint8_t>& a,
                       const std::vector<uint8_t>& b,int tolerance=1) {
    if(a.size()!=size_t(kWidth*kHeight*4) || a.size()!=b.size()) {
        check(false,"full RGBA8 captures valid for independent oracle");
        return 0;
    }
    size_t changed=0;
    for(size_t i=0;i<a.size();++i)
        changed+=std::abs(int(a[i])-int(b[i]))>tolerance;
    return changed;
}
struct Means {
    std::array<double,3> rgb{};
};
Means regionMean(const std::vector<uint8_t>& rgba,int side) {
    Means result;
    if(rgba.size()!=size_t(kWidth*kHeight*4)) {
        check(false,"full framebuffer captured for region color oracle");
        return result;
    }
    const int x0=side==0?kWidth/5:3*kWidth/5;
    const int x1=side==0?2*kWidth/5:4*kWidth/5;
    const int y0=kHeight/3,y1=2*kHeight/3;
    for(int y=y0;y<y1;++y)
        for(int x=x0;x<x1;++x)
            for(int c=0;c<3;++c)
                result.rgb[size_t(c)]+=rgba[(size_t(y*kWidth+x)*4)+size_t(c)];
    const double n=double((x1-x0)*(y1-y0));
    for(double& v:result.rgb)v/=n;
    return result;
}
void independentWrapOracle(const std::vector<uint8_t>& coarsePixels,
                           const std::vector<uint8_t>& actual,
                           bool leftShouldUseFine,const char* label) {
    // This prediction is made from the authored UVs and the *expected*
    // Repeat/MirrorRepeat policy, not from either shader path's pixels:
    //   -0.125  -> Repeat=.875 (tile3), Mirror=.125 (tile0)
    //   +1.125  -> Repeat=.125 (tile0), Mirror=.875 (tile3)
    //   -0.0005 -> Repeat=.9995, Mirror=.0005
    //   +1.0005 -> Repeat=.0005, Mirror=.9995
    // Only tile(3,1) is fine; tile(0,1) must retain pinned coarse.
    const auto coarseLeft=regionMean(coarsePixels,0).rgb;
    const auto coarseRight=regionMean(coarsePixels,1).rgb;
    const auto testLeft=regionMean(actual,0).rgb;
    const auto testRight=regionMean(actual,1).rgb;
    auto l1=[](auto a,auto b) {
        return std::abs(a[0]-b[0])+std::abs(a[1]-b[1])+std::abs(a[2]-b[2]);
    };
    const double leftDifference=l1(coarseLeft,testLeft);
    const double rightDifference=l1(coarseRight,testRight);
    std::printf("WRAP ORACLE %s: expectedFine=%s left_delta=%.2f "
                "right_delta=%.2f left_RGB_delta=(%.1f,%.1f,%.1f) "
                "right_RGB_delta=(%.1f,%.1f,%.1f)\n",
                label,leftShouldUseFine?"Repeat-left":"Mirror-right",
                leftDifference,rightDifference,
                testLeft[0]-coarseLeft[0],testLeft[1]-coarseLeft[1],
                testLeft[2]-coarseLeft[2],testRight[0]-coarseRight[0],
                testRight[1]-coarseRight[1],testRight[2]-coarseRight[2]);
    const bool correctRegion=leftShouldUseFine ?
        leftDifference>20.0 && rightDifference<=3.0 :
        rightDifference>20.0 && leftDifference<=3.0;
    const bool expectedHue=leftShouldUseFine ?
        testLeft[0]-coarseLeft[0]>10.0 &&
        testLeft[1]-coarseLeft[1]<-10.0 :
        testRight[0]-coarseRight[0]>10.0 &&
        testRight[2]-coarseRight[2]<-10.0;
    check(correctRegion,"predicted Repeat/MirrorRepeat fine-tile region only");
    check(expectedHue,"independent known-page red/green/blue direction oracle");
    if(correctRegion && expectedHue)
        std::printf("PASS: independent wrap oracle %s\n",label);
}

enum class Phase : int {
    NoVtControl,
    EmptyDirect,EmptyIcb,
    WaitCoarse,CoarseDirect,CoarseIcb,
    WaitMissing,MissingDirect,MissingIcb,
    WaitFine,NegativeDirect,NegativeIcb,
    PositiveDirect,PositiveIcb,
    NearNegativeDirect,NearNegativeIcb,
    NearPositiveDirect,NearPositiveIcb,
    Done
};
struct Capture {
    std::vector<uint8_t> pixels;
    bool requested=false,ready=false;
};
bool phaseGpu(Phase p) {
    return p==Phase::EmptyIcb || p==Phase::CoarseIcb ||
           p==Phase::MissingIcb || p==Phase::NegativeIcb ||
           p==Phase::PositiveIcb || p==Phase::NearNegativeIcb ||
           p==Phase::NearPositiveIcb;
}
int pairFor(Phase p) {
    switch(p) {
        case Phase::EmptyDirect: case Phase::EmptyIcb: return 0;
        case Phase::CoarseDirect: case Phase::CoarseIcb: return 1;
        case Phase::MissingDirect: case Phase::MissingIcb: return 2;
        case Phase::NegativeDirect: case Phase::NegativeIcb: return 3;
        case Phase::PositiveDirect: case Phase::PositiveIcb: return 4;
        case Phase::NearNegativeDirect: case Phase::NearNegativeIcb: return 5;
        case Phase::NearPositiveDirect: case Phase::NearPositiveIcb: return 6;
        default: return -1;
    }
}
const char* pairName(int i) {
    constexpr const char* names[]={
      "nonresident", "pinned coarse", "missing file preserved coarse",
      "UV -0.125", "UV +1.125", "UV -0.0005 seam", "UV +1.0005 seam"
    };
    return i>=0 && i<7?names[i]:"invalid";
}
float uvUFor(int pair) {
    return pair==4?1.125f:pair==5?-0.0005f:
           pair==6?1.0005f:-0.125f;
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
            std::fprintf(stderr,"FAIL: missing opt-in: %s\n",flag);
            return 2;
        }

    std::unique_ptr<render::Model> model;
    try {
        auto graph=std::make_shared<render::graph::Program>(compileAuthoredGraph());
        check(graph->textures.size()==4 && graph->usesVirtualTexture &&
              graph->vertexMsl.empty() && graph->vertexGlsl.empty(),
              "real Shader Graph compiled four samples and fragment-only VT");
        if(failed)return 1;
        model=makeScene(graph);
    }catch(const std::exception& e) {
        std::fprintf(stderr,"FAIL: authored graph: %s\n",e.what());
        return 2;
    }
    DiskFixture disk;
    check(disk.ready,"trusted temporary page root created");
    if(!disk.ready)return 1;
    for(int side=0;side<2;++side) {
        check(disk.write(side,coarse(side),side==0?
              Pixel{22,185,35,255}:Pixel{35,65,210,255}),
              "known coarse disk page written");
        check(disk.write(side,finePageKey(side),side==0?
              Pixel{245,25,30,255}:Pixel{225,215,15,255}),
              "known fine disk page written");
        // Intentionally DO NOT write missing(side): the async worker must
        // report an error and leave the coarse physical page unchanged.
    }
    if(failed)return 1;

    render::Renderer3D renderer;
    gfx::Device* device=nullptr;
    bool rendererReady=false,modelReady=false,vtResolverInstalled=false;
    std::array<std::unique_ptr<gfx::VirtualTextureFileStreamer>,2> streams;
    std::array<bool,2> missingFailed{};
    std::array<bool,7> submitted{};
    std::array<Capture,15> captures; // control=0, then (direct,ICB) per pair
    size_t frames=0,stable=0;
    Phase phase=Phase::NoVtControl;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(55);

    platform::AppConfig config;
    config.title="AIVERSE Shader Graph disk VT seam/missing Metal ICB";
    config.width=kWidth;config.height=kHeight;config.highDpi=false;
    platform::AppCallbacks cb;
    cb.init=[&](gfx::Device& d) {
        device=&d;
        check(d.info().backend==gfx::Backend::Metal && d.supportsGpuEncodedIcb(),
              "native real Metal GPU ICB available");
        if(failed)return;
        rendererReady=renderer.init(d);
        check(rendererReady,"production Renderer3D initialized");
        if(!rendererReady)return;
        renderer.setShadowQuality(render::ShadowQuality::Off);
        renderer.setMaterialQuality(render::MaterialQuality::High);
        renderer.setModelMultiDrawEnabled(true);
        renderer.setModelInstancingEnabled(true);
        renderer.lighting.tonemap=false;
        renderer.lighting.fogDensity=0;
        modelReady=render::uploadModel(d,*model);
        check(modelReady,"native Model upload complete");
        if(!modelReady)return;
        for(auto& image:model->images)
            check(image.tex.valid(),"all four graph sources per material uploaded");
        for(int side=0;side<2;++side) {
            gfx::TextureStreamConfig limits;
            limits.gpuBudgetBytes=4096;limits.stagingBudgetBytes=256;
            limits.uploadBytesPerFrame=128;limits.uploadsPerFrame=2;
            streams[size_t(side)]=
                std::make_unique<gfx::VirtualTextureFileStreamer>(d,limits);
            gfx::VirtualTextureStreamConfig atlas;
            atlas.tilesX=atlas.tilesY=4;atlas.mipLevels=3;
            atlas.virtualLayers=2;atlas.physicalPages=2;
            atlas.physicalPageSize=4;atlas.physicalFormat=gfx::PixelFormat::RGBA8;
            atlas.pageTableBytesPerFlush=64;
            gfx::VirtualDiskPageConfig files;
            files.assetRoot=disk.root/(side==0?"left":"right");
            files.tilesX=files.tilesY=4;files.mipLevels=3;
            files.virtualLayers=2;files.pageSize=4;
            files.maxOutstanding=8;files.maxReadyBytes=256;
            const bool ready=streams[size_t(side)]->initialize(atlas,files,2);
            check(ready,"disk-backed physical atlas initialized");
            if(!ready)std::fprintf(stderr,"FAIL: disk VT initialize: %s\n",
                                    streams[size_t(side)]->lastError().c_str());
        }
        if(failed)return;
        check(streams[0]->physicalAtlas()!=streams[1]->physicalAtlas() &&
              streams[0]->pageTableTexture()!=streams[1]->pageTableTexture(),
              "two independently owned atlas/table domains");
    };
    auto installResolver=[&] {
        if(vtResolverInstalled)return;
        renderer.setModelVirtualTextureResolver(
          [&](const render::Model&,const render::Material& m,
              render::ModelVirtualTextureBinding& b) {
              const int side=m.name=="vt-left"?0:m.name=="vt-right"?1:-1;
              if(side<0 || !streams[size_t(side)] ||
                 !streams[size_t(side)]->ready())return false;
              const auto& stream=*streams[size_t(side)];
              b.physicalAtlas=stream.physicalAtlas();
              b.pageTable=stream.pageTableTexture();
              b.layer=side;b.gutterPixels=0.0f;
              b.addressU=side==0?gfx::SamplerAddressMode::Repeat:
                                 gfx::SamplerAddressMode::MirrorRepeat;
              b.addressV=gfx::SamplerAddressMode::ClampToEdge;
              return b.valid();
          });
        vtResolverInstalled=true;
    };
    auto request=[&](gfx::VirtualPageKey(*key)(int),bool pin,const char* what) {
        for(int side=0;side<2;++side) {
            auto ticket=streams[size_t(side)]->requestPage(key(side),pin);
            if(!ticket.valid()) {
                std::fprintf(stderr,"FAIL: %s side=%d: %s\n",what,side,
                             streams[size_t(side)]->lastError().c_str());
                failed=true;
            }
        }
    };
    auto validateCoarse=[&](bool detailed) {
        for(int side=0;side<2;++side) {
            const auto& stream=*streams[size_t(side)];
            check(stream.ready() && stream.exactResidentPage(coarse(side)) &&
                  stream.stats().pages.pinned==1,
                  "pinned coarse survives missing page and fine residency");
            check(stream.exactResidentPage(finePageKey(side))==detailed,
                  "exact tile (3,1) residency matches phase");
            validateTable(*streams[size_t(side)],side,
                          detailed?Pixel{1,0,0,255}:Pixel{0,0,2,255},
                          "CPU expected GPU page-table texel");
        }
    };
    auto data=[&](int pair,bool gpu)->const std::vector<uint8_t>& {
        return captures[size_t(1+pair*2+(gpu?1:0))].pixels;
    };
    auto finishPair=[&](int pair) {
        check(submitted[size_t(pair)],"real mixed Graph+VT GPU ICB submitted");
        compareCaptured(data(pair,false),data(pair,true),pairName(pair));
        if(failed)return;
        if(pair==0) {
            const size_t difference=changedChannels(captures[0].pixels,data(0,false));
            std::printf("FALLBACK ORACLE empty page table vs no-VT graph: changed_channels=%zu\n",
                        difference);
            check(difference==0,"unresident VT matches independent no-resolver tex1 fallback");
            request(coarse,true,"pin coarse disk fallback");
            phase=Phase::WaitCoarse;
        } else if(pair==1) {
            const size_t change=changedChannels(data(0,false),data(1,false),2);
            std::printf("FALLBACK ORACLE published coarse changes=%zu\n",change);
            check(change>100,"published coarse page changes fallback pixels");
            validateCoarse(false);
            request(missing,false,"queue intentionally missing page");
            phase=Phase::WaitMissing;
        } else if(pair==2) {
            const size_t change=changedChannels(data(1,false),data(2,false));
            std::printf("MISSING PAGE ORACLE coarse changed_channels=%zu\n",change);
            check(change==0,"missing page cannot change pinned coarse shading");
            validateCoarse(false);
            request(finePageKey,false,"queue fine tile (3,1) disk page");
            phase=Phase::WaitFine;
        } else {
            validateCoarse(true);
            independentWrapOracle(data(1,false),data(pair,false),
                                  pair==3 || pair==5,pairName(pair));
            // Check the same independent oracle on the ICB hardware
            // pixels, rather than only trusting direct-vs-ICB equality.
            independentWrapOracle(data(1,true),data(pair,true),
                                  pair==3 || pair==5,pairName(pair));
            if(pair<6)phase=Phase(int(Phase::PositiveDirect)+2*(pair-3));
            else {
                std::puts("PASS: Renderer3D Shader Graph disk VT missing-page "
                          "and Repeat/MirrorRepeat seam GPU ICB oracle");
                phase=Phase::Done;
                finished=true;
                platform::requestQuit();
            }
        }
        stable=0;
    };
    cb.frame=[&](gfx::Device& d,double) {
        if(failed || finished || frames>=700 ||
           std::chrono::steady_clock::now()>=deadline) {
            if(!failed && !finished)check(false,"native seam/fallback test timed out");
            platform::requestQuit();
            return;
        }
        ++frames;
        for(int side=0;side<2;++side) {
            if(!streams[size_t(side)]) {check(false,"streamer initialized");break;}
            auto& stream=*streams[size_t(side)];
            stream.pump(2,[&,side](gfx::VirtualDiskPageTicket,
                                   gfx::VirtualPageKey key,bool ok,
                                   const std::string& reason) {
                if(key==missing(side) && !ok && !reason.empty()) {
                    missingFailed[size_t(side)]=true;
                    std::printf("PASS: expected missing disk page failure side=%d: %s\n",
                                side,reason.c_str());
                }else if(!ok) {
                    std::fprintf(stderr,"FAIL: unexpected page error side=%d "
                                        "mip=%d x=%d y=%d layer=%d: %s\n",
                                 side,key.mip,key.x,key.y,key.layer,reason.c_str());
                    failed=true;
                }
            });
            check(stream.ready(),"missing-page read must not quarantine live atlas");
        }
        if(failed){platform::requestQuit();return;}
        if(phase==Phase::WaitCoarse || phase==Phase::WaitFine ||
           phase==Phase::WaitMissing) {
            bool ready=true;
            for(int side=0;side<2;++side) {
                auto& stream=*streams[size_t(side)];
                if(phase==Phase::WaitCoarse && !stream.exactResidentPage(coarse(side)))ready=false;
                if(phase==Phase::WaitFine && !stream.exactResidentPage(finePageKey(side)))ready=false;
                if(phase==Phase::WaitMissing && !missingFailed[size_t(side)])ready=false;
            }
            if(ready) {
                if(phase==Phase::WaitFine)validateCoarse(true);
                if(phase==Phase::WaitCoarse || phase==Phase::WaitMissing)
                    validateCoarse(false);
                phase=phase==Phase::WaitCoarse?Phase::CoarseDirect:
                      phase==Phase::WaitMissing?Phase::MissingDirect:
                                                Phase::NegativeDirect;
                stable=0;
            }
        }
        if(phase==Phase::EmptyDirect) {
            installResolver(); // now compare live empty page table vs no-VT control
            for(int side=0;side<2;++side) {
                check(!streams[size_t(side)]->exactResidentPage(coarse(side)),
                      "no page published before nonresident fallback test");
                validateTable(*streams[size_t(side)],side,Pixel{0,0,0,0},
                              "before publication all page-table entries invalid");
            }
        }
        const int pair=pairFor(phase);
        if(pair>=0) {
            const float u=uvUFor(pair);
            for(auto& mat:model->materials) {
                mat.uvOffset={u,0.375f};
                mat.uvScale={0.0001f,0.0001f};
            }
        }
        const bool gpu=phaseGpu(phase);
        renderer.setGpuAuthoredDrawEnabled(gpu);
        render::RTSCamera camera;
        camera.target={0,0,0};camera.distance=4.0f;
        camera.pitch=0.10f;camera.yaw=0.0f;
        renderer.beginScene(camera,kWidth,kHeight);
        render::ModelDraw draw;
        draw.model=model.get();draw.flags=render::kDrawStatic;
        renderer.drawModel(draw);
        gfx::PassDesc pass;
        renderer.render(pass);
        if(!renderer.modelError().empty()) {
            std::fprintf(stderr,"FAIL: production renderer: %s\n",
                         renderer.modelError().c_str());
            failed=true;
        }
        if(gpu && pair>=0) {
            const auto stats=renderer.gpuModelDrawStats();
            if(stats.shaderGraphIcbSubmissions>=1 &&
               stats.mixedGraphSourceSubmissions>=1 &&
               stats.perCommandVtExecutions>=1 &&
               stats.mixedVtExecutions>=1 &&
               stats.gpuEncodedCommands>=2 &&
               stats.gpuIcbRejected==0 && stats.gpuIcbExecuteFailed==0)
                submitted[size_t(pair)]=true;
            if(stable==2)
                std::printf("WRAP GPU %s: graph=%u mixedGraph=%u vt=%u "
                            "mixedVt=%u commands=%u rejected=%u errors=%u\n",
                            pairName(pair),stats.shaderGraphIcbSubmissions,
                            stats.mixedGraphSourceSubmissions,
                            stats.perCommandVtExecutions,stats.mixedVtExecutions,
                            stats.gpuEncodedCommands,stats.gpuIcbRejected,
                            stats.gpuIcbExecuteFailed);
        }
        const int shot=phase==Phase::NoVtControl?0:
                       pair>=0?1+pair*2+(gpu?1:0):-1;
        if(shot>=0 && ++stable>=3) {
            auto& capture=captures[size_t(shot)];
            if(!capture.requested) {
                capture.requested=true;
                d.captureNextFrame([&,shot](int width,int height,std::vector<uint8_t> pixels) {
                    check(width==kWidth && height==kHeight,
                          "native framebuffer captures exact 192x128 extent");
                    captures[size_t(shot)].pixels=std::move(pixels);
                    captures[size_t(shot)].ready=true;
                });
            }
        }
        d.endPass();
        if(shot>=0 && captures[size_t(shot)].ready) {
            if(phase==Phase::NoVtControl) {
                phase=Phase::EmptyDirect;
                stable=0;
            }else if(!gpu) {
                phase=Phase(int(phase)+1); // direct -> ICB
                stable=0;
            }else {
                finishPair(pair);
            }
        }
    };
    cb.shutdown=[&] {
        if(device) {
            renderer.clearModelVirtualTextureResolver();
            if(rendererReady)renderer.shutdown();
            if(modelReady)render::releaseModel(*device,*model);
            for(auto& stream:streams)
                if(stream)stream->shutdown();
        }
    };
    platform::run(config,cb);
    check(finished,"fallback and four out-of-range seam states completed");
    return failed?1:0;
}
