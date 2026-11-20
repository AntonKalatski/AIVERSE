// AIVERSE genre-agnostic GPU texture residency facade.
// Combines owned, frame-budgeted staging with per-mip/face residency and
// generation-safe upload completions. GPU transfers are render-thread
// operations; this is NOT an independent I/O worker or sparse virtual texture.
#pragma once

#include "TextureMipResidency.h"
#include "TextureStreaming.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::gfx {

// Only complete, contiguous coarse-to-fine mip ranges are sampleable.
// The producer supplies packed texels for an entire single mip/layer
// (or the entire volume mip for Texture3D); the staging queue makes a copy
// before returning. The caller can fetch/decompress data on a separate
// worker but must stage and drain on the graphics-device thread.
class TextureResidentStreamer {
public:
    using FillFn = std::function<bool(TextureMipRequest request,
                                      int width, int height, int depth,
                                      std::vector<uint8_t>& pixels)>;
    using CompletionFn = std::function<void(TextureStreamId resource,
                                            TextureMipRequest request,
                                            bool success)>;

    explicit TextureResidentStreamer(Device& gpu, TextureStreamConfig limits = {})
        : queue_(gpu, limits), limits_(limits) {}
    ~TextureResidentStreamer() = default;
    TextureResidentStreamer(const TextureResidentStreamer&) = delete;
    TextureResidentStreamer& operator=(const TextureResidentStreamer&) = delete;
    TextureResidentStreamer(TextureResidentStreamer&&) = delete;
    TextureResidentStreamer& operator=(TextureResidentStreamer&&) = delete;

    // Allocate every mip up front, but expose no sampled handle until the
    // coarsest mip of every layer/face has been uploaded successfully.
    TextureStreamId create(TextureDesc desc) {
        if (desc.data || desc.levelCount || desc.levelData || desc.levelSize ||
            desc.renderTarget || desc.storage || desc.mipmaps ||
            desc.sampleCount != 1 || isCompressedTextureFormat(desc.format) ||
            !uncompressedTexturePixelBytes(desc.format)) {
            error_ = "resident textures require empty uncompressed sampled storage";
            return {};
        }
        if (!desc.mipLevels) desc.mipLevels = 1;
        if (validateTextureDesc(desc) != TextureDescError::None) {
            error_ = "invalid resident texture descriptor";
            return {};
        }
        Entry entry;
        if (!entry.residency.initialize(desc)) {
            error_ = "mip residency planner rejected descriptor";
            return {};
        }
        const TextureStreamId id = queue_.createTexture(desc);
        if (!id.valid()) {
            error_ = queue_.lastError();
            return {};
        }
        entry.desc = desc;
        entry.desc.label = nullptr;
        entries_.emplace(id.value, std::move(entry));
        error_.clear();
        return id;
    }

    // Request the next absent subresource, prioritized by coarsest mip
    // and then face/layer. A producer failure or rejected staging allocation
    // immediately unclaims the mip so it can be retried.
    TextureUploadTicket stageNext(TextureStreamId id, const FillFn& producer) {
        Entry* entry = find(id);
        if (!entry || entry->failed || queue_.failed(id) || !producer) {
            error_ = "invalid or failed texture residency resource/producer";
            return {};
        }
        TextureMipRequest request = entry->residency.claimNext();
        if (!request.valid()) {
            error_ = "no missing subresource available to stage";
            return {};
        }
        const TextureDesc& d = entry->desc;
        const int width = std::max(1, d.width >> request.mip);
        const int height = std::max(1, d.height >> request.mip);
        const int depth = d.dimension == TextureDimension::Texture3D ?
                          std::max(1, d.depth >> request.mip) : 1;
        const size_t bpp = size_t(uncompressedTexturePixelBytes(d.format));
        size_t bytes = bpp;
        bool overflow = false;
        for (int dim : {width, height, depth}) {
            if (bytes > std::numeric_limits<size_t>::max() / size_t(dim)) {
                overflow = true;
                break;
            }
            bytes *= size_t(dim);
        }
        if (overflow || bytes > limits_.stagingBudgetBytes ||
            bytes > limits_.uploadBytesPerFrame ||
            size_t(width)*bpp > size_t(std::numeric_limits<int>::max()) ||
            bytes == 0) {
            entry->residency.complete(request, false);
            error_ = "mip exceeds available staging/frame budget or pitch limits";
            return {};
        }
        std::vector<uint8_t> pixels;
        bool produced = false;
        try {
            produced = producer(request, width, height, depth, pixels);
        } catch (...) {
            entry->residency.complete(request, false);
            error_ = "texture mip producer threw an exception";
            return {};
        }
        if (!produced || pixels.size() != bytes) {
            entry->residency.complete(request, false);
            error_ = "producer did not provide a complete packed mip/layer image";
            return {};
        }
        TextureUploadDesc upload;
        upload.mipLevel = request.mip;
        upload.arrayLayer = request.layer;
        upload.width = width;
        upload.height = height;
        upload.depth = depth;
        upload.rowPitchBytes = int(size_t(width) * bpp);
        const size_t pitch = size_t(upload.rowPitchBytes) * size_t(height);
        if (pitch > size_t(std::numeric_limits<int>::max())) {
            entry->residency.complete(request, false);
            error_ = "mip slice pitch exceeds the API limit";
            return {};
        }
        upload.slicePitchBytes = int(pitch);
        const TextureUploadTicket ticket =
            queue_.enqueue(id, upload, pixels.data(), pixels.size());
        if (!ticket.valid()) {
            entry->residency.complete(request, false);
            error_ = queue_.lastError();
            return {};
        }
        pending_.emplace(ticket.value, Pending{id, request});
        error_.clear();
        return ticket;
    }

