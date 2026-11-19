// Safe, genre-independent bridge from a published immutable VT archive to
// the model renderer's opt-in VT material binding. Never guess archive/atlas
// dimensions or indexed page-table ABI from raw texture handles.
#pragma once
#include "ModelVirtualTextureSampling.h"
#include "../gfx/VirtualTexturePageArchiveWriter.h"
#include "../gfx/VirtualTextureFileStreamer.h"
#include <cmath>

namespace aiverse::render {

inline bool makeModelVirtualTextureArchiveBinding(
    const gfx::VirtualTextureArchiveVersion& archive,
    const gfx::VirtualTextureStreamer& streamer,
    int logicalLayer, ModelVirtualTextureBinding& out,
    float mipBias=0.0f) {
    out={};
    if(!archive.valid() || !streamer.ready() ||
       !std::isfinite(mipBias) ||
       logicalLayer<0 || logicalLayer>=archive.virtualLayers ||
       archive.gutter<0 || int64_t(archive.gutter)*2>=archive.pageSize)
        return false;
    const auto& cfg=streamer.configuration();
    if(cfg.tilesX!=archive.baseTilesX || cfg.tilesY!=archive.baseTilesY ||
       cfg.mipLevels!=archive.mipLevels ||
       cfg.virtualLayers!=archive.virtualLayers ||
       cfg.physicalPageSize!=archive.pageSize)
        return false;

    ModelVirtualTextureBinding candidate;
    candidate.physicalAtlas=streamer.physicalAtlas();
    candidate.pageTable=cfg.indexedMipTable ?
        streamer.indexedMipPageTableTexture() : streamer.pageTableTexture();
    candidate.indexedMipTable=cfg.indexedMipTable;
    candidate.layer=logicalLayer;
    candidate.gutterPixels=float(archive.gutter);
    candidate.addressU=archive.addressU;
    candidate.addressV=archive.addressV;
    if(cfg.indexedMipTable) {
        candidate.virtualMipLevels=streamer.indexedMipTableLevels();
        candidate.virtualWidthPixels=float(archive.width);
        candidate.virtualHeightPixels=float(archive.height);
        candidate.virtualMipBias=mipBias;
    }
    if(!candidate.valid()) return false;
    out=candidate;
    return true;
}

inline bool makeModelVirtualTextureArchiveBinding(
    const gfx::VirtualTextureArchiveVersion& archive,
    const gfx::VirtualTextureFileStreamer& streamer,
    int logicalLayer, ModelVirtualTextureBinding& out,
    float mipBias=0.0f) {
    out={};
    return streamer.ready() &&
        makeModelVirtualTextureArchiveBinding(
            archive,streamer.atlasStreamer(),logicalLayer,out,mipBias);
}

} // namespace aiverse::render
