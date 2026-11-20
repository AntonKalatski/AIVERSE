// SpectraFX native Metal GPU kernel acceptance.
// Uses the exact runtime MSL strings and a real MTLCommandQueue.
// Does not require a CAMetalLayer, an editor window, or a full game scene.
// Error messages include the Metal compiler/command-buffer failure reason.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/fx/SpectraFxComputeShaders.h"
#include "engine/fx/SpectraFxGpuAlphaSort.h"
#include "engine/fx/SpectraFxGpuCompaction.h"

namespace {
struct alignas(16) F4 { float x,y,z,w; };
struct alignas(16) EvolutionParams {
    uint32_t count;
    float dt,damping,feedbackEnabled;
    F4 acceleration,ground;
};
static_assert(sizeof(EvolutionParams)==48);
struct alignas(16) SortParams {
    F4 eye,forward;
    uint32_t count,paddedCount,width,stage,pass,mode,pad0,pad1;
};
static_assert(sizeof(SortParams)==64);
struct alignas(16) CompactParams {
    uint32_t count,width,mode,padding;
};
static_assert(sizeof(CompactParams)==16);
struct DrawArgs {
    uint32_t indexCount,instanceCount,firstIndex;
    int32_t baseVertex;
    uint32_t firstInstance;
};
static_assert(sizeof(DrawArgs)==20);

int errors=0;
void require(bool ok,const char* label) {
    if(!ok) {
        ++errors;
        std::fprintf(stderr,"FAIL [spectrafx-metal-gpu] %s\n",label);
    }
}
bool approximately(float actual,float expected,float tolerance=0.0005f) {
    return std::isfinite(actual)&&std::fabs(actual-expected)<=tolerance;
}
id<MTLComputePipelineState> compile(
    id<MTLDevice> device,const char* source,const char* label) {
    NSError* error=nil;
    NSString* text=[NSString stringWithUTF8String:source];
    id<MTLLibrary> lib=[device newLibraryWithSource:text
                                             options:nil error:&error];
    if(!lib) {
        ++errors;
        std::fprintf(stderr,"FAIL [spectrafx-metal-gpu] %s library: %s\n",
                     label,error ? error.localizedDescription.UTF8String:"unknown");
        return nil;
    }
    id<MTLFunction> fn=[lib newFunctionWithName:@"cs_main"];
    if(!fn) {
        ++errors;
        std::fprintf(stderr,"FAIL [spectrafx-metal-gpu] %s: cs_main not found\n",
                     label);
        return nil;
    }
    id<MTLComputePipelineState> pipeline=
        [device newComputePipelineStateWithFunction:fn error:&error];
    if(!pipeline || pipeline.maxTotalThreadsPerThreadgroup<64) {
        ++errors;
        std::fprintf(stderr,"FAIL [spectrafx-metal-gpu] %s pipeline: %s\n",
                     label,error ? error.localizedDescription.UTF8String:
                                   "64-thread workgroup unsupported");
        return nil;
    }
    std::printf("PASS [spectrafx-metal-gpu] compile %s\n",label);
    return pipeline;
}
id<MTLTexture> rgbaDataTexture(id<MTLDevice> device) {
    MTLTextureDescriptor* desc=
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
            MTLPixelFormatRGBA32Float width:256 height:1 mipmapped:NO];
    desc.storageMode=MTLStorageModeShared;
    desc.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;
    return [device newTextureWithDescriptor:desc];
}
using Encoder=id<MTLComputeCommandEncoder>;
void encode(id<MTLCommandBuffer> command,id<MTLComputePipelineState> pipeline,
            NSUInteger threads,id<MTLBuffer> b0,id<MTLBuffer> b1,
            id<MTLBuffer> b2,id<MTLTexture> t0,id<MTLTexture> t1,
            const void* params,NSUInteger paramBytes) {
    Encoder e=[command computeCommandEncoder];
    if(!e) {require(false,"create compute command encoder");return;}
    [e setComputePipelineState:pipeline];
    if(b0) [e setBuffer:b0 offset:0 atIndex:0];
    if(b1) [e setBuffer:b1 offset:0 atIndex:1];
    if(b2) [e setBuffer:b2 offset:0 atIndex:2];
    if(t0) [e setTexture:t0 atIndex:0];
    if(t1) [e setTexture:t1 atIndex:1];
    [e setBytes:params length:paramBytes atIndex:8];
    [e dispatchThreadgroups:MTLSizeMake((threads+63)/64,1,1)
      threadsPerThreadgroup:MTLSizeMake(64,1,1)];
    [e endEncoding]; // deliberate encoder break, mirroring SpectraFX RHI
}
std::vector<F4> readTexture(id<MTLTexture> texture) {
    std::vector<F4> pixels(256);
    [texture getBytes:pixels.data()
         bytesPerRow:256*sizeof(F4)
         fromRegion:MTLRegionMake2D(0,0,256,1) mipmapLevel:0];
    return pixels;
}
} // namespace

