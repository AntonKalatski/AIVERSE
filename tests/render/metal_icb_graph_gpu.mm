// Experimental on-device Metal GPU-authored ICB / Shader Graph source ABI.
// Explicitly opt-in: this path may be unsafe on older AGX drivers.
// Separate process from ordinary Metal texture acceptance. No mock GPU.
// This is a backend resource-binding test, not the full Model Shader Graph
// renderer, and cannot establish all material graph compilation parity.
#include "engine/gfx/Gfx.h"
#include "engine/platform/Platform.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Foundation/Foundation.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string& out) {
    out.clear();
    return false;
}
}

using namespace aiverse::gfx;
namespace {
int failures=0;
void require(bool ok,const char* message) {
    if (!ok) { ++failures;std::fprintf(stderr,"FAIL: %s\n",message); }
}
using Pixel=std::array<uint8_t,4>;
constexpr int kWidth=4,kHeight=4;
std::vector<uint8_t> solid(Pixel value) {
    std::vector<uint8_t> bytes(kWidth*kHeight*4);
    for(size_t i=0;i<bytes.size();i+=4)
        std::copy(value.begin(),value.end(),bytes.begin()+std::ptrdiff_t(i));
    return bytes;
}
constexpr const char* kGraphAbiShader=R"MSL(
#define AIVERSE_MODEL_SSR 1
#define AIVERSE_SG_ICB_GRAPH 1
#define AIVERSE_SG_TEXTURE_COUNT 1
#include <metal_stdlib>
using namespace metal;

// IDs must match GfxMetal.mm's fragment argument encoder slot*2 layout.
// The ordinary pipeline uses direct bindings; the GPU-ICB variant
// binds this structure separately for every indexed command.
struct IcbGraphSources {
    texture2d<float> tex0 [[id(0)]];
    sampler s0 [[id(1)]];
    texture2d<float> tex1 [[id(2)]];
    sampler s1 [[id(3)]];
    texture2d<float> tex2 [[id(4)]];
    sampler s2 [[id(5)]];
    texture2d<float> tex3 [[id(6)]];
    sampler s3 [[id(7)]];
    texture2d<float> tex4 [[id(8)]];
    sampler s4 [[id(9)]];
    texture2d<float> tex5 [[id(10)]];
    sampler s5 [[id(11)]];
    texture2d<float> tex6 [[id(12)]];
    sampler s6 [[id(13)]];
    texture2d<float> tex9 [[id(18)]];
    sampler s9 [[id(19)]];
    texture2d<float> tex10 [[id(20)]];
    sampler s10 [[id(21)]];
    texture2d<float> tex11 [[id(22)]];
    sampler s11 [[id(23)]];
    texture2d<float> tex12 [[id(24)]];
    sampler s12 [[id(25)]];
    texture2d<float> tex13 [[id(26)]];
    sampler s13 [[id(27)]];
#if AIVERSE_SG_TEXTURE_COUNT == 4
    texture2d<float> tex14 [[id(28)]];
    sampler s14 [[id(29)]];
    texture2d<float> tex15 [[id(30)]];
    sampler s15 [[id(31)]];
    texture2d<float> tex16 [[id(32)]];
#endif
};

struct GraphVertexOut {
    float4 position [[position]];
};
vertex GraphVertexOut vs_main(device const float4* vertices [[buffer(0)]],
                               uint vid [[vertex_id]]) {
    GraphVertexOut out;
    out.position=vertices[vid];
    return out;
}
fragment float4 fs_main(
    GraphVertexOut in [[stage_in]],
    constant float4* u2 [[buffer(3)]],
#if defined(AIVERSE_ICB_PBR)
    constant IcbGraphSources& source [[buffer(8)]]
#else
    texture2d<float> base [[texture(1)]],
    sampler baseSampler [[sampler(1)]],
    texture2d<float> graph [[texture(13)]],
    sampler graphSampler [[sampler(13)]]
#endif
) {
#if defined(AIVERSE_ICB_PBR)
    float4 ordinary=source.tex1.sample(source.s1,float2(0.5));
    float4 graphColor=source.tex13.sample(source.s13,float2(0.5));
#else
    float4 ordinary=base.sample(baseSampler,float2(0.5));
    float4 graphColor=graph.sample(graphSampler,float2(0.5));
#endif
#if AIVERSE_SG_TEXTURE_COUNT == 4 && defined(AIVERSE_ICB_PBR)
    // Exactly as in the production graph ICB path, texture slot 16 uses
    // a constexpr sampler: Metal fragment dynamic sampler slots stop at 15.
    constexpr sampler s16(address::repeat, filter::linear);
    graphColor += source.tex14.sample(source.s14,float2(0.5));
    graphColor += source.tex15.sample(source.s15,float2(0.5));
    graphColor += source.tex16.sample(s16,float2(0.5));
#endif
    return float4((ordinary.rgb+graphColor.rgb)*u2[0].rgb,1.0);
}
)MSL";

