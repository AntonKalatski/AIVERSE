// PRISMA Depth of Field / Motion Blur GPU acceptance on the real backend.
// AIVERSE_PRISMA_EFFECT_TEST=dof|motion. Uses the default HDR scene geometry.
// Sharpness = mean squared horizontal luminance step of the captured frame
// (a blur keeps the summed |step| of an edge but spreads it, so the squares drop).
// Each phase frame is also written as <AIVERSE_HDR_CAPTURE>.<test><phase>.ppm.
//   dof:    0 no effect, 1 focus on the scene (stays sharp), 2 focus near the
//           camera (the scene blurs).
//   motion: 0 still camera, no effect; 1 camera turning, no effect; 2 same turn
//           with motion blur (blurs); 3 still camera with motion blur (stays sharp).
#pragma once
#include <cmath>
#include <cstring>
namespace aiverse::test {
struct PrismaEffectsState {
    int phase=0;
    double sharp[4]={};
};
inline PrismaEffectsState& prismaEffectsState(){static PrismaEffectsState s;return s;}
inline double frameSharpness(int w,int h,const std::vector<uint8_t>& px) {
    double sum=0;size_t n=0;
    for(int y=h/8;y<h-h/8;++y)for(int x=w/8;x<w-w/8-1;++x) {
        const auto lum=[&](int xx){const uint8_t* p=px.data()+(size_t(y)*size_t(w)+size_t(xx))*4;
            return .2126*p[0]+.7152*p[1]+.0722*p[2];};
        const double step=lum(x+1)-lum(x);
        sum+=step*step;++n;
    }
    return n?sum/double(n):0;
}
// ---- 2026-11-22 effects: phase 0 = same HDR/High chain without the effect, phase 1 = with it.
// The effect must be encoded and change the frame in its own direction:
//   ao: darker; ssr / distort / haze / smaa / fsr: different; flare / streaks / halation: brighter;
//   smaa: edge steps soften but keep at least 40% of the gradient energy (no blur); histogram: encoded and changes exposure.
struct PrismaNewEffect { const char* id; PrismaType type; const char* json; };
inline const PrismaNewEffect* prismaNewEffect(const char* which) {
    static const PrismaNewEffect k[] = {
        {"ao", PrismaType::AmbientOcclusion, R"({"type":"ambientOcclusion","intensity":2,"radius":1.5})"},
        {"ssr", PrismaType::ScreenSpaceReflections, R"({"type":"screenSpaceReflections","intensity":1,"minNormalY":0.5,"fresnel":0})"},
        {"flare", PrismaType::LensFlare, R"({"type":"lensFlare","intensity":1.5,"threshold":2,"dirt":0.6})"},
        {"smaa", PrismaType::Smaa, R"({"type":"smaa","threshold":0.05})"},
        {"distort", PrismaType::LensDistortion, R"({"type":"lensDistortion","distortion":0.5,"panini":0.5})"},
        {"streaks", PrismaType::AnamorphicStreaks, R"({"type":"anamorphicStreaks","intensity":1.5,"threshold":2})"},
        {"halation", PrismaType::Halation, R"({"type":"halation","intensity":2,"threshold":1})"},
        {"haze", PrismaType::HeatHaze, R"({"type":"heatHaze","amplitude":3,"scale":30})"},
        {"fsr", PrismaType::Fsr, R"({"type":"fsr","renderScale":0.5})"},
        {"histogram", PrismaType::AutoExposure, R"({"type":"autoExposure","histogram":true,"histogramLow":0.1,"histogramHigh":0.6,"speedUp":20,"speedDown":20})"},
    };
    for (const auto& e : k) if (std::strcmp(e.id, which) == 0) return &e;
    return nullptr;
}
inline void prismaFrameStats(int w, int h, const std::vector<uint8_t>& px, double& luma, double& red) {
    luma = red = 0;
    const size_t n = size_t(w) * size_t(h);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = px.data() + i * 4;
        luma += .2126 * p[0] + .7152 * p[1] + .0722 * p[2];
        red += p[0];
    }
    luma /= double(n); red /= double(n);
}
inline void drawPrismaNewEffect(HdrScene& s, gfx::Device& d, int w, int h, const PrismaNewEffect& fx) {
    struct State { int phase = 0; double luma[2] = {}, red[2] = {}, sharp[2] = {}; std::vector<uint8_t> first; double diff = 0; };
    static State st;
    constexpr int kFrames = 12;
    if (s.frame == 0) {
        const std::string effects = st.phase == 1 ? fx.json : "";
        s.post.configure(Json::parse("{\"hdr\":\"on\",\"effects\":[" + effects + "]}"), Json());
        s.post.setQuality(PrismaQuality::High);
        s.camera.yaw = .18f;
        if (fx.type == PrismaType::HeatHaze && st.phase == 1)
            s.post.setShockwaves(s.post.viewId(), {{{0, 0.5f, -1}, 3.0f, 1.0f, 1.0f}});
        if (fx.type == PrismaType::AutoExposure) {
#ifdef _WIN32
            _putenv_s("AIVERSE_PRISMA_AUTO_EXPOSURE", st.phase == 1 ? "1" : "0");
#else
            ::setenv("AIVERSE_PRISMA_AUTO_EXPOSURE", st.phase == 1 ? "1" : "0", 1);
#endif
        }
    }
    if (s.ownsFrame) d.beginFrame(w, h);
    auto& r = s.renderer;
    // Hosts query the internal 3D size before beginScene (FSR / DLSS / temporal upscaling).
    const auto [rw, rh] = s.post.renderSizeForOutput(w, h);
    r.beginScene(s.camera, rw, rh);
    r.drawMesh(s.floor, {}, {0.34f, 0.36f, 0.4f, 1});
    ModelDraw md; md.model = s.spheres.get(); r.drawModel(md);
    for (int i = 0; i < 5; ++i) r.drawMesh(s.cube, mat4::translate({float(i - 2) * 2, 0, -4.5f}), {0.6f, 0.24f, 0.06f, 1}, kDrawUnlit, 0, 1);
    // A small, very bright emitter for the flare / streak / halation phases.
    r.drawMesh(s.cube, mat4::translate({3.5f, 2.6f, -6}) * mat4::scale({0.3f, 0.3f, 0.3f}), {8, 7, 5, 1}, kDrawUnlit, 0, 1);
    gfx::PassDesc pass = s.output; pass.clearColor[0] = .09f; pass.clearColor[1] = .11f; pass.clearColor[2] = .15f;
    r.render(pass, s.pipeline());
    r.resolvePrisma();
    if (s.frame == kFrames) {
        if (st.phase == 1) {
            bool encoded = false; std::string reason = "not authored";
            if (const auto* report = s.post.effectReport(s.post.viewId()))
                for (const auto& e : report->effects)
                    if (e.type == fx.type) { encoded = e.encoded && !e.degraded; reason = e.reason; }
            if (!encoded) { std::fprintf(stderr, "PRISMA %s not encoded (%s) FAIL\n", fx.id, reason.c_str()); s.failed = true; }
        }
        auto receive = [&s, &fx](int cw, int ch, std::vector<uint8_t> px) {
            if (cw < 8 || ch < 8 || px.size() != size_t(cw * ch * 4)) { s.failed = true; s.captured = true; return; }
            prismaFrameStats(cw, ch, px, st.luma[st.phase], st.red[st.phase]);
            st.sharp[st.phase] = frameSharpness(cw, ch, px);
            {
                std::ofstream f(s.path + "." + fx.id + std::to_string(st.phase) + ".ppm", std::ios::binary);
                f << "P6\n" << cw << " " << ch << "\n255\n";
                for (size_t i = 0; i < px.size(); i += 4) f.write(reinterpret_cast<const char*>(px.data() + i), 3);
            }
            if (st.phase == 0) st.first = px;
            else if (st.first.size() == px.size()) {
                double sum = 0;
                for (size_t i = 0; i < px.size(); i += 4)
                    for (int c = 0; c < 3; ++c) sum += std::abs(int(px[i + c]) - int(st.first[i + c]));
                st.diff = sum / double(px.size() / 4 * 3);
            }
            std::printf("PRISMA %s phase %d: luma %.2f red %.2f sharpness %.3f\n", fx.id, st.phase,
                        st.luma[st.phase], st.red[st.phase], st.sharp[st.phase]);
            if (++st.phase == 2) {
                const std::string id = fx.id;
                bool ok = st.diff > 0.05;   // mean per-channel change, 0..255
                if (id == "ao") ok = ok && st.luma[1] < st.luma[0] - 0.2;
                if (id == "flare" || id == "streaks") ok = ok && st.luma[1] > st.luma[0] + 0.1;
                if (id == "halation") ok = ok && st.red[1] > st.red[0] + 0.1 && st.red[1] - st.red[0] > st.luma[1] - st.luma[0];
                // Upscaling reproduces the same picture: a large difference means a flipped or shifted output.
                if (id == "fsr") ok = ok && st.diff < 8.0;
                if (id == "smaa") ok = ok && st.sharp[1] < st.sharp[0] && st.sharp[1] > st.sharp[0] * 0.4;
                std::printf("PRISMA %s: %s (diff %.3f)\n", fx.id, ok ? "PASS" : "FAIL", st.diff);
                s.failed |= !ok; s.captured = true;
            }
            s.frame = -1;
        };
        if (s.readOutput) s.readOutput(receive); else d.captureNextFrame(receive);
    }
    d.endPass(); if (s.ownsFrame) d.endFrame(); ++s.frame;
}