    // GPU calls are made synchronously inside drain; callbacks run only after
    // the underlying queue finishes this frame's iteration. A failed GPU write
    // quarantines its resource so no stale, partly updated texture is sampled.
    size_t drain(const CompletionFn& completion = {}) {
        const size_t submitted = queue_.drain([&](TextureUploadTicket ticket,
                                                 TextureStreamId resource,
                                                 bool ok) {
            const auto pending = pending_.find(ticket.value);
            if (pending == pending_.end()) return;
            const Pending context = pending->second;
            pending_.erase(pending);
            if (context.resource != resource) return; // stale/reused token
            Entry* entry = find(resource);
            if (!entry) return;
            if (!entry->residency.complete(context.request, ok)) return;
            if (!ok) entry->failed = true;
            if (completion) completion(resource, context.request, ok);
        });
        // The underlying queue cancels remaining jobs after an upload failure.
        // They have no completion events. Drop their pending mappings here and
        // quarantine the entire resource until destroy/recreate.
        for (auto& [key, entry] : entries_) {
            const TextureStreamId id{key};
            if (!queue_.failed(id)) continue;
            entry.failed = true;
            for (auto it = pending_.begin(); it != pending_.end();) {
                if (it->second.resource == id) it = pending_.erase(it);
                else ++it;
            }
        }
        return submitted;
    }

    // Cancel a queued (not yet submitted) job, making the same face retryable.
    bool cancel(TextureUploadTicket ticket) {
        auto it = pending_.find(ticket.value);
        if (it == pending_.end() || !queue_.cancel(ticket)) return false;
        if (Entry* entry = find(it->second.resource))
            entry->residency.complete(it->second.request, false);
        pending_.erase(it);
        return true;
    }

    bool destroy(TextureStreamId id) {
        if (!find(id)) return false;
        // destroy() on TextureStreamQueue drops every remaining GPU job
        // before backend handles may be reused.
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (it->second.resource == id) it = pending_.erase(it);
            else ++it;
        }
        entries_.erase(id.value);
        return queue_.destroy(id);
    }

    void shutdown() {
        pending_.clear();
        entries_.clear();
        queue_.shutdown();
    }

    // Public handle access is intentionally gated: an undefined coarsest
    // face or a failed transfer cannot accidentally be sampled.
    TextureHandle sampledTexture(TextureStreamId id) const {
        const Entry* entry = find(id);
        if (!entry || entry->failed || queue_.failed(id) ||
            !entry->residency.sampleable()) return {};
        return queue_.texture(id);
    }
    bool residentSampler(TextureStreamId id, SamplerDesc requested,
                         SamplerDesc& out) const {
        const Entry* entry = find(id);
        return entry && !entry->failed && !queue_.failed(id) &&
               entry->residency.residentSampler(requested, out);
    }
    bool sampleable(TextureStreamId id) const {
        return sampledTexture(id).valid();
    }
    bool fullyResident(TextureStreamId id) const {
        const Entry* entry = find(id);
        return sampleable(id) && entry->residency.fullyResident();
    }
    int finestResidentMip(TextureStreamId id) const {
        const Entry* entry = find(id);
        return sampleable(id) ? entry->residency.finestResidentMip() : -1;
    }
    size_t queuedMipCount(TextureStreamId id) const {
        const Entry* entry = find(id);
        return entry ? entry->residency.pendingCount() : 0;
    }
    bool failed(TextureStreamId id) const {
        const Entry* entry = find(id);
        return !entry || entry->failed || queue_.failed(id);
    }
    TextureStreamStats stats() const { return queue_.stats(); }
    const std::string& lastError() const { return error_; }

private:
    struct Entry {
        TextureDesc desc;
        TextureMipResidency residency;
        bool failed = false;
    };
    struct Pending {
        TextureStreamId resource;
        TextureMipRequest request;
    };
    Entry* find(TextureStreamId id) {
        const auto it = entries_.find(id.value);
        return it == entries_.end() ? nullptr : &it->second;
    }
    const Entry* find(TextureStreamId id) const {
        const auto it = entries_.find(id.value);
        return it == entries_.end() ? nullptr : &it->second;
    }
    TextureStreamQueue queue_;
    TextureStreamConfig limits_;
    std::map<uint64_t, Entry> entries_;
    std::map<uint64_t, Pending> pending_;
    std::string error_;
};

} // namespace aiverse::gfx
