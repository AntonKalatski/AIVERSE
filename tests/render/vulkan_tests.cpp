// Vulkan RHI backend on an offscreen device: GL conventions (orientation, depth range, render targets),
// buffer versioning inside a frame, textures, shadow compare, GLSL translation. Skips without a driver.
#include "engine/gfx/vulkan/Vulkan.h"
#include "engine/gfx/vulkan/VkShaderCompiler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace aiverse::gfx;

namespace {
int g_errors = 0;
#define CHECK(c) do { if (!(c)) { ++g_errors; std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

constexpr int W = 64, H = 64;

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;   // top-down RGBA
    const uint8_t* at(int x, int y) const { return px.data() + (size_t(y) * size_t(w) + size_t(x)) * 4; }
};
bool near(const uint8_t* p, int r, int g, int b) { return std::abs(p[0] - r) <= 3 && std::abs(p[1] - g) <= 3 && std::abs(p[2] - b) <= 3; }

// One frame drawn by `draw`, read back.
template <class F> Image frame(Device& d, F&& draw) {
    Image img;
    d.captureNextFrame([&](int w, int h, std::vector<uint8_t> px) { img.w = w; img.h = h; img.px = std::move(px); });
    d.beginFrame(W, H);
    draw();
    d.endFrame();
    return img;
}

const char* kVs = R"(
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(std140) uniform U0 { vec4 u_color; vec4 u_offset; };
out vec2 v_uv;
void main() { v_uv = a_uv; gl_Position = vec4(a_pos.xy + u_offset.xy, a_pos.z, 1.0); }
)";
const char* kColorFs = R"(
in vec2 v_uv;
layout(std140) uniform U0 { vec4 u_color; vec4 u_offset; };
out vec4 o_color;
void main() { o_color = u_color; }
)";
const char* kTexFs = R"(
in vec2 v_uv;
uniform sampler2D tex0;
out vec4 o_color;
void main() { o_color = texture(tex0, v_uv); }
)";
const char* kShadowFs = R"(
in vec2 v_uv;
uniform highp sampler2DShadow tex1;
layout(std140) uniform U0 { vec4 u_color; vec4 u_offset; };
out vec4 o_color;
void main() { float lit = texture(tex1, vec3(v_uv, u_color.x)); o_color = vec4(lit, lit, lit, 1.0); }
)";

struct Vtx { float x, y, z, u, v; };
BufferDesc bufferDesc(Usage usage, size_t size, const void* data) { BufferDesc b; b.usage = usage; b.size = size; b.data = data; return b; }
std::vector<Vtx> quad(float x0, float y0, float x1, float y1, float z) {
    return {{x0, y0, z, 0, 0}, {x1, y0, z, 1, 0}, {x1, y1, z, 1, 1}, {x0, y0, z, 0, 0}, {x1, y1, z, 1, 1}, {x0, y1, z, 0, 1}};
}

PipelineHandle pipeline(Device& d, ShaderHandle s, BlendMode blend = BlendMode::Opaque, bool depth = false, bool depthOnly = false) {
    PipelineDesc p;
    p.shader = s;
    p.attrs[0] = {0, VertexFormat::Float3, 0};
    p.attrs[1] = {1, VertexFormat::Float2, 12};
    p.attrCount = 2;
    p.stride = sizeof(Vtx);
    p.indexType = IndexType::None;
    p.cull = CullMode::Back;
    p.blend = blend;
    p.depthTest = p.depthWrite = depth;
    p.depthOnly = depthOnly;
    return d.createPipeline(p);
}

