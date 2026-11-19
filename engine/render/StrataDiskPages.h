#pragma once
// STRATA Stage 15.3: independently stored, checksum-verified index pages.
// Offline writer creates a deterministic .strataidx archive. Runtime keeps
// a bounded worker queue; no file reads occur in per-frame demand gathering.
// Does NOT own or modify ModelAsset, gfx::Device, ECS or Renderer3D.
#include "StrataModelAdapter.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace aiverse::render::strata {

struct DiskPageStats {
    uint64_t completedReads=0, failedReads=0, bytesRead=0;
    uint64_t requests=0, queueRefused=0, queued=0, readyBytes=0;
};

// One archive contains exactly one immutable ModelAsset source. Metadata and
// source signature are validated at open; every page is CRC-checked on read.
// Maximum queued requests and cached result bytes are bounded. Threading is
// intentionally disabled for Emscripten/WebGL2 (fallback to CPU indices).
class DiskIndexPages final {
public:
    DiskIndexPages();
    ~DiskIndexPages();
    DiskIndexPages(const DiskIndexPages&)=delete;
    DiskIndexPages& operator=(const DiskIndexPages&)=delete;

    // Offline/importer, synchronous, atomic temp-file rename.
    static bool writeArchive(const std::string& path, const ModelAsset& asset,
                             std::string* error=nullptr);
    // Runtime. Fails on missing/stale/corrupt manifest or unsupported target.
    bool open(const std::string& path, const ModelAsset& asset,
              size_t readyBudgetBytes=4u*1024u*1024u,
              size_t maxRequests=512, std::string* error=nullptr);
    // Pinned root pages are synchronously read ONCE at scene load. Not a
    // render-frame API; caller must not invoke this in an active encoder.
    bool readRoot(uint32_t primitive, uint32_t node,
                  std::vector<uint32_t>& result, std::string* error=nullptr);
    // Nonblocking frame-thread APIs. request() schedules IO, ready() copies
    // verified bytes if available, consume() frees cache after GPU publication.
    bool request(uint32_t primitive, uint32_t node);
    bool ready(uint32_t primitive, uint32_t node,
               std::vector<uint32_t>& out) const;
    void consume(uint32_t primitive, uint32_t node);
    bool failed(uint32_t primitive, uint32_t node) const;
    DiskPageStats stats() const;
    bool valid() const;
    void close(); // cancels queue and joins worker; call before asset unload.

    // Safe deterministic filesystem name from original source key; no path
    // traversal or direct use of model-provided path fragments.
    static std::string archiveFilename(const std::string& sourceKey);
private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace aiverse::render::strata
