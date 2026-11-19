// On-device macOS Metal RHI texture acceptance (opt-in).
// This is NOT a mock: createMetalDevice, updateTextureRegion and
// readTextureAsync run through the same backend as AIVERSE applications.
// Call only on a Mac with a real Metal device. No silent device skip.
#include "engine/gfx/Gfx.h"
#include "engine/platform/Platform.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Foundation/Foundation.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// MetalShaderAssets probes optional precompiled shader archives through this
// platform function. This texture test creates no shaders or game content.
namespace aiverse::platform {
bool readContentFile(const std::string&, std::string& out) {
    out.clear();
    return false;
}
}

using namespace aiverse::gfx;

namespace {
int failures = 0;
void check(bool ok, const char* why) {
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", why); }
}
using Color = std::array<uint8_t, 4>;
using Clock = std::chrono::steady_clock;
double elapsedMs(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double,std::milli>(end-start).count();
}
struct ProbeTiming {
    double submitMs = 0;
    double completeMs = 0;
    double compareMs = 0;
    double gpuCommandMs = 0;
    bool gpuCommandValid = false;
    unsigned frames = 0;
};
uint64_t nextGpuTimingTag() {
    static uint64_t index=0;
    return 0x54584d4554414c00ULL + ++index;
}

std::vector<uint8_t> solid(int w, int h, Color color) {
    std::vector<uint8_t> rgba(size_t(w)*size_t(h)*4);
    for (size_t i=0; i<rgba.size(); i+=4)
        std::copy(color.begin(), color.end(), rgba.begin()+std::ptrdiff_t(i));
    return rgba;
}
void append(std::vector<uint8_t>& dst, const std::vector<uint8_t>& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}
struct Read {
    TextureHandle texture;
    TextureReadbackDesc subresource;
    int width = 0, height = 0;
    std::vector<uint8_t> expected;
    std::string label;
    int tolerance = 0; // zero for blits, <=1 LSB for sampled float->UNORM
    TextureReadbackResult output;
    bool done = false;
};
void verifyReads(Device& dev, const std::vector<std::shared_ptr<Read>>& probes,
                 const char* group) {
    // Report wall-clock scheduling separately from the GPU command interval.
    // The latter measures the entire Metal command buffer, not only blit
    // throughput; short transfers cannot be benchmarked with CTest time.
    ProbeTiming timing;
    const auto submitStart=Clock::now();
    dev.beginFrame(16,16);
    bool queued = true;
    for (const auto& p : probes) {
        const bool ok = dev.readTextureAsync(p->texture,p->subresource,
            [p](TextureReadbackResult result) {
                p->output=std::move(result);
                p->done=true;
            });
        if (!ok) {
            queued=false;
            std::fprintf(stderr,"FAIL: %s readback rejected: %s\n",
                         p->label.c_str(),dev.lastError().c_str());
        }
    }
    const uint64_t timingTag=nextGpuTimingTag();
    const bool gpuTimingRequested=dev.timeGpuFrame(timingTag);
    dev.endFrame();
    const auto submitEnd=Clock::now();
    timing.submitMs=elapsedMs(submitStart,submitEnd);
    check(queued, "all native GPU readbacks accepted");
    // Never report a skipped GPU test as passed. A timeout/failure is red.
    const auto deadline=Clock::now()+std::chrono::seconds(10);
    while (Clock::now()<deadline) {
        dev.beginFrame(16,16);
        dev.endFrame();
        ++timing.frames;
        const bool all=std::all_of(probes.begin(),probes.end(),
            [](const auto& p) {return p->done;});
        if (all) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto completionEnd=Clock::now();
    timing.completeMs=elapsedMs(submitEnd,completionEnd);
    if (gpuTimingRequested) {
        for (const auto& gpu : dev.takeGpuTimings()) {
            if (gpu.tag == timingTag && gpu.valid) {
                timing.gpuCommandMs=gpu.milliseconds;
                timing.gpuCommandValid=true;
            }
        }
    }
    check(dev.gpuHealthy(),"Metal command buffers completed without GPU error");
    const auto compareStart=Clock::now();
    for (const auto& p : probes) {
        if (!p->done) {
            ++failures;
            std::fprintf(stderr,"FAIL: %s GPU readback timed out\n",p->label.c_str());
            continue;
        }
        const bool shape=p->output.success() &&
            p->output.format==PixelFormat::RGBA8 &&
            p->output.width==p->width && p->output.height==p->height &&
            p->output.pixels.size()==p->expected.size();
        if (!shape) {
            ++failures;
            std::fprintf(stderr,"FAIL: %s GPU readback has wrong format/extent/bytes\n",
                         p->label.c_str());
            continue;
        }
        const auto mismatch=std::mismatch(
            p->expected.begin(),p->expected.end(),p->output.pixels.begin(),
            [&](uint8_t want,uint8_t got) {
                return std::abs(int(want)-int(got)) <= p->tolerance;
            });
        if (mismatch.first!=p->expected.end()) {
            ++failures;
            const size_t at=size_t(mismatch.first-p->expected.begin());
            std::fprintf(stderr,
                "FAIL: %s GPU bytes differ at %zu (wanted %u, got %u, tolerance %d)\n",
                p->label.c_str(),at,unsigned(*mismatch.first),
                unsigned(p->output.pixels[at]),p->tolerance);
        } else std::printf("PASS: %s (%dx%d GPU RGBA8 bytes; tolerance %d)\n",
                           p->label.c_str(),p->width,p->height,p->tolerance);
    }
    timing.compareMs=elapsedMs(compareStart,Clock::now());
    std::printf("TIMING %s readbacks=%zu submit_ms=%.3f callback_wait_ms=%.3f "
                "poll_frames=%u compare_ms=%.3f gpu_command_ms=",
                group,probes.size(),timing.submitMs,timing.completeMs,
                timing.frames,timing.compareMs);
    if (timing.gpuCommandValid) std::printf("%.3f\n",timing.gpuCommandMs);
    else std::puts("unavailable");
}
std::shared_ptr<Read> probe(TextureHandle h,int mip,int layer,int w,int height,
                            std::vector<uint8_t> expected,std::string label,
                            int tolerance=0) {
    auto p=std::make_shared<Read>();
    p->texture=h; p->subresource={mip,layer};
    p->width=w; p->height=height; p->expected=std::move(expected);
    p->label=std::move(label);
    p->tolerance=tolerance;
    return p;
}
void volume(Device& dev) {
    const auto setupStart=Clock::now();
    TextureDesc d;
    d.dimension=TextureDimension::Texture3D;
    d.width=4;d.height=4;d.depth=4;d.mipLevels=2;d.filter=Filter::Nearest;
    std::vector<uint8_t> base;
    for(int z=0;z<4;++z) append(base,solid(4,4,{uint8_t(20+z*13),42,67,255}));
    d.data=base.data();
    const auto h=dev.createTexture(d);
    check(h.valid(),"Metal creates RGBA8 Texture3D with initial depth-plane data");
    if (!h.valid()) return;

    const auto slice=solid(2,2,{191,92,30,255});
    TextureUploadDesc mip;
    mip.mipLevel=1;mip.x=0;mip.y=0;mip.z=1;
    mip.width=2;mip.height=2;mip.depth=1;
    mip.rowPitchBytes=8;mip.slicePitchBytes=16;mip.data=slice.data();
    check(dev.updateTextureRegion(h,mip),
          "Metal uploads only z=1 of mip 1 in Texture3D");

    const auto patch=solid(2,2,{225,17,88,255});
    TextureUploadDesc sub;
    sub.mipLevel=0;sub.x=1;sub.y=1;sub.z=2;
    sub.width=2;sub.height=2;sub.depth=1;
    sub.rowPitchBytes=8;sub.slicePitchBytes=16;sub.data=patch.data();
    check(dev.updateTextureRegion(h,sub),
          "Metal updates partial x/y and z=2 of mip 0");

    auto invalid=mip;
    invalid.z=2; // mip-1 depth is two, so z=2 is outside.
    check(!dev.updateTextureRegion(h,invalid),"Texture3D rejects out-of-range mip z");
    invalid=mip;invalid.rowPitchBytes=7;
    check(!dev.updateTextureRegion(h,invalid),"Texture3D rejects short row pitch");

    const auto untouched=solid(4,4,{20,42,67,255});
    auto changed=solid(4,4,{46,42,67,255});
    for (int y=1;y<=2;++y) for(int x=1;x<=2;++x)
        std::copy_n(patch.data()+size_t((y-1)*2+(x-1))*4,4,
                    changed.data()+size_t(y*4+x)*4);
    std::printf("TIMING Texture3D create_and_upload_ms=%.3f\n",
                elapsedMs(setupStart,Clock::now()));
    verifyReads(dev,{
        probe(h,0,0,4,4,untouched,"3D base z0 unchanged"),
        probe(h,0,2,4,4,changed,"3D base z2 partial rectangle"),
        probe(h,1,1,2,2,slice,"3D mip1 z1 partial depth selection")
    },"Texture3D");
    const auto before=dev.textureResources();
    check(std::any_of(before.begin(),before.end(),[&](const auto& r) {
        return r.handle==h && r.dimension==TextureDimension::Texture3D &&
            r.depth==4 && r.mipLevels==2;
    }),"Metal resource inventory reports live 3D texture");
    dev.destroyTexture(h);
    const auto after=dev.textureResources();
    check(std::none_of(after.begin(),after.end(),[&](const auto& r) {
        return r.handle==h;
    }),"Metal resource inventory removes destroyed 3D texture");
}
void layered(Device& dev, TextureDimension dimension,int layers,
             const char* name) {
    const auto setupStart=Clock::now();
    TextureDesc d;
    d.dimension=dimension;d.width=4;d.height=4;
    d.arrayLayers=layers;d.mipLevels=2;d.filter=Filter::Nearest;
    std::vector<uint8_t> base;
    for(int i=0;i<layers;++i)
        append(base,solid(4,4,{uint8_t(10+i*9),uint8_t(70+i),33,255}));
    d.data=base.data();
    const auto h=dev.createTexture(d);
    if (!h.valid()) {
        ++failures;
        std::fprintf(stderr,"FAIL: Metal %s allocation: %s\n",
                     name,dev.lastError().c_str());
        return;
    }
    const int last=layers-1;
    const auto mipData=solid(2,2,{156,49,uint8_t(layers),255});
    TextureUploadDesc mip;
    mip.mipLevel=1;mip.arrayLayer=last;
    mip.width=2;mip.height=2;mip.depth=1;
    mip.rowPitchBytes=8;mip.slicePitchBytes=16;mip.data=mipData.data();
    check(dev.updateTextureRegion(h,mip),
          "Metal uploads selected array/cube face and authored mip");

    std::printf("TIMING %s create_and_upload_ms=%.3f\n",
                name,elapsedMs(setupStart,Clock::now()));
    verifyReads(dev,{
        probe(h,0,0,4,4,solid(4,4,{10,70,33,255}),
              std::string(name)+" face/layer0"),
        probe(h,0,last,4,4,
              solid(4,4,{uint8_t(10+last*9),uint8_t(70+last),33,255}),
              std::string(name)+" last face/layer"),
        probe(h,1,last,2,2,mipData,
              std::string(name)+" authored mip1")
    },name);
    dev.destroyTexture(h);
}

// This is a real fragment texture fetch, not a CPU-side upload or Metal blit.
// It closes the acceptance gap for 3D volume + manual mip + partial z updates.
constexpr const char* kVolumeSampleMSL = R"MSL(
#include <metal_stdlib>
using namespace metal;
struct VolumeVertexOut { float4 position [[position]]; };
vertex VolumeVertexOut vs_main(uint vertexId [[vertex_id]]) {
    float2 positions[3]={float2(-1.0,-1.0),float2(3.0,-1.0),
                          float2(-1.0,3.0)};
    VolumeVertexOut out;
    out.position=float4(positions[vertexId],0.0,1.0);
    return out;
}
fragment float4 fs_main(VolumeVertexOut in [[stage_in]],
                        texture3d<float> tex0 [[texture(0)]],
                        sampler s0 [[sampler(0)]],
                        constant float4& coord [[buffer(1)]]) {
    return tex0.sample(s0,coord.xyz,level(coord.w));
}
)MSL";

void shaderSampleVolume(Device& dev) {
    const auto start=Clock::now();
    TextureDesc src;
    src.dimension=TextureDimension::Texture3D;
    src.width=4;src.height=4;src.depth=4;src.mipLevels=2;
    src.filter=Filter::Nearest;
    std::vector<uint8_t> base;
    for(int z=0;z<4;++z)
        append(base,solid(4,4,{uint8_t(11+z*35),55,77,255}));
    src.data=base.data();
    const auto volumeTex=dev.createTexture(src);
    check(volumeTex.valid(),"3D shader sample volume allocated");
    if (!volumeTex.valid()) return;
    const auto patch=solid(2,2,{225,17,88,255});
    TextureUploadDesc part;
    part.x=1;part.y=1;part.z=2;
    part.width=2;part.height=2;part.depth=1;
    part.rowPitchBytes=8;part.slicePitchBytes=16;part.data=patch.data();
    check(dev.updateTextureRegion(volumeTex,part),
          "3D shader source partial Z rectangle uploaded");
    const auto mipData=solid(2,2,{191,92,30,255});
    TextureUploadDesc nextMip;
    nextMip.mipLevel=1;nextMip.z=1;
    nextMip.width=2;nextMip.height=2;nextMip.depth=1;
    nextMip.rowPitchBytes=8;nextMip.slicePitchBytes=16;
    nextMip.data=mipData.data();
    check(dev.updateTextureRegion(volumeTex,nextMip),
          "3D shader source authored mip1 Z plane uploaded");

    TextureDesc color;
    color.width=4;color.height=4;color.renderTarget=true;
    color.format=PixelFormat::RGBA8;color.mipLevels=1;
    TextureDesc depth=color;
    depth.format=PixelFormat::Depth;
    const auto target=dev.createTexture(color);
    const auto depthTarget=dev.createTexture(depth);
    ShaderDesc shader;
    shader.msl=kVolumeSampleMSL;
    shader.textureSlots=1;shader.uniformSlots=1;
    shader.label="MetalVolumeSampleAcceptance";
    const auto sh=dev.createShader(shader);
    PipelineDesc desc;
    desc.shader=sh;
    desc.attrCount=0; desc.stride=0;
    desc.indexType=IndexType::None;
    desc.cull=CullMode::None;
    desc.depthTest=false;desc.depthWrite=false;
    desc.colorFormat=PixelFormat::RGBA8;
    desc.colorAttachmentCount=1;
    desc.label="MetalVolumeSampleAcceptance";
    const auto pipeline=sh.valid() ? dev.createPipeline(desc) : PipelineHandle{};
    if (!target.valid() || !depthTarget.valid() || !sh.valid() || !pipeline.valid()) {
        ++failures;
        std::fprintf(stderr,"FAIL: native Metal fragment sampling setup: %s\n",
                     dev.lastError().c_str());
    } else {
        std::printf("TIMING Texture3D shader_pipeline_setup_ms=%.3f\n",
                    elapsedMs(start,Clock::now()));
        struct Sample {
            std::array<float,4> coord;
            Color expected;
            const char* label;
        };
        const Sample cases[] = {
            {{0.125f,0.125f,0.125f,0.0f},{11,55,77,255},
             "3D fragment samples original z0"},
            {{0.375f,0.375f,0.625f,0.0f},{225,17,88,255},
             "3D fragment samples patched z2"},
            {{0.75f,0.75f,0.75f,1.0f},{191,92,30,255},
             "3D fragment samples explicit mip1 z1"}
        };
        for(const auto& sample:cases) {
            dev.beginFrame(16,16);
            RenderTargetPassDesc pass;
            pass.colorCount=1;
            pass.colors[0].texture=target;
            pass.colors[0].clear=true;
            pass.depth.texture=depthTarget;
            pass.depth.clear=true;
            const bool active=dev.beginRenderTargets(pass);
            if (!active) {
                ++failures;
                std::fprintf(stderr,"FAIL: 3D fragment render target: %s\n",
                             dev.lastError().c_str());
            } else {
                dev.setViewport({0,0,4,4});
                dev.setPipeline(pipeline);
                dev.setTexture(0,volumeTex);
                dev.setUniforms(0,sample.coord.data(),sizeof(sample.coord));
                dev.draw(3);
                dev.endPass();
            }
            dev.endFrame();
            if (active) {
                verifyReads(dev,{
                    probe(target,0,0,4,4,solid(4,4,sample.expected),
                          sample.label,1)
                },"Texture3D fragment sampling");
            }
        }
    }
    if (pipeline.valid()) dev.destroyPipeline(pipeline);
    if (sh.valid()) dev.destroyShader(sh);
    if (target.valid()) dev.destroyTexture(target);
    if (depthTarget.valid()) dev.destroyTexture(depthTarget);
    dev.destroyTexture(volumeTex);
    std::printf("TIMING Texture3D fragment_sampling_total_ms=%.3f\n",
                elapsedMs(start,Clock::now()));
}

} // namespace

