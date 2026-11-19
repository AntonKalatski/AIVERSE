// AIVERSE virtual-texture RGBA8 page archive writer (runtime/offline).
// Creates the SAME m<MIP>/l<LAYER>/x<X>_y<Y>.rgba layout as the Python
// tools/virtual_texture_pack.py and gfx::VirtualTextureDiskQueue.
// Genre independent. No GPU calls. Call on a job worker with immutable
// source images, then initialize the disk streamer on the render thread.
// Archives are versioned and atomically published: never mutate an archive
// while another worker/streamer is reading it.
#pragma once

#include "VirtualTextureFileStreamer.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace aiverse::gfx {

struct VirtualTextureArchiveLayer {
    int width = 0;
    int height = 0;
    std::span<const uint8_t> rgba; // top-left rows, tightly packed RGBA8
};
struct VirtualTextureArchivePackConfig {
    int pageSize = 128;  // including gutters
    int gutter = 2;
    size_t maxSourceBytes = size_t(64) << 20;
    size_t maxArchiveBytes = size_t(512) << 20;
    size_t maxPages = 262144;
};
struct VirtualTextureArchiveVersion {
    std::filesystem::path directory;
    uint64_t revision = 0;
    uint64_t contentHash = 0;
    int width = 0, height = 0;
    int baseTilesX = 0, baseTilesY = 0;
    int mipLevels = 0, virtualLayers = 0;
    int pageSize = 0, gutter = 0;
    size_t pageCount = 0, archiveBytes = 0;
    bool valid() const {
        return !directory.empty() && width > 0 && height > 0 &&
               baseTilesX > 0 && baseTilesY > 0 && mipLevels > 0 &&
               virtualLayers > 0 && pageSize > 0;
    }
    VirtualDiskPageConfig diskConfig(size_t outstanding = 128,
                                     size_t readyBytes = size_t(16)<<20) const {
        VirtualDiskPageConfig result;
        result.assetRoot = directory;
        result.tilesX = baseTilesX;
        result.tilesY = baseTilesY;
        result.mipLevels = mipLevels;
        result.virtualLayers = virtualLayers;
        result.pageSize = pageSize;
        result.maxOutstanding = outstanding;
        result.maxReadyBytes = readyBytes;
        return result;
    }
    VirtualTextureStreamConfig streamConfig(int physicalPages = 128) const {
        VirtualTextureStreamConfig result;
        result.tilesX = baseTilesX;
        result.tilesY = baseTilesY;
        result.mipLevels = mipLevels;
        result.virtualLayers = virtualLayers;
        result.physicalPageSize = pageSize;
        result.physicalPages = physicalPages;
        result.physicalFormat = PixelFormat::RGBA8;
        return result;
    }
};