bool readPixels(Device& dev,TextureHandle target,std::vector<uint8_t>& output) {
    bool done=false,accepted=false;
    TextureReadbackResult result;
    dev.beginFrame(16,16);
    accepted=dev.readTextureAsync(target,{},[&](TextureReadbackResult pixels) {
        result=std::move(pixels);
        done=true;
    });
    dev.endFrame();
    if (!accepted) {
        std::fprintf(stderr,"FAIL: GPU ICB render target readback rejected: %s\n",
                     dev.lastError().c_str());
        return false;
    }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while (!done && std::chrono::steady_clock::now()<deadline) {
        dev.beginFrame(16,16);
        dev.endFrame();
        if (!done) std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    if (!done || !dev.gpuHealthy() || !result.success() ||
        result.width!=kWidth || result.height!=kHeight ||
        result.format!=PixelFormat::RGBA8 ||
        result.pixels.size()!=kWidth*kHeight*4u) {
        std::fprintf(stderr,"FAIL: GPU ICB readback timeout / unhealthy GPU / wrong shape\n");
        return false;
    }
    output=std::move(result.pixels);
    return true;
}

int run(uint32_t graphCount) {
    const bool fourSources=graphCount==4;
    require(graphCount==1 || fourSources,"only the 1- and 4-source GPU ICB layouts are accepted");
    id<MTLDevice> metal=MTLCreateSystemDefaultDevice();
    if (!metal) {
        std::fprintf(stderr,"FAIL: Metal GPU unavailable; explicit ICB test cannot skip\n");
        return 2;
    }
    CAMetalLayer* layer=[CAMetalLayer layer];
    layer.device=metal;
    layer.pixelFormat=MTLPixelFormatBGRA8Unorm;
    layer.frame=CGRectMake(0,0,16,16);
    layer.drawableSize=CGSizeMake(16,16);
    auto dev=createMetalDevice((__bridge void*)layer);
    if (!dev || !dev->supportsGpuEncodedIcb()) {
        std::fprintf(stderr,
            "FAIL: experimental GPU encoded ICB unsupported / opt-in flags missing\n");
        return 2; // Fail closed; never call a CPU fallback "GPU ICB pass".
    }
    std::printf("Metal GPU ICB graph ABI acceptance: %s, graph sources=%u\n",
                dev->info().name.c_str(),unsigned(graphCount));
    const auto start=std::chrono::steady_clock::now();

    std::vector<TextureHandle> textures;
    auto createTex=[&](Pixel color,bool graphSource=false)->TextureHandle {
        auto bytes=solid(color);
        TextureDesc d;
        d.width=d.height=kWidth;
        d.mipLevels=1;
        d.filter=graphSource?Filter::Linear:Filter::Nearest;
        d.repeat=graphSource;
        d.data=bytes.data();
        const auto h=dev->createTexture(d);
        if (h.valid()) textures.push_back(h);
        return h;
    };
    const TextureHandle dummy=createTex({0,0,0,255});
    const TextureHandle baseA=createTex({90,0,0,255});
    const TextureHandle graphA=createTex({0,0,160,255},true);
    const TextureHandle baseB=createTex({0,140,0,255});
    const TextureHandle graphB=createTex({200,0,0,255},true);
    // The 4-source path covers the upper Shader Graph texture slot 16
    // and per-command argument IDs 28, 30 and 32, not just tex13.
    std::array<TextureHandle,3> extraA{},extraB{};
    if (fourSources) {
        extraA={createTex({0,20,0,255},true),
                createTex({0,0,20,255},true),
                createTex({10,0,0,255},true)};
        extraB={createTex({0,20,0,255},true),
                createTex({0,0,30,255},true),
                createTex({20,0,0,255},true)};
        for (const auto t:extraA) require(t.valid(),"left 4-source graph texture allocated");
        for (const auto t:extraB) require(t.valid(),"right 4-source graph texture allocated");
    }
    require(dummy.valid() && baseA.valid() && graphA.valid() &&
            baseB.valid() && graphB.valid(),
            "native GPU per-command material textures allocated");

    TextureDesc rt;
    rt.width=rt.height=kWidth;
    rt.renderTarget=true;
    rt.mipLevels=1;
    const auto color=dev->createTexture(rt);
    rt.format=PixelFormat::Depth;
    const auto depth=dev->createTexture(rt);
    if (color.valid()) textures.push_back(color);
    if (depth.valid()) textures.push_back(depth);
    ShaderDesc shDesc;
    std::string shaderSource=kGraphAbiShader;
    if (fourSources) {
        constexpr const char* marker="#define AIVERSE_SG_TEXTURE_COUNT 1";
        const auto pos=shaderSource.find(marker);
        if (pos==std::string::npos)
            require(false,"ICB Shader Graph source marker missing");
        else shaderSource.replace(pos,std::char_traits<char>::length(marker),
                                  "#define AIVERSE_SG_TEXTURE_COUNT 4");
    }
    // ShaderDesc stores a non-owning const char*. Keep this string alive
    // through createShader(), which synchronously copies/compiles MSL.
    shDesc.msl=shaderSource.c_str();
    shDesc.label="TextureIcbGraphAbiAcceptance";
    shDesc.textureSlots=fourSources?17:14;
    shDesc.uniformSlots=8;
    const auto shader=dev->createShader(shDesc);
    PipelineDesc pDesc;
    pDesc.shader=shader;
    pDesc.attrCount=0;
    pDesc.stride=0;
    pDesc.indexType=IndexType::UInt32;
    pDesc.cull=CullMode::None;
    pDesc.depthTest=false;
    pDesc.depthWrite=false;
    pDesc.colorFormat=PixelFormat::RGBA8;
    pDesc.label="TextureIcbGraphAbiAcceptance";
    const auto pipeline=shader.valid()?dev->createPipeline(pDesc):PipelineHandle{};
    require(color.valid() && depth.valid() && shader.valid() &&
            pipeline.valid(),"native ICB graph ABI shader and render pipeline created");

    // Two indexed quads: the left and right halves of the target.
    // GPU ICB's compute encoder selects a distinct source command
    // and fragment argument record for each one, no CPU draw fallback.
    const std::array<std::array<float,4>,8> verts{{
        {{-1,-1,0,1}},{{0,-1,0,1}},{{-1,1,0,1}},{{0,1,0,1}},
        {{0,-1,0,1}},{{1,-1,0,1}},{{0,1,0,1}},{{1,1,0,1}}
    }};
    const std::array<uint32_t,12> indices{
        0,1,2,1,3,2, 4,5,6,5,7,6
    };
    const std::array<DrawIndexedIndirectCommand,2> commands{{
        {6,1,0,0,0},{6,1,6,0,0}
    }};
    auto makeBuffer=[&](BufferType type,const void* data,size_t bytes) {
        BufferDesc desc;
        desc.type=type;
        desc.size=bytes;
        desc.data=data;
        return dev->createBuffer(desc);
    };
    const auto vertex=makeBuffer(BufferType::Vertex,verts.data(),sizeof(verts));
    const auto index=makeBuffer(BufferType::Index,indices.data(),sizeof(indices));
    const auto arguments=makeBuffer(BufferType::Storage,commands.data(),sizeof(commands));
    require(vertex.valid() && index.valid() && arguments.valid(),
            "actual Metal GPU ICB inputs include storage-authored commands");
    std::array<GpuIcbPbrMaterial,2> materials{};
    for(auto& m:materials) {
        m.textures.fill(dummy);
        m.graphTextureCount=graphCount;
    }
    materials[0].textures[0]=baseA;
    materials[0].graphTextures[0]=graphA;
    materials[0].u2[0]=1.0f;materials[0].u2[1]=1.0f;
    materials[0].u2[2]=1.0f;materials[0].u2[3]=1.0f;
    materials[1].textures[0]=baseB;
    materials[1].graphTextures[0]=graphB;
    if (fourSources) {
        for (size_t j=0;j<3;++j) {
            materials[0].graphTextures[j+1]=extraA[j];
            materials[1].graphTextures[j+1]=extraB[j];
        }
    }
    materials[1].u2[0]=0.5f;materials[1].u2[1]=1.0f;
    materials[1].u2[2]=1.0f;materials[1].u2[3]=1.0f;
    require(materials[0].valid() && materials[1].valid(),
            "PBR U2 and graph source descriptors valid");
    std::array<float,72> instanceRecords{};
    const uint32_t sourceSlots[2]={0,1};

    if (failures==0) {
        dev->beginFrame(16,16);
        const auto token=dev->prepareGpuEncodedIndexedIcbWithPbrMaterials(
            arguments,vertex,index,sourceSlots,2,
            instanceRecords.data(),sizeof(instanceRecords),
            materials.data());
        require(token!=0,"GPU compute prepared ICB with two distinct graph sources");
        dev->finishGpuEncodedIcbPreparation();
        RenderTargetPassDesc pass;
        pass.colorCount=1;
        pass.colors[0].texture=color;
        pass.colors[0].clear=true;
        pass.depth.texture=depth;
        pass.depth.clear=true;
        if (!dev->beginRenderTargets(pass)) {
            require(false,"ICB graph render target pass started");
        } else {
            dev->setViewport({0,0,kWidth,kHeight});
            dev->setPipeline(pipeline);
            dev->setVertexBuffer(vertex);
            dev->setIndexBuffer(index);
            // Non-PBR state comes from parent's current pass. U2 is
            // *per-command* and was captured in the material entries.
            std::array<float,36> uniform{};
            for(int slot=1;slot<8;++slot)
                dev->setUniforms(slot,uniform.data(),sizeof(uniform));
            for(int slot : {0,9,10,11,12})dev->setTexture(slot,dummy);
            dev->setTexture(1,baseA);
            dev->setTexture(13,graphA);
            dev->setGpuIcbUniformBinding(true);
            if (token!=0) {
                const bool executed=dev->executeGpuEncodedIndexedIcb(token);
                if (!executed) {
                    ++failures;
                    std::fprintf(stderr,
                        "FAIL: GPU ICB graph ABI execution refused: %s\n",
                        dev->lastError().c_str());
                } else {
                    std::puts("PASS: production Metal GPU ICB executed two material commands");
                }
            }
            dev->setGpuIcbUniformBinding(false);
            dev->endPass();
        }
        dev->endFrame();
        if (token!=0 && failures==0) {
            std::vector<uint8_t> output;
            if (!readPixels(*dev,color,output)) ++failures;
            else {
                const Pixel left=fourSources?Pixel{100,20,180,255}:Pixel{90,0,160,255};
                const Pixel right=fourSources?Pixel{110,160,30,255}:Pixel{100,140,0,255};
                bool match=true;
                for(int y=0;y<kHeight;++y)
                    for(int x=0;x<kWidth;++x) {
                        const Pixel& expected=x<2?left:right;
                        for(int channel=0;channel<4;++channel) {
                            const int found=int(output[size_t(y*kWidth+x)*4+size_t(channel)]);
                            if (std::abs(found-int(expected[size_t(channel)]))>1) {
                                match=false;
                                std::fprintf(stderr,
                                    "FAIL: GPU ICB pixel (%d,%d) channel %d expected=%d observed=%d\n",
                                    x,y,channel,int(expected[size_t(channel)]),found);
                            }
                        }
                    }
                require(match,
                    "actual ICB fragment pixels have distinct per-command PBR/graph textures and U2");
                if (match) std::printf(
                    "PASS: GPU ICB graph sources=%u, per-command textures/U2 pixel parity\n",
                    unsigned(graphCount));
            }
        }
    }

    if (arguments.valid())dev->destroyBuffer(arguments);
    if (index.valid())dev->destroyBuffer(index);
    if (vertex.valid())dev->destroyBuffer(vertex);
    if (pipeline.valid())dev->destroyPipeline(pipeline);
    if (shader.valid())dev->destroyShader(shader);
    for(auto texture:textures)dev->destroyTexture(texture);

    std::printf("TIMING Metal GPU ICB graph ABI acceptance_ms=%.3f\n",
        std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-start).count());
    return failures?1:0;
}
}

int main() {
    @autoreleasepool {
        const char* enabled=std::getenv("AIVERSE_EXPERIMENTAL_METAL_ICB");
        const char* authored=std::getenv("AIVERSE_GPU_ENCODED_ICB");
        const char* materials=std::getenv("AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR");
        if (!enabled || std::string(enabled)!="1" ||
            !authored || std::string(authored)!="1" ||
            !materials || std::string(materials)!="1") {
            std::fprintf(stderr,
                "FAIL: test requires all three explicit experimental Metal ICB flags\n");
            return 2;
        }
        const int oneSource=run(1);
        return oneSource==0?run(4):oneSource;
    }
}
