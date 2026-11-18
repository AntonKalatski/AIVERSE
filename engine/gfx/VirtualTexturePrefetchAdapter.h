// AIVERSE virtual-texture demand-driven prefetch adapter.
// Converts generic UV visibility into bounded asynchronous page requests.
// Must run on the VirtualTextureFileStreamer owner/render thread.
#pragma once

#include "VirtualTexturePrefetch.h"
#include "VirtualTextureFileStreamer.h"

#include <vector>

namespace aiverse::gfx {

struct VirtualTexturePrefetchDispatch {
    VirtualTexturePrefetchStats planning;
    size_t submittedNewRequests = 0;
    size_t refusedRequests = 0;
    std::vector<VirtualDiskPageTicket> tickets;
};

// View projection is supplied by the game/editor (any genre); this wrapper
// does not estimate geometry visibility or execute GPU work. It only queues
// physical page-file reads and delegates GPU uploads to streamer.pump().
inline VirtualTexturePrefetchDispatch prefetchVisibleVirtualPages(
    VirtualTextureFileStreamer& streamer,
    VirtualTexturePrefetchPlanner& planner,
    const std::vector<VirtualTextureVisibleRegion>& views) {
    VirtualTexturePrefetchDispatch result;
    if (!streamer.ready() || !planner.ready()) return result;
    const bool pinCoarse = planner.config().pinCoarsest;
    const int lastMip = planner.config().mipLevels - 1;
    const auto work = planner.plan(views, [&](VirtualPageKey key) {
        const bool resident = streamer.exactResidentPage(key);
        const bool pending = streamer.pendingPage(key);
        if ((resident || pending) && pinCoarse && key.mip == lastMip)
            streamer.pinPage(key,true);
        return resident || pending;
    });
    result.planning = planner.stats();
    result.tickets.reserve(work.size());
    for (const auto& page:work) {
        // Queue exhaustion must not drop the prefetch hierarchy: do not
        // submit finer pages after a rejected coarse page. Next frame
        // reprojection and planning will retry it.
        const VirtualDiskPageTicket ticket =
            streamer.requestPage(page.key,page.pin);
        if (!ticket.valid()) {
            ++result.refusedRequests;
            break;
        }
        result.tickets.push_back(ticket);
        ++result.submittedNewRequests;
    }
    return result;
}

} // namespace aiverse::gfx