class VirtualTexturePageArchiveWriter {
public:
    // Write into versionsRoot/v<revision>-<hash> and return its metadata.
    // If exactly the same immutable revision was already packed, return it
    // without rewriting. A revision with a different hash gets a new path.
    // Failures leave the previous versions untouched and delete staging.
    static bool pack(const std::filesystem::path& versionsRoot,
                     uint64_t revision,
                     std::span<const VirtualTextureArchiveLayer> layers,
                     const VirtualTextureArchivePackConfig& cfg,
                     VirtualTextureArchiveVersion& out,
                     std::string& error) {
        out = {};
        error.clear();
        try {
            if (versionsRoot.empty() || layers.empty() || layers.size() > 16 ||
                cfg.pageSize < 4 || cfg.pageSize > 4096 ||
                cfg.gutter < 0 || cfg.gutter*2 >= cfg.pageSize ||
                cfg.maxPages == 0 || cfg.maxArchiveBytes == 0 ||
                cfg.maxSourceBytes == 0)
                return fail(error,"invalid VT archive layout, gutter or budget");
            const auto width = layers.front().width;
            const auto height = layers.front().height;
            if (width < 1 || height < 1 || width > (1 << 20) ||
                height > (1 << 20) || width > 2048*(cfg.pageSize-2*cfg.gutter) ||
                height > 2048*(cfg.pageSize-2*cfg.gutter))
                return fail(error,"VT source dimensions outside page codec limits");
            const size_t pixels = size_t(width)*size_t(height);
            if (pixels > cfg.maxSourceBytes/4u/layers.size())
                return fail(error,"VT source exceeds cumulative source byte budget");
            const size_t bytes = pixels*4u;
            uint64_t hash = uint64_t(14695981039346656037ULL);
            hashWord(hash,uint64_t(width));
            hashWord(hash,uint64_t(height));
            hashWord(hash,uint64_t(cfg.pageSize));
            hashWord(hash,uint64_t(cfg.gutter));
            for (const auto& layer : layers) {
                if (layer.width != width || layer.height != height ||
                    layer.rgba.size() != bytes)
                    return fail(error,"VT layer dimensions or RGBA8 byte count mismatch");
                for (const uint8_t byte : layer.rgba)
                    hash = (hash ^ uint64_t(byte))*1099511628211ULL;
            }
            const int interior = cfg.pageSize-2*cfg.gutter;
            const int tilesX = (width+interior-1)/interior;
            const int tilesY = (height+interior-1)/interior;
            int levels = 1;
            for(int n=std::max(tilesX,tilesY); n>1; n=(n+1)/2)
                ++levels;
            if (levels>30) return fail(error,"VT mip count exceeds feedback codec");
            size_t pages=0;
            int mw=width,mh=height;
            for(int mip=0;mip<levels;++mip) {
                const size_t count=size_t((mw+interior-1)/interior) *
                                   size_t((mh+interior-1)/interior);
                if (count > cfg.maxPages/layers.size() ||
                    pages > cfg.maxPages - count*layers.size())
                    return fail(error,"VT archive exceeds page budget");
                pages += count*layers.size();
                mw=std::max(1,(mw+1)/2);
                mh=std::max(1,(mh+1)/2);
            }
            const size_t pageBytes=size_t(cfg.pageSize)*size_t(cfg.pageSize)*4u;
            if (pages>cfg.maxArchiveBytes/pageBytes)
                return fail(error,"VT archive exceeds on-disk byte budget");

            VirtualTextureArchiveVersion value;
            value.revision=revision;
            value.contentHash=hash;
            value.width=width; value.height=height;
            value.baseTilesX=tilesX; value.baseTilesY=tilesY;
            value.mipLevels=levels; value.virtualLayers=int(layers.size());
            value.pageSize=cfg.pageSize; value.gutter=cfg.gutter;
            value.pageCount=pages; value.archiveBytes=pages*pageBytes;
            const auto label=std::string("v")+std::to_string(revision)+
                             "-"+hex(hash);
            value.directory=versionsRoot/label;
            const std::string manifest=makeManifest(value,interior);

            std::error_code ec;
            if (std::filesystem::exists(value.directory,ec)) {
                std::ifstream previous(value.directory/"manifest.json",std::ios::binary);
                std::string prior((std::istreambuf_iterator<char>(previous)),
                                  std::istreambuf_iterator<char>());
                if (previous.bad() || prior!=manifest)
                    return fail(error,"existing VT revision has incompatible manifest");
                out=std::move(value);
                return true;
            }
            std::filesystem::create_directories(versionsRoot);
            std::filesystem::path staging;
            for(int n=0;n<1024;++n) {
                auto candidate=versionsRoot/("."+label+".partial."+
                                              std::to_string(n));
                if(std::filesystem::create_directory(candidate,ec)) {
                    staging=std::move(candidate); break;
                }
                if(ec && ec!=std::errc::file_exists)
                    return fail(error,"cannot allocate VT staging directory");
                ec.clear();
            }
            if(staging.empty())
                return fail(error,"no free VT staging directory");
            struct Cleanup {
                std::filesystem::path dir;
                ~Cleanup() {
                    if(!dir.empty()) {
                        std::error_code ignore;
                        std::filesystem::remove_all(dir,ignore);
                    }
                }
            } cleanup{staging};

            for(size_t layerIndex=0;layerIndex<layers.size();++layerIndex) {
                std::vector<uint8_t> mipImage(layers[layerIndex].rgba.begin(),
                                              layers[layerIndex].rgba.end());
                int w=width,h=height;
                for(int mip=0;mip<levels;++mip) {
                    auto folder=staging/("m"+std::to_string(mip))/
                                        ("l"+std::to_string(layerIndex));
                    std::filesystem::create_directories(folder);
                    const int cx=(w+interior-1)/interior;
                    const int cy=(h+interior-1)/interior;
                    std::vector<uint8_t> tile(pageBytes);
                    for(int y=0;y<cy;++y)
                        for(int x=0;x<cx;++x) {
                            for(int iy=0;iy<cfg.pageSize;++iy) {
                                const int sy=std::clamp(y*interior+iy-cfg.gutter,0,h-1);
                                for(int ix=0;ix<cfg.pageSize;++ix) {
                                    const int sx=std::clamp(x*interior+ix-cfg.gutter,0,w-1);
                                    const size_t source=(size_t(sy)*size_t(w)+size_t(sx))*4u;
                                    const size_t dest=(size_t(iy)*size_t(cfg.pageSize)+
                                                       size_t(ix))*4u;
                                    std::copy_n(mipImage.data()+source,4,tile.data()+dest);
                                }
                            }
                            writeBytes(folder/("x"+std::to_string(x)+"_y"+
                                               std::to_string(y)+".rgba"),
                                       tile.data(),tile.size());
                        }
                    if(mip+1<levels) {
                        const int nw=std::max(1,(w+1)/2);
                        const int nh=std::max(1,(h+1)/2);
                        std::vector<uint8_t> smaller(size_t(nw)*size_t(nh)*4u);
                        for(int y=0;y<nh;++y)
                            for(int x=0;x<nw;++x) {
                                unsigned total[4]={0,0,0,0};
                                for(int oy=0;oy<2;++oy)
                                    for(int ox=0;ox<2;++ox) {
                                        const int sy=std::min(h-1,y*2+oy);
                                        const int sx=std::min(w-1,x*2+ox);
                                        const size_t k=(size_t(sy)*size_t(w)+size_t(sx))*4u;
                                        for(int c=0;c<4;++c) total[c]+=mipImage[k+size_t(c)];
                                    }
                                const size_t k=(size_t(y)*size_t(nw)+size_t(x))*4u;
                                for(int c=0;c<4;++c)
                                    smaller[k+size_t(c)]=uint8_t((total[c]+2u)/4u);
                            }
                        mipImage.swap(smaller);
                        w=nw;h=nh;
                    }
                }
            }
            writeBytes(staging/"manifest.json",
                       reinterpret_cast<const uint8_t*>(manifest.data()),
                       manifest.size());
            // Both paths belong to the same versionsRoot filesystem.
            // Publish ALL pages and the manifest in one directory rename.
            std::filesystem::rename(staging,value.directory);
            cleanup.dir.clear();
            out=std::move(value);
            return true;
        } catch(const std::exception& e) {
            return fail(error,std::string("VT archive pack failure: ")+e.what());
        }
    }

private:
    static bool fail(std::string& dst,const std::string& message) {
        dst=message;
        return false;
    }
    static void hashWord(uint64_t& hash,uint64_t v) {
        for(int i=0;i<8;++i) {
            hash=(hash^((v>>(8*i))&255u))*1099511628211ULL;
        }
    }
    static std::string hex(uint64_t value) {
        std::ostringstream output;
        output<<std::hex<<std::setfill('0')<<std::setw(16)<<value;
        return output.str();
    }
    static std::string makeManifest(const VirtualTextureArchiveVersion& a,
                                    int interior) {
        std::ostringstream ss;
        ss<<"{\n  \"schema\": \"aiverse.vt.disk-pages.v1\",\n"
          <<"  \"format\": \"RGBA8\",\n"
          <<"  \"revision\": "<<a.revision<<",\n"
          <<"  \"contentHashFNV64\": \""<<hex(a.contentHash)<<"\",\n"
          <<"  \"width\": "<<a.width<<", \"height\": "<<a.height<<",\n"
          <<"  \"tilesX\": "<<a.baseTilesX<<", \"tilesY\": "<<a.baseTilesY<<",\n"
          <<"  \"mipLevels\": "<<a.mipLevels
          <<", \"virtualLayers\": "<<a.virtualLayers<<",\n"
          <<"  \"physicalPageSize\": "<<a.pageSize<<", \"gutter\": "<<a.gutter<<",\n"
          <<"  \"interiorPageSize\": "<<interior<<",\n"
          <<"  \"pageCount\": "<<a.pageCount<<",\n"
          <<"  \"archiveBytes\": "<<a.archiveBytes<<"\n}\n";
        return ss.str();
    }
    static void writeBytes(const std::filesystem::path& file,
                           const uint8_t* data,size_t bytes) {
        std::ofstream out(file,std::ios::binary|std::ios::trunc);
        if(!out) throw std::runtime_error("cannot open VT page archive output");
        out.write(reinterpret_cast<const char*>(data),std::streamsize(bytes));
        out.flush();
        if(!out) throw std::runtime_error("cannot write VT page archive output");
    }
};

} // namespace aiverse::gfx
