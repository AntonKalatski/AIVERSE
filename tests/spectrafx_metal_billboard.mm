// SpectraFX native Metal billboard shader/pipeline and blend validation.
// Reads pixels from an offscreen RGBA8 Metal render target: no GUI required.
// Uses precisely the MSL string shipped by SpectraFxGpuBillboardRenderer.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "engine/fx/SpectraFxGpuBillboardRenderer.h"

namespace {
struct F4 {float x,y,z,w;};
struct alignas(16) Uniforms {
    float viewProj[16];
    F4 right,up,colorSize,meta;
};
static_assert(sizeof(Uniforms)==128);
struct Corner {float x,y;};
int failures=0;
void check(bool ok,const char* message) {
    if(!ok) {
        std::fprintf(stderr,"FAIL [spectrafx-metal-billboard] %s\n",message);
        ++failures;
    }
}
id<MTLRenderPipelineState> compile(
    id<MTLDevice> device,id<MTLFunction> vs,id<MTLFunction> fs,
    bool additive) {
    MTLRenderPipelineDescriptor* desc=[MTLRenderPipelineDescriptor new];
    desc.vertexFunction=vs;
    desc.fragmentFunction=fs;
    desc.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
    MTLVertexDescriptor* vertex=[MTLVertexDescriptor vertexDescriptor];
    vertex.attributes[0].format=MTLVertexFormatFloat2;
    vertex.attributes[0].offset=0;
    vertex.attributes[0].bufferIndex=0;
    vertex.layouts[0].stride=sizeof(Corner);
    vertex.layouts[0].stepFunction=MTLVertexStepFunctionPerVertex;
    desc.vertexDescriptor=vertex;
    auto color=desc.colorAttachments[0];
    color.blendingEnabled=YES;
    color.rgbBlendOperation=MTLBlendOperationAdd;
    color.alphaBlendOperation=MTLBlendOperationAdd;
    color.sourceRGBBlendFactor=MTLBlendFactorSourceAlpha;
    color.destinationRGBBlendFactor=additive ?
        MTLBlendFactorOne : MTLBlendFactorOneMinusSourceAlpha;
    color.sourceAlphaBlendFactor=MTLBlendFactorOne;
    color.destinationAlphaBlendFactor=additive ?
        MTLBlendFactorOne : MTLBlendFactorOneMinusSourceAlpha;
    NSError* error=nil;
    auto pipeline=[device newRenderPipelineStateWithDescriptor:desc
                                                         error:&error];
    if(!pipeline) {
        ++failures;
        std::fprintf(stderr,"FAIL [spectrafx-metal-billboard] %s pipeline: %s\n",
                     additive?"additive":"alpha",
                     error?error.localizedDescription.UTF8String:"unknown error");
    }
    return pipeline;
}
id<MTLTexture> makeTexture(id<MTLDevice> device,MTLPixelFormat fmt,
                            int width,int height,MTLTextureUsage usage) {
    MTLTextureDescriptor* td=
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt
               width:NSUInteger(width) height:NSUInteger(height) mipmapped:NO];
    td.storageMode=MTLStorageModeShared;
    td.usage=usage;
    return [device newTextureWithDescriptor:td];
}
void uploadF4(id<MTLTexture> texture,const std::vector<F4>& data) {
    [texture replaceRegion:MTLRegionMake2D(0,0,texture.width,texture.height)
               mipmapLevel:0 withBytes:data.data()
               bytesPerRow:size_t(texture.width)*sizeof(F4)];
}
struct Image {
    static constexpr int kSize=128;
    std::vector<uint8_t> rgba;
    uint8_t red(int x,int y) const {
        return rgba[(size_t(y)*kSize+size_t(x))*4];
    }
};
Image render(id<MTLDevice> device,id<MTLCommandQueue> queue,
             id<MTLRenderPipelineState> pipeline,
             id<MTLTexture> positions,id<MTLTexture> indices,
             bool remap,uint32_t instances,const F4 colorSize) {
    const Corner quad[]={{-1,-1},{1,-1},{1,1},{-1,1}};
    const uint16_t triangleIndices[]={0,1,2,0,2,3};
    id<MTLBuffer> verts=[device newBufferWithBytes:quad
        length:sizeof(quad) options:MTLResourceStorageModeShared];
    id<MTLBuffer> indexed=[device newBufferWithBytes:triangleIndices
        length:sizeof(triangleIndices) options:MTLResourceStorageModeShared];
    auto target=makeTexture(device,MTLPixelFormatRGBA8Unorm,
        Image::kSize,Image::kSize,
        MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead);
    if(!verts||!indexed||!target||!positions||!indices||!pipeline) {
        check(false,"Metal offscreen resource allocation");
        return {};
    }
    Uniforms u{};
    u.viewProj[0]=1;
    u.viewProj[5]=1;
    u.viewProj[10]=0.1f; // map the test's positive Z into Metal clip space
    u.viewProj[15]=1;
    u.right={1,0,0,0};
    u.up={0,1,0,0};
    u.colorSize=colorSize;
    u.meta={256,remap?1.0f:0.0f,0,0};
    MTLRenderPassDescriptor* pass=[MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture=target;
    pass.colorAttachments[0].loadAction=MTLLoadActionClear;
    pass.colorAttachments[0].storeAction=MTLStoreActionStore;
    pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,0);
    id<MTLCommandBuffer> cb=[queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder=[cb renderCommandEncoderWithDescriptor:pass];
    if(!encoder) {
        check(false,"begin Metal offscreen render encoder");
        return {};
    }
    [encoder setRenderPipelineState:pipeline];
    [encoder setCullMode:MTLCullModeNone];
    [encoder setVertexBuffer:verts offset:0 atIndex:0];
    [encoder setVertexBytes:&u length:sizeof(u) atIndex:1];
    // The billboards use texel-read from the vertex stage only.
    [encoder setVertexTexture:positions atIndex:0];
    [encoder setVertexTexture:indices atIndex:1];
    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                       indexCount:6 indexType:MTLIndexTypeUInt16
                      indexBuffer:indexed indexBufferOffset:0
                    instanceCount:instances];
    [encoder endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if(cb.status!=MTLCommandBufferStatusCompleted) {
        ++failures;
        std::fprintf(stderr,"FAIL [spectrafx-metal-billboard] GPU draw: %s\n",
            cb.error?cb.error.localizedDescription.UTF8String:"incomplete");
        return {};
    }
    Image result;
    result.rgba.resize(Image::kSize*Image::kSize*4);
    [target getBytes:result.rgba.data()
           bytesPerRow:Image::kSize*4
           fromRegion:MTLRegionMake2D(0,0,Image::kSize,Image::kSize)
           mipmapLevel:0];
    return result;
}
} // namespace

