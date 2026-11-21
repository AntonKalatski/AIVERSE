// Real EGL/GLES3 acceptance for the GL backend's redundant-state filter and native
// base-vertex draws: a frame with pipeline, texture, uniform and buffer churn
// (including resources recreated mid-frame, whose GL names may be reused) must
// produce exactly the expected pixels, every frame. Optional benchmark mode
// (AIVERSE_GL_BENCH=draws) times CPU submission of many redundant draws.
#include "engine/gfx/Gfx.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace aiverse::gfx;

namespace {
constexpr int kSize = 96;   // 3x3 grid of 32 px cells
int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", what.c_str()); }
}
struct Context {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    bool init() {
        auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (getPlatformDisplay) display = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
        if (display == EGL_NO_DISPLAY) display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint major = 0, minor = 0;
        if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API)) return false;
        const EGLint configAttribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE};
        EGLConfig config = nullptr; EGLint count = 0;
        if (!eglChooseConfig(display, configAttribs, &config, 1, &count) || count != 1) return false;
        const EGLint surfaceAttribs[] = {EGL_WIDTH, kSize, EGL_HEIGHT, kSize, EGL_NONE};
        surface = eglCreatePbufferSurface(display, config, surfaceAttribs);
        const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
        return surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT &&
               eglMakeCurrent(display, surface, surface, context) == EGL_TRUE;
    }
    ~Context() {
        if (display == EGL_NO_DISPLAY) return;
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
        if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
        eglTerminate(display);
    }
};

const char* kVs = R"(
layout(std140) uniform U0 { vec4 place; vec4 tint; };
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUv;
out vec2 vUv;
void main() { vUv = aUv; gl_Position = vec4(aPos * place.zw + place.xy, 0.0, 1.0); }
)";
const char* kFsTex = R"(
precision highp float;
uniform sampler2D tex0;
layout(std140) uniform U0 { vec4 place; vec4 tint; };
in vec2 vUv;
out vec4 fragColor;
void main() { fragColor = texture(tex0, vUv) * tint; }
)";
const char* kFsColor = R"(
precision highp float;
layout(std140) uniform U0 { vec4 place; vec4 tint; };
in vec2 vUv;
out vec4 fragColor;
void main() { fragColor = tint; }
)";

struct Block { float place[4]; float tint[4]; };
// Cell (cx, cy) of the 3x3 grid, cy=0 at the top; the quad covers the cell.
Block cell(int cx, int cy, std::array<float, 4> tint) {
    const float w = 2.0f / 3.0f;
    Block b{};
    b.place[0] = -1.0f + w * (float(cx) + 0.5f);
    b.place[1] = 1.0f - w * (float(cy) + 0.5f);
    b.place[2] = b.place[3] = w * 0.5f;
    std::memcpy(b.tint, tint.data(), sizeof b.tint);
    return b;
}
TextureHandle solid(Device& dev, std::array<uint8_t, 4> c) {
    std::vector<uint8_t> px(4 * 4 * 4);
    for (size_t i = 0; i < px.size(); i += 4) std::memcpy(&px[i], c.data(), 4);
    TextureDesc d; d.width = d.height = 4; d.format = PixelFormat::RGBA8; d.filter = Filter::Nearest; d.data = px.data();
    return dev.createTexture(d);
}
std::array<uint8_t, 4> pixelAt(int cx, int cy) {
    std::array<uint8_t, 4> p{};
    const int x = cx * 32 + 16, y = kSize - 1 - (cy * 32 + 16);   // GL origin is bottom-left
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
    return p;
}
bool near(std::array<uint8_t, 4> a, std::array<uint8_t, 4> b) {
    for (int i = 0; i < 3; ++i) if (std::abs(int(a[i]) - int(b[i])) > 2) return false;
    return true;
}
} // namespace

