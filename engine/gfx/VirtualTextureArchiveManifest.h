// Safe, optional import of immutable page archives produced by
// tools/virtual_texture_pack.py or VirtualTexturePageArchiveWriter.
// Filesystem validation is CPU-only. Never mutate a live streamer's archive.
// JSON is parsed by the engine's existing data layer, not a second parser.
#pragma once
#include "VirtualTexturePageArchiveWriter.h"
#include "../data/Json.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <system_error>

namespace aiverse::gfx {
namespace archive_manifest_detail {
inline bool unsignedValue(const Json& j, uint64_t& out) {
    if (!j.isNumber()) return false;
    const std::string& s = j.numberLiteral();
    if (s.empty()) return false;
    uint64_t result = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return false; // no negative, fraction or exponent
        const uint64_t digit = uint64_t(c - '0');
        if (result > (std::numeric_limits<uint64_t>::max() - digit)/10u)
            return false;
        result = result*10u + digit;
    }
    out = result;
    return true;
}
inline bool intField(const Json& j, const char* key, int min, int max, int& value) {
    uint64_t n = 0;
    if (!unsignedValue(j[key], n) || n < uint64_t(min) ||
        n > uint64_t(max)) return false;
    value = int(n);
    return true;
}
inline bool sizeField(const Json& j, const char* key, size_t& value) {
    uint64_t n = 0;
    if (!unsignedValue(j[key], n) ||
        n > uint64_t(std::numeric_limits<size_t>::max())) return false;
    value = size_t(n);
    return true;
}
inline bool addressField(const Json& j, const char* key,
                         SamplerAddressMode& out) {
    if (!j.has(key)) { out = SamplerAddressMode::ClampToEdge; return true; }
    if (!j[key].isString()) return false;
    const auto text = j[key].str();
    if (text == "clamp") out = SamplerAddressMode::ClampToEdge;
    else if (text == "repeat") out = SamplerAddressMode::Repeat;
    else if (text == "mirror") out = SamplerAddressMode::MirrorRepeat;
    else return false; // border cannot be sampled by this page-table shader
    return true;
}
inline bool hashField(const Json& j, uint64_t& value) {
    if (!j.has("contentHashFNV64")) { value = 0; return true; }
    if (!j["contentHashFNV64"].isString()) return false;
    const auto s = j["contentHashFNV64"].str();
    if (s.size() != 16) return false;
    value = 0;
    for (const char c : s) {
        const int digit = c >= '0' && c <= '9' ? c - '0' :
                          c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                          c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0) return false;
        value = (value << 4u) | uint64_t(digit);
    }
    return true;
}
inline bool sha256List(const Json& j, int layers) {
    if (!j.has("sourceSha256")) return true; // native writer uses FNV metadata
    const auto& a = j["sourceSha256"];
    if (!a.isArray() || a.size() != size_t(layers)) return false;
    for (const auto& item : a.items()) {
        if (!item.isString() || item.str().size() != 64) return false;
        for (char c : item.str())
            if (!((c >= '0' && c <= '9') ||
                  (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}
} // namespace archive_manifest_detail

// On failure: out is empty, error is nonempty, no existing GPU session is
// changed. 'directory' is caller-selected; paths from JSON are never used.
// This verifies required page files and their exact sizes (not cryptographic
// authenticity). Use only trusted immutable archive roots; do not race writes.
// maxArchiveBytes limits I/O and guards arithmetic before filesystem access.
inline bool importVirtualTextureArchiveManifest(
    const Json& manifest, const std::filesystem::path& directory,
    VirtualTextureArchiveVersion& out, std::string& error,
    size_t maxArchiveBytes = size_t(512) << 20,
    size_t maxPages = 262144, uint64_t importedRevision = 0) {

    out = {};
    error.clear();
    auto fail = [&](const char* message) { error = message; return false; };
    using namespace archive_manifest_detail;
    try {
        if (!manifest.isObject() || directory.empty() ||
            manifest.getStr("schema") != "aiverse.vt.disk-pages.v1" ||
            manifest.getStr("format") != "RGBA8")
            return fail("unsupported VT archive schema, format or directory");
        // Reject ambiguous duplicate fields, even if the JSON parser accepts them.
        std::set<std::string> keys;
        for (const auto& [key, ignored] : manifest.members()) {
            (void)ignored;
            if (!keys.insert(key).second) return fail("duplicate VT manifest key");
        }
        VirtualTextureArchiveVersion next;
        if (!intField(manifest,"width",1,1<<20,next.width) ||
            !intField(manifest,"height",1,1<<20,next.height) ||
            !intField(manifest,"tilesX",1,2048,next.baseTilesX) ||
            !intField(manifest,"tilesY",1,2048,next.baseTilesY) ||
            !intField(manifest,"mipLevels",1,30,next.mipLevels) ||
            !intField(manifest,"virtualLayers",1,16,next.virtualLayers) ||
            !intField(manifest,"physicalPageSize",4,4096,next.pageSize) ||
            !intField(manifest,"gutter",0,2047,next.gutter) ||
            next.gutter*2 >= next.pageSize ||
            !addressField(manifest,"addressU",next.addressU) ||
            !addressField(manifest,"addressV",next.addressV) ||
            !hashField(manifest,next.contentHash) ||
            !sha256List(manifest,next.virtualLayers))
            return fail("invalid VT archive shape, addressing or integrity metadata");

        const int interior = next.pageSize-2*next.gutter;
        const int expectedX = 1+(next.width-1)/interior;
        const int expectedY = 1+(next.height-1)/interior;
        if (expectedX != next.baseTilesX || expectedY != next.baseTilesY)
            return fail("manifest page grid mismatches source dimensions");
        if (manifest.has("interiorPageSize")) {
            int specified=0;
            if (!intField(manifest,"interiorPageSize",1,4096,specified) ||
                specified != interior)
                return fail("manifest interior tile size mismatch");
        }

        const size_t bytesPerPage = size_t(next.pageSize)*size_t(next.pageSize)*4u;
        if (maxPages == 0 || maxArchiveBytes < bytesPerPage)
            return fail("VT archive limits too small");
        int w=next.width, h=next.height, mip=0;
        size_t totalPages=0;
        for (; mip < next.mipLevels; ++mip) {
            const int cx=1+(w-1)/interior, cy=1+(h-1)/interior;
            const size_t perMip=size_t(cx)*size_t(cy)*size_t(next.virtualLayers);
            if (perMip > maxPages-totalPages)
                return fail("VT archive exceeds safe page count");
            totalPages+=perMip;
            w=std::max(1,(w+1)/2);
            h=std::max(1,(h+1)/2);
        }
        // The page-table decoder expects every mip down to one tile.
        const int lastW=1+(w-1)/interior, lastH=1+(h-1)/interior;
        const int beforeW=std::max(1,(next.width+(1<<(next.mipLevels-1))-1) /
                                   (1<<(next.mipLevels-1)));
        (void)beforeW; // Mip layout is checked below by the exact iterator.
        if (lastW > 1 || lastH > 1)
            return fail("VT archive lacks coarsest fallback mip");
        // Extra authored mips with identical single-page resolutions are invalid.
        int expectedMips=1;
        for (int n=std::max(next.baseTilesX,next.baseTilesY);n>1;n=(n+1)/2)
            ++expectedMips;
        if (expectedMips != next.mipLevels)
            return fail("invalid number of VT mip levels");
        if (totalPages > maxArchiveBytes/bytesPerPage)
            return fail("VT archive exceeds byte budget");
        next.pageCount=totalPages;
        next.archiveBytes=totalPages*bytesPerPage;

        if (manifest.has("pageCount")) {
            size_t count=0;
            if (!sizeField(manifest,"pageCount",count) || count != totalPages)
                return fail("VT manifest page count mismatch");
        }
        if (manifest.has("archiveBytes")) {
            size_t count=0;
            if (!sizeField(manifest,"archiveBytes",count) ||
                count != next.archiveBytes)
                return fail("VT manifest byte count mismatch");
        }
        if (manifest.has("revision")) {
            uint64_t revision=0;
            if (!unsignedValue(manifest["revision"],revision))
                return fail("invalid VT archive revision");
            if (importedRevision != 0 && importedRevision != revision)
                return fail("VT archive revision mismatch");
            next.revision=revision;
        } else next.revision=importedRevision;

        std::error_code ec;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(directory,ec)) ||
            ec || !std::filesystem::is_directory(directory))
            return fail("VT archive is not a regular directory");
        const auto manifestPath = directory/"manifest.json";
        const auto manifestStatus=std::filesystem::symlink_status(manifestPath,ec);
        if (ec || !std::filesystem::is_regular_file(manifestStatus))
            return fail("VT archive manifest file missing or symbolic link");

        w=next.width;h=next.height;
        for (mip=0; mip<next.mipLevels; ++mip) {
            const int cx=1+(w-1)/interior, cy=1+(h-1)/interior;
            for (int layer=0;layer<next.virtualLayers;++layer)
                for (int y=0;y<cy;++y)
                    for (int x=0;x<cx;++x) {
                        const auto file=directory/("m"+std::to_string(mip))/
                            ("l"+std::to_string(layer))/
                            ("x"+std::to_string(x)+"_y"+std::to_string(y)+".rgba");
                        ec.clear();
                        const auto status=std::filesystem::symlink_status(file,ec);
                        if (ec || !std::filesystem::is_regular_file(status))
                            return fail("missing or nonregular VT archive page");
                        const auto length=std::filesystem::file_size(file,ec);
                        if (ec || length != bytesPerPage)
                            return fail("VT archive page length mismatch");
                    }
            w=std::max(1,(w+1)/2);
            h=std::max(1,(h+1)/2);
        }
        next.directory=directory;
        out=std::move(next);
        return true;
    } catch (const std::exception& e) {
        error=std::string("VT archive import failed: ")+e.what();
        out={};
        return false;
    }
}

// Convenience for offline import. Native callers may instead parse the JSON
// elsewhere and pass a Json object above. Not safe for concurrently edited files.
inline bool loadVirtualTextureArchiveManifest(
    const std::filesystem::path& directory,
    VirtualTextureArchiveVersion& out, std::string& error,
    size_t maxArchiveBytes = size_t(512) << 20,
    size_t maxPages = 262144, uint64_t importedRevision = 0) {
    out={};
    error.clear();
    try {
        std::error_code ec;
        const auto manifestPath=directory/"manifest.json";
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(directory,ec)) ||
            ec || !std::filesystem::is_directory(directory) ||
            std::filesystem::is_symlink(std::filesystem::symlink_status(manifestPath,ec)) ||
            ec || !std::filesystem::is_regular_file(manifestPath) ||
            std::filesystem::file_size(manifestPath,ec) > 65536 || ec) {
            error="invalid VT manifest file or path";
            return false;
        }
        const auto json=Json::parseFile(manifestPath.string());
        return importVirtualTextureArchiveManifest(json,directory,out,error,
                                                   maxArchiveBytes,maxPages,
                                                   importedRevision);
    } catch (const std::exception& e) {
        error=std::string("VT manifest parse failed: ")+e.what();
        out={};
        return false;
    }
}
} // namespace aiverse::gfx
