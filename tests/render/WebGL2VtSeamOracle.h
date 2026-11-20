// Opt-in WebGL2 production-RHI seam sampling oracle.
// Separately authored logical texels provide the CPU bilinear reference.
#pragma once
#include "engine/gfx/Gfx.h"
#include "engine/gfx/VirtualTextureSampling.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::test {
class WebGL2VtSeamOracle {
    using Color=std::array<uint8_t,4>;
    static constexpr int kTile=8,kInterior=6,kLogicalWidth=12,kLogicalHeight=6;
    gfx::Device& device_;
    gfx::TextureHandle atlas_[2]{},table_{},target_{};
    gfx::ShaderHandle shader_{};
    gfx::PipelineHandle pipeline_{};
    std::string fragment_;
    unsigned pending_=0;
    bool failed_=false;
    static int address(int i,int limit,int mode) {
        if(mode==1) return (i%limit+limit)%limit; // Repeat
        const int period=limit*2;
        const int wrapped=(i%period+period)%period;
        return wrapped>=limit?period-1-wrapped:wrapped; // MirrorRepeat
    }
    static float addressed(float value,int mode) {
        if(mode==1) return value-std::floor(value);
        const float p=value-std::floor(value/2.0f)*2.0f;
        return p>1.0f?2.0f-p:p;
    }
    static Color logical(int x,int y,int mode) {
        x=address(x,kLogicalWidth,mode);
        y=std::clamp(y,0,kLogicalHeight-1);
        return {uint8_t(28+9*x+2*y),uint8_t(55+4*x+7*y),
                uint8_t(216-8*x-3*y),255};
    }
    static std::vector<uint8_t> page(int tile,int mode) {
        std::vector<uint8_t> data(size_t(kTile*kTile*4),0);
        for(int y=0;y<kTile;++y)for(int x=0;x<kTile;++x) {
            const auto p=logical(tile*kInterior+x-1,y-1,mode);
            for(int c=0;c<4;++c)
                data[size_t((y*kTile+x)*4+c)]=p[size_t(c)];
        }
        return data;
    }
    static Color expected(float u,float v,int mode) {
        const float x=addressed(u,mode)*float(kLogicalWidth)-0.5f;
        const float y=std::clamp(v,0.0f,1.0f)*float(kLogicalHeight)-0.5f;
        const int x0=int(std::floor(x)),y0=int(std::floor(y));
        const float tx=x-float(x0),ty=y-float(y0);
        const auto a=logical(x0,y0,mode),b=logical(x0+1,y0,mode);
        const auto c=logical(x0,y0+1,mode),d=logical(x0+1,y0+1,mode);
        Color result{};
        for(int k=0;k<4;++k) {
            const float top=float(a[size_t(k)])*(1.0f-tx)+float(b[size_t(k)])*tx;
            const float bottom=float(c[size_t(k)])*(1.0f-tx)+float(d[size_t(k)])*tx;
            result[size_t(k)]=uint8_t(std::clamp(int(std::lround(top*(1.0f-ty)+bottom*ty)),0,255));
        }
        return result;
    }
    bool check(bool ok,const char* message) {
        if(!ok) {
            failed_=true;
            std::fprintf(stderr,"FAIL: WebGL2 nonuniform seam oracle %s\n",message);
        }
        return ok;
    }
public:
    explicit WebGL2VtSeamOracle(gfx::Device& d):device_(d){}
    WebGL2VtSeamOracle(const WebGL2VtSeamOracle&)=delete;
    WebGL2VtSeamOracle& operator=(const WebGL2VtSeamOracle&)=delete;
    ~WebGL2VtSeamOracle(){shutdown();}
    bool initialize() {
        // Guard against a degenerate fixture: at the internal tile seam
        // the correct bilinear sample MUST differ from simply sampling the
        // first interior texel in the right tile (legacy centre-inset bug).
        const auto middle=expected(0.5f,0.5f,1);
        const auto rightFirst=logical(kInterior,2,1);
        int sensitivity=0;
        for(int c=0;c<3;++c)
            sensitivity=std::max(sensitivity,
                std::abs(int(middle[size_t(c)])-int(rightFirst[size_t(c)])));
        if(!check(sensitivity>=3,
                  "logical gradient must detect legacy seam centre-inset regression"))
            return false;
        // Independent authored page-table bytes. Physical slice 0 = left,
        // slice 1 = right. One virtual mip; all pixels are resident.
        const uint8_t tableBytes[]={0,0,0,255,1,0,0,255};
        gfx::TextureDesc tableDesc;
        tableDesc.width=2;tableDesc.height=1;
        tableDesc.dimension=gfx::TextureDimension::Texture2DArray;
        tableDesc.arrayLayers=1;tableDesc.mipLevels=1;
        tableDesc.filter=gfx::Filter::Nearest;
        table_=device_.createTexture(tableDesc);
        if(!check(table_.valid(),"page table allocation"))return false;
        gfx::TextureUploadDesc upload;
        upload.width=2;upload.height=1;
        upload.rowPitchBytes=8;upload.slicePitchBytes=8;upload.data=tableBytes;
        if(!check(device_.updateTextureRegion(table_,upload),"page table upload"))return false;
        gfx::TextureDesc atlasDesc;
        atlasDesc.width=atlasDesc.height=kTile;
        atlasDesc.dimension=gfx::TextureDimension::Texture2DArray;
        atlasDesc.arrayLayers=2;atlasDesc.mipLevels=1;
        atlasDesc.filter=gfx::Filter::Linear;
        for(int mode=1;mode<=2;++mode) {
            atlas_[mode-1]=device_.createTexture(atlasDesc);
            if(!check(atlas_[mode-1].valid(),"atlas allocation"))return false;
            for(int tile=0;tile<2;++tile) {
                const auto bytes=page(tile,mode);
                gfx::TextureUploadDesc u;
                u.width=u.height=kTile;
                u.arrayLayer=tile;
                u.rowPitchBytes=kTile*4;u.slicePitchBytes=kTile*kTile*4;
                u.data=bytes.data();
                if(!check(device_.updateTextureRegion(atlas_[mode-1],u),
                          "nonuniform gutter page upload"))return false;
            }
        }
        static constexpr const char* vertex=R"GLSL(
void main() {
    vec2 p=vec2(gl_VertexID==1?3.0:-1.0,gl_VertexID==2?3.0:-1.0);
    gl_Position=vec4(p,0.0,1.0);
}
)GLSL";
        fragment_=std::string(gfx::kVirtualTextureGLSL)+R"GLSL(
uniform highp sampler2DArray tex0;
uniform highp sampler2DArray tex1;
layout(std140) uniform U0 { int mode; };
layout(location=0) out vec4 outColor;
void main() {
    // Exact four RGBA8 GPU pixel columns: two sides of the internal seam,
    // seam midpoint, and an outer Repeat/MirrorRepeat wrap boundary.
    int col=int(gl_FragCoord.x);
    float u=col==0?0.48:(col==1?0.5:(col==2?0.52:1.02));
    outColor=aiverseSampleVirtual(tex0,tex1,vec2(u,0.5),0,1.0,
                                  vec4(0.0),mode,0);
}
)GLSL";
        gfx::ShaderDesc sd;
        sd.glslVertex=vertex;sd.glslFragment=fragment_.c_str();
        sd.textureSlots=2;sd.uniformSlots=1;
        shader_=device_.createShader(sd);
        if(!check(shader_.valid(),"canonical GLSL shader compile"))return false;
        gfx::PipelineDesc pd;
        pd.shader=shader_;pd.attrCount=0;pd.depthTest=false;
        pd.depthWrite=false;pd.cull=gfx::CullMode::None;
        pipeline_=device_.createPipeline(pd);
        if(!check(pipeline_.valid(),"seam pipeline"))return false;
        gfx::TextureDesc output;
        output.width=4;output.height=4;
        output.format=gfx::PixelFormat::RGBA8;output.renderTarget=true;
        target_=device_.createTexture(output);
        return check(target_.valid(),"seam RGBA8 offscreen target");
    }
    bool queueSamples() {
        if(!pipeline_.valid()||!target_.valid())return false;
        for(int mode=1;mode<=2;++mode) {
            gfx::PassDesc pass;pass.colorTarget=target_;
            pass.clearDepthEnabled=false;
            device_.beginPass(pass);
            device_.setPipeline(pipeline_);
            device_.setTexture(0,atlas_[mode-1]);
            device_.setTexture(1,table_);
            const std::array<int,4> params{{mode,0,0,0}};
            device_.setUniforms(0,params.data(),sizeof(params));
            device_.draw(3);
            device_.endPass();
            ++pending_;
            const bool queued=device_.readTextureAsync(target_,{0,0},
                [this,mode](gfx::TextureReadbackResult read) {
                    bool ok=read.success()&&read.width==4&&read.height==4&&
                            read.pixels.size()==size_t(4*4*4);
                    const std::array<float,4> positions{{0.48f,0.5f,0.52f,1.02f}};
                    if(ok)for(int y=0;y<4;++y)for(int x=0;x<4;++x) {
                        const Color oracle=expected(positions[size_t(x)],0.5f,mode);
                        for(int c=0;c<4;++c) {
                            const int actual=int(read.pixels[size_t((y*4+x)*4+c)]);
                            const int delta=std::abs(actual-int(oracle[size_t(c)]));
                            if(delta>2) {
                                std::fprintf(stderr,
                                    "FAIL: WebGL2 seam mode=%d u=%.5f v=0.5 "
                                    "pixel=(%d,%d) channel=%d expected=%u got=%d delta=%d\\n",
                                    mode,double(positions[size_t(x)]),x,y,c,
                                    unsigned(oracle[size_t(c)]),actual,delta);
                                ok=false;
                            }
                        }
                    }
                    check(ok,"Repeat/MirrorRepeat seam RGBA8 GPU/CPU parity");
                    --pending_;
                });
            if(!queued) {
                --pending_;
                return check(false,"queue seam framebuffer GPU readback");
            }
        }
        return true;
    }
    bool complete() const {return pending_==0&&!failed_;}
    bool failed() const {return failed_;}
    void shutdown() {
        if(target_.valid())device_.destroyTexture(target_);
        if(pipeline_.valid())device_.destroyPipeline(pipeline_);
        if(shader_.valid())device_.destroyShader(shader_);
        for(auto& a:atlas_)if(a.valid())device_.destroyTexture(a);
        if(table_.valid())device_.destroyTexture(table_);
        target_={};pipeline_={};shader_={};table_={};atlas_[0]={};atlas_[1]={};
    }
};
} // namespace aiverse::test
