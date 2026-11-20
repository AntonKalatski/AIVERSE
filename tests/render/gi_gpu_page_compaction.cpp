#include "engine/render/GiGpuPagedBvhArena.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace aiverse::render;

static StaticShadowGeometry::Triangle triangle() {
    return {{-2,-2,2},{2,-2,2},{2,2,2},0};
}
static GiChunkedGeometrySnapshot source(size_t triangles) {
    GiChunkedGeometryBuilder builder;
    builder.replace(1,std::vector<StaticShadowGeometry::Triangle>(
        triangles,triangle()),{{.5f,.25f,.75f}});
    return builder.publish();
}
int main() {
    constexpr uint32_t nodeBudget=GiGpuPagedBvhArena::kTlasCapacity+128;
    // Small logical GPU address-space budgets reproduce physical fragmentation
    // without requiring an expensive 8,192-triangle test for every revision.
    GiGpuPagedBvhArena pages(64,nodeBudget);
    GiGpuChunkBvhCache cache;
    uint32_t oldFirst=0;
    for(size_t triangles:{1u,2u,3u,5u,9u,17u,33u}) {
        auto geometry=source(triangles);
        const auto stats=cache.refresh(geometry);
        AIVERSE_TEST_CHECK(stats.rebuilt==1);
        auto next=pages.plan(geometry,cache);
        AIVERSE_TEST_CHECK(next.accepted);
        AIVERSE_TEST_CHECK(next.stats.relevantTriangles==triangles);
        const auto result=next.packet.trace({0,0,0},{0,0,1},10);
        AIVERSE_TEST_CHECK(std::abs(result.distance-2.f)<.0001f);
        if(triangles==33) {
            // Old 32-slot page is at offset 31; a 64-slot new allocation
            // would exceed the 64-slot physical buffer. Transactional
            // compaction rebuilds a fresh page at 0 without truncation.
            AIVERSE_TEST_CHECK(next.stats.compacted);
            AIVERSE_TEST_CHECK(next.stats.physicalTriangles==64);
            AIVERSE_TEST_CHECK(next.stats.relevantTriangles==33);
            AIVERSE_TEST_CHECK(pages.findPage(1)->triangles.first==oldFirst);
            AIVERSE_TEST_CHECK(next.packet.triangles.size()==64);
        } else AIVERSE_TEST_CHECK(!next.stats.compacted);
        pages.commit(std::move(next));
        AIVERSE_TEST_CHECK(pages.findPage(1)!=nullptr);
        oldFirst=pages.findPage(1)->triangles.first;
        if(triangles==33)AIVERSE_TEST_CHECK(oldFirst==0);
    }

    // If even compact fresh allocation exceeds the node budget, refuse the
    // update and keep the previously accepted source/page offsets untouched.
    GiGpuPagedBvhArena tight(64,GiGpuPagedBvhArena::kTlasCapacity+2);
    auto one=source(2);
    cache.refresh(one);
    auto initial=tight.plan(one,cache);
    AIVERSE_TEST_CHECK(initial.accepted);
    tight.commit(std::move(initial));
    AIVERSE_TEST_CHECK(tight.findPage(1)->triangleCount==2);
    const auto previousFirst=tight.findPage(1)->triangles.first;
    auto more=source(9);
    cache.refresh(more);
    bool rejected=false;
    try {(void)tight.plan(more,cache);}
    catch(const std::runtime_error&) {rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    AIVERSE_TEST_CHECK(tight.findPage(1)->triangleCount==2);
    AIVERSE_TEST_CHECK(tight.findPage(1)->triangles.first==previousFirst);
}
