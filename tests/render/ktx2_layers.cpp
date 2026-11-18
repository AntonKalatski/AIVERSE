#include "engine/render/TextureCodec.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse::render;
namespace {
void check(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
void write32(std::string& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + size_t(i)] = char(v >> (8 * i));
}
void write64(std::string& b, size_t at, uint64_t v) {
    for (int i = 0; i < 8; ++i) b[at + size_t(i)] = char(v >> (8 * i));
}
std::string fixture(int width, int height, int depth, int layers, int faces, int images) {
    constexpr uint8_t magic[12] = {0xAB, 'K','T','X',' ','2','0',0xBB,0x0D,0x0A,0x1A,0x0A};
    const int bytesPerImage = width * std::max(1, height) * 4;
    const uint64_t payloadSize = uint64_t(bytesPerImage) * uint64_t(images);
    std::string out(size_t(104 + payloadSize), '\0');
    for (int i = 0; i < 12; ++i) out[size_t(i)] = char(magic[i]);
    write32(out, 12, 37); // R8G8B8A8_UNORM
    write32(out, 16, 1); // typeSize
    write32(out, 20, uint32_t(width));
    write32(out, 24, uint32_t(height));
    write32(out, 28, uint32_t(depth));
    write32(out, 32, uint32_t(layers));
    write32(out, 36, uint32_t(faces));
    write32(out, 40, 1); // level count
    write32(out, 44, 0); // no supercompression
    write64(out, 80, 104);
    write64(out, 88, payloadSize);
    write64(out, 96, payloadSize);
    for (int slice = 0; slice < images; ++slice)
        for (int pixel = 0; pixel < width * std::max(1,height); ++pixel) {
            size_t off = 104 + (size_t(slice) * size_t(width) * size_t(std::max(1,height)) +
                                size_t(pixel)) * 4;
            out[off] = char(slice & 255); out[off+1] = 13;
            out[off+2] = 27; out[off+3] = char(255);
        }
    return out;
}
}
int main() {
    try {
        CompressedImage img;
        std::string err;
        std::vector<uint8_t> rgba;
        std::string cube = fixture(2,2,0,2,6,12);
        check(parseKtx2(cube,img,err), "cube array parse");
        check(img.arrayLayers == 2 && img.faces == 6 &&
              img.depth == 1 && img.levels.size() == 1 &&
              img.levels[0].size() == 192, "cube metadata and full payload");
        check(decodeBlocksSlice(img,0,7,rgba) && rgba.size() == 16 &&
              rgba[0] == 7 && rgba[1] == 13, "cube array face 7");
        check(decodeBlocks(img,0,rgba) && rgba[0] == 0, "legacy first slice");
        check(!decodeBlocksSlice(img,0,12,rgba), "array bounds");
        std::string volume = fixture(2,2,3,0,1,3);
        check(parseKtx2(volume,img,err), "3D volume parse");
        check(img.depth == 3 && levelImageCount(img,0) == 3 &&
              img.levels[0].size() == 48, "volume payload");
        check(decodeBlocksSlice(img,0,2,rgba) && rgba[0] == 2, "3D z slice");
        std::string singleSliceVolume = fixture(2,2,1,0,1,1);
        check(parseKtx2(singleSliceVolume,img,err) &&
              img.threeDimensional && img.depth == 1 && img.faces == 1,
              "KTX2 one-slice volume retains explicit Texture3D topology");
        check(!parseKtx2(fixture(2,2,1,0,6,6),img,err),
              "KTX2 one-slice 3D cube combination rejected");
        std::string bc5 = fixture(4,4,0,1,1,1);
        bc5.resize(104 + 16);
        write32(bc5, 12, 141); // VK_FORMAT_BC5_UNORM_BLOCK
        write64(bc5, 88, 16);
        write64(bc5, 96, 16);
        std::fill(bc5.begin() + 104, bc5.end(), char(0));
        bc5[104] = char(255);
        bc5[112] = char(128);
        check(parseKtx2(bc5,img,err) &&
              img.format == BlockFormat::BC5 && img.levels[0].size() == 16,
              "BC5 KTX2 format and complete mip payload");
        check(decodeBlocksSlice(img,0,0,rgba) && rgba.size() == 64 &&
              rgba[0] == 255 && rgba[1] == 128 && rgba[3] == 255,
              "BC5 KTX2 CPU fallback");
        std::string oneD = fixture(2,0,0,0,1,1);
        check(parseKtx2(oneD,img,err) && img.oneDimensional && img.height == 1,
              "1D KTX2 preserved");
        cube.pop_back();
        check(!parseKtx2(cube,img,err), "truncated array payload");
        cube = fixture(2,1,0,1,6,6);
        check(!parseKtx2(cube,img,err), "non-square cube invalid");
    } catch (const std::exception& e) {
        std::fprintf(stderr,"KTX2 layer test FAILED: %s\n",e.what());
        return 1;
    }
    std::puts("KTX2 array, cubemap and 3D payload: OK");
    return 0;
}