int main() {
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();
        if(!device) {
            std::puts("SKIP [spectrafx-metal-billboard] no Metal device");
            return 77;
        }
        auto queue=[device newCommandQueue];
        if(!queue) {
            std::puts("SKIP [spectrafx-metal-billboard] no command queue");
            return 77;
        }
        std::printf("[spectrafx-metal-billboard] device=%s\n",
                    device.name.UTF8String);
        NSError* error=nil;
        id<MTLLibrary> library=[device newLibraryWithSource:
            [NSString stringWithUTF8String:
                aiverse::fx::gpu_render_shaders::kMetal]
            options:nil error:&error];
        if(!library) {
            std::fprintf(stderr,"FAIL [spectrafx-metal-billboard] MSL: %s\n",
                error?error.localizedDescription.UTF8String:"unknown");
            return 1;
        }
        auto vertex=[library newFunctionWithName:@"vs_main"];
        auto fragment=[library newFunctionWithName:@"fs_main"];
        check(vertex!=nil && fragment!=nil,"billboard shader entry points");
        if(!vertex||!fragment) return 1;
        auto alpha=compile(device,vertex,fragment,false);
        auto additive=compile(device,vertex,fragment,true);
        if(!alpha||!additive) return 1;
        std::puts("PASS [spectrafx-metal-billboard] MSL + alpha/additive pipelines");

        auto positionTexture=makeTexture(device,MTLPixelFormatRGBA32Float,
            256,1,MTLTextureUsageShaderRead);
        auto indexTexture=makeTexture(device,MTLPixelFormatRGBA32Float,
            256,1,MTLTextureUsageShaderRead);
        if(!positionTexture||!indexTexture) {
            check(false,"RGBA32F shader input textures");
            return 1;
        }
        std::vector<F4> particles(256),indices(256);
        particles[0]={-0.45f,0,1,0.8f};
        particles[1]={ 0.45f,0,2,0.8f};
        // Sorted-index map deliberately swaps source slot order:
        // instance 0 reads physical slot 1 instead of physical slot 0.
        indices[0]={1,0,0,1};
        uploadF4(positionTexture,particles);
        uploadF4(indexTexture,indices);
        const F4 color{0.8f,0.3f,0.1f,0.3f};
        const Image direct=render(device,queue,alpha,
            positionTexture,indexTexture,false,1,color);
        const Image remapped=render(device,queue,alpha,
            positionTexture,indexTexture,true,1,color);
        if(direct.rgba.empty()||remapped.rgba.empty()) return 1;
        // World x=-.45 -> pixel 35; world x=+.45 -> pixel 93.
        check(direct.red(35,64)>70,"non-remapped source slot 0 is visible");
        check(direct.red(93,64)<8,"non-remapped source slot 1 was not drawn");
        check(remapped.red(93,64)>70,"GPU sorted index remapping draws slot 1");
        check(remapped.red(35,64)<8,"sorted remap does not draw slot 0");
        check(remapped.red(0,0)==0 && direct.red(0,0)==0,
              "offscreen clear untouched at viewport corner");

        // Additive blending must accumulate identical overlapping particles.
        particles[0]={0,0,1,0.6f};
        particles[1]={0,0,2,0.6f};
        uploadF4(positionTexture,particles);
        const Image once=render(device,queue,additive,
            positionTexture,indexTexture,false,1,{0.5f,0.2f,0.1f,0.28f});
        const Image twice=render(device,queue,additive,
            positionTexture,indexTexture,false,2,{0.5f,0.2f,0.1f,0.28f});
        if(once.rgba.empty()||twice.rgba.empty()) return 1;
        const int one=int(once.red(64,64)),two=int(twice.red(64,64));
        check(one>20 && two>one+25,
              "additive blend accumulates multiple live billboards");
        std::printf("[spectrafx-metal-billboard] alpha direct/remap=%d/%d "
                    "additive red 1x/2x=%d/%d\n",
                    int(direct.red(35,64)),int(remapped.red(93,64)),one,two);
        if(failures) {
            std::fprintf(stderr,
                "SpectraFX Metal billboard acceptance: %d failures\n",failures);
            return 1;
        }
        std::puts("PASS [spectrafx-metal-billboard] alpha/additive draw, "
                  "GPU index remap and offscreen readback");
        return 0;
    }
}
