// OpenGL ES 3.0 / WebGL2 / OpenGL 3.3 core backend.
#include "../Gfx.h"
#include "../ColorSpace.h"
#include "GLApi.h"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

#if defined(AIVERSE_GL_LOADER)
#define AIVERSE_GL_DEFINE(ret, name, args) PFN_##name name = nullptr;
AIVERSE_GL_FUNCS(AIVERSE_GL_DEFINE)
#undef AIVERSE_GL_DEFINE
namespace aiverse::gfx {
bool loadGLFunctions() {
    bool ok = true;
#define AIVERSE_GL_LOAD(ret, name, args) name = (PFN_##name)wglGetProcAddress(#name); if (!name) ok = false;
    AIVERSE_GL_FUNCS(AIVERSE_GL_LOAD)
#undef AIVERSE_GL_LOAD
    return ok;
}
} // namespace aiverse::gfx
#endif

namespace aiverse::gfx {
namespace {

// desktop GL only (ES / WebGL always encode into sRGB attachments); GLES headers lack the name
constexpr GLenum kGL_FRAMEBUFFER_SRGB = 0x8DB9;
constexpr GLenum kGL_MAX_ANISOTROPY = 0x84FF;
constexpr GLenum kGL_TEXTURE_ANISOTROPY = 0x84FE;
constexpr GLenum kGL_TEXTURE_MIN_LOD = 0x813A;
constexpr GLenum kGL_TEXTURE_MAX_LOD = 0x813B;
constexpr GLenum kGL_TEXTURE_LOD_BIAS = 0x8501;
constexpr GLenum kGL_CLAMP_TO_BORDER = 0x812D;
constexpr GLenum kGL_MIRRORED_REPEAT = 0x8370;

// compressed formats (may be missing from older headers)
constexpr GLenum kGL_ETC2_RGB8 = 0x9274, kGL_ETC2_RGBA8 = 0x9278, kGL_DXT1_RGBA = 0x83F1, kGL_DXT5 = 0x83F3,
                 kGL_BPTC_UNORM = 0x8E8C, kGL_ASTC_4x4 = 0x93B0, kGL_ASTC_6x6 = 0x93B4;
constexpr GLenum kGL_BC4_RGTC1 = 0x8DBB, kGL_BC5_RGTC2 = 0x8DBD, kGL_EAC_R11 = 0x9270;
GLenum compressedEnum(PixelFormat f) {
    switch (f) {
        case PixelFormat::ETC2_RGB8: return kGL_ETC2_RGB8;
        case PixelFormat::ETC2_RGBA8: return kGL_ETC2_RGBA8;
        case PixelFormat::BC1: return kGL_DXT1_RGBA;
        case PixelFormat::BC3: return kGL_DXT5;
        case PixelFormat::BC4: return kGL_BC4_RGTC1;
        case PixelFormat::BC5: return kGL_BC5_RGTC2;
        case PixelFormat::EAC_R11: return kGL_EAC_R11;
        case PixelFormat::BC7: return kGL_BPTC_UNORM;
        case PixelFormat::ASTC_4x4: return kGL_ASTC_4x4;
        case PixelFormat::ASTC_6x6: return kGL_ASTC_6x6;
        default: return 0;
    }
}

template <class T> class Pool {
public:
    uint32_t add(T v) {
        if (!free_.empty()) { uint32_t i = free_.back(); free_.pop_back(); items_[i] = std::move(v); alive_[i] = 1; return i + 1; }
        items_.push_back(std::move(v)); alive_.push_back(1); return uint32_t(items_.size());
    }
    T* get(uint32_t id) { return id && id <= items_.size() && alive_[id - 1] ? &items_[id - 1] : nullptr; }
    void remove(uint32_t id) { if (get(id)) { alive_[id - 1] = 0; free_.push_back(id - 1); } }
    template <class F> void each(F&& fn) {
        for (size_t i = 0; i < items_.size(); ++i)
            if (alive_[i]) fn(uint32_t(i + 1), items_[i]);
    }
private:
    std::vector<T> items_; std::vector<uint8_t> alive_; std::vector<uint32_t> free_;
};

struct GLBuffer { GLuint id = 0; GLenum target = 0; size_t size = 0; Usage usage = Usage::Immutable; };
struct GLTexture {
    GLuint id = 0;
    PixelFormat fmt = PixelFormat::RGBA8;
    TextureDimension dimension = TextureDimension::Texture2D;
    GLenum target = GL_TEXTURE_2D;
    int w = 0, h = 0, depth = 1, layers = 1, levels = 1;
    bool mips = false, compressed = false, renderTarget = false;
};
struct GLTextureView { uint32_t parentId = 0; };
struct GLSampler { GLuint native = 0; };
struct GLFramebuffer { uint32_t color = 0, depth = 0; GLuint fbo = 0; };
struct GLShader { GLuint program = 0; std::string vs, fs; int textures = 0, uniforms = 0; bool linear = false; };
struct GLPipeline { PipelineDesc desc; GLuint program = 0; PipelineHandle hdr; ShaderHandle hdrShader; bool hdrFailed = false; };

constexpr size_t kUniformRingSize = 1 << 20;
constexpr int kMaxUniformSlots = 8;

class GLDevice final : public Device {
public:
    explicit GLDevice(Backend flavor) {
        info_.backend = flavor;
        info_.clipDepthZeroToOne = false;
        const char* r = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* v = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        info_.name = std::string(flavor == Backend::GL33 ? "GL33 " : "GLES3 ") + (r ? r : "?") + " / " + (v ? v : "?");
        glGenVertexArrays(1, &vao_);
        glBindVertexArray(vao_);
        GLint align = 256;
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
        uboAlign_ = align > 0 ? size_t(align) : 256;
#if defined(__EMSCRIPTEN__) || defined(__APPLE__)
        glGenBuffers(kMaxUniformSlots, slotUbos_);
#else
        glGenBuffers(1, &ubo_);
        glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
        glBufferData(GL_UNIFORM_BUFFER, kUniformRingSize, nullptr, GL_STREAM_DRAW);
#endif
        // compressed formats: ETC2 is core in ES 3.0 (not in desktop GL 3.3, not guaranteed on WebGL2)
        std::string ext;
        if (flavor == Backend::GL33) {
            GLint count = 0; glGetIntegerv(GL_NUM_EXTENSIONS, &count);
            for (GLint i = 0; i < count; ++i) {
                if (const char* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i)))) { ext += e; ext += ' '; }
            }
        } else if (const char* e = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS))) ext = e;
        auto has = [&](const char* name) { return ext.find(name) != std::string::npos; };
#if defined(__EMSCRIPTEN__)
        etc2_ = has("compressed_texture_etc");
#else
        etc2_ = flavor != Backend::GL33 || has("ES3_compatibility");
