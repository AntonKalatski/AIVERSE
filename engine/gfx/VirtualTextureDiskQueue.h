// AIVERSE bounded asynchronous virtual texture disk-page source.
// A single I/O worker reads immutable packed RGBA8 page files. The render
// thread polls ready results and submits them to VirtualTextureStreamer.
// No GPU calls, Editor state, ECS authority, or background renderer work here.
#pragma once
#include "VirtualTexturePages.h"
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace aiverse::gfx {

struct VirtualDiskPageTicket {
    uint64_t id = 0;
    bool valid() const { return id != 0; }
    bool operator==(const VirtualDiskPageTicket&) const = default;
};

struct VirtualDiskPageConfig {
    std::filesystem::path assetRoot;
    int tilesX = 0, tilesY = 0, mipLevels = 0, virtualLayers = 1;
    int pageSize = 128; // including gutters; packed RGBA8, bytes=pageSize^2*4
    size_t maxOutstanding = 128;
    size_t maxReadyBytes = size_t(16) << 20;
};

struct VirtualDiskPageResult {
    VirtualDiskPageTicket ticket;
    VirtualPageKey key;
    std::vector<uint8_t> rgba; // exactly pageSize*pageSize*4 bytes on success
    std::string error;
    bool success() const { return error.empty() && !rgba.empty(); }
};

struct VirtualDiskPageStats {
    size_t queued = 0, active = 0, ready = 0;
    size_t readyBytes = 0, outstanding = 0;
    uint64_t delivered = 0, failed = 0, cancelled = 0;
};

// Request a page using an indexed layout under an application-selected trusted
// asset directory: m<MIP>/l<LAYER>/x<X>_y<Y>.rgba. All path components are
// integer values validated against configured virtual texture dimensions;
// untrusted callers cannot inject a relative filename such as "../../".
// App must create this asset layout at build/import time.
class VirtualTextureDiskQueue {
public:
    VirtualTextureDiskQueue() = default;
    ~VirtualTextureDiskQueue() { shutdown(); }
    VirtualTextureDiskQueue(const VirtualTextureDiskQueue&) = delete;
    VirtualTextureDiskQueue& operator=(const VirtualTextureDiskQueue&) = delete;
    VirtualTextureDiskQueue(VirtualTextureDiskQueue&&) = delete;
    VirtualTextureDiskQueue& operator=(VirtualTextureDiskQueue&&) = delete;

    bool start(VirtualDiskPageConfig config) {
        shutdown();
        const size_t side = config.pageSize > 0 ? size_t(config.pageSize) : 0;
        if (config.assetRoot.empty() || config.tilesX < 1 || config.tilesY < 1 ||
            config.tilesX > (1 << 20) || config.tilesY > (1 << 20) ||
            config.mipLevels < 1 || config.mipLevels > 30 ||
            config.virtualLayers < 1 || config.virtualLayers > (1 << 16) ||
            side < 1 || side > 4096 ||
            config.maxOutstanding < 1 || config.maxOutstanding > 65536 ||
            side > std::numeric_limits<size_t>::max()/side ||
            side*side > std::numeric_limits<size_t>::max()/4 ||
            side*side*4 > config.maxReadyBytes ||
            side*side*4 > (size_t(512) << 20)) {
            setError("invalid disk page dimensions, request or staging budget");
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            config_ = std::move(config);
            pageBytes_ = side*side*4;
            stopping_ = false;
            running_ = true;
            error_.clear();
        }
        try {
            worker_ = std::thread([this] { readWorker(); });
        } catch (const std::exception& e) {
            shutdown();
            setError(std::string("cannot start disk page worker: ") + e.what());
            return false;
        }
        return true;
    }

    static std::filesystem::path relativePath(VirtualPageKey key) {
        return std::filesystem::path("m" + std::to_string(key.mip)) /
               ("l" + std::to_string(key.layer)) /
               ("x" + std::to_string(key.x) + "_y" +
                std::to_string(key.y) + ".rgba");
    }

    // Returns the same ticket for an already-queued/active/ready page.
    // Consumers requesting the same page must coordinate that ticket: one
    // cancellation cancels the shared request for all consumers.
    VirtualDiskPageTicket request(VirtualPageKey key) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ || stopping_ || !validKey(key)) {
            error_ = "disk page queue not running or virtual coordinate invalid";
            return {};
        }
        const auto existing = byKey_.find(key);
        if (existing != byKey_.end()) return {existing->second};
        if (outstanding_.size() >= config_.maxOutstanding ||
            nextTicket_ == std::numeric_limits<uint64_t>::max()) {
            error_ = "disk page outstanding request budget exhausted";
            return {};
        }
        VirtualDiskPageTicket ticket{nextTicket_++};
        outstanding_.emplace(ticket.id, key);
        byKey_.emplace(key, ticket.id);
        queued_.push_back({ticket, key});
        cv_.notify_one();
        return ticket;
    }

    // Cancels queued, in-flight, or already-completed-but-not-polled work.
    // An in-flight blocking file read may finish; its stale result is dropped
    // at the worker's final ticket check, never published to the render thread.
    bool cancel(VirtualDiskPageTicket ticket) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = outstanding_.find(ticket.id);
        if (it == outstanding_.end()) return false;
        byKey_.erase(it->second);
        outstanding_.erase(it);
        for (auto q = queued_.begin(); q != queued_.end();) {
            if (q->ticket == ticket) q = queued_.erase(q);
            else ++q;
        }
        for (auto r = ready_.begin(); r != ready_.end();) {
            if (r->ticket == ticket) {
                readyBytes_ -= r->rgba.size();
                r = ready_.erase(r);
            } else ++r;
        }
        ++cancelled_;
        cv_.notify_all();
        return true;
    }

    // Poll only on the render thread. Each result is moved out of the bounded
    // ready buffer; the caller owns its bytes and can pass them to
    // VirtualTextureStreamer::stage(key,rgba.data(),rgba.size()).
    // A budget smaller than one ready page leaves that page in the queue.
    size_t takeReady(std::vector<VirtualDiskPageResult>& out,
                     size_t maxBytes = std::numeric_limits<size_t>::max(),
                     size_t maxResults = 64) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t taken = 0, transferred = 0;
        while (!ready_.empty() && taken < maxResults) {
            const size_t bytes = ready_.front().rgba.size();
            if (bytes > maxBytes - transferred) break;
            VirtualDiskPageResult result = std::move(ready_.front());
            ready_.pop_front();
            readyBytes_ -= bytes;
            auto it = outstanding_.find(result.ticket.id);
            if (it == outstanding_.end()) continue; // cancelled before poll
            byKey_.erase(it->second);
            outstanding_.erase(it);
            if (!result.success()) ++failed_;
            out.push_back(std::move(result));
            transferred += bytes;
            ++delivered_;
            ++taken;
        }
        cv_.notify_all();
        return taken;
    }

    VirtualDiskPageStats stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {queued_.size(), active_ ? 1u : 0u, ready_.size(),
                readyBytes_, outstanding_.size(), delivered_, failed_, cancelled_};
    }
    std::string lastError() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return error_;
    }
    size_t pageBytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pageBytes_;
    }

    // Blocks until any in-flight filesystem read finishes; no callbacks run.
    // Safe to call before releasing VirtualTextureStreamer / GPU resources.
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            cv_.notify_all();
        }
        if (worker_.joinable()) worker_.join();
        std::lock_guard<std::mutex> lock(mutex_);
        queued_.clear();
        ready_.clear();
        byKey_.clear();
        outstanding_.clear();
        active_ = 0;
        readyBytes_ = 0;
        pageBytes_ = 0;
        running_ = false;
        stopping_ = false;
        config_ = {};
    }

