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
#include <cstdint>
#include <cstdio>
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
    TextureReadbackResult output;
    bool done = false;
};
void verifyReads(Device& dev, const std::vector<std::shared_ptr<Read>>& probes) {
    // The Metal backend queues blits on the current frame command buffer.
    // Completions must be delivered on the owner thread in a later beginFrame.
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
    dev.endFrame();
    check(queued, "all native GPU readbacks accepted");
    // Never report a skipped GPU test as passed. A timeout/failure is red.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while (std::chrono::steady_clock::now()<deadline) {
        dev.beginFrame(16,16);
        dev.endFrame();
        const bool all=std::all_of(probes.begin(),probes.end(),
            [](const auto& p) {return p->done;});
        if (all) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(dev.gpuHealthy(),"Metal command buffers completed without GPU error");
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
        if (p->output.pixels!=p->expected) {
            ++failures;
            const auto mismatch=std::mismatch(p->expected.begin(),p->expected.end(),
                                             p->output.pixels.begin());
            const size_t at=size_t(mismatch.first-p->expected.begin());
            std::fprintf(stderr,
                "FAIL: %s GPU bytes differ at %zu (wanted %u, got %u)\n",
                p->label.c_str(),at,unsigned(*mismatch.first),
                unsigned(p->output.pixels[at]));
        } else std::printf("PASS: %s (%dx%d GPU RGBA8 bytes)\n",
                           p->label.c_str(),p->width,p->height);
    }
}
std::shared_ptr<Read> probe(TextureHandle h,int mip,int layer,int w,int height,
                            std::vector<uint8_t> expected,std::string label) {
    auto p=std::make_shared<Read>();
    p->texture=h; p->subresource={mip,layer};
    p->width=w; p->height=height; p->expected=std::move(expected);
    p->label=std::move(label);
    return p;
}
void volume(Device& dev) {
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
    verifyReads(dev,{
        probe(h,0,0,4,4,untouched,"3D base z0 unchanged"),
        probe(h,0,2,4,4,changed,"3D base z2 partial rectangle"),
        probe(h,1,1,2,2,slice,"3D mip1 z1 partial depth selection")
    });
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

    verifyReads(dev,{
        probe(h,0,0,4,4,solid(4,4,{10,70,33,255}),
              std::string(name)+" face/layer0"),
        probe(h,0,last,4,4,
              solid(4,4,{uint8_t(10+last*9),uint8_t(70+last),33,255}),
              std::string(name)+" last face/layer"),
        probe(h,1,last,2,2,mipData,
              std::string(name)+" authored mip1")
    });
    dev.destroyTexture(h);
}
} // namespace

int main() {
    @autoreleasepool {
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
        std::printf("Metal GPU acceptance device: %s\n",device->info().name.c_str());
        volume(*device);
        layered(*device,TextureDimension::Texture2DArray,2,"Texture2DArray");
        layered(*device,TextureDimension::Cube,6,"Cube");
        layered(*device,TextureDimension::CubeArray,12,"CubeArray");
        if (failures)
            std::fprintf(stderr,"FAIL: %d Metal texture GPU acceptance checks\n",failures);
        else std::puts("PASS: Metal GPU Texture3D/array/cube mip and async readback parity");
        return failures ? 1 : 0;
    }
}
