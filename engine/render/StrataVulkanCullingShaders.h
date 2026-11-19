#pragma once
// STRATA Stage 16.1 Vulkan diagnostic cluster selection. Complete GLSL450,
// matched to GpuMetadataHeader/GpuPrimitive/GpuNode and GpuSelectedCluster.
// This kernel only produces candidate ranges/counters; NO draw submission.
// No CPU readback is used during rendering. Async readback is diagnostics only.
namespace aiverse::render::strata {
inline constexpr const char* kVulkanGpuCulling=R"GLSL(#version 450
layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

layout(std430,set=0,binding=0) readonly buffer Metadata {uint words[];} metadata;
struct Instance {
    mat4 objectToWorld;
    uvec4 fields; // primitiveIndex, enabled, instanceToken, reserved
};
layout(std430,set=0,binding=1) readonly buffer Instances {Instance data[];} instances;
struct Selected {
    uvec4 first;  // token, firstIndex, count, material
    uvec4 second; // globalNode, primitiveIndex, localFirstIndex, reserved
};
layout(std430,set=0,binding=2) buffer SelectedRanges {Selected records[];} ranges;
layout(std430,set=0,binding=3) buffer Counters {uint c[4];} counts;

layout(push_constant) uniform Params {
    mat4 viewProjection;
    uint instanceCount, outputCapacity, width, height;
    float thresholdPixels;
    uint clipDepthZeroToOne, frustumCull, reserved;
} params;

uint wordAt(uint byteOffset) {return metadata.words[byteOffset>>2];}
struct Node {
    vec3 low, high;
    uint child, children, firstIndex, count, material, resident;
    float error;
};
Node getNode(uint nodesOffset,uint id) {
    uint address=nodesOffset+id*64u;
    Node n;
    n.low=vec3(uintBitsToFloat(wordAt(address+0u)),
               uintBitsToFloat(wordAt(address+4u)),
               uintBitsToFloat(wordAt(address+8u)));
    n.high=vec3(uintBitsToFloat(wordAt(address+16u)),
                uintBitsToFloat(wordAt(address+20u)),
                uintBitsToFloat(wordAt(address+24u)));
    n.child=wordAt(address+32u);
    n.children=wordAt(address+36u);
    n.firstIndex=wordAt(address+40u);
    n.count=wordAt(address+44u);
    n.material=wordAt(address+48u);
    n.resident=wordAt(address+52u);
    n.error=uintBitsToFloat(wordAt(address+56u));
    return n;
}
void invalid(){atomicOr(counts.c[3],1u);}
bool bad(vec4 a){return any(isnan(a)) || any(isinf(a));}

float projectedError(mat4 clip,Node node) {
    float nearW=1.0e30, maxX=0.0, maxY=0.0;
    for(uint c=0u;c<8u;++c){
        vec3 p=vec3((c&1u)!=0u?node.high.x:node.low.x,
                    (c&2u)!=0u?node.high.y:node.low.y,
                    (c&4u)!=0u?node.high.z:node.low.z);
        vec4 q=clip*vec4(p,1.0);
        if(bad(q)||q.w<=0.0||
           q.z<(params.clipDepthZeroToOne!=0u?0.0:-q.w))
            return 1.0e30;
        nearW=min(nearW,q.w);
        maxX=max(maxX,abs(q.x));
        maxY=max(maxY,abs(q.y));
    }
    float e=node.error;
    float nx=length(vec3(clip[0][0],clip[1][0],clip[2][0]));
    float ny=length(vec3(clip[0][1],clip[1][1],clip[2][1]));
    float nw=length(vec3(clip[0][3],clip[1][3],clip[2][3]));
    float delta=nw*e;
    if(nearW<=delta)return 1.0e30;
    float px=0.5*max(float(params.width)*e*(nx*nearW+maxX*nw),
                     float(params.height)*e*(ny*nearW+maxY*nw))/
             (nearW*(nearW-delta));
    if(isnan(px)||isinf(px))return 1.0e30;
    return px;
}
bool outside(mat4 clip,Node node) {
    uint allOutside=63u;
    for(uint c=0u;c<8u;++c){
        vec3 p=vec3((c&1u)!=0u?node.high.x:node.low.x,
                    (c&2u)!=0u?node.high.y:node.low.y,
                    (c&4u)!=0u?node.high.z:node.low.z);
        vec4 q=clip*vec4(p,1.0);
        if(bad(q))return false;
        uint flags=0u;
        if(q.x<-q.w)flags|=1u;
        if(q.x>q.w)flags|=2u;
        if(q.y<-q.w)flags|=4u;
        if(q.y>q.w)flags|=8u;
        if(q.z<-q.w)flags|=16u; // exact geometryBoxVisible oracle
        if(q.z>q.w)flags|=32u;
        allOutside&=flags;
    }
    return allOutside!=0u;
}
void main(){
    uint tid=gl_GlobalInvocationID.x;
    if(tid>=params.instanceCount)return;
    if(wordAt(0u)!=2u)return invalid();
    uint primitiveCount=wordAt(4u), nodeCount=wordAt(8u);
    uint rootCount=wordAt(12u);
    uint primitivesOffset=wordAt(16u), nodesOffset=wordAt(20u);
    uint rootsOffset=wordAt(24u);
    if(primitiveCount==0u||nodeCount==0u||rootCount==0u){
        invalid();return;
    }
    Instance instance=instances.data[tid];
    if(instance.fields.y==0u)return;
    uint primitiveID=instance.fields.x;
    if(primitiveID>=primitiveCount){invalid();return;}
    uint primitiveAddr=primitivesOffset+primitiveID*32u;
    uint nodeBase=wordAt(primitiveAddr+12u);
    uint nodeLength=wordAt(primitiveAddr+16u);
    uint rootBase=wordAt(primitiveAddr+20u);
    uint primitiveRoots=wordAt(primitiveAddr+24u);
    uint gpuFirstIndex=wordAt(primitiveAddr+28u);
    if(gpuFirstIndex==0xffffffffu ||
       nodeBase>nodeCount||nodeLength>nodeCount-nodeBase ||
       rootBase>rootCount||primitiveRoots>rootCount-rootBase){
        invalid();return;
    }
    mat4 clip=params.viewProjection*instance.objectToWorld;
    for(uint r=0u;r<primitiveRoots;++r){
        uint stack[128];
        uint top=0u;
        uint root=wordAt(rootsOffset+4u*(rootBase+r));
        if(root<nodeBase || root>=nodeBase+nodeLength){
            invalid();return;
        }
        stack[top++]=root;
        uint visits=0u;
        while(top!=0u){
            if(++visits>nodeLength){invalid();return;}
            uint id=stack[--top];
            if(id>=nodeCount){invalid();return;}
            Node node=getNode(nodesOffset,id);
            atomicAdd(counts.c[2],1u);
            if(node.resident==0u){invalid();return;}
            bool refine=node.children!=0u &&
                projectedError(clip,node)>params.thresholdPixels;
            if(refine){
                if(node.child<nodeBase||node.child>=nodeBase+nodeLength ||
                   node.children>nodeBase+nodeLength-node.child){
                    invalid();return;
                }
                for(uint c=0u;c<node.children;++c)
                    if(getNode(nodesOffset,node.child+c).resident==0u){
                        refine=false;
                        break;
                    }
                if(refine && top+node.children>128u){invalid();return;}
            }
            if(refine){
                for(uint c=node.children;c>0u;--c)
                    stack[top++]=node.child+c-1u;
                continue;
            }
            if(params.frustumCull!=0u && outside(clip,node))continue;
            if(node.count==0u || gpuFirstIndex>0xffffffffu-node.firstIndex){
                invalid();return;
            }
            uint at=atomicAdd(counts.c[0],1u);
            if(at>=params.outputCapacity){
                atomicOr(counts.c[1],1u);
                continue;
            }
            ranges.records[at].first=uvec4(instance.fields.z,
                gpuFirstIndex+node.firstIndex,node.count,node.material);
            ranges.records[at].second=uvec4(id,primitiveID,
                node.firstIndex,0u);
        }
    }
}
)GLSL";
} // namespace aiverse::render::strata