private:
    struct Request {
        VirtualDiskPageTicket ticket;
        VirtualPageKey key;
    };

    bool validKey(VirtualPageKey k) const {
        if (k.mip < 0 || k.mip >= config_.mipLevels ||
            k.layer < 0 || k.layer >= config_.virtualLayers ||
            k.x < 0 || k.y < 0) return false;
        const int scale = 1 << k.mip;
        const int mipX = (config_.tilesX + scale - 1) / scale;
        const int mipY = (config_.tilesY + scale - 1) / scale;
        return k.x < mipX && k.y < mipY;
    }

    static VirtualDiskPageResult load(const VirtualDiskPageConfig& config,
                                      size_t exactBytes, Request request) {
        VirtualDiskPageResult result;
        result.ticket = request.ticket;
        result.key = request.key;
        const auto path = config.assetRoot / relativePath(request.key);
        std::error_code ec;
        const uintmax_t length = std::filesystem::file_size(path, ec);
        if (ec || length != uintmax_t(exactBytes)) {
            result.error = "missing page or incorrect RGBA8 page byte count";
            return result;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            result.error = "cannot open virtual texture page";
            return result;
        }
        result.rgba.resize(exactBytes);
        input.read(reinterpret_cast<char*>(result.rgba.data()),
                   std::streamsize(exactBytes));
        if (!input || input.gcount() != std::streamsize(exactBytes)) {
            result.rgba.clear();
            result.error = "short read of virtual texture page";
        }
        return result;
    }

    void readWorker() {
        for (;;) {
            Request request;
            VirtualDiskPageConfig config;
            size_t bytes = 0;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stopping_ || !queued_.empty(); });
                if (stopping_) return;
                request = queued_.front();
                queued_.pop_front();
                active_ = request.ticket.id;
                config = config_;
                bytes = pageBytes_;
            }
            VirtualDiskPageResult result = load(config, bytes, request);
            {
                std::unique_lock<std::mutex> lock(mutex_);
                active_ = 0;
                if (stopping_) return;
                if (!outstanding_.count(request.ticket.id)) continue;
                if (result.success()) {
                    cv_.wait(lock, [&] {
                        return stopping_ || !outstanding_.count(request.ticket.id) ||
                               readyBytes_ <= config_.maxReadyBytes - bytes;
                    });
                    if (stopping_) return;
                    if (!outstanding_.count(request.ticket.id)) continue;
                    readyBytes_ += bytes;
                }
                ready_.push_back(std::move(result));
            }
            cv_.notify_all();
        }
    }

    void setError(std::string message) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = std::move(message);
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    VirtualDiskPageConfig config_;
    std::deque<Request> queued_;
    std::deque<VirtualDiskPageResult> ready_;
    std::map<VirtualPageKey,uint64_t> byKey_;
    std::map<uint64_t,VirtualPageKey> outstanding_;
    size_t active_ = 0, readyBytes_ = 0, pageBytes_ = 0;
    uint64_t nextTicket_ = 1;
    uint64_t delivered_ = 0, failed_ = 0, cancelled_ = 0;
    bool running_ = false, stopping_ = false;
    std::string error_;
};

} // namespace aiverse::gfx
