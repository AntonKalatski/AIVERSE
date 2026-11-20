// Dimension-aware DirectDraw Surface container reader.
// Parse as CompressedImage (mip-major, all array/cube/3D slices retained).
// Old model-material decodeDds is intentionally a separate first-face path.
#pragma once
#include "TextureCodec.h"
#include <string>

namespace aiverse::render {
// Supports DX10/legacy DDS arrays, cubemaps and volumes for BC1/2/3/4/5/7
// and common 8/16/24/32-bit masked RGB/BGRA/RGBA formats.
// BC2/BC5 and masked inputs are decoded to RGBA8 per 2D slice.
// Unknown signed/float DXGI formats, incomplete cubemaps and malformed
// payloads fail with a readable error. Input is tightly packed per mip.
bool parseDdsTexture(const std::string& bytes, CompressedImage& out, std::string& error);
} // namespace aiverse::render