int main() {
    @autoreleasepool {
        const auto testStart=Clock::now();
        id<MTLDevice> metal=MTLCreateSystemDefaultDevice();
        if (!metal) {
            std::fprintf(stderr,"FAIL: no Metal GPU available; opt-in test cannot skip\n");
            return 2;
        }
        CAMetalLayer* layer=[CAMetalLayer layer];
        layer.device=metal;
        layer.pixelFormat=MTLPixelFormatBGRA8Unorm;
        layer.frame=CGRectMake(0,0,16,16);
        layer.drawableSize=CGSizeMake(16,16);
        auto device=createMetalDevice((__bridge void*)layer);
        if (!device || device->info().backend != Backend::Metal) {
            std::fprintf(stderr,"FAIL: AIVERSE Metal backend unavailable\n");
            return 2;
        }
        const auto deviceReady=Clock::now();
        std::printf("Metal GPU acceptance device: %s\n",device->info().name.c_str());
        std::printf("TIMING Metal device_and_layer_init_ms=%.3f\n",
                    elapsedMs(testStart,deviceReady));
        volume(*device);
        layered(*device,TextureDimension::Texture2DArray,2,"Texture2DArray");
        layered(*device,TextureDimension::Cube,6,"Cube");
        layered(*device,TextureDimension::CubeArray,12,"CubeArray");
        shaderSampleVolume(*device);
        std::printf("TIMING Metal fixture_total_ms=%.3f gpu_test_body_ms=%.3f\n",
                    elapsedMs(testStart,Clock::now()),
                    elapsedMs(deviceReady,Clock::now()));
        if (failures)
            std::fprintf(stderr,"FAIL: %d Metal texture GPU acceptance checks\n",failures);
        else std::puts("PASS: Metal GPU Texture3D/array/cube mip and async readback parity");
        return failures ? 1 : 0;
    }
}
