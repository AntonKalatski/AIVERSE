// STRATA load-time benchmark on a real asset: loadModel -> prepareModelUpload ->
// SceneStaging::stage (the same order GeometryScene uses for portable STRATA).
//   aiverse_strata_stage_bench <file.glb> [repeats]
// Prints wall-clock milliseconds per phase plus staging counters and warnings.
// Not a CTest: it needs an external asset. Exit 0 when the source was staged.
#include "engine/render/Model.h"
#include "engine/render/StrataModelAdapter.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace aiverse::render;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <model.glb> [repeats]\n", argv[0]); return 2; }
    const std::string path = argv[1];
    const int repeats = argc > 2 ? std::max(1, std::atoi(argv[2])) : 1;
    FileReader read = [](const std::string& p, std::string& out) {
        std::ifstream f(p, std::ios::binary); if (!f) return false;
        std::ostringstream s; s << f.rdbuf(); out = s.str(); return true;
    };
    using clock = std::chrono::steady_clock;
    auto ms = [](clock::time_point a, clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    bool ok = true;
    for (int r = 0; r < repeats; ++r) {
        const auto t0 = clock::now();
        std::vector<std::string> warnings;
        auto model = loadModel(path, read, &warnings);
        const auto t1 = clock::now();
        if (!model) { std::fprintf(stderr, "load failed\n"); return 1; }
        size_t tris = 0; for (const auto& m : model->meshes) for (const auto& p : m.primitives) tris += p.indexCount / 3;
        prepareModelUpload(*model);
        const auto t2 = clock::now();
        strata::SceneStaging staging;
        staging.setAllowPreparedRootLods(true);
        staging.setTolerateSourceSeamDefects(true); // as GeometryScene does for production scenes
        const bool staged = staging.stage(path, *model);
        const auto t3 = clock::now();
        const auto& st = staging.stats();
        std::printf("[strata-stage] run=%d vertices=%zu triangles=%zu load_ms=%.1f prepare_ms=%.1f stage_ms=%.1f staged=%d "
                    "sources=%u skipped=%u failed=%u primitives=%u skippedPrimitives=%u nodes=%llu meshlets=%llu indexBytes=%llu\n",
                    r, model->vertices.size(), tris, ms(t0, t1), ms(t1, t2), ms(t2, t3), int(staged), st.stagedSources,
                    st.skippedSources, st.failedSources, st.stagedPrimitives, st.skippedPrimitives,
                    (unsigned long long)st.clusterNodes, (unsigned long long)st.meshlets, (unsigned long long)st.indexBytes);
        for (const auto& w : staging.warnings()) std::printf("[strata-stage] warning: %s\n", w.c_str());
        if (const auto* asset = staging.find(path))
            for (const auto& s : asset->skipped) std::printf("[strata-stage] skipped primitive: %s\n", s.reason.c_str());
        std::fflush(stdout);
        ok = ok && st.stagedSources == 1;
    }
    return ok ? 0 : 1;
}