#endif
        s3tc_ = has("texture_compression_s3tc") || has("compressed_texture_s3tc");
        bptc_ = has("texture_compression_bptc");
        rgtc_ = flavor == Backend::GL33 || has("texture_compression_rgtc");
        if (ext.empty() && flavor == Backend::GL33) s3tc_ = true; // core profiles hide the list; every desktop GPU has DXT
        astc_ = has("texture_compression_astc");
        hdr_ = flavor == Backend::GL33 || has("EXT_color_buffer_float") || has("EXT_color_buffer_half_float");
        anisotropy_ = has("texture_filter_anisotropic");
        if (anisotropy_) {
            GLfloat maxAniso = 1.0f;
            glGetFloatv(kGL_MAX_ANISOTROPY, &maxAniso);
            maxAnisotropy_ = std::max(1.0f, maxAniso);
        }
    }
    bool supportsTextureDimension(TextureDimension dim) const override {
        // Common OpenGL ES 3 / desktop GL 3.3 subset. 1D and cube arrays need
        // platform-specific support; never emulate them as ordinary 2D textures.
        return dim == TextureDimension::Texture2D || dim == TextureDimension::Texture2DArray ||
               dim == TextureDimension::Texture3D || dim == TextureDimension::Cube;
    }
    // Core GL 3.3 / GLES 3 has independent sampler objects, but not native
    // arbitrary subresource texture views. Identity views can still be bound.
    bool supportsTextureViews() const override { return false; }
    bool supports(PixelFormat f) const override {
        switch (f) {
            case PixelFormat::ETC2_RGB8: case PixelFormat::ETC2_RGBA8:
            case PixelFormat::EAC_R11: return etc2_;
            case PixelFormat::BC1: case PixelFormat::BC3: return s3tc_;
            case PixelFormat::BC4: case PixelFormat::BC5: return rgtc_;
            case PixelFormat::BC7: return bptc_;
            case PixelFormat::ASTC_4x4: case PixelFormat::ASTC_6x6: return astc_;
            case PixelFormat::RGBA16F: return hdr_;
            default: return true;
        }
    }
    ~GLDevice() override {
#if defined(__APPLE__) || defined(_WIN32)
        if (queryActive_) glEndQuery(GL_TIME_ELAPSED);
        for (const auto& t:timedQueries_) glDeleteQueries(1,&t.query);
#endif
#if defined(__EMSCRIPTEN__) || defined(__APPLE__)
        glDeleteBuffers(kMaxUniformSlots, slotUbos_);
#else
        glDeleteBuffers(1, &ubo_);
#endif
        glDeleteVertexArrays(1, &vao_);
    }

    const DeviceInfo& info() const override { return info_; }
    const std::string& lastError() const override { return error_; }

    // ---------- buffers ----------
    BufferHandle createBuffer(const BufferDesc& d) override {
        GLBuffer b;
        b.target = d.type == BufferType::Index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
        b.size = d.size; b.usage = d.usage;
        glGenBuffers(1, &b.id);
        glBindBuffer(b.target, b.id);
        glBufferData(b.target, GLsizeiptr(d.size), d.data, d.usage == Usage::Dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
        attribsDirty_ = true;
        return {buffers_.add(b)};
    }
    void updateBuffer(BufferHandle h, const void* data, size_t size, size_t offset) override {
        auto* b = buffers_.get(h.id);
        if (!b) return;
        glBindBuffer(b->target, b->id);
        if (offset + size > b->size) {
            b->size = (offset + size) * 3 / 2;
            glBufferData(b->target, GLsizeiptr(b->size), nullptr, GL_DYNAMIC_DRAW);
        } else if (offset == 0 && b->usage == Usage::Dynamic) {
            glBufferData(b->target, GLsizeiptr(b->size), nullptr, GL_DYNAMIC_DRAW); // orphan
        }
        glBufferSubData(b->target, GLintptr(offset), GLsizeiptr(size), data);
        attribsDirty_ = true;
    }
    void destroyBuffer(BufferHandle h) override {
        if (auto* b = buffers_.get(h.id)) { glDeleteBuffers(1, &b->id); buffers_.remove(h.id); }
    }

    // ---------- textures ----------
    // Native ES3 / GL3.3 dimension support. Keep the existing Texture2D path below
    // unchanged; non-2D targets are sampled through matching GLSL sampler types.
    TextureHandle createDimensionalTexture(const TextureDesc& d) {
        if (!supportsTextureDimension(d.dimension) || d.storage || d.sampleCount != 1 ||
            (d.dimension == TextureDimension::Texture3D && d.format == PixelFormat::Depth) ||
            (d.format == PixelFormat::Depth && d.data)) {
            error_ = "GL: texture dimension or usage unsupported"; return {};
        }
        if (d.format == PixelFormat::RGBA16F && !hdr_) { error_ = "GL: HDR textures unsupported"; return {}; }
        const GLenum target = d.dimension == TextureDimension::Texture3D ? GL_TEXTURE_3D :
                              d.dimension == TextureDimension::Texture2DArray ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_CUBE_MAP;
        const GLenum compressed = compressedEnum(d.format);
        if (compressed && (!supports(d.format) || !d.levelData || !d.levelSize || d.levelCount < 1 ||
            d.dimension == TextureDimension::Texture3D)) {
            error_ = "GL: compressed arrays/cubes require complete block payloads"; return {};
        }
        const int z = d.dimension == TextureDimension::Texture3D ? d.depth : d.arrayLayers;
        const bool autoMips = d.mipmaps && !compressed &&
                              d.format != PixelFormat::RGBA16F && d.format != PixelFormat::RGBA32F;
        const int levels = compressed ? d.levelCount : d.mipLevels > 0 ? d.mipLevels :
                           autoMips ? maxTextureMipLevels(d.width, d.height, d.depth) : 1;
        const GLenum internal = d.format == PixelFormat::Depth ? GL_DEPTH_COMPONENT24 :
                                d.format == PixelFormat::R8 ? GL_R8 :
                                d.format == PixelFormat::RGBA16F ? GL_RGBA16F :
                                d.format == PixelFormat::RGBA32F ? GL_RGBA32F :
                                d.format == PixelFormat::RGBA8_sRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8;
        const GLenum format = d.format == PixelFormat::Depth ? GL_DEPTH_COMPONENT :
                              d.format == PixelFormat::R8 ? GL_RED : GL_RGBA;
        const GLenum type = d.format == PixelFormat::Depth ? GL_UNSIGNED_INT :
                            d.format == PixelFormat::RGBA16F ? GL_HALF_FLOAT :
                            d.format == PixelFormat::RGBA32F ? GL_FLOAT : GL_UNSIGNED_BYTE;
        const size_t bpp = d.format == PixelFormat::R8 ? 1 :
                           d.format == PixelFormat::RGBA16F ? 8 :
                           d.format == PixelFormat::RGBA32F ? 16 : 4;
        GLTexture t;
        t.fmt = d.format; t.dimension = d.dimension; t.target = target;
        t.w = d.width; t.h = d.height; t.depth = d.depth; t.layers = d.arrayLayers;
        t.levels = levels; t.mips = autoMips; t.compressed = compressed != 0;
        t.renderTarget = d.renderTarget || d.format == PixelFormat::Depth;
        glGenTextures(1, &t.id);
        if (!t.id) { error_ = "GL: texture allocation failed"; return {}; }
        glBindTexture(target, t.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (int l = 0; l < levels; ++l) {
            const int w = std::max(1, d.width >> l), h = std::max(1, d.height >> l);
            const int slices = d.dimension == TextureDimension::Texture3D ? std::max(1, d.depth >> l) : z;
            const size_t faceBytes = size_t(w) * size_t(h) * bpp;
            if (compressed) {
                if (!d.levelData[l]) { glDeleteTextures(1, &t.id); error_ = "GL: missing mip payload"; return {}; }
                if (d.dimension == TextureDimension::Cube) {
                    if (d.levelSize[l] % 6 != 0) { glDeleteTextures(1, &t.id); error_="GL: bad cube payload"; return {}; }
                    const size_t one = d.levelSize[l] / 6;
                    const auto* src = static_cast<const uint8_t*>(d.levelData[l]);
                    for (int face = 0; face < 6; ++face)
                        glCompressedTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + GLenum(face),
                          l, compressed, w, h, 0, GLsizei(one), src + size_t(face) * one);
                } else {
                    glCompressedTexImage3D(target, l, compressed, w, h, slices, 0,
                                           GLsizei(d.levelSize[l]), d.levelData[l]);
                }
            } else if (d.dimension == TextureDimension::Cube) {
                const auto* src = static_cast<const uint8_t*>(d.data);
                for (int face = 0; face < 6; ++face)
                    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + GLenum(face), l,
                        GLint(internal), w, h, 0, format, type,
                        l == 0 && src ? src + size_t(face) * faceBytes : nullptr);
            } else {
                glTexImage3D(target, l, GLint(internal), w, h, slices, 0, format, type, l == 0 ? d.data : nullptr);
            }
        }
        glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, levels - 1);
        const bool linear = d.filter == Filter::Linear && d.format != PixelFormat::RGBA32F;
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, levels > 1 ?
            (linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST) :
            (linear ? GL_LINEAR : GL_NEAREST));
        const GLint wrap = d.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
        glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
        glTexParameteri(target, GL_TEXTURE_WRAP_R, wrap);
        if (d.format == PixelFormat::Depth && d.compare) {
            glTexParameteri(target, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glTexParameteri(target, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        }
        if (autoMips && d.data && levels > 1) glGenerateMipmap(target);
        if (glGetError() != GL_NO_ERROR) {
            glDeleteTextures(1, &t.id); error_ = "GL: unsupported dimensional texture allocation"; return {};
        }
        return {textures_.add(t)};
    }

    TextureHandle createTexture(const TextureDesc& d) override {
        if (const TextureDescError e = validateTextureDesc(d); e != TextureDescError::None) {
            error_ = textureDescErrorString(e); return {};
        }
        if (d.dimension != TextureDimension::Texture2D)
            return createDimensionalTexture(d);
        if (d.sampleCount != 1 || d.storage) {
            error_ = "GL: 2D multisampling and storage images are unsupported";
            return {};
        }
        if (d.format == PixelFormat::RGBA16F && !hdr_) { error_ = "RGBA16F rendering unsupported"; return {}; }
        GLTexture t; t.fmt = d.format; t.w = d.width; t.h = d.height;
        t.renderTarget = d.renderTarget || d.format == PixelFormat::Depth;
        t.mips = d.mipmaps && d.format != PixelFormat::Depth && !d.renderTarget;
        t.compressed = compressedEnum(d.format) != 0;
        t.levels = t.compressed ? std::max(1, d.levelCount) :
                   d.mipLevels > 0 ? d.mipLevels :
                   t.mips ? maxTextureMipLevels(d.width, d.height) : 1;
        glGenTextures(1, &t.id);
        glBindTexture(GL_TEXTURE_2D, t.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (const GLenum ce = compressedEnum(d.format)) {
            for (int l = 0; l < d.levelCount; ++l)
                glCompressedTexImage2D(GL_TEXTURE_2D, l, ce, std::max(1, d.width >> l), std::max(1, d.height >> l), 0,
                                       GLsizei(d.levelSize[l]), d.levelData[l]);
            t.mips = false;
            const GLint mf = d.levelCount > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, std::max(0, d.levelCount - 1));
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, d.filter == Filter::Linear ? mf : GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, d.filter == Filter::Linear ? GL_LINEAR : GL_NEAREST);
            GLint wrap = d.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
            return {textures_.add(t)};
        }
        if (d.format == PixelFormat::Depth) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, d.width, d.height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
            if (d.compare) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
            }
        } else if (d.format == PixelFormat::R8) glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, d.width, d.height, 0, GL_RED, GL_UNSIGNED_BYTE, d.data);
        else if (d.format == PixelFormat::RGBA16F) { t.mips = false; glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, d.width, d.height, 0, GL_RGBA, GL_HALF_FLOAT, d.data); }
        else if (d.format == PixelFormat::RGBA32F) { t.mips = false; glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, d.width, d.height, 0, GL_RGBA, GL_FLOAT, d.data); }
        else if (d.format == PixelFormat::RGBA8_sRGB) glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, d.width, d.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, d.data);
        else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, d.width, d.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, d.data);
        // Explicit mip allocations are independent from automatic mip generation.
        // Every allocated subresource must exist before attaching a nonzero mip to an FBO.
        if (!t.compressed && t.levels > 1) {
            const GLenum internal = d.format == PixelFormat::Depth ? GL_DEPTH_COMPONENT24 :
                                    d.format == PixelFormat::R8 ? GL_R8 :
                                    d.format == PixelFormat::RGBA16F ? GL_RGBA16F :
                                    d.format == PixelFormat::RGBA32F ? GL_RGBA32F :
                                    d.format == PixelFormat::RGBA8_sRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8;
            const GLenum format = d.format == PixelFormat::Depth ? GL_DEPTH_COMPONENT :
                                  d.format == PixelFormat::R8 ? GL_RED : GL_RGBA;
            const GLenum type = d.format == PixelFormat::Depth ? GL_UNSIGNED_INT :
                                d.format == PixelFormat::RGBA16F ? GL_HALF_FLOAT :
                                d.format == PixelFormat::RGBA32F ? GL_FLOAT : GL_UNSIGNED_BYTE;
            for (int mip = 1; mip < t.levels; ++mip)
                glTexImage2D(GL_TEXTURE_2D, mip, GLint(internal),
                             std::max(1,d.width >> mip), std::max(1,d.height >> mip),
                             0, format, type, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, t.levels - 1);
        }
        GLint f = d.filter == Filter::Linear && d.format != PixelFormat::RGBA32F ? GL_LINEAR : GL_NEAREST; // float textures are not filterable on ES3
        if (t.mips && d.data) glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, t.levels > 1 ?
                        (d.filter == Filter::Linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST) : f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
        if (d.format == PixelFormat::RGBA16F && d.renderTarget) {
            GLint oldDraw=0, oldRead=0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDraw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldRead);
            GLuint probe=0; glGenFramebuffers(1,&probe); glBindFramebuffer(GL_FRAMEBUFFER,probe);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t.id,0);
            const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(oldDraw)); glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(oldRead)); glDeleteFramebuffers(1,&probe);
            if (!complete) { glDeleteTextures(1,&t.id); error_="RGBA16F framebuffer allocation failed"; return {}; }
        }
        GLint wrap = d.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        return {textures_.add(t)};
    }
    void updateTexture(TextureHandle h, int x, int y, int w, int hh, const void* pixels, int rowPitch) override {
        auto* t = textures_.get(h.id);
        if (!t || t->dimension != TextureDimension::Texture2D || t->compressed || t->fmt == PixelFormat::Depth) return;
        glBindTexture(GL_TEXTURE_2D, t->id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        const int bpp = t->fmt == PixelFormat::R8 ? 1 : t->fmt == PixelFormat::RGBA32F ? 16 : t->fmt == PixelFormat::RGBA16F ? 8 : 4;
        const GLenum fmt = t->fmt == PixelFormat::R8 ? GL_RED : GL_RGBA;
        const GLenum type = t->fmt == PixelFormat::RGBA32F ? GL_FLOAT : t->fmt == PixelFormat::RGBA16F ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
        if (rowPitch == w * bpp) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, hh, fmt, type, pixels);
        } else { // GL_UNPACK_ROW_LENGTH exists in ES3 but upload row by row for maximum portability
            for (int r = 0; r < hh; ++r)
                glTexSubImage2D(GL_TEXTURE_2D, 0, x, y + r, w, 1, fmt, type, static_cast<const uint8_t*>(pixels) + size_t(r) * rowPitch);
        }
        if (t->mips) glGenerateMipmap(GL_TEXTURE_2D);
    }
    bool updateTextureRegion(TextureHandle h, const TextureUploadDesc& u) override {
        auto* t = textures_.get(h.id);
        if (!t || !u.data || t->compressed || t->fmt == PixelFormat::Depth ||
            u.mipLevel < 0 || u.mipLevel >= t->levels || u.arrayLayer < 0 ||
            u.x < 0 || u.y < 0 || u.z < 0 || u.width < 1 || u.height < 1 || u.depth < 1)
            return false;
        const int mw = std::max(1, t->w >> u.mipLevel), mh = std::max(1, t->h >> u.mipLevel);
        const int md = t->dimension == TextureDimension::Texture3D ?
                       std::max(1, t->depth >> u.mipLevel) : 1;
        if (u.x > mw - u.width || u.y > mh - u.height || u.z > md - u.depth ||
            (t->dimension != TextureDimension::Texture3D && (u.z != 0 || u.depth != 1)) ||
            (t->dimension == TextureDimension::Texture2D && u.arrayLayer != 0) ||
            (t->dimension == TextureDimension::Cube && u.arrayLayer >= 6) ||
            (t->dimension == TextureDimension::Texture2DArray && u.arrayLayer >= t->layers))
            return false;
        const int bpp = t->fmt == PixelFormat::R8 ? 1 :
                        t->fmt == PixelFormat::RGBA16F ? 8 :
                        t->fmt == PixelFormat::RGBA32F ? 16 : 4;
        if (u.rowPitchBytes < u.width * bpp) return false;
        const int imagePitch = u.slicePitchBytes > 0 ? u.slicePitchBytes : u.rowPitchBytes * u.height;
        if (imagePitch < u.rowPitchBytes * u.height) return false;
        const GLenum format = t->fmt == PixelFormat::R8 ? GL_RED : GL_RGBA;
        const GLenum type = t->fmt == PixelFormat::RGBA16F ? GL_HALF_FLOAT :
                            t->fmt == PixelFormat::RGBA32F ? GL_FLOAT : GL_UNSIGNED_BYTE;
        glBindTexture(t->target, t->id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        const auto* src = static_cast<const uint8_t*>(u.data);
        if (t->dimension == TextureDimension::Texture2D ||
            t->dimension == TextureDimension::Cube) {
            const GLenum target = t->dimension == TextureDimension::Cube ?
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + GLenum(u.arrayLayer) : GL_TEXTURE_2D;
            for (int y = 0; y < u.height; ++y)
                glTexSubImage2D(target, u.mipLevel, u.x, u.y + y, u.width, 1, format, type,
                                src + size_t(y) * size_t(u.rowPitchBytes));
        } else {
            const int z = t->dimension == TextureDimension::Texture3D ? u.z : u.arrayLayer;
            for (int zz = 0; zz < u.depth; ++zz)
                for (int y = 0; y < u.height; ++y)
                    glTexSubImage3D(t->target, u.mipLevel, u.x, u.y + y, z + zz,
                      u.width, 1, 1, format, type,
                      src + size_t(zz) * size_t(imagePitch) + size_t(y) * size_t(u.rowPitchBytes));
        }
        if (t->mips && u.mipLevel == 0) glGenerateMipmap(t->target);
        return glGetError() == GL_NO_ERROR;
    }
    TextureViewHandle createTextureView(TextureHandle h, const TextureViewDesc& v) override {
        // Explicitly support only identity views on the GL33/GLES3 portability path.
        // Real aliases of individual mips/faces require GL_ARB_texture_view / GL 4.3
        // and must not be silently reinterpreted as the whole parent.
        const GLTexture* t = textures_.get(h.id);
        if (!t) { error_ = "GL texture view: invalid parent"; return {}; }
        TextureDesc parent;
        parent.width = t->w; parent.height = t->h; parent.depth = t->depth;
        parent.arrayLayers = t->layers; parent.dimension = t->dimension;
        parent.format = t->fmt; parent.mipLevels = t->levels;
        if (!validateTextureViewDesc(parent, v) ||
            v.dimension != t->dimension || v.baseMipLevel != 0 ||
            v.mipLevelCount != t->levels || v.baseArrayLayer != 0 ||
            v.arrayLayerCount != t->layers) {
            error_ = "GL texture view: only whole-resource identity views are supported"; return {};
        }
        return {views_.add({h.id})};
    }
    void destroyTextureView(TextureViewHandle h) override { views_.remove(h.id); }

    SamplerHandle createSampler(const SamplerDesc& s) override {
        if (!validateSamplerDesc(s) ||
            (s.maxAnisotropy > 1.0f && !anisotropy_) ||
            (s.lodBias != 0.0f && info_.backend != Backend::GL33) ||
            (info_.backend != Backend::GL33 &&
             (s.addressU == SamplerAddressMode::ClampToBorder ||
              s.addressV == SamplerAddressMode::ClampToBorder ||
              s.addressW == SamplerAddressMode::ClampToBorder))) {
            error_ = "GL sampler: unsupported addressing, anisotropy or LOD bias"; return {};
        }
        auto address = [](SamplerAddressMode mode) -> GLint {
            switch (mode) {
            case SamplerAddressMode::ClampToEdge: return GL_CLAMP_TO_EDGE;
            case SamplerAddressMode::Repeat: return GL_REPEAT;
            case SamplerAddressMode::MirrorRepeat: return GLint(kGL_MIRRORED_REPEAT);
            case SamplerAddressMode::ClampToBorder: return GLint(kGL_CLAMP_TO_BORDER);
            }
            return GL_CLAMP_TO_EDGE;
        };
        auto compare = [](CompareFunction mode) -> GLint {
            switch (mode) {
            case CompareFunction::Never: return GL_NEVER;
            case CompareFunction::Less: return GL_LESS;
            case CompareFunction::LessEqual: return GL_LEQUAL;
            case CompareFunction::Equal: return GL_EQUAL;
            case CompareFunction::Greater: return GL_GREATER;
            case CompareFunction::GreaterEqual: return GL_GEQUAL;
            case CompareFunction::NotEqual: return GL_NOTEQUAL;
            case CompareFunction::Always: return GL_ALWAYS;
            }
            return GL_LEQUAL;
        };
        GLuint sampler = 0;
        glGenSamplers(1, &sampler);
        if (!sampler) { error_ = "GL sampler allocation failed"; return {}; }
        const GLint min = s.minFilter == Filter::Linear ? GL_LINEAR : GL_NEAREST;
        const GLint mag = s.magFilter == Filter::Linear ? GL_LINEAR : GL_NEAREST;
        const GLint minMip = s.mipFilter == SamplerMipFilter::None ? min :
                             s.mipFilter == SamplerMipFilter::Nearest ?
                             (min == GL_LINEAR ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST) :
                             (min == GL_LINEAR ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR);
        glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, minMip);
        glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, mag);
        glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, address(s.addressU));
        glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, address(s.addressV));
        glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R, address(s.addressW));
        glSamplerParameterf(sampler, kGL_TEXTURE_MIN_LOD, s.minLod);
        glSamplerParameterf(sampler, kGL_TEXTURE_MAX_LOD, s.maxLod);
        if (info_.backend == Backend::GL33 && s.lodBias != 0.0f)
            glSamplerParameterf(sampler, kGL_TEXTURE_LOD_BIAS, s.lodBias);
        if (anisotropy_ && s.maxAnisotropy > 1.0f)
            glSamplerParameterf(sampler, kGL_TEXTURE_ANISOTROPY,
                                std::min(s.maxAnisotropy, maxAnisotropy_));
        glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_MODE, s.compare ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE);
        if (s.compare) glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_FUNC, compare(s.comparison));
        if (glGetError() != GL_NO_ERROR) {
            glDeleteSamplers(1, &sampler); error_ = "GL sampler settings rejected"; return {};
        }
        return {samplers_.add({sampler})};
    }
    void destroySampler(SamplerHandle h) override {
        GLSampler* s = samplers_.get(h.id);
        if (!s) return;
        glDeleteSamplers(1, &s->native);
        samplers_.remove(h.id);
    }
    bool bindTextureView(int slot, TextureViewHandle viewHandle, SamplerHandle samplerHandle = {}) override {
        if (slot < 0 || slot >= 16) return false;
        const GLTextureView* v = views_.get(viewHandle.id);
        if (!v) return false;
        const GLTexture* t = textures_.get(v->parentId);
        if (!t) return false;
        GLuint sampler = 0;
        if (samplerHandle.valid()) {
            const GLSampler* s = samplers_.get(samplerHandle.id);
            if (!s) return false;
            sampler = s->native;
        }
        glActiveTexture(GL_TEXTURE0 + GLenum(slot));
        glBindTexture(t->target, t->id);
        glBindSampler(GLuint(slot), sampler);
        return glGetError() == GL_NO_ERROR;
    }

    void destroyTexture(TextureHandle h) override {
        std::vector<uint32_t> children;
        views_.each([&](uint32_t id, GLTextureView& view) {
            if (view.parentId == h.id) children.push_back(id);
        });
        for (uint32_t id : children) views_.remove(id);
        if (auto* t = textures_.get(h.id)) { glDeleteTextures(1, &t->id); textures_.remove(h.id); }
        for (size_t i = 0; i < fbos_.size();) {
            if (fbos_[i].color == h.id || fbos_[i].depth == h.id) { glDeleteFramebuffers(1, &fbos_[i].fbo); fbos_.erase(fbos_.begin() + long(i)); }
            else ++i;
        }
    }
    GLuint framebufferFor(uint32_t color, uint32_t depth) {
        for (auto& f : fbos_) if (f.color == color && f.depth == depth) return f.fbo;
        GLFramebuffer f; f.color = color; f.depth = depth;
        glGenFramebuffers(1, &f.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, f.fbo);
        if (auto* c = textures_.get(color)) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c->id, 0);
        if (auto* d = textures_.get(depth)) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, d->id, 0);
        GLenum none = GL_NONE, c0 = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, textures_.get(color) ? &c0 : &none);
        glReadBuffer(textures_.get(color) ? GL_COLOR_ATTACHMENT0 : GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) error_ = "framebuffer incomplete";
        fbos_.push_back(f);
        return f.fbo;
    }

    // ---------- shaders ----------
    GLuint compile(GLenum type, const char* body) {
        std::string src = info_.backend == Backend::GL33 ? "#version 330 core\n" : "#version 300 es\nprecision highp float;\nprecision highp int;\n";
        src += body;
        GLuint s = glCreateShader(type);
        const char* p = src.c_str();
        glShaderSource(s, 1, &p, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048]; GLsizei n = 0;
            glGetShaderInfoLog(s, sizeof log, &n, log);
            error_ = std::string(type == GL_VERTEX_SHADER ? "vertex" : "fragment") + " shader: " + std::string(log, size_t(n));
            glDeleteShader(s);
            return 0;
        }
        return s;
    }
    ShaderHandle createShader(const ShaderDesc& d) override {
        if (!d.glslVertex || !d.glslFragment) { error_ = "shader has no GLSL source"; return {}; }
        const std::string hdr = colorShaderPrefix(false, d.linearTarget);
        GLuint vs = compile(GL_VERTEX_SHADER, (hdr + d.glslVertex).c_str());
        if (!vs) return {};
        GLuint fs = compile(GL_FRAGMENT_SHADER, (hdr + d.glslFragment).c_str());
        if (!fs) { glDeleteShader(vs); return {}; }
        GLuint p = glCreateProgram();
        glAttachShader(p, vs); glAttachShader(p, fs);
        glLinkProgram(p);
        glDeleteShader(vs); glDeleteShader(fs);
        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048]; GLsizei n = 0;
            glGetProgramInfoLog(p, sizeof log, &n, log);
            error_ = "link: " + std::string(log, size_t(n));
            glDeleteProgram(p);
            return {};
        }
        glUseProgram(p);
        for (int i = 0; i < d.uniformSlots; ++i) {
            std::string n = "U" + std::to_string(i);
            GLuint bi = glGetUniformBlockIndex(p, n.c_str());
            if (bi != GL_INVALID_INDEX) glUniformBlockBinding(p, bi, GLuint(i));
        }
        for (int i = 0; i < d.textureSlots; ++i) {
            std::string n = "tex" + std::to_string(i);
            GLint loc = glGetUniformLocation(p, n.c_str());
            if (loc >= 0) glUniform1i(loc, i);
        }
        currentProgram_ = p;
        return {shaders_.add(GLShader{p, d.glslVertex, d.glslFragment, d.textureSlots, d.uniformSlots, d.linearTarget})};
    }
    void destroyShader(ShaderHandle h) override {
        if (auto* s = shaders_.get(h.id)) { glDeleteProgram(s->program); shaders_.remove(h.id); }
    }

    PipelineHandle createPipeline(const PipelineDesc& d) override {
        if (d.colorAttachmentCount < 1 || d.colorAttachmentCount > kMaxRenderTargetColors) {
            error_ = "GL: pipeline color output count must be 1..4";
            return {};
        }
        if (d.sampleCount != 1) {
            error_ = "GL: multisample pipeline variants are not implemented";
            return {};
        }
        auto* s = shaders_.get(d.shader.id);
        if (!s) { error_ = "pipeline: invalid shader"; return {}; }
        return {pipelines_.add(GLPipeline{d, s->program, {}, {}, false})};
    }
    void destroyPipeline(PipelineHandle h) override {
        if (auto* p = pipelines_.get(h.id)) {
            const auto variant = p->hdr; const auto shader = p->hdrShader;
            pipelines_.remove(h.id);
            if (variant.valid()) destroyPipeline(variant);
            if (shader.valid()) destroyShader(shader);
        }
    }
    PipelineHandle hdrPipeline(PipelineHandle h) {
        auto* p = pipelines_.get(h.id);
        if (!p) return {};
        if (p->hdr.valid()) return p->hdr;
        if (p->hdrFailed) return {};
        p->hdrFailed = true; // compile at most once; a broken variant must not stall every frame
        PipelineDesc pd = p->desc;
        auto* s = shaders_.get(pd.shader.id);
        if (!s) return {};
        ShaderDesc sd;
        const std::string vs = s->vs, fs = s->fs;
        sd.glslVertex = vs.c_str(); sd.glslFragment = fs.c_str();
        sd.textureSlots = s->textures; sd.uniformSlots = s->uniforms;
        sd.linearTarget = true;
        const auto shader = createShader(sd);
        if (!shader.valid()) { std::fprintf(stderr, "[gfx] HDR shader: %s\n", error_.c_str()); return {}; }
        pd.shader = shader; pd.colorFormat = PixelFormat::RGBA16F;
        for (int i = 1; i < pd.colorAttachmentCount; ++i)
            pd.additionalColorFormats[i - 1] = PixelFormat::RGBA16F;
        const auto variant = createPipeline(pd);
        if (!variant.valid()) { std::fprintf(stderr, "[gfx] HDR pipeline: %s\n", error_.c_str()); destroyShader(shader); return {}; }
        p = pipelines_.get(h.id); // pool may have reallocated
        p->hdr = variant; p->hdrShader = shader; p->hdrFailed = false;
        return variant;
    }

    // ---------- frame ----------
    CaptureFn capture_;
