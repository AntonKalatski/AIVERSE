#include "Prisma.h"
#include "PrismaVolumes.h"
#include "PrismaRhiExecutor.h"
#include <chrono>
#include "graph/ShaderGraph.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "PrismaShaders.h"

namespace aiverse::render {

namespace {

// Composite feature bits (also the shader cache key).
enum : uint32_t {
    kPfPixelate = 1u << 0, kPfCa = 1u << 1, kPfSharpen = 1u << 2, kPfTilt = 1u << 3, kPfBloom = 1u << 4,
    kPfShafts = 1u << 5, kPfGrade = 1u << 6, kPfLut = 1u << 7, kPfPosterize = 1u << 8, kPfVignette = 1u << 9,
    kPfGrain = 1u << 10, kPfFade = 1u << 11, kPfLetterbox = 1u << 12,
    kPfAutoExposure = 1u << 13,
};
const char* kPfNames[] = {"PF_PIXELATE", "PF_CA", "PF_SHARPEN", "PF_TILT", "PF_BLOOM", "PF_SHAFTS", "PF_GRADE",
                          "PF_LUT", "PF_POSTERIZE", "PF_VIGNETTE", "PF_GRAIN", "PF_FADE", "PF_LETTERBOX", "PF_AUTO_EXPOSURE"};

// Target roles inside one Sized set.
enum Role { kScene = 0, kAa = 1, kBloomDown = 10, kBloomUp = 20, kTilt0 = 30, kTilt1 = 31, kShaft0 = 40, kShaft1 = 41, kDof = 50, kExposureMip = 80, kExposureHistory = 100, kMotionVelocity = 130, kMotionColor = 131 };

struct SimpleU { float px[4]; float p0[4]; float sun[4]; };
struct MotionUniforms {
    float px[4], p0[4], sun[4];
    float inv0[4], inv1[4], inv2[4], inv3[4];
    float prev0[4], prev1[4], prev2[4], prev3[4];
};
const char* kMotionFields[] = {
    "px", "p0", "sun", "inv0", "inv1", "inv2", "inv3",
    "prev0", "prev1", "prev2", "prev3"
};
struct CompositeU {
    float px[4], grade0[4], grade1[4], lift[4], gamma[4], gain[4], filt[4], vig[4], bloom[4], shafts[4], tilt[4], fade[4], lut[4], lbox[4], graphFrame[4];
};
const char* kSimpleFields[] = {"px", "p0", "sun"};
const char* kCompositeFields[] = {"px", "grade0", "grade1", "lift", "gamma", "gain", "filt", "vig", "bloom", "shafts", "tilt", "fade", "lut", "lbox", "graphFrame"};

struct Built { std::string vs, fs, msl; };

// Wraps a common-subset body (PrismaShaders.h) into GLSL ES 3.00 and MSL.
Built buildProgram(const char* const* fields, size_t fieldCount, uint32_t texMask, const std::string& defines,
                   const char* helpers, const char* body, uint32_t depthMask = 0) {
    Built b;
    b.vs = "layout(location = 0) in vec2 a_pos;\nout vec2 v_screen;\n"
           "void main() { v_screen = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
    std::string block = "layout(std140) uniform U0 {";
    std::string mstruct = "struct U0 {", mlocals;
    for (size_t i = 0; i < fieldCount; ++i) {
        block += std::string(" vec4 ") + fields[i] + ";";
        mstruct += std::string(" float4 ") + fields[i] + ";";
        mlocals += std::string("    const float4 ") + fields[i] + " = u." + fields[i] + ";\n";
    }
    block += " };\n";
    mstruct += " };\n";
    const std::string common = "#define TUV(p) (px.z > 0.5 ? vec2((p).x, 1.0 - (p).y) : (p))\n"
                               "#define TDIR(d) (px.z > 0.5 ? vec2((d).x, -(d).y) : (d))\n";
    std::string gTex, mArgs, gMacros, mMacros;
    for (int t = 0; t < 9; ++t) {
        if (!(texMask & (1u << t))) continue;
        const std::string n = std::to_string(t);
        gTex += "uniform sampler2D tex" + n + ";\n";
        gMacros += "#define TEX" + n + "(p) texture(tex" + n + ", p)\n";
        const bool isDepth = (depthMask & (1u << t)) != 0;
        mArgs += std::string(", ") + (isDepth ? "depth2d<float>" : "texture2d<float>") +
                 " tex" + n + " [[texture(" + n + ")]], sampler tex" + n + "_s [[sampler(" + n + ")]]";
        mMacros += "#define TEX" + n + "(p) " + (isDepth ? "vec4(" : "") +
                   "tex" + n + ".sample(tex" + n + "_s, p)" + (isDepth ? ")" : "") + "\n";
    }
    b.fs = defines + block + gTex + "in vec2 v_screen;\nout vec4 o_color;\n" + common + gMacros +
           "#define RETURN(c) { o_color = (c); return; }\n#define PF_FN\n" + (helpers ? helpers : "") +
           "void main() {\n    vec2 s = v_screen;\n" + body + "}\n";
    b.msl = "#include <metal_stdlib>\nusing namespace metal;\n" + defines +
            "#define vec2 float2\n#define vec3 float3\n#define vec4 float4\n" + mstruct +
            "struct VIn { float2 pos [[attribute(0)]]; };\nstruct VOut { float4 pos [[position]]; float2 screen; };\n"
            "vertex VOut vs_main(VIn in [[stage_in]]) { VOut o; o.pos = float4(in.pos, 0.0, 1.0); o.screen = in.pos * 0.5 + 0.5; return o; }\n" +
            common + mMacros + "#define RETURN(c) return (c)\n#define PF_FN static\n" + (helpers ? helpers : "") +
            "fragment float4 fs_main(VOut in [[stage_in]], constant U0& u [[buffer(1)]]" + mArgs + ") {\n" + mlocals +
            "    vec2 s = in.screen;\n" + body + "}\n";
    return b;
}

void put(float* d, float a, float b, float c, float e) { d[0] = a; d[1] = b; d[2] = c; d[3] = e; }

} // namespace

PrismaQuality Prisma::defaultQualityFor(const gfx::Device& dev, bool mobile) {
    if (const char* q = std::getenv("AIVERSE_POSTFX")) {   // testing override: off|low|medium|high
        PrismaQuality r;
        if (parsePrismaQuality(q, r)) return r;
    }
    const std::string& n = dev.info().name;
    if (n.find("llvmpipe") != std::string::npos || n.find("SwiftShader") != std::string::npos) return PrismaQuality::Low;
    if (mobile) {
        for (const char* weak : {"Mali-4", "Mali-T", "Adreno (TM) 3", "Adreno (TM) 4", "Adreno (TM) 50", "PowerVR SGX", "PowerVR Rogue GE"})
            if (n.find(weak) != std::string::npos) return PrismaQuality::Low;
        return PrismaQuality::Medium;
    }
    return PrismaQuality::High;
}

bool Prisma::init(gfx::Device& dev) {
    dev_ = &dev;
    const float tri[6] = {-1, -1, 3, -1, -1, 3};
    gfx::BufferDesc bd; bd.data = tri; bd.size = sizeof tri; bd.label = "postfx triangle";
    triangle_ = dev.createBuffer(bd);
    const uint32_t zero = 0xff000000u;
    gfx::TextureDesc td; td.width = td.height = 1; td.data = &zero; td.label = "postfx black";
    black_ = dev.createTexture(td);
    return triangle_.valid();
}

void Prisma::release(gfx::TextureHandle& t) { if (t.valid() && dev_) dev_->destroyTexture(t); t = {}; }

void Prisma::shutdown() {
    if (!dev_) return;
    for (auto& s : sizes_) {
        if (s.graphExecutor) s.graphExecutor->resetAfterGpuIdle();
        for (auto& [r, t] : s.targets) release(t.tex);
        for (auto& [k, d] : s.depths) release(d);
    }
    sizes_.clear();
    for (auto& [id, view] : exposureViews_) {
        for (auto& history : view.history) release(history);
    }
    exposureViews_.clear();
    motionViews_.clear();
    for (auto& [p, l] : luts_) release(l.tex);
    luts_.clear();
    for(auto& [path,t]:graphTextures_)release(t);
    graphTextures_.clear();graphPaths_.clear();
    for (int i = 0; i < kProgCount; ++i) {
        if (pipes_[i].valid()) dev_->destroyPipeline(pipes_[i]);
        if (shaders_[i].valid()) dev_->destroyShader(shaders_[i]);
        pipes_[i] = {}; shaders_[i] = {}; failed_[i] = false;
    }
    for (auto& [m, sp] : composites_) { dev_->destroyPipeline(sp.second); dev_->destroyShader(sp.first); }
    composites_.clear();
    compositeFailed_.clear();
    if (triangle_.valid()) dev_->destroyBuffer(triangle_);
    release(black_);
    triangle_ = {};
    dev_ = nullptr;
}

void Prisma::setImageLoader(ImageLoader fn) {
    for(auto& [path,l]:luts_)release(l.tex);
    for(auto& [path,t]:graphTextures_)release(t);
    luts_.clear();graphTextures_.clear();loader_=std::move(fn);
}

void Prisma::configure(const Json& game, const Json& map, std::vector<std::string>* issues) {
    std::vector<std::string> local;
    setSettings(parsePrisma(game, map, issues ? issues : &local));
    if (!issues) for (auto& m : local) std::fprintf(stderr, "[postfx] %s\n", m.c_str());
}

void Prisma::setSettings(PrismaSettings s) {
    // New map/project configuration invalidates adaptation across scenes.
    resetAutoExposure();
    resetMotionHistory();
    baseSettings_ = s;
    settings_ = std::move(s);
    error_.clear();
}

void Prisma::applyCameraVolumes(const PrismaVolumeStack& stack, float3 eye, uint32_t cameraMask) {
    settings_ = stack.resolve(baseSettings_, eye, cameraMask);
}

void Prisma::clearCameraVolumes() { settings_ = baseSettings_; }

bool Prisma::wantsObjectMotionVectors() const {
    const auto* effect = settings_.find(PrismaType::MotionBlur);
    const char* env = std::getenv("AIVERSE_PRISMA_MOTION_BLUR");
    return enabled_ && settings_.enabled && hdrFrame_ &&
        env && std::strcmp(env, "1") == 0 &&
        effect && effect->enabled &&
        int(quality_) >= int(effect->minQuality) &&
        effect->get("intensity") > 0 && effect->get("maxPixels") > 0;
}
uint64_t Prisma::objectMotionEpoch(uint64_t id) const {
    const auto it = motionViews_.find(id);
    return it == motionViews_.end() ? 1 : it->second.cutEpoch;
}

bool Prisma::objectMotionContext(uint64_t& id, mat4& previous) const {
    id = viewId_;
    const auto* effect = settings_.find(PrismaType::MotionBlur);
    const char* env = std::getenv("AIVERSE_PRISMA_MOTION_BLUR");
    if (!env || std::strcmp(env, "1") != 0 || !enabled_ ||
        !settings_.enabled || !hdrFrame_ || !effect || !effect->enabled ||
        int(quality_) < int(effect->minQuality) ||
        effect->get("intensity") <= 0.0f || effect->get("maxPixels") <= 0.0f)
        return false;
    const auto found = motionViews_.find(viewId_);
    if (found == motionViews_.end() || !found->second.valid)
        return false;
    previous = found->second.previousViewProj;
    return true;
}
void Prisma::acceptObjectMotionVectors(gfx::TextureHandle texture) {
    setObjectMotionVectors(viewId_, texture);
}

void Prisma::setObjectMotionVectors(uint64_t id, gfx::TextureHandle texture) {
    motionViews_[id].objectVelocity = texture;
}
void Prisma::setMotionReactiveMask(uint64_t id, gfx::TextureHandle texture) {
    motionViews_[id].reactiveMask = texture;
}
void Prisma::acceptMotionReactiveMask(gfx::TextureHandle texture) {
    setMotionReactiveMask(viewId_, texture);
}
void Prisma::resetMotionHistory(uint64_t id) {
    // Epoch is created even if the camera has not rendered before; this
    // prevents a renderer's prior transform history from surviving a cut.
    auto& view = motionViews_[id];
    view.valid = false;
    ++view.cutEpoch;
    view.objectVelocity = {};
    view.reactiveMask = {};
}
void Prisma::resetMotionHistory() {
    for (auto& [id, view] : motionViews_) {
        view.valid = false;
        ++view.cutEpoch;
        view.objectVelocity = {};
        view.reactiveMask = {};
    }
}

void Prisma::resetAutoExposure() {
    for (auto& [id, view] : exposureViews_) {
        view.valid = false;
        view.time = 0;
    }
}
void Prisma::resetAutoExposure(uint64_t id) {
    auto it = exposureViews_.find(id);
    if (it != exposureViews_.end()) {
        it->second.valid = false;
        it->second.time = 0;
    }
}

bool Prisma::hdrRequested() const {
    if (!dev_ || hdrFailed_ || !enabled_ || !settings_.enabled || quality_ <= PrismaQuality::Low || !dev_->supports(gfx::PixelFormat::RGBA16F)) return false;
    HdrMode mode = settings_.hdr;
    if (const char* e = std::getenv("AIVERSE_HDR")) {
        const std::string v = e;
        if (v == "off" || v == "0") mode = HdrMode::Off;
        else if (v == "on" || v == "1") mode = HdrMode::On;
        else if (v == "auto") mode = HdrMode::Auto;
    }
    return hdrAtQuality(mode, quality_, dev_->supports(gfx::PixelFormat::RGBA16F));
}

bool Prisma::active() const {
    return dev_ && triangle_.valid() && enabled_ && (hdrRequested() || settings_.anyActive(quality_));
}

Prisma::Sized& Prisma::sized(int w, int h) {
    ++frame_;
    for (auto& s : sizes_) if (s.w == w && s.h == h) { s.lastUse = frame_; return s; }
    if (sizes_.size() >= 3) {   // drop the least recently used set (window resized, view closed)
        auto it = std::min_element(sizes_.begin(), sizes_.end(), [](const Sized& a, const Sized& b) { return a.lastUse < b.lastUse; });
        if (it->graphExecutor) it->graphExecutor->resetAfterGpuIdle();
        for (auto& [r, t] : it->targets) release(t.tex);
        for (auto& [k, d] : it->depths) release(d);
        sizes_.erase(it);
    }
    sizes_.reserve(3);
    sizes_.push_back({});
    Sized& s = sizes_.back();
    s.w = w; s.h = h; s.lastUse = frame_;
    return s;
}

Prisma::Target& Prisma::target(Sized& s, int role, int w, int h) {
    w = std::max(w, 1); h = std::max(h, 1);
    Target& t = s.targets[role];
    const auto format = hdrFrame_ ? gfx::PixelFormat::RGBA16F : gfx::PixelFormat::RGBA8;
    if (t.tex.valid() && t.w == w && t.h == h && t.format == format) return t;
    release(t.tex);
    gfx::TextureDesc td;
    td.width = w; td.height = h;
    td.renderTarget = true;
    td.filter = gfx::Filter::Linear;
    td.label = "postfx target";
    td.format = format; t.format = format;
    t.tex = dev_->createTexture(td);
    t.w = w; t.h = h;
    return t;
}

gfx::TextureHandle Prisma::depthFor(Sized& s, int w, int h, bool scratch) {
    const uint64_t key = (uint64_t(uint32_t(w)) << 32) | uint32_t(h) |
                         (scratch ? (uint64_t(1) << 63) : 0);
    auto it = s.depths.find(key);
    if (it != s.depths.end()) return it->second;
    gfx::TextureDesc td;
    td.width = w; td.height = h;
    td.format = gfx::PixelFormat::Depth;
    td.renderTarget = true;
    td.filter = gfx::Filter::Nearest;
    td.label = "postfx depth";
    return s.depths[key] = dev_->createTexture(td);
}

gfx::PipelineHandle Prisma::program(Prog p) {
    if (pipes_[p].valid() || failed_[p]) return pipes_[p];
    struct Def { const char* body; const char* helpers; uint32_t tex; const char* label; };
    static const Def defs[kProgCount] = {
        {prisma::kPrefilter, nullptr, 1, "postfx prefilter"},
        {prisma::kDown, nullptr, 1, "postfx down"},
        {prisma::kUp, nullptr, 3, "postfx up"},
        {prisma::kBlur, nullptr, 1, "postfx blur"},
        {prisma::kShaftsPre, nullptr, 1, "postfx shafts mask"},
        {prisma::kShafts, nullptr, 1, "postfx shafts"},
        {prisma::kFxaa, prisma::kFxaaHelpers, 1, "postfx fxaa"},
        {prisma::kDepthOfField, nullptr, 3, "prisma depth of field"},
        {prisma::kExposureLog, nullptr, 1, "prisma exposure luminance"},
        {prisma::kExposureReduce, nullptr, 1, "prisma exposure reduce"},
        {prisma::kExposureAdapt, nullptr, 3, "prisma exposure adapt"},
        {prisma::kMotionVectors, nullptr, 7, "prisma camera motion vectors"},
        {prisma::kMotionBlur, nullptr, 15, "prisma shutter motion blur"},
    };
    const Def& d = defs[p];
    const uint32_t sampledDepth = p == kMotionVectors || p == kDepthOfField
        ? (1u << 1) : (p == kMotionBlur ? (1u << 2) : 0u);
    Built b = (p == kMotionVectors)
        ? buildProgram(kMotionFields, 11, d.tex, "", d.helpers, d.body, sampledDepth)
        : buildProgram(kSimpleFields, 3, d.tex, "", d.helpers, d.body, sampledDepth);
    gfx::ShaderDesc sd;
    sd.glslVertex = b.vs.c_str(); sd.glslFragment = b.fs.c_str(); sd.msl = b.msl.c_str();
    sd.textureSlots = p == kMotionBlur ? 4 : (p == kMotionVectors ? 3 : 2);
    sd.uniformSlots = 1; sd.label = d.label;
    shaders_[p] = dev_->createShader(sd);
    if (!shaders_[p].valid()) {
        failed_[p] = true;
        error_ = std::string(d.label) + ": " + dev_->lastError();
        std::fprintf(stderr, "[postfx] %s\n", error_.c_str());
        return {};
    }
    gfx::PipelineDesc pd;
    pd.shader = shaders_[p];
    pd.attrs[0] = {0, gfx::VertexFormat::Float2, 0};
    pd.attrCount = 1;
    pd.stride = 8;
    pd.indexType = gfx::IndexType::None;
    pd.cull = gfx::CullMode::None;
    pd.depthTest = false; pd.depthWrite = false;
    pd.label = d.label;
    pipes_[p] = dev_->createPipeline(pd);
    if (!pipes_[p].valid()) failed_[p] = true;
    return pipes_[p];
}

gfx::PipelineHandle Prisma::composite(uint32_t mask, const Json* graphData) {
    const std::string key = std::to_string(mask) + (graphData ? graphData->dump(-1) : "");
    if (auto it = composites_.find(key); it != composites_.end()) return it->second.second;
    if (compositeFailed_.count(key)) return {};
    std::string defines;
    for (int i = 0; i < 14; ++i) if (mask & (1u << i)) defines += std::string("#define ") + kPfNames[i] + " 1\n";
    uint32_t tex = 1;
    if (mask & kPfBloom) tex |= 2;
    if (mask & kPfTilt) tex |= 4;
    if (mask & kPfShafts) tex |= 8;
    if (mask & kPfLut) tex |= 16;
    if (mask & kPfAutoExposure) tex |= (1u << 8);
    std::string body = prisma::kComposite;
    graph::Program graphProgram;
    if (graphData) {
        try {
            graphProgram=graph::compile(*graphData,"postfx");
            graphPaths_[graphData->dump(-1)]=graphProgram.textures;
            const std::string marker="    RETURN(vec4(clamp(col, vec3(0.0), vec3(1.0)), 1.0));";
            std::string macros="\n#define SG_SAMPLE(uv) TEX0(TUV(uv))\n";
            for(size_t i=0;i<graphProgram.textures.size();++i) {
                const auto slot=std::to_string(5+i);tex|=1u<<(5+i);
                macros+="#define SG_TEXTURE"+std::to_string(i)+"(uv) TEX"+slot+"(uv)\n";
            }
            body.replace(body.find(marker),marker.size(),macros+"vec2 sg_uv=s,sg_screen=s;vec4 sg_input=vec4(col,1.0);float sg_time=graphFrame.x;\n// SG_CODE\nRETURN("+graphProgram.outputs.at("color")+");\n");
        }catch(const std::exception& ex){error_=ex.what();compositeFailed_[key]=true;return {};}
    }
    Built b=buildProgram(kCompositeFields,15,tex,defines,prisma::kCompositeHelpers,body.c_str());
    if(graphData) {
        auto at=b.fs.find("// SG_CODE");b.fs.replace(at,10,graphProgram.glsl);
        at=b.msl.find("// SG_CODE");b.msl.replace(at,10,graphProgram.msl);
    }
    gfx::ShaderDesc sd;
    sd.glslVertex = b.vs.c_str(); sd.glslFragment = b.fs.c_str(); sd.msl = b.msl.c_str();
    sd.textureSlots = (mask & kPfAutoExposure) ? 9 : 5+int(graphProgram.textures.size());
    sd.uniformSlots = 1; sd.label = "postfx composite";
    gfx::ShaderHandle sh = dev_->createShader(sd);
    if (!sh.valid()) {
        compositeFailed_[key] = true;
        error_ = "postfx composite: " + dev_->lastError();
        std::fprintf(stderr, "[postfx] %s\n", error_.c_str());
        return {};
    }
    gfx::PipelineDesc pd;
    pd.shader = sh;
    pd.attrs[0] = {0, gfx::VertexFormat::Float2, 0};
    pd.attrCount = 1;
    pd.stride = 8;
    pd.indexType = gfx::IndexType::None;
    pd.cull = gfx::CullMode::None;
    pd.depthTest = false; pd.depthWrite = false;
    pd.label = "postfx composite";
    gfx::PipelineHandle p = dev_->createPipeline(pd);
    if (!p.valid()) { dev_->destroyShader(sh); compositeFailed_[key] = true; return {}; }
    composites_[key] = {sh, p};
    return p;
}

const Prisma::Lut* Prisma::lut(const std::string& path) {
    auto it = luts_.find(path);
    if (it != luts_.end()) return it->second.failed ? nullptr : &it->second;
    Lut& l = luts_[path];
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    if (!loader_ || !loader_(path, w, h, rgba)) {
        l.failed = true;
        std::fprintf(stderr, "[postfx] lut '%s': can't read the image\n", path.c_str());
        return nullptr;
    }
    if (h < 2 || w != h * h || rgba.size() < size_t(w) * size_t(h) * 4) {
        l.failed = true;
        std::fprintf(stderr, "[postfx] lut '%s': expected a strip of N squares N x N (e.g. 256x16, 1024x32), got %dx%d\n", path.c_str(), w, h);
        return nullptr;
    }
    gfx::TextureDesc td;
    td.width = w; td.height = h; td.data = rgba.data();
    td.filter = gfx::Filter::Linear;
    td.label = "postfx lut";
    l.tex = dev_->createTexture(td);
    l.n = h;
    if (!l.tex.valid()) { l.failed = true; return nullptr; }
    return &l;
}

gfx::TextureHandle Prisma::graphTexture(const std::string& path) {
    auto it=graphTextures_.find(path);if(it!=graphTextures_.end()){if(it->second.valid())return it->second;error_="shader graph: cannot load texture '"+path+"'";return black_;}
    int w=0,h=0;std::vector<uint8_t> bytes;
    if(!loader_ || !loader_(path,w,h,bytes) || w<=0 || h<=0 || bytes.size()<size_t(w)*size_t(h)*4) {
        error_="shader graph: cannot load texture '"+path+"'";graphTextures_[path]={};return black_;
    }
    gfx::TextureDesc td;td.width=w;td.height=h;td.data=bytes.data();td.filter=gfx::Filter::Linear;
    auto texture=dev_->createTexture(td);
    if(!texture.valid()){error_=dev_->lastError();return black_;}
    graphTextures_[path]=texture;return texture;
}

gfx::PassDesc Prisma::scenePass(const gfx::PassDesc& out, int w, int h) {
    hdrFrame_ = hdrRequested();
    Sized& s = sized(w, h);
    gfx::PassDesc p = out;
    p.colorTarget = target(s, kScene, w, h).tex;
    p.depthTarget = depthFor(s, w, h);
    sceneDepthForMotion_ = p.depthTarget;
    sceneReady_ = p.colorTarget.valid() && p.depthTarget.valid();
    if (!sceneReady_ && hdrFrame_) {
        hdrFailed_ = true; hdrFrame_ = false;
        std::fprintf(stderr, "[postfx] HDR allocation failed; falling back to RGBA8: %s\n", dev_->lastError().c_str());
        return scenePass(out, w, h);
    }
    if (!sceneReady_) return out;
    if (hdrFrame_) for (int i = 0; i < 3; ++i) {
        const float c = std::max(p.clearColor[i], 0.0f);
        p.clearColor[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
    p.clearColorEnabled = true;     // the composite covers every pixel; clearing is cheaper than loading on tilers
    p.clearDepthEnabled = true;
    return p;
}

void Prisma::draw(Sized& s, const Target& dst, gfx::PipelineHandle pipe, const void* u, size_t size,
                  std::initializer_list<std::pair<int, gfx::TextureHandle>> tex) {
    if (!dst.tex.valid() || !pipe.valid()) return; // never redirect a failed effect to the screen
    gfx::PassDesc p;
    p.colorTarget = dst.tex;
    p.depthTarget = depthFor(s, dst.w, dst.h);
    p.clearColor[0] = p.clearColor[1] = p.clearColor[2] = 0; p.clearColor[3] = 1;
    dev_->beginPass(p);
    dev_->setViewport({0, 0, dst.w, dst.h});
    dev_->setPipeline(pipe);
    dev_->setVertexBuffer(triangle_);
    dev_->setUniforms(0, u, size);
    for (auto& [slot, t] : tex) dev_->setTexture(slot, t);
    dev_->draw(3);
    dev_->endPass();
    ++stats_.passes;
}

void Prisma::resolve(gfx::Device& dev, const gfx::PassDesc& out, const PostFrame& f) {
    stats_ = {};
    if (!sceneReady_) { auto p = out; p.clearColorEnabled = p.clearDepthEnabled = false; dev.beginPass(p); return; }
    const int w = std::max(f.width, 1), h = std::max(f.height, 1);
    stats_.width = w; stats_.height = h;
    Sized& set = sized(w, h);
    const Target scene = target(set, kScene, w, h);
    const float flip = dev.info().renderTargetFlipY ? 1.0f : 0.0f;
    const auto active = settings_.activeAt(quality_);
    stats_.effects = int(active.size());
    auto find = [&](PrismaType t) -> const PrismaEffect* {
        for (auto* e : active) if (e->type == t) return e;
        return nullptr;
    };
    const float seed = float(frame_ % 61) * 0.173f;

    // ---- Stage 23: GPU-only HDR eye adaptation (linear scene -> log luminance
    // pyramid -> 1x1 exposure ping-pong). The 1x1 multiplier is applied in
    // composite BEFORE tone mapping; never post-process UI/HUD.
    gfx::TextureHandle automaticExposure;
    const PrismaEffect* autoEffect = find(PrismaType::AutoExposure);
    const char* autoFlag = std::getenv("AIVERSE_PRISMA_AUTO_EXPOSURE");
    const bool autoRequested = hdrFrame_ && autoEffect && autoFlag &&
        std::strcmp(autoFlag, "1") == 0;
    if (autoRequested && program(kExposureLog).valid() &&
        program(kExposureReduce).valid() && program(kExposureAdapt).valid()) {
        gfx::TextureHandle source = scene.tex;
        int pw = w, ph = h;
        bool valid = source.valid();
        for (int level = 0; valid && (pw > 1 || ph > 1) && level < 15; ++level) {
            const int nextW = std::max(1, (pw + 1) / 2);
            const int nextH = std::max(1, (ph + 1) / 2);
            const Target mip = target(set, kExposureMip + level, nextW, nextH);
            if (!mip.tex.valid() || !depthFor(set, nextW, nextH).valid()) {
                valid = false;
                break;
            }
            SimpleU measure{};
            put(measure.px, 1.0f / float(pw), 1.0f / float(ph), flip, 0);
            if (level == 0) {
                put(measure.p0, autoEffect->get("centerWeight"),
                    autoEffect->get("centerRadius"), 0, 0);
                put(measure.sun, autoEffect->get("minMeterLogL"),
                    autoEffect->get("maxMeterLogL"), 0, 0);
            }
            draw(set, mip, pipes_[level == 0 ? kExposureLog : kExposureReduce],
                 &measure, sizeof measure, {{0, source}});
            source = mip.tex;
            pw = nextW; ph = nextH;
        }
        // A 1x1 scene input still needs logarithmic luminance encoding.
        if (valid && pw == w && ph == h) {
            const Target mip = target(set, kExposureMip, 1, 1);
            if (mip.tex.valid() && depthFor(set, 1, 1).valid()) {
                SimpleU measure{};
                put(measure.px, 1, 1, flip, 0);
                put(measure.p0, autoEffect->get("centerWeight"),
                    autoEffect->get("centerRadius"), 0, 0);
                put(measure.sun, autoEffect->get("minMeterLogL"),
                    autoEffect->get("maxMeterLogL"), 0, 0);
                draw(set, mip, pipes_[kExposureLog], &measure, sizeof measure,
                     {{0, scene.tex}});
                source = mip.tex;
            } else valid = false;
        }
        // Avoid running the history filter with a partially reduced image.
        valid = valid && pw == 1 && ph == 1;
        auto& exposureView = exposureViews_[viewId_];
        // History belongs to the stable view ID, not to the shared 1x1
        // Sized cache: same-resolution Scene/Game views never ping-pong
        // against each other's exposure or time delta.
        for (auto& handle : exposureView.history) {
            if (handle.valid()) continue;
            gfx::TextureDesc desc;
            desc.width = desc.height = 1;
            desc.format = gfx::PixelFormat::RGBA16F;
            desc.renderTarget = true;
            desc.filter = gfx::Filter::Nearest;
            desc.label = "prisma exposure camera history";
            handle = dev.createTexture(desc);
        }
        const int nextIndex = 1 - exposureView.current;
        const gfx::TextureHandle nextExposure = exposureView.history[nextIndex];
        const gfx::TextureHandle prevExposure = exposureView.history[exposureView.current];
        if (valid && nextExposure.valid() && prevExposure.valid() &&
            depthFor(set, 1, 1).valid()) {
            const double timeNow =
                std::chrono::duration<double>(std::chrono::steady_clock::now()
                    .time_since_epoch()).count();
            const float dt = exposureView.valid && exposureView.time > 0
                ? std::clamp(float(timeNow - exposureView.time), 0.0f, 0.1f)
                : 0.0f;
            SimpleU adapt{};
            put(adapt.px, 1, 1, flip, 0);
            put(adapt.p0, dt, autoEffect->get("speedUp"),
                autoEffect->get("speedDown"), exposureView.valid ? 1.0f : 0.0f);
            put(adapt.sun, autoEffect->get("keyValue"),
                autoEffect->get("minEV"), autoEffect->get("maxEV"),
                autoEffect->get("compensation"));
            const Target exposureOutput{nextExposure, 1, 1, gfx::PixelFormat::RGBA16F};
            draw(set, exposureOutput, pipes_[kExposureAdapt], &adapt, sizeof adapt,
                 {{0, source}, {1, exposureView.valid ? prevExposure : black_}});
            exposureView.current = nextIndex;
            exposureView.valid = true;
            exposureView.time = timeNow;
            automaticExposure = nextExposure;
        } else {
            exposureView.valid = false;
        }
    } else {
        resetAutoExposure(viewId_);
    }

    // ---- Stage 25: view-specific camera/object motion vectors and blur.
    // Motion history stores matrices only (never prior gameplay/ECS values).
    // Signed velocities require RGBA16F: no implicit RGBA8 quantization.
    Target src = scene;
    auto& motionView = motionViews_[viewId_];
    const bool previousValid = motionView.valid &&
        motionView.width == w && motionView.height == h;
    const PrismaEffect* motionEffect = find(PrismaType::MotionBlur);
    const char* motionFlag = std::getenv("AIVERSE_PRISMA_MOTION_BLUR");
    if (motionEffect && motionFlag && std::strcmp(motionFlag, "1") == 0 &&
        hdrFrame_ && previousValid && motionEffect->get("intensity") > 0 &&
        motionEffect->get("maxPixels") > 0 &&
        program(kMotionVectors).valid() && program(kMotionBlur).valid()) {
        const Target velocity = target(set, kMotionVelocity, w, h);
        const Target blurred = target(set, kMotionColor, w, h);
        const auto sceneDepth = depthFor(set, w, h);
        const auto scratchDepth = depthFor(set, w, h, true);
        if (velocity.tex.valid() && blurred.tex.valid() &&
            scene.tex.valid() && sceneDepth.valid() && scratchDepth.valid()) {
            MotionUniforms vectors{};
            put(vectors.px, 1.0f / float(w), 1.0f / float(h), flip, 0);
            put(vectors.p0, dev.info().clipDepthZeroToOne ? 1.0f : 0.0f,
                motionView.objectVelocity.valid() ? 1.0f : 0.0f, 0, 0);
            const mat4 inverseViewProj = f.viewProj.inverse();
            std::memcpy(vectors.inv0, inverseViewProj.m, sizeof inverseViewProj.m);
            std::memcpy(vectors.prev0, motionView.previousViewProj.m,
                        sizeof motionView.previousViewProj.m);
            // Explicit RHI pass, not Prisma::draw(), because the scene depth
            // texture is simultaneously sampled: a different scratch depth
            // attachment prevents read/write feedback.
            auto raster = [&](const Target& output, gfx::PipelineHandle pipeline,
                              const void* uniforms, size_t bytes,
                              gfx::TextureHandle input0,
                              gfx::TextureHandle input1,
                              gfx::TextureHandle input2) {
                gfx::PassDesc pass;
                pass.colorTarget = output.tex;
                pass.depthTarget = scratchDepth;
                pass.clearColor[0] = pass.clearColor[1] = pass.clearColor[2] = 0;
                pass.clearColor[3] = 1;
                dev.beginPass(pass);
                dev.setViewport({0, 0, w, h});
                dev.setPipeline(pipeline);
                dev.setVertexBuffer(triangle_);
                dev.setUniforms(0, uniforms, bytes);
                dev.setTexture(0, input0);
                dev.setTexture(1, input1);
                if (pipeline == pipes_[kMotionVectors] || pipeline == pipes_[kMotionBlur])
                    dev.setTexture(2, input2);
                if (pipeline == pipes_[kMotionBlur])
                    dev.setTexture(3, motionView.reactiveMask.valid()
                                      ? motionView.reactiveMask : black_);
                dev.draw(3);
                dev.endPass();
                ++stats_.passes;
            };
            raster(velocity, pipes_[kMotionVectors], &vectors, sizeof vectors,
                   scene.tex, sceneDepth,
                   motionView.objectVelocity.valid() ? motionView.objectVelocity : black_);
            SimpleU blur{};
            put(blur.px, 1.0f / float(w), 1.0f / float(h), flip, 0);
            put(blur.p0, motionEffect->get("shutterScale"),
                motionEffect->get("maxPixels"),
                motionEffect->get("samples"), motionEffect->get("intensity"));
            put(blur.sun, motionEffect->get("depthThreshold"),
                motionEffect->get("reactiveStrength"), 0, 0);
            raster(blurred, pipes_[kMotionBlur], &blur, sizeof blur,
                   scene.tex, velocity.tex, sceneDepth);
            src = blurred;
        }
    }
    // Snapshot once per rendered view. A cut invalidates the previous data,
    // so the first subsequent frame presents sharp color and seeds history.
    motionView.previousViewProj = f.viewProj;
    motionView.width = w;
    motionView.height = h;
    motionView.valid = true;
    motionView.objectVelocity = {};
    motionView.reactiveMask = {};

    // ---- experimental depth of field: device-depth driven defocus
    // The scene depth texture must NEVER be attached for depth writes while
    // sampled, so a separate scratch depth attachment is used for this pass.
    if (const PrismaEffect* dof = find(PrismaType::DepthOfField)) {
        const char* flag = std::getenv("AIVERSE_PRISMA_DOF");
        if (flag && std::strcmp(flag, "1") == 0 &&
            dof->get("intensity") > 0.0f && dof->get("maxBlur") > 0.0f &&
            program(kDepthOfField).valid()) {
            const Target destination = target(set, kDof, w, h);
            const auto sceneDepth = depthFor(set, w, h);
            const auto scratchDepth = depthFor(set, w, h, true);
            if (sceneDepth.valid() && scratchDepth.valid() &&
                destination.tex.valid() && scene.tex.valid()) {
                SimpleU dofU{};
                put(dofU.px, 1.0f / float(w), 1.0f / float(h), flip, 0);
                put(dofU.p0, dof->get("focusDepth"),
                    dof->get("focusRange"), dof->get("maxBlur"),
                    dof->get("intensity"));
                gfx::PassDesc pass;
                pass.colorTarget = destination.tex;
                pass.depthTarget = scratchDepth;
                pass.clearColor[0] = pass.clearColor[1] = pass.clearColor[2] = 0;
                pass.clearColor[3] = 1;
                dev.beginPass(pass);
                dev.setViewport({0, 0, w, h});
                dev.setPipeline(pipes_[kDepthOfField]);
                dev.setVertexBuffer(triangle_);
                dev.setUniforms(0, &dofU, sizeof dofU);
                dev.setTexture(0, src.tex);
                dev.setTexture(1, sceneDepth);
                dev.draw(3);
                dev.endPass();
                ++stats_.passes;
                src = destination;
            }
        }
    }

    // ---- FXAA: the composite reads the anti-aliased image instead of the scene
    if (const PrismaEffect* e = find(PrismaType::Fxaa); !hdrFrame_ && e && program(kFxaa).valid()) {
        SimpleU u{};
        put(u.px, 1.0f / float(w), 1.0f / float(h), flip, 0);
        put(u.p0, e->get("subpixel"), 0, 0, 0);
        const Target fxaaInput = src;
        src = target(set, kAa, w, h);
        // Experimental Stage 14: only the SDR FXAA prepass uses the compiled
        // RenderGraph, with the same pipeline/uniforms/inputs as the fixed path.
        // Off by default; the fallback is kept for unsupported devices/errors.
        bool graphDrawn = false;
        const char* optIn = std::getenv("AIVERSE_PRISMA_GRAPH_FXAA");
        if (optIn && std::strcmp(optIn, "1") == 0 && src.tex.valid()) {
            const gfx::TextureHandle aaDepth = depthFor(set, w, h);
            if (aaDepth.valid()) {
                PrismaRenderGraph graph;
                const auto input = graph.addResource({
                    "scene-fxaa", PrismaGraphFormat::RGBA8,
                    static_cast<uint32_t>(w), static_cast<uint32_t>(h), true});
                const auto output = graph.addResource({
                    "fxaa-output", PrismaGraphFormat::RGBA8,
                    static_cast<uint32_t>(w), static_cast<uint32_t>(h), true});
                PrismaGraphPass fxaa;
                fxaa.name = "fxaa";
                fxaa.reads = {input};
                fxaa.writes = {output};
                graph.addPass(std::move(fxaa));
                const auto plan = graph.compile();
                // No graph-managed targets for this first pass: both handles
                // already exist in the same per-view Sized allocator.
                if (!set.graphExecutor) set.graphExecutor = std::make_unique<PrismaRhiExecutor>(dev);
                    auto& executor = *set.graphExecutor;
                std::vector<gfx::TextureHandle> external{fxaaInput.tex, src.tex};
                const auto run = executor.execute(
                    graph, plan, external,
                    [&](gfx::Device& gpu, PrismaGraphId,
                        const PrismaGraphDispatchView<gfx::TextureHandle>& view) {
                        gpu.setViewport({0, 0, w, h});
                        gpu.setPipeline(pipes_[kFxaa]);
                        gpu.setVertexBuffer(triangle_);
                        gpu.setUniforms(0, &u, sizeof u);
                        gpu.setTexture(0, view.resource(input));
                        gpu.draw(3);
                        return true;
                    },
                    [](gfx::Device&, PrismaGraphId,
                       const PrismaGraphDispatchView<gfx::TextureHandle>&) {
                        return false; // FXAA is raster-only.
                    },
                    aaDepth);
                graphDrawn = run.success;
                if (graphDrawn) ++stats_.passes;
                else if (error_.empty()) error_ = run.error;
            }
        }
        if (!graphDrawn)
            draw(set, src, pipes_[kFxaa], &u, sizeof u, {{0, fxaaInput.tex}});
    }

    // ---- bloom: prefilter -> dual-filter down chain -> up chain
    gfx::TextureHandle bloomTex;
    float bloomNorm = 1;
    const PrismaEffect* bloom = find(PrismaType::Bloom);
    if (bloom && program(kPrefilter).valid() && program(kDown).valid() && program(kUp).valid()) {
        const BloomPlan plan = bloomPlan(quality_);
        const char* chainFlag = std::getenv("AIVERSE_PRISMA_GRAPH_BLOOM_CHAIN");
        const bool chainEnabled = chainFlag && std::strcmp(chainFlag, "1") == 0;
        const char* poolFlag = std::getenv("AIVERSE_PRISMA_GRAPH_BLOOM_POOL");
        const bool pooledBloom = chainEnabled && poolFlag && std::strcmp(poolFlag, "1") == 0;
        std::vector<Target> down;
        int lw = std::max(1, w / plan.startDiv), lh = std::max(1, h / plan.startDiv);
        for (int i = 0; i < plan.levels && lw >= 2 && lh >= 2; ++i, lw /= 2, lh /= 2) {
            if (pooledBloom) {
                Target descriptor;
                descriptor.w = lw; descriptor.h = lh;
                down.push_back(descriptor);
            } else {
                down.push_back(target(set, kBloomDown + i, lw, lh));
            }
        }
        if (!down.empty()) {
            // Stage 16: opt-in complete Bloom graph. All physical textures
            // remain in the existing per-viewport Sized cache; the graph
            // handles pass dependencies/order, not cross-frame ownership.
            bool bloomChainHandled = false;
            if (chainEnabled && src.tex.valid()) {
                const PrismaGraphFormat format =
                    hdrFrame_ ? PrismaGraphFormat::RGBA16F : PrismaGraphFormat::RGBA8;
                std::vector<PrismaBloomMip> mipSizes;
                mipSizes.reserve(down.size());
                for (const auto& mip : down)
                    mipSizes.push_back({static_cast<uint32_t>(mip.w),
                                        static_cast<uint32_t>(mip.h)});
                // Only recompile when viewport dimensions, HDR mode or the
                // quality-dependent mip layout change. Per-frame uniforms and
                // texture handles remain dynamic.
                auto& cache = set.bloomGraph;
                if (cache.configure(static_cast<uint32_t>(src.w),
                                    static_cast<uint32_t>(src.h), format, mipSizes,
                                    pooledBloom)) {
                std::vector<Target> upTargets(down.size());
                for (size_t i = 0; i + 1 < down.size(); ++i) {
                    if (pooledBloom) {
                        upTargets[i].w = down[i].w;
                        upTargets[i].h = down[i].h;
                    } else {
                        upTargets[i] = target(set, kBloomUp + static_cast<int>(i),
                                              down[i].w, down[i].h);
                    }
                }

                std::vector<gfx::TextureHandle> handles(
                    cache.graph().resources().size());
                handles[cache.sourceId()] = src.tex;
                for (size_t i = 0; i < down.size(); ++i)
                    handles[cache.downIds()[i]] = down[i].tex;
                for (size_t i = 0; i + 1 < down.size(); ++i)
                    handles[cache.upIds()[i]] = upTargets[i].tex;

                struct BloomGraphOp {
                    Prog pipeline;
                    Target destination;
                    SimpleU uniforms;
                    PrismaGraphId input0;
                    PrismaGraphId input1;
                };
                std::vector<BloomGraphOp> ops;
                ops.reserve(cache.operations().size());
                const float scatter = 0.3f + 0.7f * bloom->get("radius");
                for (const auto& binding : cache.operations()) {
                    const size_t i = binding.mipIndex;
                    const bool upsample = binding.type == PrismaBloomPassType::Upsample;
                    Target destination = upsample ? upTargets[i] : down[i];
                    SimpleU bu{};
                    Prog pipeline = kPrefilter;
                    if (binding.type == PrismaBloomPassType::Prefilter) {
                        put(bu.px, float(plan.startDiv) * 0.5f / float(w),
                            float(plan.startDiv) * 0.5f / float(h), flip, 0);
                        put(bu.p0, bloom->get("threshold"), bloom->get("knee"), 0, 0);
                    } else if (binding.type == PrismaBloomPassType::Downsample) {
                        pipeline = kDown;
                        put(bu.px, 1.0f / float(down[i-1].w),
                            1.0f / float(down[i-1].h), flip, 0);
                    } else {
                        pipeline = kUp;
                        const Target low = (i + 1 == down.size() - 1)
                            ? down.back() : upTargets[i+1];
                        put(bu.px, 1.0f / float(low.w),
                            1.0f / float(low.h), flip, 0);
                        put(bu.p0, scatter, 0, 0, 0);
                    }
                    ops.push_back({pipeline, destination, bu,
                                   binding.input0, binding.input1});
                }
                const auto& graphPlan = cache.plan();
                std::vector<gfx::TextureHandle> depths;
                bool ready = graphPlan.valid;
                if (ready) for (const auto& op : ops) {
                    const auto depth = depthFor(set, op.destination.w, op.destination.h);
                    depths.push_back(depth);
                    ready = ready && depth.valid() &&
                        (pooledBloom || op.destination.tex.valid());
                }
                for (size_t id = 0; id < handles.size(); ++id)
                    if (cache.graph().resources()[id].external)
                        ready = ready && handles[id].valid();
                if (ready) {
                    if (!set.graphExecutor) set.graphExecutor = std::make_unique<PrismaRhiExecutor>(dev);
                    auto& executor = *set.graphExecutor;
                    const auto run = executor.execute(
                        cache.graph(), graphPlan, handles,
                        [&](gfx::Device& gpu, PrismaGraphId id,
                            const PrismaGraphDispatchView<gfx::TextureHandle>& view) {
                            const auto& op = ops[id];
                            gpu.setViewport({0, 0, op.destination.w, op.destination.h});
                            gpu.setPipeline(pipes_[op.pipeline]);
                            gpu.setVertexBuffer(triangle_);
                            gpu.setUniforms(0, &op.uniforms, sizeof op.uniforms);
                            gpu.setTexture(0, view.resource(op.input0));
                            if (op.input1 != kInvalidPrismaGraphId)
                                gpu.setTexture(1, view.resource(op.input1));
                            gpu.draw(3);
                            return true;
                        },
                        [](gfx::Device&, PrismaGraphId,
                           const PrismaGraphDispatchView<gfx::TextureHandle>&) {
                            return false;
                        }, {}, true,
                        [&](PrismaGraphId id) { return depths[id]; });
                    if (run.success) {
                        stats_.passes += static_cast<int>(run.passes);
                        bloomTex = pooledBloom ? run.resources[cache.outputId()] :
                            ((down.size() == 1) ? down.back().tex : upTargets[0].tex);
                        bloomNorm = 0.0f;
                        for (size_t k = 0; k < down.size(); ++k)
                            bloomNorm += std::pow(scatter, float(k));
                        bloomChainHandled = true;
                    } else {
                        if (error_.empty()) error_ = run.error;
                        // Never repeat a partially submitted GPU graph.
                        bloomChainHandled = run.passes > 0;
                        stats_.passes += static_cast<int>(run.passes);
                    }
                }
                } // compiled/cached Bloom graph is valid
            }
            if (!bloomChainHandled) {
            if (pooledBloom) for (size_t i = 0; i < down.size(); ++i)
                down[i] = target(set, kBloomDown + static_cast<int>(i),
                                 down[i].w, down[i].h);
            SimpleU u{};
            const float spread = float(plan.startDiv) * 0.5f;
            put(u.px, spread / float(w), spread / float(h), flip, 0);
            put(u.p0, bloom->get("threshold"), bloom->get("knee"), 0, 0);
            // Stage 15: optionally route bloom's *first* prefilter pass
            // through the same graph/RHI contract used by experimental FXAA.
            // The remaining bloom down/up passes keep the existing path.
            bool bloomGraphDrawn = false;
            const char* bloomOptIn = std::getenv("AIVERSE_PRISMA_GRAPH_BLOOM_PREFILTER");
            if (bloomOptIn && std::strcmp(bloomOptIn, "1") == 0 &&
                src.tex.valid() && down[0].tex.valid()) {
                const gfx::TextureHandle prefilterDepth =
                    depthFor(set, down[0].w, down[0].h);
                if (prefilterDepth.valid()) {
                    PrismaRenderGraph graph;
                    const PrismaGraphFormat pixelFormat =
                        hdrFrame_ ? PrismaGraphFormat::RGBA16F : PrismaGraphFormat::RGBA8;
                    const auto input = graph.addResource({
                        "bloom-source", pixelFormat,
                        static_cast<uint32_t>(src.w), static_cast<uint32_t>(src.h), true});
                    const auto output = graph.addResource({
                        "bloom-prefilter", pixelFormat,
                        static_cast<uint32_t>(down[0].w),
                        static_cast<uint32_t>(down[0].h), true});
                    PrismaGraphPass prefilter;
                    prefilter.name = "bloom-prefilter";
                    prefilter.reads = {input};
                    prefilter.writes = {output};
                    graph.addPass(std::move(prefilter));
                    const auto graphPlan = graph.compile();
                    if (!set.graphExecutor) set.graphExecutor = std::make_unique<PrismaRhiExecutor>(dev);
                    auto& executor = *set.graphExecutor;
                    const std::vector<gfx::TextureHandle> external{
                        src.tex, down[0].tex};
                    const auto result = executor.execute(
                        graph, graphPlan, external,
                        [&](gfx::Device& gpu, PrismaGraphId,
                            const PrismaGraphDispatchView<gfx::TextureHandle>& view) {
                            gpu.setViewport({0, 0, down[0].w, down[0].h});
                            gpu.setPipeline(pipes_[kPrefilter]);
                            gpu.setVertexBuffer(triangle_);
                            gpu.setUniforms(0, &u, sizeof u);
                            gpu.setTexture(0, view.resource(input));
                            gpu.draw(3);
                            return true;
                        },
                        [](gfx::Device&, PrismaGraphId,
                           const PrismaGraphDispatchView<gfx::TextureHandle>&) {
                            return false;
                        },
                        prefilterDepth);
                    bloomGraphDrawn = result.success;
                    if (bloomGraphDrawn) ++stats_.passes;
                    else if (error_.empty()) error_ = result.error;
                }
            }
            if (!bloomGraphDrawn)
                draw(set, down[0], pipes_[kPrefilter], &u, sizeof u, {{0, src.tex}});
            for (size_t i = 1; i < down.size(); ++i) {
                put(u.px, 1.0f / float(down[i - 1].w), 1.0f / float(down[i - 1].h), flip, 0);
                draw(set, down[i], pipes_[kDown], &u, sizeof u, {{0, down[i - 1].tex}});
            }
            const float scatter = 0.3f + 0.7f * bloom->get("radius");
            Target low = down.back();
            for (int i = int(down.size()) - 2; i >= 0; --i) {
                const Target up = target(set, kBloomUp + i, down[size_t(i)].w, down[size_t(i)].h);
                put(u.px, 1.0f / float(low.w), 1.0f / float(low.h), flip, 0);
                put(u.p0, scatter, 0, 0, 0);
                draw(set, up, pipes_[kUp], &u, sizeof u, {{0, low.tex}, {1, down[size_t(i)].tex}});
                low = up;
            }
            bloomTex = low.tex;
            bloomNorm = 0;
            for (size_t k = 0; k < down.size(); ++k) bloomNorm += std::pow(scatter, float(k));
            } // legacy Bloom chain
        }
    }

    // ---- tilt-shift: downsample + one separable gaussian (H, V)
    gfx::TextureHandle tiltTex;
    const PrismaEffect* tilt = find(PrismaType::TiltShift);
    if (tilt && program(kDown).valid() && program(kBlur).valid()) {
        const BlurPlan plan = tiltBlurPlan(quality_);
        const Target a = target(set, kTilt0, w / plan.div, h / plan.div), b = target(set, kTilt1, w / plan.div, h / plan.div);
        SimpleU u{};
        put(u.px, float(plan.div) * 0.5f / float(w), float(plan.div) * 0.5f / float(h), flip, 0);
        const SimpleU downU = u;
        const float spread = (0.5f + 2.0f * tilt->get("blur")) * (plan.div == 2 ? 2.0f : 1.0f);   // same radius on screen
        put(u.px, 1.0f / float(a.w), 1.0f / float(a.h), flip, 0);
        put(u.p0, spread, 0, 0, 0);
        const SimpleU horizontalU = u;
        put(u.p0, 0, spread, 0, 0);
        const SimpleU verticalU = u;

        bool graphHandled = false;
        const char* graphFlag = std::getenv("AIVERSE_PRISMA_GRAPH_TILT_SHIFT");
        if (graphFlag && std::strcmp(graphFlag, "1") == 0 &&
            src.tex.valid() && a.tex.valid() && b.tex.valid()) {
            const auto depth = depthFor(set, a.w, a.h);
            if (depth.valid()) {
                const auto format = hdrFrame_ ? PrismaGraphFormat::RGBA16F :
                                                PrismaGraphFormat::RGBA8;
                PrismaRenderGraph graph;
                const auto source = graph.addResource({
                    "tilt-source", format, static_cast<uint32_t>(src.w),
                    static_cast<uint32_t>(src.h), true});
                const auto down = graph.addResource({
                    "tilt-down", format, static_cast<uint32_t>(a.w),
                    static_cast<uint32_t>(a.h), true});
                const auto horizontal = graph.addResource({
                    "tilt-horizontal", format, static_cast<uint32_t>(b.w),
                    static_cast<uint32_t>(b.h), true});
                const auto output = graph.addResource({
                    "tilt-output", format, static_cast<uint32_t>(a.w),
                    static_cast<uint32_t>(a.h), true});
                auto pass = [&](const char* name, PrismaGraphId input,
                                PrismaGraphId dest) {
                    PrismaGraphPass p; p.name = name;
                    p.reads = {input}; p.writes = {dest};
                    graph.addPass(std::move(p));
                };
                pass("tilt-downsample", source, down);
                pass("tilt-blur-horizontal", down, horizontal);
                pass("tilt-blur-vertical", horizontal, output);
                // The two logical versions of A alias the same existing
                // physical target, but their lifetimes never overlap.
                const std::vector<gfx::TextureHandle> handles{
                    src.tex, a.tex, b.tex, a.tex};
                if (!set.graphExecutor)
                    set.graphExecutor = std::make_unique<PrismaRhiExecutor>(dev);
                const auto result = set.graphExecutor->execute(
                    graph, graph.compile(), handles,
                    [&](gfx::Device& gpu, PrismaGraphId id,
                        const PrismaGraphDispatchView<gfx::TextureHandle>& view) {
                        const SimpleU& params = id == 0 ? downU :
                            (id == 1 ? horizontalU : verticalU);
                        gpu.setViewport({0, 0, a.w, a.h});
                        gpu.setPipeline(id == 0 ? pipes_[kDown] : pipes_[kBlur]);
                        gpu.setVertexBuffer(triangle_);
                        gpu.setUniforms(0, &params, sizeof params);
                        gpu.setTexture(0, view.resource(id == 0 ? source :
                            (id == 1 ? down : horizontal)));
                        gpu.draw(3);
                        return true;
                    },
                    [](gfx::Device&, PrismaGraphId,
                       const PrismaGraphDispatchView<gfx::TextureHandle>&) {
                        return false;
                    }, depth);
                graphHandled = result.success || result.passes > 0;
                stats_.passes += static_cast<int>(result.passes);
                if (!result.success && error_.empty()) error_ = result.error;
                if (result.success) tiltTex = a.tex;
            }
        }
        if (!graphHandled) {
            draw(set, a, pipes_[kDown], &downU, sizeof downU, {{0, src.tex}});
            draw(set, b, pipes_[kBlur], &horizontalU, sizeof horizontalU, {{0, a.tex}});
            draw(set, a, pipes_[kBlur], &verticalU, sizeof verticalU, {{0, b.tex}});
            tiltTex = a.tex;
        }
    }

    // ---- sun shafts: from the sun on screen, or (top-down RTS cameras rarely see the sun) from a point
    // just past the screen edge in the direction the sunlight comes from
    gfx::TextureHandle shaftTex;
    float shaftColor[3] = {0, 0, 0};
    const PrismaEffect* shafts = find(PrismaType::SunShafts);
    if (shafts && program(kShaftsPre).valid() && program(kShafts).valid()) {
        const float3 sd = normalize(f.lighting.sunDir);
        const float4 c = f.viewProj * float4{sd.x, sd.y, sd.z, 0.0f};   // direction: the sun at infinity
        float nx = 0, ny = 0;
        bool placed = false;
        if (c.w > 1e-4f && std::max(std::fabs(c.x / c.w), std::fabs(c.y / c.w)) < 1.3f) { nx = c.x / c.w; ny = c.y / c.w; placed = true; }
        else {
            const mat4 inv = f.viewProj.inverse();
            const float4 m = inv * float4{0, 0, 0.5f, 1};
            if (std::fabs(m.w) > 1e-6f) {
                const float3 p0{m.x / m.w, m.y / m.w, m.z / m.w};
                const float3 p1 = p0 + sd * std::max(1.0f, length(p0 - f.cameraPos) * 0.25f);
                const float4 a0 = f.viewProj * float4{p0.x, p0.y, p0.z, 1}, a1 = f.viewProj * float4{p1.x, p1.y, p1.z, 1};
                if (a0.w > 1e-4f && a1.w > 1e-4f) {
                    float dx = a1.x / a1.w - a0.x / a0.w, dy = a1.y / a1.w - a0.y / a0.w;
                    const float l = std::sqrt(dx * dx + dy * dy);
                    if (l > 1e-5f) { nx = dx / l * 1.35f; ny = dy / l * 1.35f; placed = true; }
                }
            }
        }
        const float vis = placed ? std::clamp(f.lighting.sunIntensity, 0.0f, 1.0f) * std::clamp(sd.y * 4.0f + 0.2f, 0.0f, 1.0f) : 0.0f;
        {
            if (vis > 0.01f) {
                const ShaftsPlan plan = shaftsPlan(quality_);
                float sx = nx * 0.5f + 0.5f, sy = ny * 0.5f + 0.5f;
                if (flip > 0.5f) sy = 1.0f - sy;
                const Target a = target(set, kShaft0, w / plan.div, h / plan.div), b = target(set, kShaft1, w / plan.div, h / plan.div);
                SimpleU u{};
                const float spread = float(plan.div) * 0.5f;
                put(u.px, spread / float(w), spread / float(h), flip, 0);
                put(u.p0, shafts->get("threshold"), 0, 0, 0);
                put(u.sun, sx, sy, 0, float(w) / float(h));
                const SimpleU prefilterU = u;
                put(u.px, 1.0f / float(a.w), 1.0f / float(a.h), flip, 0);
                put(u.p0, float(plan.samples), 0.3f + 0.6f * shafts->get("length"), 0.97f, 1.6f);
                const SimpleU radialU = u;
                bool shaftGraphHandled = false;
                const char* graphFlag = std::getenv("AIVERSE_PRISMA_GRAPH_SUN_SHAFTS");
                if (graphFlag && std::strcmp(graphFlag, "1") == 0 &&
                    src.tex.valid() && a.tex.valid() && b.tex.valid()) {
                    const auto depth = depthFor(set, a.w, a.h);
                    if (depth.valid()) {
                        const auto fmt = hdrFrame_ ? PrismaGraphFormat::RGBA16F :
                                                     PrismaGraphFormat::RGBA8;
                        PrismaRenderGraph graph;
                        const auto source = graph.addResource({
                            "shafts-source", fmt, static_cast<uint32_t>(src.w),
                            static_cast<uint32_t>(src.h), true});
                        const auto mask = graph.addResource({
                            "shafts-mask", fmt, static_cast<uint32_t>(a.w),
                            static_cast<uint32_t>(a.h), true});
                        const auto output = graph.addResource({
                            "shafts-radial", fmt, static_cast<uint32_t>(b.w),
                            static_cast<uint32_t>(b.h), true});
                        PrismaGraphPass pre;
                        pre.name = "shafts-prefilter"; pre.reads = {source};
                        pre.writes = {mask}; graph.addPass(std::move(pre));
                        PrismaGraphPass radial;
                        radial.name = "shafts-radial"; radial.reads = {mask};
                        radial.writes = {output}; graph.addPass(std::move(radial));
                        if (!set.graphExecutor)
                            set.graphExecutor = std::make_unique<PrismaRhiExecutor>(dev);
                        const auto run = set.graphExecutor->execute(
                            graph, graph.compile(),
                            std::vector<gfx::TextureHandle>{src.tex, a.tex, b.tex},
                            [&](gfx::Device& gpu, PrismaGraphId id,
                                const PrismaGraphDispatchView<gfx::TextureHandle>& view) {
                                const SimpleU& params = id == 0 ? prefilterU : radialU;
                                gpu.setViewport({0, 0, a.w, a.h});
                                gpu.setPipeline(id == 0 ? pipes_[kShaftsPre] : pipes_[kShafts]);
                                gpu.setVertexBuffer(triangle_);
                                gpu.setUniforms(0, &params, sizeof params);
                                gpu.setTexture(0, view.resource(id == 0 ? source : mask));
                                gpu.draw(3);
                                return true;
                            },
                            [](gfx::Device&, PrismaGraphId,
                               const PrismaGraphDispatchView<gfx::TextureHandle>&) {
                                return false;
                            }, depth);
                        shaftGraphHandled = run.success || run.passes > 0;
                        stats_.passes += static_cast<int>(run.passes);
                        if (!run.success && error_.empty()) error_ = run.error;
                        if (run.success) shaftTex = b.tex;
                    }
                }
                if (!shaftGraphHandled) {
                    draw(set, a, pipes_[kShaftsPre], &prefilterU,
                         sizeof prefilterU, {{0, src.tex}});
                    draw(set, b, pipes_[kShafts], &radialU,
                         sizeof radialU, {{0, a.tex}});
                    shaftTex = b.tex;
                }
                const float* col = shafts->color("color");
                const float k = shafts->get("intensity") * vis;
                for (int i = 0; i < 3; ++i) shaftColor[i] = col[i] * k;
            }
        }
    }

    // ---- composite into the caller's pass (left open)
    CompositeU u{};
    const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if(graphEpoch_<0)graphEpoch_=now;
    u.graphFrame[0]=float(now-graphEpoch_);
    uint32_t mask = 0;
    put(u.px, 1.0f / float(w), 1.0f / float(h), flip, seed);
    put(u.grade1, 1, 1, 1, 1);
    put(u.gamma, 1, 1, 1, 0.5f);
    put(u.gain, 1, 1, 1, 0);
    put(u.grade0, 0, 1, 1, 8);
    put(u.filt, hdrFrame_ ? 1.0f : 0.0f, f.lighting.tonemap ? 1.0f : 0.0f, 1, 1);
    for (auto* e : active) {
        switch (e->type) {
        case PrismaType::Pixelate: mask |= kPfPixelate; u.grade1[3] = std::max(1.0f, e->get("size")); break;
        case PrismaType::ChromaticAberration: mask |= kPfCa; u.vig[3] = e->get("intensity"); break;
        case PrismaType::Sharpen: mask |= kPfSharpen; u.bloom[3] = e->get("amount"); break;
        case PrismaType::TiltShift:
            if (tiltTex.valid()) { mask |= kPfTilt; put(u.tilt, e->get("focus"), e->get("width"), e->get("falloff"), 0); }
            break;
        case PrismaType::Bloom:
            if (bloomTex.valid()) {
                mask |= kPfBloom;
                const float* t = e->color("tint");
                const float k = e->get("intensity") / std::max(bloomNorm, 1.0f);
                for (int i = 0; i < 3; ++i) u.bloom[i] = t[i] * k;
            }
            break;
        case PrismaType::SunShafts:
            if (shaftTex.valid()) { mask |= kPfShafts; for (int i = 0; i < 3; ++i) u.shafts[i] = shaftColor[i]; }
            break;
        case PrismaType::ColorGrade: {
            mask |= kPfGrade;
            const float exposure = std::exp2(e->get("exposure"));
            const float ex = hdrFrame_ ? 1.0f : exposure;
            if (hdrFrame_) u.filt[2] = exposure;
            const float t = e->get("temperature"), ti = e->get("tint");
            const float* fl = e->color("filter");
            const float wb[3] = {1.0f + 0.25f * t + 0.1f * ti, 1.0f - 0.2f * ti, 1.0f - 0.25f * t + 0.1f * ti};
            for (int i = 0; i < 3; ++i) u.grade1[i] = ex * wb[i] * fl[i];
            const float* li = e->color("lift");
            const float* ga = e->color("gamma");
            const float* gn = e->color("gain");
            for (int i = 0; i < 3; ++i) { u.lift[i] = li[i]; u.gamma[i] = 1.0f / std::max(ga[i], 0.05f); u.gain[i] = gn[i]; }
            u.grade0[1] = e->get("contrast");
            u.grade0[2] = e->get("saturation");
            break;
        }
        case PrismaType::Lut:
            if (const Lut* l = lut(e->text)) { mask |= kPfLut; u.lut[0] = float(l->n); u.lut[1] = e->get("flipY"); u.shafts[3] = e->get("intensity"); }
            break;
        case PrismaType::Posterize: mask |= kPfPosterize; u.grade0[3] = std::max(2.0f, e->get("levels")); break;
        case PrismaType::Vignette: {
            mask |= kPfVignette;
            u.lift[3] = e->get("intensity");
            u.gamma[3] = e->get("softness");
            const float* c = e->color("color");
            for (int i = 0; i < 3; ++i) u.vig[i] = c[i];
            break;
        }
        case PrismaType::Grain:
            mask |= kPfGrain;
            u.gain[3] = e->get("intensity");
            u.filt[3] = std::max(1.0f, e->get("size"));
            if (e->get("animated") < 0.5f) u.px[3] = 0;
            break;
        case PrismaType::Fade: { mask |= kPfFade; const float* c = e->color("color"); put(u.fade, c[0], c[1], c[2], e->get("amount")); break; }
        case PrismaType::Letterbox: { mask |= kPfLetterbox; const float* c = e->color("color"); put(u.lbox, c[0], c[1], c[2], e->get("amount")); break; }
        default: break;
        }
    }
    const auto* graphEffect = find(PrismaType::ShaderGraph);
    const Json* graphData = graphEffect ? &graphEffect->shaderGraph : nullptr;
    std::vector<gfx::TextureHandle> graphImages;
    if (graphEffect) {
        // Populate the shader graph texture list before deciding whether
        // fixed slot 8 is available for the HDR exposure history texture.
        // Existing compiled composite variants are cached by mask+graph.
        composite(mask, graphData);
        const auto it = graphPaths_.find(graphEffect->shaderGraph.dump(-1));
        if (it != graphPaths_.end())
            for (const auto& path : it->second)
                graphImages.push_back(graphTexture(path));
    }
    // Custom graph textures occupy consecutive slots 5..8; never bind
    // automatic exposure over one of them. The shader path remains valid,
    // but auto exposure is omitted for graphs using all four slots.
    const bool useAutomaticExposure = automaticExposure.valid() &&
        graphImages.size() <= 3;
    if (useAutomaticExposure) mask |= kPfAutoExposure;
    gfx::PipelineHandle pipe = composite(mask, graphData);
    if (!pipe.valid()) {
        // A failed exposure variant may fall back to identical non-auto
        // color processing without binding an absent TEX8 sampler.
        mask &= ~kPfAutoExposure;
        pipe = composite(mask, graphData);
    }
    if (!pipe.valid()) pipe = composite(0);   // a broken variant still shows the scene
    const PrismaEffect* finalAa = hdrFrame_ ? find(PrismaType::Fxaa) : nullptr;
    bool aaAfter = finalAa && program(kFxaa).valid();
    gfx::PassDesc compositeOut = out;
    Target aaTarget;
    if (aaAfter) {
        hdrFrame_ = false; aaTarget = target(set, kAa, w, h); hdrFrame_ = true;
        const auto aaDepth = depthFor(set, w, h);
        aaAfter = aaTarget.tex.valid() && aaDepth.valid();
        if (aaAfter) { compositeOut.colorTarget = aaTarget.tex; compositeOut.depthTarget = aaDepth; }
    }
    dev.beginPass(compositeOut);
    dev.setViewport({0, 0, w, h});
    if (pipe.valid()) {
        dev.setPipeline(pipe);
        dev.setVertexBuffer(triangle_);
        dev.setUniforms(0, &u, sizeof u);
        dev.setTexture(0, src.tex);
        dev.setTexture(1, bloomTex.valid() ? bloomTex : black_);
        dev.setTexture(2, tiltTex.valid() ? tiltTex : black_);
        dev.setTexture(3, shaftTex.valid() ? shaftTex : black_);
        const Lut* l = (mask & kPfLut) ? lut(find(PrismaType::Lut)->text) : nullptr;
        dev.setTexture(4, l ? l->tex : black_);
        for(size_t i=0;i<graphImages.size();++i)dev.setTexture(int(i)+5,graphImages[i]);
        if (mask & kPfAutoExposure) dev.setTexture(8, automaticExposure);
        dev.draw(3);
    }
    ++stats_.passes;
    if (aaAfter) {
        dev.endPass(); dev.beginPass(out); dev.setViewport({0, 0, w, h});
        SimpleU au{}; put(au.px, 1.0f / w, 1.0f / h, flip, 0); put(au.p0, finalAa->get("subpixel"), 0, 0, 0);
        dev.setPipeline(pipes_[kFxaa]); dev.setVertexBuffer(triangle_); dev.setUniforms(0, &au, sizeof au);
        dev.setTexture(0, aaTarget.tex); dev.draw(3); ++stats_.passes;
    }
}

} // namespace aiverse::render