void translation() {
    const std::string vs = aiverse::gfx::vk::translateGlsl(kVs, true, false);
    CHECK(vs.find("binding = 0) uniform U0") != std::string::npos);
    CHECK(vs.find("aiverse_user_main") != std::string::npos);
    CHECK(vs.find("gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5") != std::string::npos);
    uint32_t shadow = 0;
    const std::string fs = aiverse::gfx::vk::translateGlsl(kShadowFs, false, false, &shadow);
    CHECK(fs.find("binding = 9) uniform highp sampler2DShadow tex1") != std::string::npos);
    CHECK(shadow == 2u);
    const std::string ids = aiverse::gfx::vk::translateGlsl("void main() { int a = gl_InstanceID + gl_VertexID; } // uniform sampler2D tex3;", true, false);
    CHECK(ids.find("gl_InstanceIndex") != std::string::npos && ids.find("gl_VertexIndex") != std::string::npos);
    CHECK(ids.find("tex3") == std::string::npos);   // comments are dropped, not bound
    aiverse::gfx::vk::SpirvProgram p;
    CHECK(aiverse::gfx::vk::compileProgram(kVs, kColorFs, false, p) && !p.vertex.empty() && !p.fragment.empty());
    CHECK(!aiverse::gfx::vk::compileProgram(kVs, "void main() { undefined_call(); }", false, p) && !p.error.empty());
}
} // namespace