int main() {
    Context egl;
    if (!egl.init()) { std::fprintf(stderr, "SKIP: EGL ES3 pbuffer context unavailable\n"); return 77; }
    while (glGetError() != GL_NO_ERROR) {}
    auto dev = createGLDevice(Backend::GLES3);
    if (!dev) { std::fprintf(stderr, "FAIL: device\n"); return 1; }
    std::printf("device: %s\n", dev->info().name.c_str());

    ShaderDesc texShader; texShader.glslVertex = kVs; texShader.glslFragment = kFsTex; texShader.textureSlots = 1;
    ShaderDesc colorShader; colorShader.glslVertex = kVs; colorShader.glslFragment = kFsColor;
    const ShaderHandle sTex = dev->createShader(texShader), sColor = dev->createShader(colorShader);
    check(sTex.valid() && sColor.valid(), "shaders: " + dev->lastError());

    PipelineDesc base;
    base.attrCount = 2; base.stride = 16;
    base.attrs[0] = {0, VertexFormat::Float2, 0};
    base.attrs[1] = {1, VertexFormat::Float2, 8};
    base.cull = CullMode::None; base.depthTest = false; base.depthWrite = false;
    PipelineDesc opaque = base; opaque.shader = sTex;
    PipelineDesc opaqueTwin = opaque;                  // same state, different pipeline object
    PipelineDesc blended = base; blended.shader = sColor; blended.blend = BlendMode::Alpha;
    PipelineDesc culled = opaque; culled.cull = CullMode::Back; // quads are CCW: still visible
    const PipelineHandle pOpaque = dev->createPipeline(opaque), pTwin = dev->createPipeline(opaqueTwin),
                         pBlend = dev->createPipeline(blended), pCulled = dev->createPipeline(culled);
    check(pOpaque.valid() && pTwin.valid() && pBlend.valid() && pCulled.valid(), "pipelines: " + dev->lastError());

    // Two meshes in one pooled vertex buffer: mesh B starts at vertex 4 (baseVertex draws).
    const float verts[] = {
        -1, -1, 0, 0,   1, -1, 1, 0,   1, 1, 1, 1,   -1, 1, 0, 1,            // mesh A
        -0.6f, -0.6f, 0, 0,  0.6f, -0.6f, 1, 0,  0.6f, 0.6f, 1, 1,  -0.6f, 0.6f, 0, 1}; // mesh B (smaller)
    const uint16_t idx[] = {0, 1, 2, 0, 2, 3};
    BufferDesc vb; vb.type = BufferType::Vertex; vb.size = sizeof verts; vb.data = verts;
    BufferDesc ib; ib.type = BufferType::Index; ib.size = sizeof idx; ib.data = idx;
    const BufferHandle hVb = dev->createBuffer(vb);
    BufferHandle hIb = dev->createBuffer(ib);

    TextureHandle red = solid(*dev, {255, 0, 0, 255}), green = solid(*dev, {0, 255, 0, 255});
    const std::array<float, 4> white{1, 1, 1, 1};
    bool ok = true;
    for (int frame = 0; frame < 3; ++frame) {
        dev->beginFrame(kSize, kSize);
        PassDesc pass; pass.clearColor[0] = pass.clearColor[1] = pass.clearColor[2] = 0; pass.clearColor[3] = 1;
        dev->beginPass(pass);
        dev->setVertexBuffer(hVb, 0);
        dev->setIndexBuffer(hIb, 0);
        auto draw = [&](PipelineHandle p, TextureHandle t, Block b, int32_t baseVertex) {
            dev->setPipeline(p);
            if (t.valid()) dev->setTexture(0, t);
            dev->setUniforms(0, &b, sizeof b);
            dev->drawIndexed(6, 0, baseVertex, 1);
        };
        draw(pOpaque, red, cell(0, 0, white), 0);                        // (0,0) red
        draw(pOpaque, green, cell(1, 0, white), 4);                      // (1,0) green, baseVertex 4 (inner)
        draw(pTwin, green, cell(2, 0, white), 0);                        // twin pipeline, same texture
        draw(pBlend, {}, cell(0, 1, {0, 0, 1, 0.5f}), 0);                // (0,1) 50% blue over black
        draw(pOpaque, red, cell(1, 1, {1, 1, 1, 1}), 4);                 // back to opaque after blend
        // Identical uniforms twice, then a different block: must not reuse a stale one.
        draw(pOpaque, red, cell(2, 1, {0, 1, 1, 1}), 0);
        draw(pOpaque, red, cell(2, 1, {0, 1, 1, 1}), 0);
        draw(pCulled, red, cell(0, 2, {1, 1, 0, 1}), 0);                 // red*yellow = red, culling on
        // Destroy a bound texture and create another mid-frame (GL may hand out the
        // same name), recreate the index buffer: the next draws must use the new ones.
        TextureHandle temp = solid(*dev, {255, 0, 255, 255});
        draw(pOpaque, temp, cell(1, 2, white), 0);
        dev->destroyTexture(temp);
        temp = solid(*dev, {0, 0, 255, 255});
        dev->destroyBuffer(hIb);
        hIb = dev->createBuffer(ib);
        dev->setIndexBuffer(hIb, 0);
        draw(pOpaque, temp, cell(1, 2, white), 0);                       // (1,2) blue, not magenta
        draw(pBlend, {}, cell(2, 2, {1, 1, 1, 1}), 4);                   // (2,2) white inner, blend pipeline
        dev->endPass();
        dev->endFrame();
        glFinish();
        dev->destroyTexture(temp);
        struct Expect { int cx, cy; std::array<uint8_t, 4> rgba; };
        const Expect expect[] = {
            {0, 0, {255, 0, 0, 255}}, {1, 0, {0, 255, 0, 255}}, {2, 0, {0, 255, 0, 255}},
            {0, 1, {0, 0, 128, 255}}, {1, 1, {255, 0, 0, 255}}, {2, 1, {0, 0, 0, 255}},
            {0, 2, {255, 0, 0, 255}}, {1, 2, {0, 0, 255, 255}}, {2, 2, {255, 255, 255, 255}}};
        for (const auto& e : expect) {
            const auto got = pixelAt(e.cx, e.cy);
            if (!near(got, e.rgba)) {
                ok = false;
                check(false, "frame " + std::to_string(frame) + " cell " + std::to_string(e.cx) + "," + std::to_string(e.cy) +
                    " got " + std::to_string(got[0]) + "," + std::to_string(got[1]) + "," + std::to_string(got[2]));
            }
        }
        // The (1,0) draw used baseVertex 4: its smaller inner quad leaves the cell corner black.
        std::array<uint8_t, 4> corner{};
        glReadPixels(32 + 1, kSize - 1 - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner.data());
        check(near(corner, {0, 0, 0, 255}), "baseVertex mesh B drew mesh A in frame " + std::to_string(frame));
    }
    check(glGetError() == GL_NO_ERROR, "GL error after frames");
    if (ok) std::printf("pixels: 3 frames x 9 cells PASS\n");

    // Benchmark: N draws alternating two pipelines with equal state, one texture,
    // pooled meshes (alternating baseVertex), repeated per-pass uniform block.
    if (const char* n = std::getenv("AIVERSE_GL_BENCH")) {
        const int draws = std::max(1, std::atoi(n));
        double best = 1e9;
        for (int run = 0; run < 5; ++run) {
            dev->beginFrame(kSize, kSize);
            PassDesc pass; dev->beginPass(pass);
            dev->setVertexBuffer(hVb, 0); dev->setIndexBuffer(hIb, 0);
            Block b = cell(1, 1, white);
            const bool vary = std::getenv("AIVERSE_GL_BENCH_VARY") != nullptr; // per-draw uniforms
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < draws; ++i) {
                dev->setPipeline((i & 1) ? pTwin : pOpaque);
                dev->setTexture(0, red);
                if (vary) b.place[0] = float(i % 7) * 0.01f;
                dev->setUniforms(0, &b, sizeof b);
                dev->drawIndexed(6, 0, (i & 2) ? 4 : 0, 1);
            }
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            dev->endPass(); dev->endFrame(); glFinish();
            best = std::min(best, ms);
        }
        std::printf("bench: %d draws, CPU submission best of 5 = %.2f ms (%.2f us/draw)\n", draws, best, best * 1000.0 / draws);
    }
    if (failures) { std::fprintf(stderr, "gl state cache FAIL (%d)\n", failures); return 1; }
    std::printf("gl state cache PASS\n");
    return 0;
}