#if defined(__APPLE__) || defined(_WIN32)
    struct TimedQuery { uint64_t tag; GLuint query; };
    std::vector<TimedQuery> timedQueries_;
    bool queryActive_ = false;
    bool timeGpuFrame(uint64_t tag) override {
        if (info_.backend != Backend::GL33 || queryActive_ || timedQueries_.size() >= 64) return false;
        GLuint q=0; glGenQueries(1,&q); glBeginQuery(GL_TIME_ELAPSED,q);
        timedQueries_.push_back({tag,q}); queryActive_=true; return true;
    }
    std::vector<GpuTiming> takeGpuTimings() override {
        std::vector<GpuTiming> out;
        for (size_t i=0;i<timedQueries_.size();) {
            auto t=timedQueries_[i]; GLuint ready=0;
            glGetQueryObjectuiv(t.query,GL_QUERY_RESULT_AVAILABLE,&ready);
            if (!ready) {++i;continue;}
            GLuint64 ns=0; glGetQueryObjectui64v(t.query,GL_QUERY_RESULT,&ns);
            out.push_back({t.tag,double(ns)/1000000.0,true}); glDeleteQueries(1,&t.query);
            timedQueries_.erase(timedQueries_.begin()+i);
        }
        return out;
    }
#endif
    void beginFrame(int w, int h) override {
        fbW_ = w; fbH_ = h;
        uboOffset_ = 0;
        for (auto& d : slotData_) d.clear();
#if !defined(__EMSCRIPTEN__) && !defined(__APPLE__)
        glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
        glBufferData(GL_UNIFORM_BUFFER, kUniformRingSize, nullptr, GL_STREAM_DRAW); // orphan the ring
#endif
        glBindVertexArray(vao_);
    }
    void endFrame() override { // swap is done by the platform layer (EGL/WGL/browser)
#if defined(__APPLE__) || defined(_WIN32)
        if (queryActive_) {glEndQuery(GL_TIME_ELAPSED);queryActive_=false;glFlush();}
#endif
        if (!capture_) return;
        std::vector<uint8_t> px(size_t(fbW_) * size_t(fbH_) * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, fbW_, fbH_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        const size_t row = size_t(fbW_) * 4;
        std::vector<uint8_t> tmp(row);
        for (int y = 0; y < fbH_ / 2; ++y) { // GL is bottom-up
            uint8_t* a = px.data() + size_t(y) * row;
            uint8_t* b = px.data() + size_t(fbH_ - 1 - y) * row;
            std::copy(a, a + row, tmp.data()); std::copy(b, b + row, a); std::copy(tmp.data(), tmp.data() + row, b);
        }
        auto fn = std::move(capture_);
        capture_ = nullptr;
        fn(fbW_, fbH_, std::move(px));
    }
    void captureNextFrame(CaptureFn fn) override { capture_ = std::move(fn); }

    bool supportsRenderTargetPasses() const override { return true; }

    bool beginRenderTargets(const RenderTargetPassDesc& desc) override {
        if (!validateRenderTargetPassDesc(desc) || inPass_) {
            error_ = "GL: invalid or nested render target pass";
            return false;
        }
        GLint maxColors = 0, maxDraws = 0;
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxColors);
        glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDraws);
        if (desc.colorCount > maxColors || desc.colorCount > maxDraws) {
            error_ = "GL: MRT attachment count exceeds device limits";
            return false;
        }
        // GL33/GLES3 don't expose a portable multisample texture resolve
        // through this API. Do not accept the pass and lose the resolve silently.
        for (int i = 0; i < desc.colorCount; ++i) {
            if (desc.colors[size_t(i)].resolveTexture.valid()) {
                error_ = "GL: explicit MSAA color resolve is not implemented";
                return false;
            }
        }
        auto validate = [&](const RenderTargetAttachment& a, bool wantDepth, int& w, int& h) {
            const GLTexture* t = textures_.get(a.texture.id);
            if (!t || !t->id || !t->renderTarget || t->compressed ||
                a.mipLevel < 0 || a.mipLevel >= t->levels || a.arrayLayer < 0 ||
                t->dimension == TextureDimension::Texture1D ||
                (wantDepth ? t->fmt != PixelFormat::Depth :
                             (t->fmt != PixelFormat::RGBA8 && t->fmt != PixelFormat::RGBA8_sRGB &&
                              t->fmt != PixelFormat::RGBA16F))) return false;
            const int slices = t->dimension == TextureDimension::Texture3D ?
                               std::max(1, t->depth >> a.mipLevel) :
                               t->dimension == TextureDimension::Cube ? 6 : t->layers;
            if (a.arrayLayer >= slices) return false;
            w = std::max(1, t->w >> a.mipLevel);
            h = std::max(1, t->h >> a.mipLevel);
            return true;
        };
        int width = 0, height = 0;
        const GLTexture* colors[kMaxRenderTargetColors]{};
        const GLTexture* depth = nullptr;
        PixelFormat sharedColorFormat = PixelFormat::RGBA8;
        for (int i = 0; i < desc.colorCount; ++i) {
            int w = 0, h = 0;
            const auto& attachment = desc.colors[size_t(i)];
            if (!validate(attachment, false, w, h) ||
                (width && (width != w || height != h))) {
                error_ = "GL: color mip/layer unsupported or attachment sizes differ";
                return false;
            }
            const GLTexture* t = textures_.get(attachment.texture.id);
            // Current HDR shader variants and blend policies are shared among
            // output slots; heterogeneous formats need explicit per-slot metadata.
            if (i && t->fmt != sharedColorFormat) {
                error_ = "GL: mixed color pixel formats require MRT pipeline variants";
                return false;
            }
            sharedColorFormat = t->fmt;
            colors[i] = t;
            width = w; height = h;
        }
        if (desc.depth.texture.valid()) {
            int w = 0, h = 0;
            if (!validate(desc.depth, true, w, h) ||
                (width && (width != w || height != h))) {
                error_ = "GL: depth mip/layer unsupported or extent mismatch";
                return false;
            }
            depth = textures_.get(desc.depth.texture.id);
            if (!width) { width = w; height = h; }
        }
        if (!width || !height) return false;
        auto attach = [&](GLenum attachment, const GLTexture* tex,
                          const RenderTargetAttachment& a) {
            if (!tex) return;
            if (tex->dimension == TextureDimension::Texture2D) {
                glFramebufferTexture2D(GL_FRAMEBUFFER, attachment,
                                       GL_TEXTURE_2D, tex->id, a.mipLevel);
            } else if (tex->dimension == TextureDimension::Cube) {
                glFramebufferTexture2D(GL_FRAMEBUFFER, attachment,
                    GL_TEXTURE_CUBE_MAP_POSITIVE_X + GLenum(a.arrayLayer),
                    tex->id, a.mipLevel);
            } else {
                glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment, tex->id,
                                          a.mipLevel, a.arrayLayer);
            }
        };
        GLint previousFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
        GLuint framebuffer = 0;
        glGenFramebuffers(1, &framebuffer);
        if (!framebuffer) {
            error_ = "GL: framebuffer creation failed";
            return false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        GLenum drawBuffers[kMaxRenderTargetColors]{};
        for (int i = 0; i < desc.colorCount; ++i) {
            const GLenum slot = GL_COLOR_ATTACHMENT0 + GLenum(i);
            attach(slot, colors[i], desc.colors[size_t(i)]);
            drawBuffers[i] = slot;
        }
        if (depth) attach(GL_DEPTH_ATTACHMENT, depth, desc.depth);
        const GLenum none = GL_NONE;
        glDrawBuffers(desc.colorCount > 0 ? desc.colorCount : 1,
                      desc.colorCount > 0 ? drawBuffers : &none);
        glReadBuffer(desc.colorCount > 0 ? GL_COLOR_ATTACHMENT0 : GL_NONE);
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                                  GL_FRAMEBUFFER_COMPLETE &&
                              glGetError() == GL_NO_ERROR;
        if (!complete) {
            glBindFramebuffer(GL_FRAMEBUFFER, GLuint(previousFbo));
            glDeleteFramebuffers(1, &framebuffer);
            error_ = "GL: selected MRT mip/layer framebuffer incomplete";
            return false;
        }
        scopedFbo_ = framebuffer;
        inPass_ = true;
        explicitPassColors_ = desc.colorCount;
        explicitPassFormat_ = sharedColorFormat;
        passW_ = width; passH_ = height;
        hdrPass_ = desc.colorCount > 0 && sharedColorFormat == PixelFormat::RGBA16F;
        glBindVertexArray(vao_);
        glDisable(GL_SCISSOR_TEST);
        if (desc.colorCount && sharedColorFormat == PixelFormat::RGBA8_sRGB &&
            info_.backend == Backend::GL33 && !srgbWrite_) {
            glEnable(kGL_FRAMEBUFFER_SRGB);
            srgbWrite_ = true;
        }
        glViewport(0, 0, width, height);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        // glClear(GL_COLOR_BUFFER_BIT) would erase *all* attachments with the
        // same value. Per-attachment clears preserve unrelated render outputs.
        for (int i = 0; i < desc.colorCount; ++i) {
            const auto& attachment = desc.colors[size_t(i)];
            if (attachment.clear)
                glClearBufferfv(GL_COLOR, i, attachment.clearColor);
        }
        if (depth && desc.depth.clear) {
            glDepthMask(GL_TRUE);
            glClearBufferfv(GL_DEPTH, 0, &desc.clearDepth);
        }
        pipelineId_ = 0;
        return true;
    }

    void beginPass(const PassDesc& d) override {
        explicitPassColors_ = -1;
        inPass_ = true;
        const auto* color = textures_.get(d.colorTarget.id);
        hdrPass_ = color && color->fmt == PixelFormat::RGBA16F;
        glBindVertexArray(vao_);
        glDisable(GL_SCISSOR_TEST);
        passW_ = fbW_; passH_ = fbH_;
        if (d.colorTarget.valid() || d.depthTarget.valid()) {
            glBindFramebuffer(GL_FRAMEBUFFER, framebufferFor(d.colorTarget.id, d.depthTarget.id));
            auto* t = textures_.get(d.colorTarget.valid() ? d.colorTarget.id : d.depthTarget.id);
            if (t) { passW_ = t->w; passH_ = t->h; }
            auto* c = textures_.get(d.colorTarget.id);
            if (c && c->fmt == PixelFormat::RGBA8_sRGB && info_.backend == Backend::GL33 && !srgbWrite_) {
                glEnable(kGL_FRAMEBUFFER_SRGB);   // desktop GL encodes (and blends in linear) only when asked
                srgbWrite_ = true;
            }
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        glViewport(0, 0, passW_, passH_);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        GLbitfield mask = 0;
        if (d.clearColorEnabled) { glClearColor(d.clearColor[0], d.clearColor[1], d.clearColor[2], d.clearColor[3]); mask |= GL_COLOR_BUFFER_BIT; }
        if (d.clearDepthEnabled) {
            glDepthMask(GL_TRUE);
#if defined(AIVERSE_GL_LOADER)
            glClearDepth(d.clearDepth);
#else
            glClearDepthf(d.clearDepth);
#endif
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (mask) glClear(mask);
        pipelineId_ = 0;
    }
    void endPass() override {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (scopedFbo_) { glDeleteFramebuffers(1, &scopedFbo_); scopedFbo_ = 0; }
        inPass_ = false;
        explicitPassColors_ = -1;
        glDisable(GL_POLYGON_OFFSET_FILL);
        if (srgbWrite_) { glDisable(kGL_FRAMEBUFFER_SRGB); srgbWrite_ = false; }
        passW_ = fbW_; passH_ = fbH_;
    }

    bool copyColor(TextureHandle from, TextureHandle to) override {
        auto* dst = textures_.get(to.id);
        if (!dst || dst->fmt != PixelFormat::RGBA8) return false;
        int sw = fbW_, sh = fbH_;
        if (from.valid()) {
            auto* src = textures_.get(from.id);
            if (!src) return false;
            sw = src->w; sh = src->h;
        }
        if (sw != dst->w || sh != dst->h) return false;
        // a blit (not glCopyTexSubImage2D): converts RGB8 / multisampled default framebuffers too
        const GLuint readFbo = from.valid() ? framebufferFor(from.id, 0) : 0, drawFbo = framebufferFor(to.id, 0);  // may bind while creating
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(0, 0, sw, sh, 0, 0, sw, sh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }

    void setViewport(const Rect& r) override { glViewport(r.x, passH_ - (r.y + r.h), r.w, r.h); }
    void setScissor(const Rect& r) override {
        if (r.w <= 0 || r.h <= 0) { glDisable(GL_SCISSOR_TEST); return; }
        glEnable(GL_SCISSOR_TEST);
        glScissor(r.x, passH_ - (r.y + r.h), r.w, r.h);
    }

    void setPipeline(PipelineHandle h) override {
        auto* p = pipelines_.get(h.id);
        if (p && hdrPass_ && !p->desc.depthOnly && p->desc.colorFormat != PixelFormat::RGBA16F) {
            h = hdrPipeline(h); p = pipelines_.get(h.id);
        }
        pipelineId_ = p ? h.id : 0;
        if (!p) return;
        const auto& d = p->desc;
        if (explicitPassColors_ >= 0) {
            const int pipelineColors = d.depthOnly ? 0 : d.colorAttachmentCount;
            if (pipelineColors != explicitPassColors_) {
                error_ = "GL: pipeline color output count differs from active MRT pass";
                pipelineId_ = 0;
                return;
            }
            if (explicitPassColors_ > 0 && d.colorFormat != explicitPassFormat_) {
                error_ = "GL: pipeline color format differs from MRT attachments";
                pipelineId_ = 0;
                return;
            }
            for (int i = 1; i < explicitPassColors_; ++i) {
                if (d.additionalColorFormats[i - 1] != explicitPassFormat_) {
                    error_ = "GL: additional pipeline color format differs from MRT attachments";
                    pipelineId_ = 0;
                    return;
                }
            }
        }
        glUseProgram(p->program);
        if (d.cull == CullMode::None) glDisable(GL_CULL_FACE);
        else { glEnable(GL_CULL_FACE); glCullFace(d.cull == CullMode::Back ? GL_BACK : GL_FRONT); glFrontFace(GL_CCW); }
        if (d.depthTest) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); } else glDisable(GL_DEPTH_TEST);
        glDepthMask(d.depthWrite ? GL_TRUE : GL_FALSE);
        const GLboolean cw = d.colorWrite ? GL_TRUE : GL_FALSE;
        glColorMask(cw, cw, cw, cw);
        if (d.depthBiasConstant != 0 || d.depthBiasSlope != 0) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(d.depthBiasSlope, d.depthBiasConstant);
        } else glDisable(GL_POLYGON_OFFSET_FILL);
        switch (d.blend) {
            case BlendMode::Opaque: glDisable(GL_BLEND); break;
            case BlendMode::Alpha:
                glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
            case BlendMode::Additive:
                glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE); break;
            case BlendMode::Premultiplied:
                glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
                glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
            case BlendMode::Multiply:
                glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
                glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE); break;
            case BlendMode::Subtract:
                glEnable(GL_BLEND); glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE); break;
            case BlendMode::Screen:
                glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
                glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
        }
        attribsDirty_ = true;
    }
    void setVertexBuffer(BufferHandle b, size_t offset) override { vbId_ = b.id; vbOffset_ = offset; attribsDirty_ = true; }
    void setIndexBuffer(BufferHandle b, size_t offset) override {
        ibId_ = b.id; ibOffset_ = offset;
        if (auto* ib = buffers_.get(b.id)) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->id);
    }
    void setTexture(int slot, TextureHandle h) override {
        if (slot < 0 || slot >= 16) return;
        auto* t = textures_.get(h.id);
        glActiveTexture(GL_TEXTURE0 + GLenum(slot));
        // Default texture-specific filtering must not inherit a previously bound
        // independent sampler from bindTextureView.
        glBindSampler(GLuint(slot), 0);
        if (t) glBindTexture(t->target, t->id);
        else {
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindTexture(GL_TEXTURE_3D, 0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        }
    }
    void setUniforms(int slot, const void* data, size_t size) override {
        if (slot < 0 || slot >= kMaxUniformSlots || size == 0) return;
#if defined(__EMSCRIPTEN__) || defined(__APPLE__)
        // Metal-backed GL/ANGLE can copy a busy buffer on subData, even for disjoint ring ranges.
        // Orphan only this slot's small block instead of cloning the 1 MB ring per draw.
        glBindBuffer(GL_UNIFORM_BUFFER, slotUbos_[slot]);
        glBufferData(GL_UNIFORM_BUFFER, GLsizeiptr(size), data, GL_STREAM_DRAW);
        glBindBufferRange(GL_UNIFORM_BUFFER, GLuint(slot), slotUbos_[slot], 0, GLsizeiptr(size));
        return;
#endif
        size_t off = (uboOffset_ + uboAlign_ - 1) / uboAlign_ * uboAlign_;
        if (off + size > kUniformRingSize) { // ring exhausted: orphan and restart
            glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
            glBufferData(GL_UNIFORM_BUFFER, kUniformRingSize, nullptr, GL_STREAM_DRAW);
            uboOffset_ = 0;
            // the other slots still point into the orphaned storage: upload their last data again
            for (int s = 0; s < kMaxUniformSlots; ++s)
                if (s != slot && !slotData_[s].empty()) writeUniforms(s, slotData_[s].data(), slotData_[s].size());
            off = (uboOffset_ + uboAlign_ - 1) / uboAlign_ * uboAlign_;
        }
        slotData_[slot].assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
        uboOffset_ = off;
        writeUniforms(slot, data, size);
    }
    void writeUniforms(int slot, const void* data, size_t size) {
        size_t off = (uboOffset_ + uboAlign_ - 1) / uboAlign_ * uboAlign_;
        glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
        glBufferSubData(GL_UNIFORM_BUFFER, GLintptr(off), GLsizeiptr(size), data);
        glBindBufferRange(GL_UNIFORM_BUFFER, GLuint(slot), ubo_, GLintptr(off), GLsizeiptr(size));
        uboOffset_ = off + size;
    }

    void applyAttribs(int32_t baseVertex) {
        auto* pl = pipelines_.get(pipelineId_);
        auto* vb = buffers_.get(vbId_);
        if (!pl || !vb) return;
        if (!attribsDirty_ && baseVertex == lastBaseVertex_) return;
        const auto& d = pl->desc;
        glBindBuffer(GL_ARRAY_BUFFER, vb->id);
        for (GLuint i = 0; i < 8; ++i) glDisableVertexAttribArray(i);
        const size_t base = vbOffset_ + size_t(baseVertex) * d.stride;
        for (int i = 0; i < d.attrCount; ++i) {
            const auto& a = d.attrs[i];
            GLint n = 3; GLenum type = GL_FLOAT; GLboolean norm = GL_FALSE;
            switch (a.format) {
                case VertexFormat::Float2: n = 2; break;
                case VertexFormat::Float3: n = 3; break;
                case VertexFormat::Float4: n = 4; break;
                case VertexFormat::UByte4N: n = 4; type = GL_UNSIGNED_BYTE; norm = GL_TRUE; break;
                case VertexFormat::UByte4: n = 4; type = GL_UNSIGNED_BYTE; break;
            }
            glEnableVertexAttribArray(a.location);
            glVertexAttribPointer(a.location, n, type, norm, GLsizei(d.stride), reinterpret_cast<const void*>(base + a.offset));
        }
        attribsDirty_ = false;
        lastBaseVertex_ = baseVertex;
    }
    GLenum prim() { auto* p = pipelines_.get(pipelineId_); return p && p->desc.primitive == Primitive::Lines ? GL_LINES : GL_TRIANGLES; }

    void draw(uint32_t count, uint32_t first) override {
        if (!pipelines_.get(pipelineId_)) return;
        applyAttribs(0);
        glDrawArrays(prim(), GLint(first), GLsizei(count));
    }
    void drawIndexed(uint32_t count, uint32_t firstIndex, int32_t baseVertex, uint32_t instances) override {
        auto* pl = pipelines_.get(pipelineId_);
        if (!pl || !buffers_.get(ibId_)) return;
        // ES 3.0 / WebGL2 lack glDrawElementsBaseVertex: emulate by offsetting attribute pointers.
        applyAttribs(baseVertex);
        const bool u32 = pl->desc.indexType == IndexType::UInt32;
        const size_t off = ibOffset_ + size_t(firstIndex) * (u32 ? 4 : 2);
        if (instances > 1) glDrawElementsInstanced(prim(), GLsizei(count), u32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, reinterpret_cast<const void*>(off), GLsizei(instances));
        else glDrawElements(prim(), GLsizei(count), u32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, reinterpret_cast<const void*>(off));
    }

private:
    bool hdr_ = false;
    bool hdrPass_ = false;
    DeviceInfo info_;
    std::string error_;
    GLuint vao_ = 0, ubo_ = 0, currentProgram_ = 0;
#if defined(__EMSCRIPTEN__) || defined(__APPLE__)   // same guard as every use (Android: unused private field)
    GLuint slotUbos_[kMaxUniformSlots] = {};
#endif
    size_t uboAlign_ = 256, uboOffset_ = 0;
    bool etc2_ = false, s3tc_ = false, bptc_ = false, astc_ = false, rgtc_ = false;
    bool anisotropy_ = false;
    float maxAnisotropy_ = 1.0f;
    bool srgbWrite_ = false;   // GL33: GL_FRAMEBUFFER_SRGB enabled for the current pass
    std::vector<uint8_t> slotData_[kMaxUniformSlots]; // last data per slot, re-uploaded when the ring is orphaned
    int passW_ = 0, passH_ = 0;
    std::vector<GLFramebuffer> fbos_;
    GLuint scopedFbo_ = 0;
    bool inPass_ = false;
    int explicitPassColors_ = -1;
    PixelFormat explicitPassFormat_ = PixelFormat::RGBA8;
    Pool<GLBuffer> buffers_;
    Pool<GLTexture> textures_;
    Pool<GLTextureView> views_;
    Pool<GLSampler> samplers_;
    Pool<GLShader> shaders_;
    Pool<GLPipeline> pipelines_;
    uint32_t pipelineId_ = 0, vbId_ = 0, ibId_ = 0; // ids, not pointers: pools may reallocate
    size_t vbOffset_ = 0, ibOffset_ = 0;
    bool attribsDirty_ = true;
    int32_t lastBaseVertex_ = 0;
};

} // namespace

std::unique_ptr<Device> createGLDevice(Backend flavor) { return std::make_unique<GLDevice>(flavor); }

} // namespace aiverse::gfx