inline void drawPrismaEffects(HdrScene& s,gfx::Device& d,int w,int h,const char* which) {
    if (const PrismaNewEffect* fx = prismaNewEffect(which)) { drawPrismaNewEffect(s, d, w, h, *fx); return; }
    auto& st=prismaEffectsState();
    const bool dof=std::strcmp(which,"dof")==0;
    constexpr int kFrames=12;
    if(s.frame==0) {
        std::string effects;
        if(dof) {
            if(st.phase==1)effects=R"({"type":"depthOfField","metricFocus":true,"focusDistance":17,"focusRangeMeters":40,"maxBlur":10,"intensity":1})";
            if(st.phase==2)effects=R"({"type":"depthOfField","metricFocus":true,"focusDistance":2,"focusRangeMeters":0.5,"maxBlur":10,"intensity":1})";
        } else if(st.phase>=2) {
            effects=R"({"type":"motionBlur","intensity":1,"maxPixels":32,"samples":12,"shutterScale":1})";
        }
        s.post.configure(Json::parse("{\"hdr\":\"on\",\"effects\":["+effects+"]}"),Json());
        s.post.setQuality(PrismaQuality::High);
        s.camera.yaw=.18f;
    }
    // Motion phases 1 and 2 turn the camera by the same amount every frame.
    if(!dof && (st.phase==1 || st.phase==2))s.camera.yaw=.18f+.04f*float(s.frame);
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
        if((dof?st.phase>0:st.phase>=2) && !encoded) {
            std::fprintf(stderr,"PRISMA %s phase %d not encoded (%s) FAIL\n",which,st.phase,
                         authored?reason.c_str():"not authored");
            s.failed=true;
        }
        auto receive=[&s,dof,which](int cw,int ch,std::vector<uint8_t> px) {
            auto& st=prismaEffectsState();
            if(cw<8||ch<8||px.size()!=size_t(cw*ch*4)){s.failed=true;return;}
            st.sharp[st.phase]=frameSharpness(cw,ch,px);
            {
                std::ofstream f(s.path+"."+which+std::to_string(st.phase)+".ppm",std::ios::binary);
                f<<"P6\n"<<cw<<" "<<ch<<"\n255\n";
                for(size_t i=0;i<px.size();i+=4)f.write(reinterpret_cast<const char*>(px.data()+i),3);
            }
            std::printf("PRISMA %s phase %d: sharpness %.3f\n",which,st.phase,st.sharp[st.phase]);
            if(++st.phase==(dof?3:4)) {
                const double base=st.sharp[0];
                // dof:    in focus within 15% of no effect, defocused at least 40% softer.
                // motion: turning + blur at least 20% softer than the same turn
                //         alone; a still camera with blur within 5% of no effect.
                bool ok=base>1.0;
                if(dof) ok=ok && st.sharp[1]>=base*.85 && st.sharp[2]<=base*.6;
                else ok=ok && st.sharp[2]<=st.sharp[1]*.8 && st.sharp[3]>=base*.95;
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
