// GPU-free PRISMA render-graph contracts; run through ./run-tests.sh and CTest.
#include "engine/render/PrismaRenderGraph.h"
#include <cstdio>
#include <string>
#include <vector>
using namespace aiverse::render;

namespace {
int errors = 0;
void check(bool ok, const char* message) {
    if (!ok) { ++errors; std::fprintf(stderr, "FAIL: %s\n", message); }
}
PrismaGraphResource resource(const char* name, bool external = false,
                             PrismaGraphFormat format = PrismaGraphFormat::RGBA16F,
                             uint32_t w = 64, uint32_t h = 64) {
    PrismaGraphResource r;
    r.name = name; r.external = external; r.format = format;
    r.width = w; r.height = h;
    return r;
}
PrismaGraphPass pass(const char* name, std::vector<PrismaGraphId> reads,
                     std::vector<PrismaGraphId> writes,
                     std::vector<PrismaGraphId> after = {}) {
    PrismaGraphPass p;
    p.name = name;
    p.reads = std::move(reads);
    p.writes = std::move(writes);
    p.after = std::move(after);
    return p;
}
void positive() {
    PrismaRenderGraph g;
    auto empty = g.compile();
    check(empty.valid && empty.order.empty() && empty.bytes == 0, "empty graph is valid");
    auto source = g.addResource(resource("SceneColor", true));
    auto a = g.addResource(resource("BloomA"));
    auto b = g.addResource(resource("BloomB"));
    auto c = g.addResource(resource("BloomC"));
    auto out = g.addResource(resource("Output", true, PrismaGraphFormat::RGBA8));
    // Deliberately declare the consumer before its producer.
    g.addPass(pass("display", {c}, {out}));
    g.addPass(pass("prefilter", {source}, {a}));
    g.addPass(pass("downsample", {a}, {b}));
    g.addPass(pass("upsample", {b}, {c}));
    auto plan = g.compile();
    check(plan.valid, "dependency graph is valid");
    check(plan.order == std::vector<PrismaGraphId>({1,2,3,0}), "topological producer-before-consumer");
    check(plan.resources[source].slot == -1 && plan.resources[out].slot == -1, "externals have no graph allocation");
    check(plan.resources[a].slot >= 0 && plan.resources[a].slot == plan.resources[c].slot,
          "non-overlapping equal descriptors alias");
    check(plan.resources[a].slot != plan.resources[b].slot, "overlapping lifetimes never alias");
    check(plan.slots.size() == 2 && plan.bytes == 2 * 64ull * 64ull * 8ull, "two HDR transient slots");
    check(plan.resources[a].first == 0 && plan.resources[a].last == 1, "lifetime from execution order");
    check(plan.resources[c].first == 2 && plan.resources[c].last == 3, "later lifetime");
}
void hazards() {
    {
        PrismaRenderGraph g;
        auto a = g.addResource(resource("a"));
        g.addPass(pass("feedback", {a}, {a}));
        check(!g.compile().valid, "in-place feedback rejected");
    }
    {
        PrismaRenderGraph g;
        auto a = g.addResource(resource("a"));
        g.addPass(pass("a1", {}, {a}));
        g.addPass(pass("a2", {}, {a}));
        check(!g.compile().valid, "double-writer rejected");
    }
    {
        PrismaRenderGraph g;
        auto a = g.addResource(resource("a"));
        g.addPass(pass("sink", {a}, {}));
        check(!g.compile().valid, "missing transient producer rejected");
    }
    {
        PrismaRenderGraph g;
        auto a = g.addResource(resource("a"));
        auto b = g.addResource(resource("b"));
        g.addPass(pass("a-write", {b}, {a}));
        g.addPass(pass("b-write", {a}, {b}));
        auto p=g.compile();
        check(!p.valid && p.error.find("cycle") != std::string::npos, "resource dependency cycle rejected");
    }
    {
        PrismaRenderGraph g;
        g.addPass(pass("a", {}, {}, {1}));
        g.addPass(pass("b", {}, {}, {0}));
        check(!g.compile().valid, "explicit cycle rejected");
    }
    {
        PrismaRenderGraph g;
        g.addPass(pass("a", {}, {}, {0}));
        check(!g.compile().valid, "self dependency rejected");
    }
    {
        PrismaRenderGraph g;
        g.addPass(pass("a", {kInvalidPrismaGraphId}, {}));
        check(!g.compile().valid, "invalid resource ID rejected");
    }
    {
        PrismaRenderGraph g;
        g.addResource(resource("dup", true));
        g.addResource(resource("dup", true));
        check(!g.compile().valid, "duplicate resource names rejected");
    }
    {
        PrismaRenderGraph g;
        g.addPass(pass("dup", {}, {}));
        g.addPass(pass("dup", {}, {}));
        check(!g.compile().valid, "duplicate pass names rejected");
    }
    {
        PrismaRenderGraph g;
        g.addResource(resource("unwritten"));
        check(!g.compile().valid, "unused transient without producer rejected");
    }
    {
        PrismaRenderGraph g;
        g.addResource(resource("zero",true,PrismaGraphFormat::RGBA8,0,128));
        check(!g.compile().valid, "zero-sized resource rejected");
    }
    {
        PrismaRenderGraph g;
        auto prev = resource("prev-frame", true);
        prev.history = true; // resource lifetime belongs to the host
        auto in = g.addResource(prev);
        auto out = g.addResource(resource("current-frame", true));
        g.addPass(pass("temporal resolve", {in}, {out}));
        const auto p = g.compile();
        check(p.valid && p.slots.empty(), "caller-managed external history is accepted");
    }
}
void resources() {
    PrismaRenderGraph g;
    auto input = g.addResource(resource("input", true));
    auto history = resource("history");
    history.history = true;
    auto h = g.addResource(history);
    auto temp = resource("temp");
    temp.allowAlias = false;
    auto t = g.addResource(temp);
    auto later = g.addResource(resource("later"));
    auto out = g.addResource(resource("output", true));
    g.addPass(pass("fill history", {input}, {h}));
    g.addPass(pass("consume history", {h}, {t}));
    g.addPass(pass("fill later", {t}, {later}));
    g.addPass(pass("send output", {later}, {out}));
    const auto p = g.compile();
    check(p.valid, "history/exclusive resources accepted");
    check(p.slots.size() == 3, "history and exclusive targets do not alias");
    check(p.resources[h].slot != p.resources[later].slot, "history never aliased");
    check(p.resources[t].slot != p.resources[later].slot, "allowAlias=false");
    PrismaRenderGraph independentlyOrdered;
    independentlyOrdered.addPass(pass("first", {}, {}));
    independentlyOrdered.addPass(pass("second", {}, {}));
    independentlyOrdered.addPass(pass("third", {}, {}));
    check(independentlyOrdered.compile().order == std::vector<PrismaGraphId>({0,1,2}),
          "independent passes preserve registration order");
}
}
int main() {
    positive();
    hazards();
    resources();
    std::printf("PRISMA RenderGraph: %d failures\n",errors);
    return errors ? 1 : 0;
}
