// PRISMA Depth of Field / Motion Blur GPU acceptance on the real backend.
// AIVERSE_PRISMA_EFFECT_TEST=dof|motion. Uses the default HDR scene geometry.
// Sharpness = mean absolute horizontal luminance step of the captured frame.
//   dof:    0 no effect, 1 focus on the scene (stays sharp), 2 focus near the
//           camera (the scene blurs).
//   motion: 0 camera turning, no effect; 1 same turn with motion blur (blurs);
//           2 still camera with motion blur (stays sharp).
#pragma once
#include <cmath>
#include <cstring>
namespace aiverse::test {
struct PrismaEffectsState {
    int phase=0;
    double sharp[3]={};
};
inline PrismaEffectsState& prismaEffectsState(){static PrismaEffectsState s;return s;}
inline double frameSharpness(int w,int h,const std::vector<uint8_t>& px) {
    double sum=0;size_t n=0;
    for(int y=h/8;y<h-h/8;++y)for(int x=w/8;x<w-w/8-1;++x) {
        const auto lum=[&](int xx){const uint8_t* p=px.data()+(size_t(y)*size_t(w)+size_t(xx))*4;
            return .2126*p[0]+.7152*p[1]+.0722*p[2];};
        sum+=std::abs(lum(x+1)-lum(x));++n;
    }
    return n?sum/double(n):0;
}
inline void drawPrismaEffects(HdrScene& s,gfx::Device& d,int w,int h,const char* which) {
    auto& st=prismaEffectsState();
    const bool dof=std::strcmp(which,"dof")==0;
    constexpr int kFrames=12;
    if(s.frame==0) {
        std::string effects;
        if(dof) {
            if(st.phase==1)effects=R"({"type":"depthOfField","metricFocus":true,"focusDistance":17,"focusRangeMeters":6,"maxBlur":10,"intensity":1})";
            if(st.phase==2)effects=R"({"type":"depthOfField","metricFocus":true,"focusDistance":2,"focusRangeMeters":0.5,"maxBlur":10,"intensity":1})";
        } else if(st.phase>=1) {
            effects=R"({"type":"motionBlur","intensity":1,"maxPixels":32,"samples":12,"shutterScale":1})";
        }
        s.post.configure(Json::parse("{\"hdr\":\"on\",\"effects\":["+effects+"]}"),Json());
        s.post.setQuality(PrismaQuality::High);
        s.camera.yaw=.18f;
    }
    // Motion phases 0 and 1 turn the camera by the same amount every frame.
    if(!dof && st.phase<=1)s.camera.yaw=.18f+.04f*float(s.frame);
    if(s.ownsFrame)d.beginFrame(w,h);
    auto& r=s.renderer;
    r.beginScene(s.camera,w,h);
    r.drawMesh(s.floor,{}, {0.34f,0.36f,0.4f,1});
    ModelDraw md;md.model=s.spheres.get();r.drawModel(md);
    for(int i=0;i<5;++i)r.drawMesh(s.cube,mat4::translate({float(i-2)*2,0,-4.5f}),{0.6f,0.24f,0.06f,1},kDrawUnlit,0,1);
    gfx::PassDesc pass=s.output;pass.clearColor[0]=.09f;pass.clearColor[1]=.11f;pass.clearColor[2]=.15f;
    r.render(pass,s.pipeline());
    r.resolvePrisma();
    if(s.frame==kFrames) {
        // The effect the phase asked for must have been encoded.
        const PrismaType type=dof?PrismaType::DepthOfField:PrismaType::MotionBlur;
        bool encoded=false,authored=false;std::string reason;
        for(auto id:s.post.effectReportViewIds())
            if(const auto* report=s.post.effectReport(id))
                for(const auto& e:report->effects)
                    if(e.type==type){authored=true;encoded|=e.encoded;reason=e.reason;}
        if(st.phase>0 && !encoded) {
            std::fprintf(stderr,"PRISMA %s phase %d not encoded (%s) FAIL\n",which,st.phase,
                         authored?reason.c_str():"not authored");
            s.failed=true;
        }
        auto receive=[&s,dof,which](int cw,int ch,std::vector<uint8_t> px) {
            auto& st=prismaEffectsState();
            if(cw<8||ch<8||px.size()!=size_t(cw*ch*4)){s.failed=true;return;}
            st.sharp[st.phase]=frameSharpness(cw,ch,px);
            std::printf("PRISMA %s phase %d: sharpness %.3f\n",which,st.phase,st.sharp[st.phase]);
            if(++st.phase==3) {
                const double base=st.sharp[0];
                // dof:    in focus within 15% of no effect, defocused at least 25% softer.
                // motion: turning + blur at least 20% softer than turning alone; a
                //         still camera with blur stays clearly sharper than that.
                bool ok=base>0.5;
                if(dof) ok=ok && st.sharp[1]>=base*.85 && st.sharp[2]<=base*.75;
                else ok=ok && st.sharp[1]<=base*.80 && st.sharp[2]>=st.sharp[1]*1.1;
                std::printf("PRISMA %s: %s\n",which,ok?"PASS":"FAIL");
                s.failed|=!ok;s.captured=true;
            }
            s.frame=-1;
        };
        if(s.readOutput)s.readOutput(receive);else d.captureNextFrame(receive);
    }
    d.endPass();if(s.ownsFrame)d.endFrame();++s.frame;
}
}
