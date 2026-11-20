#include "engine/gfx/VirtualTexturePages.h"

#include <cstdio>
#include <stdexcept>

using namespace aiverse::gfx;
namespace {
void require(bool v, const char* why) { if (!v) throw std::runtime_error(why); }

void pinnedRootsAndCoarseFallback() {
    VirtualTexturePageCache cache;
    require(cache.initialize(4,4,3,2,2), "initialize virtual page cache");
    const TextureDesc atlas = cache.atlasDescriptor(128, PixelFormat::RGBA8);
    require(atlas.dimension == TextureDimension::Texture2DArray &&
            atlas.width == 128 && atlas.height == 128 &&
            atlas.arrayLayers == 2 && atlas.mipLevels == 1 &&
            validateTextureDesc(atlas) == TextureDescError::None,
            "physical page atlas descriptor");

    const VirtualPageKey coarse{2,0,0,0};
    auto root = cache.reserve(coarse, true);
    require(root.valid() && root.needsUpload && !root.ready &&
            cache.stats().pending == 1 && cache.stats().pinned == 1,
            "pinned root needs physical upload");
    auto duplicate = cache.reserve(coarse, false);
    require(duplicate.valid() && duplicate.physicalSlice == root.physicalSlice &&
            duplicate.generation == root.generation &&
            !duplicate.needsUpload && !duplicate.ready,
            "duplicate pending page never creates another upload");
    require(!cache.resolve(coarse).valid(), "in-flight root cannot be sampled");
    require(cache.complete(coarse, root, true), "publish completed coarse page");
    require(cache.resolve(coarse).valid() &&
            cache.resolve(coarse).physicalSlice == root.physicalSlice,
            "resident coarse page published");

    const VirtualPageKey detail{0,0,0,0};
    auto child = cache.reserve(detail);
    require(child.valid() && child.needsUpload &&
            child.physicalSlice != root.physicalSlice, "child gets other atlas slice");
    require(!cache.reserve({0,1,1,0}).valid(),
            "pinned root and in-flight child must not be evicted");
    require(!cache.resolveWithCoarseFallback({0,1,1,1}).valid(),
            "fallback must never cross virtual array layers");
    auto fallback = cache.resolveWithCoarseFallback({0,1,1,0});
    require(fallback.valid() && fallback.physicalSlice == root.physicalSlice,
            "unresident detail uses same-layer coarse parent");
    require(cache.complete(detail,child,true), "publish child");
    require(cache.resolve(detail).valid(), "child residency published");

    const VirtualPageKey newDetail{0,2,2,0};
    auto replacement = cache.reserve(newDetail);
    require(replacement.valid() && replacement.needsUpload &&
            replacement.physicalSlice == child.physicalSlice &&
            replacement.generation != child.generation,
            "LRU replaces only unpinned resident child");
    require(!cache.complete(detail,child,true),
            "stale GPU completion cannot publish evicted page");
    require(!cache.resolve(detail).valid(), "evicted page unavailable");
    require(!cache.evict(coarse) && cache.stats().evictions == 1,
            "pinned coarse root immune to eviction");
    require(cache.abandon(newDetail,replacement),
            "pending physical slice can be retired after upload cancellation");
    auto third = cache.reserve({0,3,3,0});
    require(third.valid() && third.physicalSlice == replacement.physicalSlice &&
            third.generation != replacement.generation,
            "abandoned pending slot reused with new generation");
    require(!cache.complete(newDetail,replacement,true),
            "delayed completion after abandonment ignored");
    cache.clear();
    require(cache.initialize(4,4,3,2,2), "cache reinitialized");
    auto reboot = cache.reserve(coarse);
    require(reboot.valid() && reboot.generation != root.generation,
            "clear does not recycle generation token");
    require(!cache.complete(coarse,root,true),
            "old atlas completion cannot target new atlas");
}

void deterministicLruAndValidity() {
    VirtualTexturePageCache cache;
    require(!cache.initialize(0,4,1,1,1) &&
            !cache.initialize(4,4,31,1,1) &&
            !cache.initialize(4,4,1,1,0), "invalid virtual geometry rejected");
    require(cache.initialize(4,4,3,1,2), "valid small cache");
    require(!cache.reserve({1,2,0,0}).valid() &&
            !cache.reserve({0,4,0,0}).valid() &&
            !cache.reserve({0,0,0,1}).valid(),
            "mip-relative coordinates and virtual layer validated");
    const VirtualPageKey a{0,0,0,0}, b{0,1,0,0}, c{0,2,0,0};
    auto ra=cache.reserve(a), rb=cache.reserve(b);
    require(cache.complete(a,ra,true) && cache.complete(b,rb,true),
            "both page uploads completed");
    require(cache.touch(a), "touch refreshes LRU age");
    auto rc=cache.reserve(c);
    require(rc.valid() && rc.physicalSlice == rb.physicalSlice &&
            cache.resolve(a).valid() && !cache.resolve(b).valid(),
            "least recently used page evicted first");
    require(!cache.evict(c), "pending upload cannot be evicted by CPU");
    require(cache.complete(c,rc,false) && cache.stats().free == 1,
            "failed GPU upload returns physical slot to free pool");
    require(!cache.complete(c,rc,true), "failed completion cannot publish page");
    require(!cache.resolveWithCoarseFallback({0,3,3,0}).valid(),
            "no resident coarser ancestor means no virtual sampling");
}
}

int main() {
    try {
        pinnedRootsAndCoarseFallback();
        deterministicLruAndValidity();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "VirtualTexturePageCache test FAILED: %s\n", e.what());
        return 1;
    }
    std::puts("Virtual page cache pinning, LRU, stale tokens and fallback: OK");
    return 0;
}
