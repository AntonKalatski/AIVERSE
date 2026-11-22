// Offline STRATA staging probe: loads one model the way a geometry scene
// does (prepareModelUpload, prepared root LODs allowed), stages it and
// prints the result, the build time and any warning. Used to make heavy
// assets STRATA-ready without starting the editor.
//   aiverse_strata_stage_probe <model.gltf|glb> [<model>...]
#include "engine/render/Model.h"
#include "engine/render/StrataModelAdapter.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
using namespace aiverse::render;

int main(int argc, char** argv) {
    int bad = 0;
    for (int a = 1; a < argc; ++a) {
        const std::filesystem::path file(argv[a]);
        const auto dir = file.parent_path();
        FileReader read = [&](const std::string& p, std::string& out) {
            for (const auto& f : {std::filesystem::path(p), dir / p}) {
                std::ifstream in(f, std::ios::binary);
                if (!in) continue;
                std::stringstream s; s << in.rdbuf(); out = s.str(); return true;
            }
            return false;
        };
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<std::string> warnings;
        std::unique_ptr<Model> m;
        try { m = loadModel(file.string(), read, &warnings); }
        catch (const std::exception& e) { std::printf("%s: load failed: %s\n", argv[a], e.what()); ++bad; continue; }
        prepareModelUpload(*m);
        const auto t1 = std::chrono::steady_clock::now();
        strata::SceneStaging staging;
        staging.setAllowPreparedRootLods(true);
        staging.setTolerateSourceSeamDefects(true);
        const bool ok = staging.stage(file.string(), *m);
        const auto t2 = std::chrono::steady_clock::now();
        const auto& st = staging.stats();
        size_t tris = 0; for (const auto& n : m->nodes) if (n.mesh >= 0) for (const auto& p : m->meshes[size_t(n.mesh)].primitives) tris += p.indexCount / 3;
        std::printf("%s: %s verts=%zu tris=%zu load=%.0fms stage=%.0fms staged=%u skipped=%u failed=%u nodes=%llu meshlets=%llu skippedPrims=%u\n",
            argv[a], ok ? "STAGED" : "NOT STAGED", m->vertices.size(), tris,
            std::chrono::duration<double, std::milli>(t1 - t0).count(),
            std::chrono::duration<double, std::milli>(t2 - t1).count(),
            st.stagedSources, st.skippedSources, st.failedSources,
            (unsigned long long)st.clusterNodes, (unsigned long long)st.meshlets, st.skippedPrimitives);
        for (const auto& w : staging.warnings()) std::printf("  warning: %s\n", w.c_str());
        if (!ok) ++bad;
    }
    return bad ? 1 : 0;
}
