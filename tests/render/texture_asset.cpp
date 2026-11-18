#include "engine/render/TextureAsset.h"
#include "engine/render/DdsTexture.h"
#include <cstdio>
#include <map>
#include <stdexcept>
#include <vector>
#include <cmath>

using namespace aiverse;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
class TestDevice final : public gfx::Device {
public:
    gfx::DeviceInfo info_;
    std::string error_;
    gfx::TextureDesc lastDesc;
    std::vector<uint8_t> firstPixels;
    int created=0, destroyed=0, mipUploads=0, failUpload=0;
    bool failNativeCompressed = false;
    int nativeAttempts = 0;
    const gfx::DeviceInfo& info() const override { return info_; }
    const std::string& lastError() const override { return error_; }
    bool supports(gfx::PixelFormat f) const override {
        return f == gfx::PixelFormat::BC7 || f == gfx::PixelFormat::BC4 ||
               f == gfx::PixelFormat::BC5 || f == gfx::PixelFormat::EAC_R11;
    }
    gfx::BufferHandle createBuffer(const gfx::BufferDesc&) override { return {}; }
    void updateBuffer(gfx::BufferHandle,const void*,size_t,size_t) override {}
    void destroyBuffer(gfx::BufferHandle) override {}
    gfx::TextureHandle createTexture(const gfx::TextureDesc& d) override {
        if (gfx::validateTextureDesc(d) != gfx::TextureDescError::None) return {};
        if (gfx::isCompressedTextureFormat(d.format)) {
            ++nativeAttempts;
            if (failNativeCompressed) {
                error_ = "mock device rejected compressed allocation";
                return {};
            }
        }
        lastDesc = d;
        firstPixels.clear();
        if (d.data && d.format == gfx::PixelFormat::RGBA8 &&
            d.width > 0 && d.height > 0) {
            const auto* p = static_cast<const uint8_t*>(d.data);
            firstPixels.assign(p, p + size_t(d.width)*size_t(d.height)*4);
        }
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

        // BC5 RGB normal maps require positive Z reconstruction. Unless
        // explicitly opted in, a capable GPU must still receive RGBA8 CPU
        // pixels because native RGTC2 sampling does not supply blue Z.
        image = {};
        image.width = image.height = 4;
        image.format = render::BlockFormat::BC5;
        image.vkFormat = 141; // VK_FORMAT_BC5_UNORM_BLOCK
        image.levels.resize(1);
        image.levels[0].assign(16, 0);
        image.levels[0][0] = uint8_t(255); // R endpoint
        image.levels[0][8] = uint8_t(128); // G endpoint
        std::vector<uint8_t> cpuNormal;
        check(render::decodeBlocksSlice(image, 0, 0, cpuNormal) &&
              cpuNormal.size() == 64 && cpuNormal[0] == 255 &&
              cpuNormal[1] == 128 &&
              std::abs(int(cpuNormal[2]) - 128) <= 1 &&
              cpuNormal[3] == 255, "BC5 CPU normal Z reconstruction");
        h = render::uploadContainerTexture(dev, image, {}, err);
        check(h.valid() && dev.lastDesc.format == gfx::PixelFormat::RGBA8 &&
              dev.firstPixels.size() == 64 &&
              dev.firstPixels[0] == 255 && dev.firstPixels[1] == 128 &&
              std::abs(int(dev.firstPixels[2]) - 128) <= 1,
              "BC5 defaults to shader-compatible CPU RGBA");
        dev.destroyTexture(h);
        render::TextureUploadOptions bc5Options;
        bc5Options.shaderReconstructsBc5NormalZ = true;
        h = render::uploadContainerTexture(dev, image, bc5Options, err);
        check(h.valid() && dev.lastDesc.format == gfx::PixelFormat::BC5 &&
              dev.lastDesc.levelCount == 1,
              "BC5 native RGTC2 requires shader opt-in");
        dev.destroyTexture(h);

        // A backend can report a compressed format yet reject this allocation.
        // The RGBA fallback must NOT forward stale compressed levelData/size,
        // and it must allocate the authored mip chain for explicit uploads.
        image = {};
        image.width = image.height = 4;
        image.format = render::BlockFormat::BC5;
        image.vkFormat = 141;
        image.levels.resize(2);
        image.levels[0].assign(16, 0);
        image.levels[1].assign(16, 0);
        render::TextureUploadOptions rejectedNativeOptions;
        rejectedNativeOptions.shaderReconstructsBc5NormalZ = true;
        dev.failNativeCompressed = true;
        const int nativeAttemptsBefore = dev.nativeAttempts;
        const int mipUploadsBefore = dev.mipUploads;
        h = render::uploadContainerTexture(dev, image, rejectedNativeOptions, err);
        check(h.valid() && err.empty(), "failed native compressed allocation falls back to RGBA8");
        check(dev.nativeAttempts == nativeAttemptsBefore + 1,
              "native compressed allocation was attempted");
        check(dev.lastDesc.format == gfx::PixelFormat::RGBA8 &&
              dev.lastDesc.mipLevels == 2 && dev.lastDesc.levelCount == 0 &&
              dev.lastDesc.levelData == nullptr && dev.lastDesc.levelSize == nullptr,
              "CPU fallback resets compressed payload and preserves authored mips");
        check(dev.firstPixels.size() == 4u*4u*4u &&
              dev.mipUploads == mipUploadsBefore + 1,
              "CPU fallback initializes base level and uploads authored next mip");
        dev.destroyTexture(h);
        dev.failNativeCompressed = false;

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