int main() {
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();
        if(!device) {
            std::puts("SKIP [spectrafx-metal-gpu] no Metal device");
            return 77; // CTest SKIP_RETURN_CODE, not a PASS
        }
        id<MTLCommandQueue> queue=[device newCommandQueue];
        if(!queue) {
            std::puts("SKIP [spectrafx-metal-gpu] no Metal command queue");
            return 77;
        }
        std::printf("[spectrafx-metal-gpu] device=%s\n",device.name.UTF8String);
        auto evolution=compile(device,aiverse::fx::kSpectraFxComputeMetal,
                               "evolution");
        auto sorting=compile(device,aiverse::fx::kSpectraFxAlphaSortMetal,
                             "alpha-sort");
        auto compaction=compile(device,aiverse::fx::kSpectraFxCompactMetal,
                                "compaction");
        if(!evolution||!sorting||!compaction) return 1;

        // Alive particle slots have depths 3, 1, 2; the final slot is
        // dormant and must remain fully transparent and sort to the tail.
        const F4 positions[4]={
            {0,0,3,0}, {0,0,1,0}, {0,0,2,0}, {0,0,4,0}
        };
        const F4 velocities[4]={
            {0,0,0,1}, {0,0,0,1}, {0,0,0,1}, {0,0,0,0}
        };
        id<MTLBuffer> pos=[device newBufferWithBytes:positions
                     length:sizeof(positions) options:MTLResourceStorageModeShared];
        id<MTLBuffer> vel=[device newBufferWithBytes:velocities
                     length:sizeof(velocities) options:MTLResourceStorageModeShared];
        id<MTLBuffer> impacts=[device newBufferWithLength:sizeof(positions)
                                             options:MTLResourceStorageModeShared];
        id<MTLTexture> texture=rgbaDataTexture(device);
        id<MTLTexture> sorted=rgbaDataTexture(device);
        id<MTLTexture> dense=rgbaDataTexture(device);
        id<MTLBuffer> sortItems=[device newBufferWithLength:4*8
                                             options:MTLResourceStorageModeShared];
        id<MTLBuffer> liveCount=[device newBufferWithLength:4
                                             options:MTLResourceStorageModeShared];
        id<MTLBuffer> args=[device newBufferWithLength:sizeof(DrawArgs)
                                             options:MTLResourceStorageModeShared];
        if(!pos||!vel||!impacts||!texture||!sorted||!dense||
           !sortItems||!liveCount||!args) {
            require(false,"allocate Metal GPU test resources");
            return 1;
        }
        *static_cast<uint32_t*>(liveCount.contents)=0u;
        EvolutionParams evo{4,0.25f,1.0f,0.0f,{0,0,0,0},{0,0,1,0}};
        SortParams order{{0,0,0,0},{0,0,1,0},
                         4,4,256,0,0,0,0,0};
        CompactParams compact{4,256,0,0};

        id<MTLCommandBuffer> command=[queue commandBuffer];
        require(command!=nil,"create Metal command buffer");
        if(!command) return 1;
        encode(command,evolution,4,pos,vel,impacts,texture,nil,
               &evo,sizeof(evo));
        encode(command,sorting,4,sortItems,nil,nil,texture,sorted,
               &order,sizeof(order));
        // Mirrors SpectraFxGpuAlphaSort: separate encoders for each pass,
        // with half as many compare lanes as padded particles.
        for(uint32_t stage=2;stage<=4;stage<<=1)
            for(uint32_t pass=stage>>1;pass>0;pass>>=1) {
                order.mode=1;
                order.stage=stage;
                order.pass=pass;
                encode(command,sorting,2,sortItems,nil,nil,texture,sorted,
                       &order,sizeof(order));
            }
        order.mode=2;
        order.stage=order.pass=0;
        encode(command,sorting,4,sortItems,nil,nil,texture,sorted,
               &order,sizeof(order));
        encode(command,compaction,4,liveCount,args,nil,texture,dense,
               &compact,sizeof(compact));
        compact.mode=1;
        encode(command,compaction,1,liveCount,args,nil,texture,dense,
               &compact,sizeof(compact));

        [command commit];
        [command waitUntilCompleted];
        if(command.status!=MTLCommandBufferStatusCompleted) {
            ++errors;
            std::fprintf(stderr,"FAIL [spectrafx-metal-gpu] command buffer: %s\n",
                         command.error ?
                             command.error.localizedDescription.UTF8String :
                             "status was not completed");
            return 1;
        }

        const auto output=readTexture(texture);
        require(approximately(output[0].z,3)&&approximately(output[0].w,0.75f),
                "evolution slot 0 position and lifetime alpha");
        require(approximately(output[1].z,1)&&approximately(output[1].w,0.75f),
                "evolution slot 1 position and lifetime alpha");
        require(approximately(output[2].z,2)&&approximately(output[2].w,0.75f),
                "evolution slot 2 position and lifetime alpha");
        require(approximately(output[3].w,0),
                "dormant particle must be transparent");
        const auto indices=readTexture(sorted);
        const int expected[4]={0,2,1,3};
        for(int i=0;i<4;++i) {
            if(!approximately(indices[size_t(i)].x,float(expected[i]))) {
                ++errors;
                std::fprintf(stderr,
                    "FAIL [spectrafx-metal-gpu] alpha sort[%d]: got %.2f expected %d\n",
                    i,indices[size_t(i)].x,expected[i]);
            }
        }
        const auto draw=*static_cast<const DrawArgs*>(args.contents);
        const auto count=*static_cast<const uint32_t*>(liveCount.contents);
        require(count==3 && draw.indexCount==6 && draw.instanceCount==3,
                "GPU compaction writes live count and indexed-indirect arguments");
        auto compacted=readTexture(dense);
        std::vector<int> compactedSlots;
        for(uint32_t i=0;i<count && i<4;++i)
            compactedSlots.push_back(int(std::round(compacted[i].x)));
        std::sort(compactedSlots.begin(),compactedSlots.end());
        require(compactedSlots==std::vector<int>({0,1,2}),
                "compacted output contains each live source slot exactly once");
        if(errors) {
            std::fprintf(stderr,"SpectraFX Metal GPU acceptance: %d failures\n",
                         errors);
            return 1;
        }
        std::puts("PASS [spectrafx-metal-gpu] evolution, alpha sorting, compaction");
        return 0;
    }
}
