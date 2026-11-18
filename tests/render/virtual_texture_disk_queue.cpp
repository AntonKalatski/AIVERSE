#include "engine/gfx/VirtualTextureDiskQueue.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace aiverse::gfx;
namespace {
void require(bool pass, const char* message) {
    if (!pass) throw std::runtime_error(message);
}
struct TemporaryPages {
    std::filesystem::path root;
    TemporaryPages() {
        root = std::filesystem::temp_directory_path() /
            ("aiverse_virtual_disk_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
    }
    ~TemporaryPages() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    void write(VirtualPageKey key, uint8_t value, size_t count = 64) const {
        const auto path = root / VirtualTextureDiskQueue::relativePath(key);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        require(bool(file), "cannot create test virtual page file");
        const std::vector<char> pixels(count, char(value));
        file.write(pixels.data(), std::streamsize(pixels.size()));
        require(bool(file), "cannot write test virtual page file");
    }
};
bool waitReady(VirtualTextureDiskQueue& queue, size_t ready) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < limit) {
        if (queue.stats().ready >= ready) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    return false;
}
VirtualDiskPageConfig settings(const TemporaryPages& root) {
    VirtualDiskPageConfig c;
    c.assetRoot = root.root;
    c.tilesX = 4;
    c.tilesY = 4;
    c.mipLevels = 3;
    c.virtualLayers = 2;
    c.pageSize = 4;
    c.maxOutstanding = 4;
    c.maxReadyBytes = 64; // exactly one physical RGBA8 page
    return c;
}

void realDiskReadsAndBackpressure() {
    TemporaryPages pages;
    const VirtualPageKey a{2,0,0,0}, b{1,0,0,0};
    pages.write(a, 17);
    pages.write(b, 29);
    VirtualTextureDiskQueue queue;
    auto config = settings(pages);
    require(queue.start(config), "start one disk I/O worker");
    require(queue.pageBytes() == 64, "bounded page byte size");
    const auto first = queue.request(a);
    const auto dedup = queue.request(a);
    const auto second = queue.request(b);
    require(first.valid() && first == dedup && second.valid() &&
            second != first && queue.stats().outstanding == 2,
            "deduplicate queued keys but preserve separate tickets");
    require(!queue.request({3,0,0,0}).valid() &&
            !queue.request({0,4,0,0}).valid() &&
            !queue.request({0,0,0,2}).valid(),
            "reject invalid mip, tile and layer requests");
    require(waitReady(queue, 1), "first physical page read from disk");
    require(queue.stats().readyBytes == 64 && queue.stats().ready == 1,
            "ready page memory cap prevents unlimited worker buffering");
    std::vector<VirtualDiskPageResult> output;
    require(queue.takeReady(output, 63) == 0 && output.empty(),
            "poll budget smaller than one page leaves it pending");
    require(queue.takeReady(output, 64) == 1 &&
            output[0].ticket == first && output[0].key == a &&
            output[0].success() && output[0].rgba.size() == 64 &&
            output[0].rgba.front() == 17 && output[0].rgba.back() == 17,
            "first page transfers exact owned bytes");
    output.clear();
    require(waitReady(queue, 1), "worker resumes after result buffer freed");
    require(queue.takeReady(output, 64) == 1 &&
            output[0].ticket == second && output[0].key == b &&
            output[0].success() && output[0].rgba.front() == 29,
            "second page queued FIFO and delivered after backpressure");
    require(queue.stats().outstanding == 0 && queue.stats().readyBytes == 0,
            "all staging memory released after poll");

    const VirtualPageKey missing{0,1,1,1};
    const auto missingTicket = queue.request(missing);
    require(missingTicket.valid() && waitReady(queue, 1),
            "missing page produces explicit error result");
    output.clear();
    require(queue.takeReady(output) == 1 &&
            output[0].ticket == missingTicket &&
            !output[0].success() && !output[0].error.empty() &&
            output[0].rgba.empty(),
            "missing file cannot publish a partial GPU page");
    const VirtualPageKey truncated{0,3,3,0};
    pages.write(truncated, 77, 63); // require EXACT file length
    const auto truncatedTicket = queue.request(truncated);
    require(truncatedTicket.valid() && waitReady(queue, 1),
            "short page yields a failure");
    output.clear();
    require(queue.takeReady(output) == 1 && !output[0].success() &&
            output[0].rgba.empty(),
            "one missing RGBA byte rejects entire physical tile");
    require(queue.stats().failed == 2, "both failed disk pages accounted");
    queue.shutdown();
}

void cancellationAndRestart() {
    TemporaryPages pages;
    const VirtualPageKey a{2,0,0,0}, b{0,2,1,0};
    pages.write(a, 91);
    pages.write(b, 42);
    VirtualTextureDiskQueue queue;
    auto config = settings(pages);
    config.maxOutstanding = 1;
    require(queue.start(config), "start request-limited queue");
    const auto first = queue.request(a);
    require(first.valid() && !queue.request(b).valid(),
            "outstanding request cap prevents overcommit");
    require(queue.cancel(first) && !queue.cancel(first) &&
            queue.stats().cancelled == 1,
            "queued or in-flight disk request can be cancelled once");
    require(queue.request(b).valid(), "slot becomes available after cancellation");
    require(waitReady(queue,1), "uncancelled page completes");
    std::vector<VirtualDiskPageResult> result;
    require(queue.takeReady(result) == 1 &&
            result[0].success() && result[0].key == b &&
            result[0].rgba[0] == 42,
            "cancelled work cannot replace a newly requested page");
    const uint64_t previousId = result[0].ticket.id;
    queue.shutdown();
    require(queue.start(config), "restart disk queue without stale worker");
    const auto replay = queue.request(a);
    require(replay.valid() && replay.id > previousId,
            "ticket ID monotonic across worker restarts");
    require(!queue.cancel(first), "old ticket cannot cancel new resource");
    require(waitReady(queue,1), "worker restarted correctly");
    result.clear();
    require(queue.takeReady(result) == 1 &&
            result[0].ticket == replay && result[0].rgba[0] == 91,
            "restart delivers correct source page");
    queue.shutdown();
    require(queue.stats().outstanding == 0 &&
            queue.stats().readyBytes == 0, "shutdown owns no pending bytes");
}

void invalidSettings() {
    TemporaryPages tmp;
    VirtualTextureDiskQueue queue;
    auto config = settings(tmp);
    config.maxReadyBytes = 63;
    require(!queue.start(config) && !queue.lastError().empty(),
            "staging cap smaller than one page rejected");
    config = settings(tmp); config.mipLevels = 31;
    require(!queue.start(config), "invalid virtual mip extent rejected");
    config = settings(tmp); config.assetRoot.clear();
    require(!queue.start(config), "asset root required");
}
}
int main() {
    try {
        realDiskReadsAndBackpressure();
        cancellationAndRestart();
        invalidSettings();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Virtual texture disk queue FAILED: %s\n", e.what());
        return 1;
    }
    std::puts("Virtual texture disk queue real file I/O, backpressure and cancellation: OK");
    return 0;
}
