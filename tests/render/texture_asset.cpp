#include "engine/render/TextureAsset.h"
#include "engine/render/DdsTexture.h"
#include <cstdio>
#include <map>
#include <stdexcept>

using namespace aiverse;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
class TestDevice final : public gfx::Device {
public:
    gfx::DeviceInfo info_;
    std::string error_;
    gfx::TextureDesc lastDesc;
    int created=0, destroyed=0, mipUploads=0, failUpload=0;
    const gfx::DeviceInfo& info() const override { return info_; }
    const std::string& lastError() const override { return error_; }
    bool supports(gfx::PixelFormat f) const override { return f == gfx::PixelFormat::BC7; }
    gfx::BufferHandle createBuffer(const gfx::BufferDesc&) override { return {}; }
    void updateBuffer(gfx::BufferHandle,const void*,size_t,size_t) override {}
    void destroyBuffer(gfx::BufferHandle) override {}
    gfx::TextureHandle createTexture(const gfx::TextureDesc& d) override {
        if (gfx::validateTextureDesc(d) != gfx::TextureDescError::None) return {};
        lastDesc = d;
        return {uint32_t(++created)};
    }
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override {}
    void destroyTexture(gfx::TextureHandle) override { ++destroyed; }
    bool updateTextureRegion(gfx::TextureHandle, const gfx::TextureUploadDesc& u) override {
        ++mipUploads;
        check(u.mipLevel == 1, "only authored mip 1 uploaded explicitly");
        return !failUpload;
    }
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override { return {}; }
    void destroyShader(gfx::ShaderHandle) override {}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override { return {}; }
    void destroyPipeline(gfx::PipelineHandle) override {}
    void beginFrame(int,int) override {}
    void endFrame() override {}
    void beginPass(const gfx::PassDesc&) override {}
    void endPass() override {}
    void setViewport(const gfx::Rect&) override {}
    void setScissor(const gfx::Rect&) override {}
    void setPipeline(gfx::PipelineHandle) override {}
    void setVertexBuffer(gfx::BufferHandle,size_t) override {}
    void setIndexBuffer(gfx::BufferHandle,size_t) override {}
    void setTexture(int,gfx::TextureHandle) override {}
    void setUniforms(int,const void*,size_t) override {}
    void draw(uint32_t,uint32_t) override {}
    void drawIndexed(uint32_t,uint32_t,int32_t,uint32_t) override {}
};
}
int main() {
    try {
        TestDevice dev;
        std::string err;
        render::CompressedImage image;
        image.width=4; image.height=4; image.arrayLayers=2; image.faces=6;
        image.format=render::BlockFormat::None;
        image.levels.resize(2);
        image.levels[0].assign(size_t(4*4*4*12), 25);
        image.levels[1].assign(size_t(2*2*4*12), 42);
        gfx::TextureHandle h = render::uploadKtx2Image(dev, image, {}, err);
        check(h.valid() && err.empty(), "cube array upload");
        check(dev.lastDesc.dimension == gfx::TextureDimension::CubeArray &&
              dev.lastDesc.arrayLayers == 12 &&
              dev.lastDesc.mipLevels == 2, "cube array metadata to RHI");
        check(dev.mipUploads == 12, "each layer of second mip");
        dev.destroyTexture(h);
        image = {};
        image.width=2; image.height=2; image.depth=2;
        image.levels.resize(1); image.levels[0].assign(32, 30);
        h = render::uploadKtx2Image(dev,image,{},err);
        check(h.valid() && dev.lastDesc.dimension == gfx::TextureDimension::Texture3D,
              "3D upload");
        dev.destroyTexture(h);
        image = {};
        image.width=2;image.height=2;image.arrayLayers=2;
        image.levels.resize(2);
        image.levels[0].resize(32);
        image.levels[1].resize(8);
        dev.failUpload=1;
        h=render::uploadKtx2Image(dev,image,{},err);
        check(!h.valid() && dev.destroyed==3, "failed mip upload destroys texture");

        image = {};
        check(!render::uploadKtx2Image(dev,image,{},err).valid() && !err.empty(),
              "invalid texture rejected");

        // DDS DX10 2D array is stored surface-major, with the complete mip
        // chain of layer zero before layer one. The container reader must
        // transpose it before the generic GPU upload.
        auto put32 = [](std::string& bytes, size_t pos, uint32_t v) {
            for (int i = 0; i < 4; ++i)
                bytes[pos + size_t(i)] = char((v >> (8 * i)) & 255u);
        };
        std::string dds(148, '\0');
        dds.replace(0, 4, "DDS ");
        put32(dds, 4, 124); put32(dds, 8, 0x20000);
        put32(dds, 12, 2); put32(dds, 16, 2);
        put32(dds, 28, 2); put32(dds, 76, 32);
        put32(dds, 80, 4); put32(dds, 84, 0x30315844u); // DX10
        put32(dds, 128, 28); put32(dds, 132, 3);
        put32(dds, 140, 2); // DXGI RGBA8, TEXTURE2D, 2 layers
        dds.append(16, char(10)); dds.append(4, char(11));
        dds.append(16, char(20)); dds.append(4, char(21));
        check(render::parseDdsTexture(dds, image, err), "DDS array parsed");
        check(image.arrayLayers == 2 && image.levels[0].size() == 32 &&
              image.levels[1].size() == 8 &&
              image.levels[0][0] == 10 && image.levels[0][16] == 20 &&
              image.levels[1][0] == 11 && image.levels[1][4] == 21,
              "DDS surface-major data transposed");
        dev.failUpload = 0;
        const int beforeDds = dev.mipUploads;
        h = render::uploadDdsImage(dev, image, {}, err);
        check(h.valid() && err.empty(), "generic DDS GPU allocation");
        check(dev.lastDesc.dimension == gfx::TextureDimension::Texture2DArray &&
              dev.lastDesc.arrayLayers == 2 && dev.lastDesc.mipLevels == 2 &&
              dev.mipUploads == beforeDds + 2,
              "DDS two layers and per-layer second mip uploaded");
        dev.destroyTexture(h);
    } catch (const std::exception& e) {
        std::fprintf(stderr,"TextureAsset test FAILED: %s\n",e.what());
        return 1;
    }
    std::puts("TextureAsset KTX2 array/volume resource upload: OK");
    return 0;
}