int main() {
    translation();
    std::string err;
    auto dev = createVulkanDevice(VulkanWindow{}, &err);
    if (!dev) {
        std::printf("Vulkan backend: skipped (%s), %d translation errors\n", err.c_str(), g_errors);
        return g_errors ? 1 : 0;
    }
    Device& d = *dev;
    std::printf("%s\n", d.info().name.c_str());
    CHECK(d.info().backend == Backend::Vulkan && !d.info().clipDepthZeroToOne && !d.info().renderTargetFlipY);

    ShaderDesc sd;
    sd.glslVertex = kVs; sd.glslFragment = kColorFs; sd.uniformSlots = 1;
    const ShaderHandle color = d.createShader(sd);
    sd.glslFragment = kTexFs; sd.textureSlots = 1;
    const ShaderHandle tex = d.createShader(sd);
    sd.glslFragment = kShadowFs; sd.textureSlots = 2;
    const ShaderHandle shadow = d.createShader(sd);
    CHECK(color.valid() && tex.valid() && shadow.valid());
    const PipelineHandle pColor = pipeline(d, color), pDepth = pipeline(d, color, BlendMode::Opaque, true);
    const PipelineHandle pTex = pipeline(d, tex), pShadow = pipeline(d, shadow), pCaster = pipeline(d, color, BlendMode::Opaque, true, true);
    const PipelineHandle pAlpha = pipeline(d, color, BlendMode::Alpha);

    const auto top = quad(-1, 0, 1, 1, 0), full = quad(-1, -1, 1, 1, 0);
    const BufferHandle vbTop = d.createBuffer(bufferDesc(Usage::Immutable, top.size() * sizeof(Vtx), top.data()));
    const BufferHandle vbFull = d.createBuffer(bufferDesc(Usage::Immutable, full.size() * sizeof(Vtx), full.data()));
    struct U { float color[4], offset[4]; };
    auto uniforms = [&](float r, float g, float b, float a, float ox = 0, float oy = 0) { U u{{r, g, b, a}, {ox, oy, 0, 0}}; d.setUniforms(0, &u, sizeof u); };
    PassDesc screen;
    screen.clearColor[0] = 0; screen.clearColor[1] = 0; screen.clearColor[2] = 1; screen.clearColor[3] = 1;

    {   // clear + orientation: clip y > 0 is the top of the screen, captures are top-down
        Image img = frame(d, [&] {
            d.beginPass(screen);
            d.setPipeline(pColor); d.setVertexBuffer(vbTop); uniforms(1, 0, 0, 1); d.draw(6);
            d.endPass();
        });
        CHECK(img.w == W && img.h == H);
        CHECK(near(img.at(W / 2, 4), 255, 0, 0));
        CHECK(near(img.at(W / 2, H - 4), 0, 0, 255));
    }
    {   // viewport / scissor rects are top-left based
        Image img = frame(d, [&] {
            d.beginPass(screen);
            d.setPipeline(pColor); d.setVertexBuffer(vbFull);
            d.setViewport({0, 0, W / 2, H / 2}); uniforms(0, 1, 0, 1); d.draw(6);
            d.setViewport({0, 0, W, H}); d.setScissor({W / 2, H / 2, W / 2, H / 2}); uniforms(1, 0, 0, 1); d.draw(6);
            d.endPass();
        });
        CHECK(near(img.at(4, 4), 0, 255, 0));
        CHECK(near(img.at(W - 4, H - 4), 255, 0, 0));
        CHECK(near(img.at(W - 4, 4), 0, 0, 255));
    }
    {   // depth: GL range, LEQUAL, nearer (smaller z) wins regardless of order
        const auto nearQuad = quad(-1, -1, 1, 1, -0.5f), farQuad = quad(-1, -1, 1, 1, 0.5f);
        const BufferHandle vbNear = d.createBuffer(bufferDesc(Usage::Immutable, nearQuad.size() * sizeof(Vtx), nearQuad.data()));
        const BufferHandle vbFar = d.createBuffer(bufferDesc(Usage::Immutable, farQuad.size() * sizeof(Vtx), farQuad.data()));
        Image img = frame(d, [&] {
            d.beginPass(screen);
            d.setPipeline(pDepth);
            d.setVertexBuffer(vbNear); uniforms(1, 0, 0, 1); d.draw(6);
            d.setVertexBuffer(vbFar); uniforms(0, 1, 0, 1); d.draw(6);
            d.endPass();
        });
        CHECK(near(img.at(W / 2, H / 2), 255, 0, 0));
        d.destroyBuffer(vbNear); d.destroyBuffer(vbFar);
    }
    {   // render target: not flipped (texture v = 1 is the top, as GL), sampled in the next pass
        TextureDesc td; td.width = W; td.height = H; td.renderTarget = true;
        const TextureHandle rt = d.createTexture(td);
        Image img = frame(d, [&] {
            PassDesc p; p.colorTarget = rt; p.clearColor[0] = 0; p.clearColor[1] = 0; p.clearColor[2] = 0; p.clearColor[3] = 1;
            d.beginPass(p);
            d.setPipeline(pColor); d.setVertexBuffer(vbTop); uniforms(1, 1, 0, 1); d.draw(6);
            d.endPass();
            d.beginPass(screen);
            d.setPipeline(pTex); d.setTexture(0, rt); d.setVertexBuffer(vbFull); d.draw(6);
            d.endPass();
        });
        CHECK(near(img.at(W / 2, 4), 255, 255, 0));
        CHECK(near(img.at(W / 2, H - 4), 0, 0, 0));
        // copyColor from the screen: same orientation
        TextureDesc cd; cd.width = W; cd.height = H; cd.renderTarget = true;
        const TextureHandle copy = d.createTexture(cd);
        Image img2 = frame(d, [&] {
            d.beginPass(screen); d.setPipeline(pColor); d.setVertexBuffer(vbTop); uniforms(0, 1, 0, 1); d.draw(6); d.endPass();
            CHECK(d.copyColor({}, copy));
            PassDesc p; p.clearColorEnabled = false;
            d.beginPass(p);
            d.setPipeline(pTex); d.setTexture(0, copy); d.setVertexBuffer(vbFull); d.setViewport({0, 0, W / 2, H}); d.draw(6);
            d.endPass();
        });
        CHECK(near(img2.at(4, 4), 0, 255, 0));
        CHECK(near(img2.at(4, H - 4), 0, 0, 255));
        d.destroyTexture(rt); d.destroyTexture(copy);
    }
    {   // Explicit offscreen subresource pass: render mip 1 / array layer 1, then sample it.
        TextureDesc rd; rd.width = 8; rd.height = 8;
        rd.dimension = TextureDimension::Texture2DArray;
        rd.arrayLayers = 2; rd.mipLevels = 3; rd.renderTarget = true;
        const TextureHandle layeredColor = d.createTexture(rd);
        CHECK(layeredColor.valid());
        TextureDesc zd = rd; zd.format = PixelFormat::Depth;
        const TextureHandle layeredDepth = d.createTexture(zd);
        CHECK(layeredDepth.valid());
        TextureViewDesc vd; vd.dimension = TextureDimension::Texture2D;
        vd.baseMipLevel = 1; vd.baseArrayLayer = 1;
        const TextureViewHandle slice = layeredColor.valid() ?
            d.createTextureView(layeredColor, vd) : TextureViewHandle{};
        CHECK(slice.valid());
        SamplerDesc sampleDesc;
        sampleDesc.minFilter = sampleDesc.magFilter = Filter::Nearest;
        sampleDesc.mipFilter = SamplerMipFilter::None;
        const SamplerHandle sampler = d.createSampler(sampleDesc);
        CHECK(sampler.valid());
        if (slice.valid() && sampler.valid() && layeredDepth.valid()) {
            Image img = frame(d, [&] {
                RenderTargetPassDesc pass;
                pass.colorCount = 1;
                pass.colors[0].texture = layeredColor;
                pass.colors[0].mipLevel = 1;
                pass.colors[0].arrayLayer = 1;
                pass.colors[0].clearColor[0] = 0.0f;
                pass.colors[0].clearColor[1] = 0.75f;
                pass.colors[0].clearColor[2] = 1.0f;
                pass.colors[0].clearColor[3] = 1.0f;
                pass.depth.texture = layeredDepth;
                pass.depth.mipLevel = 1;
                pass.depth.arrayLayer = 1;
                CHECK(d.supportsRenderTargetPasses());
                CHECK(d.beginRenderTargets(pass));
                d.endPass();
                // A rejected mip must not start another pass or change the attachment state.
                pass.colors[0].mipLevel = 20;
                CHECK(!d.beginRenderTargets(pass));
                d.beginPass(screen);
                d.setPipeline(pTex); d.setVertexBuffer(vbFull);
                CHECK(d.bindTextureView(0, slice, sampler));
                d.draw(6);
                d.endPass();
            });
            CHECK(img.w == W && img.h == H);
            if (img.w == W && img.h == H)
                CHECK(near(img.at(W / 2, H / 2), 0, 191, 255));
        }
        if (sampler.valid()) d.destroySampler(sampler);
        if (slice.valid()) d.destroyTextureView(slice);
        if (layeredColor.valid()) d.destroyTexture(layeredColor);
        if (layeredDepth.valid()) d.destroyTexture(layeredDepth);
    }
    {   // Two-target MRT: independent per-attachment clear values and fragment outputs.
        TextureDesc cd; cd.width = W; cd.height = H; cd.renderTarget = true;
        const TextureHandle target0 = d.createTexture(cd);
        const TextureHandle target1 = d.createTexture(cd);
        CHECK(target0.valid() && target1.valid());
        ShaderDesc shaderDesc;
        shaderDesc.glslVertex = kVs;
        shaderDesc.glslFragment = R"(
in vec2 v_uv;
layout(location=0) out vec4 color0;
layout(location=1) out vec4 color1;
void main() {
    color0 = vec4(0.0, 0.0, 1.0, 1.0);
    color1 = vec4(1.0, 1.0, 0.0, 1.0);
}
)";
        shaderDesc.uniformSlots = 1;
        const ShaderHandle mrtShader = d.createShader(shaderDesc);
        CHECK(mrtShader.valid());
        PipelineDesc mrtPipe;
        mrtPipe.shader = mrtShader;
        mrtPipe.attrs[0] = {0, VertexFormat::Float3, 0};
        mrtPipe.attrs[1] = {1, VertexFormat::Float2, 12};
        mrtPipe.attrCount = 2;
        mrtPipe.stride = sizeof(Vtx);
        mrtPipe.indexType = IndexType::None;
        mrtPipe.depthTest = mrtPipe.depthWrite = false;
        mrtPipe.colorAttachmentCount = 2;
        mrtPipe.additionalColorFormats[0] = PixelFormat::RGBA8;
        const PipelineHandle mrt = mrtShader.valid() ? d.createPipeline(mrtPipe) : PipelineHandle{};
        CHECK(mrt.valid());
        if (target0.valid() && target1.valid() && mrt.valid()) {
            RenderTargetPassDesc pass;
            pass.colorCount = 2;
            pass.colors[0].texture = target0;
            pass.colors[1].texture = target1;
            pass.colors[0].clearColor[0] = 1.0f;
            pass.colors[0].clearColor[3] = 1.0f;
            pass.colors[1].clearColor[1] = 1.0f;
            pass.colors[1].clearColor[3] = 1.0f;
            Image clears = frame(d, [&] {
                CHECK(d.supportsRenderTargetPasses());
                const bool started = d.beginRenderTargets(pass);
                CHECK(started);
                if (started) d.endPass();
                d.beginPass(screen);
                d.setPipeline(pTex);
                d.setVertexBuffer(vbFull);
                d.setTexture(0, target0);
                d.setViewport({0, 0, W/2, H});
                d.draw(6);
                d.setTexture(0, target1);
                d.setViewport({W/2, 0, W/2, H});
                d.draw(6);
                d.endPass();
            });
            CHECK(clears.w == W && clears.h == H);
            if (clears.w == W && clears.h == H) {
                CHECK(near(clears.at(4, H/2), 255, 0, 0));
                CHECK(near(clears.at(W-4, H/2), 0, 255, 0));
            }
            pass.colors[0].clear = pass.colors[1].clear = false;
            Image outputs = frame(d, [&] {
                const bool started = d.beginRenderTargets(pass);
                CHECK(started);
                if (started) {
                    d.setPipeline(mrt);
                    d.setVertexBuffer(vbFull);
                    uniforms(1,1,1,1);
                    d.draw(6);
                    d.endPass();
                }
                d.beginPass(screen);
                d.setPipeline(pTex);
                d.setVertexBuffer(vbFull);
                d.setTexture(0, target0);
                d.setViewport({0, 0, W/2, H});
                d.draw(6);
                d.setTexture(0, target1);
                d.setViewport({W/2, 0, W/2, H});
                d.draw(6);
                d.endPass();
            });
            CHECK(outputs.w == W && outputs.h == H);
            if (outputs.w == W && outputs.h == H) {
                CHECK(near(outputs.at(4, H/2), 0, 0, 255));
                CHECK(near(outputs.at(W-4, H/2), 255, 255, 0));
            }
        }
        if (mrt.valid()) d.destroyPipeline(mrt);
        if (mrtShader.valid()) d.destroyShader(mrtShader);
        if (target0.valid()) d.destroyTexture(target0);
        if (target1.valid()) d.destroyTexture(target1);
    }
    {   // a dynamic buffer rewritten between draws of one frame: each draw keeps its data
        const BufferHandle dyn = d.createBuffer(bufferDesc(Usage::Dynamic, top.size() * sizeof(Vtx), nullptr));
        const auto bottom = quad(-1, -1, 1, 0, 0);
        for (int pass = 0; pass < 3; ++pass) {   // several frames: versions recycle
            Image img = frame(d, [&] {
                d.beginPass(screen);
                d.setPipeline(pColor); d.setVertexBuffer(dyn);
                d.updateBuffer(dyn, top.data(), top.size() * sizeof(Vtx)); uniforms(1, 0, 0, 1); d.draw(6);
                d.updateBuffer(dyn, bottom.data(), bottom.size() * sizeof(Vtx)); uniforms(0, 1, 0, 1); d.draw(6);
                d.endPass();
            });
            CHECK(near(img.at(W / 2, 4), 255, 0, 0));
            CHECK(near(img.at(W / 2, H - 4), 0, 255, 0));
        }
        d.destroyBuffer(dyn);
    }
    {   // textures: upload with row pitch, sub-rect update, R8 reads as (r, 0, 0, 1), mipmaps
        std::vector<uint8_t> px(size_t(4) * 4 * 8, 0);   // 4x4 RGBA with an 8-pixel row pitch
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) { uint8_t* p = px.data() + (size_t(y) * 8 + size_t(x)) * 4; p[0] = 200; p[3] = 255; }
        TextureDesc td; td.width = 4; td.height = 4; td.filter = Filter::Nearest; td.mipmaps = true;
        const TextureHandle t = d.createTexture(td);
        d.updateTexture(t, 0, 0, 4, 4, px.data(), 8 * 4);
        TextureDesc rd; rd.width = 2; rd.height = 2; rd.format = PixelFormat::R8; rd.filter = Filter::Nearest;
        const uint8_t r8[4] = {100, 100, 100, 100};
        rd.data = r8;
        const TextureHandle tr = d.createTexture(rd);
        Image img = frame(d, [&] {
            d.beginPass(screen);
            d.setPipeline(pTex); d.setVertexBuffer(vbFull);
            d.setTexture(0, t); d.setViewport({0, 0, W / 2, H}); d.draw(6);
            d.setTexture(0, tr); d.setViewport({W / 2, 0, W / 2, H}); d.draw(6);
            d.endPass();
        });
        CHECK(near(img.at(4, H / 2), 200, 0, 0));
        CHECK(near(img.at(W - 4, H / 2), 100, 0, 0));
        d.destroyTexture(t); d.destroyTexture(tr);
    }
    {   // sampled dimensional resources: actual GPU image/view + GLSL sampler type pairing
        auto checkDimension = [&](TextureDesc desc, const std::vector<uint8_t>& pixels,
                                  const char* fs, int r, int g, int b) {
            desc.filter = Filter::Nearest;
            desc.data = pixels.data();
            CHECK(d.supportsTextureDimension(desc.dimension));
            const TextureHandle imageTex = d.createTexture(desc);
            CHECK(imageTex.valid());
            ShaderDesc shaderDesc; shaderDesc.glslVertex = kVs;
            shaderDesc.glslFragment = fs; shaderDesc.textureSlots = 1;
            const ShaderHandle shader = d.createShader(shaderDesc);
            CHECK(shader.valid());
            const PipelineHandle pip = shader.valid() ? pipeline(d, shader) : PipelineHandle{};
            CHECK(pip.valid());
            if (imageTex.valid() && pip.valid()) {
                Image img = frame(d, [&] {
                    d.beginPass(screen);
                    d.setPipeline(pip); d.setTexture(0, imageTex);
                    d.setVertexBuffer(vbFull); d.draw(6);
                    d.endPass();
                });
                CHECK(img.w == W && img.h == H);
                if (img.w == W && img.h == H) CHECK(near(img.at(W / 2, H / 2), r, g, b));
            }
            if (pip.valid()) d.destroyPipeline(pip);
            if (shader.valid()) d.destroyShader(shader);
            if (imageTex.valid()) d.destroyTexture(imageTex);
        };
        auto solidLayers = [](int faces, const std::vector<int>& colours) {
            std::vector<uint8_t> p(size_t(faces) * 2 * 2 * 4, 255);
            for (int face = 0; face < faces; ++face)
                for (int i = 0; i < 4; ++i) {
                    uint8_t* v = p.data() + (size_t(face) * 4 + size_t(i)) * 4;
                    v[0] = uint8_t(colours[size_t(face) * 3]);
                    v[1] = uint8_t(colours[size_t(face) * 3 + 1]);
                    v[2] = uint8_t(colours[size_t(face) * 3 + 2]);
                }
            return p;
        };
        TextureDesc desc; desc.width = desc.height = 2;
        desc.dimension = TextureDimension::Texture1D;
        desc.height = 1;
        checkDimension(desc, {255, 0, 0, 255, 255, 0, 0, 255},
            R"(in vec2 v_uv; uniform sampler1D tex0; out vec4 o_color;
               void main(){ o_color=texture(tex0,0.3); })", 255, 0, 0);
        desc = TextureDesc{}; desc.width = desc.height = 2;
        desc.dimension = TextureDimension::Texture3D; desc.depth = 2;
        checkDimension(desc, solidLayers(2, {255,0,0, 0,0,255}),
            R"(in vec2 v_uv; uniform sampler3D tex0; out vec4 o_color;
               void main(){ o_color=texture(tex0,vec3(v_uv,0.75)); })", 0, 0, 255);
        desc = TextureDesc{}; desc.width = desc.height = 2;
        desc.dimension = TextureDimension::Texture2DArray; desc.arrayLayers = 3;
        checkDimension(desc, solidLayers(3, {255,0,0, 0,255,0, 0,0,255}),
            R"(in vec2 v_uv; uniform sampler2DArray tex0; out vec4 o_color;
               void main(){ o_color=texture(tex0,vec3(v_uv,1.0)); })", 0, 255, 0);
        desc = TextureDesc{}; desc.width = desc.height = 2;
        desc.dimension = TextureDimension::Cube; desc.arrayLayers = 6;
        checkDimension(desc, solidLayers(6, {255,0,0, 0,255,0, 0,0,255,
                                              255,255,0, 0,255,255, 255,0,255}),
            R"(in vec2 v_uv; uniform samplerCube tex0; out vec4 o_color;
               void main(){ o_color=texture(tex0,vec3(1.0,0.0,0.0)); })", 255, 0, 0);
        if (d.supportsTextureDimension(TextureDimension::CubeArray)) {
            desc = TextureDesc{}; desc.width = desc.height = 2;
            desc.dimension = TextureDimension::CubeArray; desc.arrayLayers = 12;
            auto colours = std::vector<int>(36, 0);
            for (int i = 0; i < 6; ++i) colours[size_t(i) * 3] = 255;
            for (int i = 6; i < 12; ++i) colours[size_t(i) * 3 + 1] = 255;
            checkDimension(desc, solidLayers(12, colours),
                R"(in vec2 v_uv; uniform samplerCubeArray tex0; out vec4 o_color;
                   void main(){ o_color=texture(tex0,vec4(1.0,0.0,0.0,1.0)); })", 0, 255, 0);
        }
    }
    {   // Native views + independent samplers: bind one array slice as a 2D texture.
        TextureDesc td; td.width = 2; td.height = 2;
        td.dimension = TextureDimension::Texture2DArray;
        td.arrayLayers = 3; td.filter = Filter::Nearest;
        std::vector<uint8_t> px(3 * 2 * 2 * 4, 255);
        for (int slice = 0; slice < 3; ++slice)
            for (int pixel = 0; pixel < 4; ++pixel) {
                uint8_t* dst = px.data() + (size_t(slice) * 4 + size_t(pixel)) * 4;
                dst[0] = slice == 0 ? 255 : 0;
                dst[1] = slice == 1 ? 255 : 0;
                dst[2] = slice == 2 ? 255 : 0;
            }
        td.data = px.data();
        const TextureHandle array = d.createTexture(td);
        CHECK(array.valid());
        CHECK(d.supportsTextureViews());
        TextureViewDesc vd;
        vd.dimension = TextureDimension::Texture2D;
        vd.baseArrayLayer = 2;
        const TextureViewHandle blue = array.valid() ? d.createTextureView(array, vd) : TextureViewHandle{};
        vd.baseArrayLayer = 1;
        const TextureViewHandle green = array.valid() ? d.createTextureView(array, vd) : TextureViewHandle{};
        CHECK(blue.valid() && green.valid());
        SamplerDesc sp;
        sp.minFilter = sp.magFilter = Filter::Nearest;
        sp.mipFilter = SamplerMipFilter::None;
        const SamplerHandle sampler = d.createSampler(sp);
        CHECK(sampler.valid());
        if (blue.valid() && green.valid() && sampler.valid()) {
            Image img = frame(d, [&] {
                d.beginPass(screen);
                d.setPipeline(pTex); d.setVertexBuffer(vbFull);
                d.setViewport({0, 0, W / 2, H});
                CHECK(d.bindTextureView(0, blue, sampler));
                d.draw(6);
                d.setViewport({W / 2, 0, W / 2, H});
                CHECK(d.bindTextureView(0, green, sampler));
                d.draw(6);
                d.endPass();
            });
            if (img.w == W && img.h == H) {
                CHECK(near(img.at(4, H / 2), 0, 0, 255));
                CHECK(near(img.at(W - 4, H / 2), 0, 255, 0));
            } else CHECK(false);
            d.destroySampler(sampler);
            CHECK(!d.bindTextureView(0, green, sampler)); // destroyed sampler must be rejected
        } else if (sampler.valid()) d.destroySampler(sampler);
        if (array.valid()) d.destroyTexture(array);
        CHECK(!d.bindTextureView(0, blue)); // destroying parent invalidates child views
        d.destroyTextureView(blue);
        d.destroyTextureView(green);
    }
    {   // shadow map: depth-only pass, compare sampler (LEQUAL), GL depth values
        TextureDesc sd2; sd2.width = W; sd2.height = H; sd2.format = PixelFormat::Depth; sd2.renderTarget = true; sd2.compare = true;
        const TextureHandle sm = d.createTexture(sd2);
        Image img = frame(d, [&] {
            PassDesc p; p.depthTarget = sm; p.clearColorEnabled = false;
            d.beginPass(p);
            d.setPipeline(pCaster); d.setVertexBuffer(vbTop); uniforms(0, 0, 0, 0); d.draw(6);   // z 0 -> depth 0.5 in the top half
            d.endPass();
            d.beginPass(screen);
            d.setPipeline(pShadow); d.setTexture(1, sm); d.setVertexBuffer(vbFull); uniforms(0.75f, 0, 0, 0); d.draw(6);
            d.endPass();
        });
        CHECK(near(img.at(W / 2, 4), 0, 0, 0));          // 0.75 <= 0.5 fails: shadowed
        CHECK(near(img.at(W / 2, H - 4), 255, 255, 255)); // 0.75 <= 1.0 (cleared): lit
        d.destroyTexture(sm);
    }
    {   // alpha blending and an HDR (RGBA16F) pass with an LDR pipeline (HDR shader variant)
        Image img = frame(d, [&] {
            d.beginPass(screen);
            d.setPipeline(pAlpha); d.setVertexBuffer(vbFull); uniforms(1, 0, 0, 0.5f); d.draw(6);
            d.endPass();
        });
        CHECK(near(img.at(W / 2, H / 2), 128, 0, 127));
        if (d.supports(PixelFormat::RGBA16F)) {
            TextureDesc hd; hd.width = W; hd.height = H; hd.format = PixelFormat::RGBA16F; hd.renderTarget = true;
            const TextureHandle hdr = d.createTexture(hd);
            frame(d, [&] { PassDesc p; p.colorTarget = hdr; d.beginPass(p); d.setPipeline(pColor); d.setVertexBuffer(vbFull); uniforms(1, 1, 1, 1); d.draw(6); d.endPass(); });
            d.destroyTexture(hdr);
        }
    }
    CHECK(d.gpuHealthy());
    d.destroyPipeline(pColor); d.destroyPipeline(pDepth); d.destroyPipeline(pTex); d.destroyPipeline(pShadow); d.destroyPipeline(pCaster); d.destroyPipeline(pAlpha);
    d.destroyShader(color); d.destroyShader(tex); d.destroyShader(shadow);
    d.destroyBuffer(vbTop); d.destroyBuffer(vbFull);
    std::printf("Vulkan backend: %d errors\n", g_errors);
    return g_errors ? 1 : 0;
}
