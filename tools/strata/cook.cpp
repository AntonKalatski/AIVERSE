// STRATA cook: builds the cluster hierarchy of every model a geometry scene
// uses, once, on the developer machine, and writes it next to the content
// (StrataCooked.h), plus the index-page archives used for disk streaming.
// At run time the scene loads these instead of building (strata.cooked).
//
//   aiverse_strata_cook --project <dir> --scene <scene.json> [--out <dir>]
//
// --out defaults to <project>/<strata.cooked of the scene> (or strata-cooked).
// Exit code 0 only when every model was cooked.
#include "engine/data/Json.h"
#include "engine/render/Model.h"
#include "engine/render/StrataCooked.h"
#include "engine/render/StrataDiskPages.h"
#include "engine/render/StrataModelAdapter.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
using namespace aiverse;
using namespace aiverse::render;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    std::string project, scene, out;
    for (int a = 1; a + 1 < argc; a += 2) {
        if (!std::strcmp(argv[a], "--project")) project = argv[a + 1];
        else if (!std::strcmp(argv[a], "--scene")) scene = argv[a + 1];
        else if (!std::strcmp(argv[a], "--out")) out = argv[a + 1];
    }
    if (project.empty() || scene.empty()) {
        std::fprintf(stderr, "usage: aiverse_strata_cook --project <dir> --scene <scene.json> [--out <dir>]\n");
        return 2;
    }
    const fs::path root(project);
    FileReader read = [&](const std::string& p, std::string& text) {
        std::ifstream in(root / p, std::ios::binary);
        if (!in) return false;
        std::stringstream s; s << in.rdbuf(); text = s.str(); return true;
    };
    std::string text;
    if (!read(scene, text)) { std::fprintf(stderr, "scene not found: %s\n", scene.c_str()); return 2; }
    Json json = Json::parse(text);
    if (out.empty()) {
        std::string dir = json["strata"].getStr("cooked");
        out = (root / (dir.empty() ? std::string("strata-cooked") : dir)).string();
    }
    std::set<std::string> files;
    for (auto& i : json["instances"].items()) files.insert(i.getStr("model"));
    int bad = 0;
    strata::SceneStaging staging;
    staging.setAllowPreparedRootLods(true);
    staging.setTolerateSourceSeamDefects(true);
    strata::CookIo io;
    io.writeDirectory = out;
    staging.setCookIo(io);
    for (const auto& file : files) {
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<std::string> warnings;
        std::unique_ptr<Model> m;
        try { m = loadModel(file, read, &warnings); }
        catch (const std::exception& e) { std::printf("%s: load failed: %s\n", file.c_str(), e.what()); ++bad; continue; }
        // Exactly the preparation GeometryScene does before staging.
        prepareModelUpload(*m);
        const auto t1 = std::chrono::steady_clock::now();
        const uint32_t before = staging.stats().cookedWrittenSources;
        const bool ok = staging.stage(file, *m) && staging.stats().cookedWrittenSources > before;
        const auto t2 = std::chrono::steady_clock::now();
        size_t tris = 0;
        for (const auto& n : m->nodes) if (n.mesh >= 0) for (const auto& p : m->meshes[size_t(n.mesh)].primitives) tris += p.indexCount / 3;
        const auto cooked = fs::path(out) / strata::cookedFilename(file);
        std::error_code ec;
        std::printf("%s: %s tris=%zu load=%.0fms build=%.0fms -> %s (%.1f MB)\n", file.c_str(),
            ok ? "COOKED" : "NOT COOKED", tris,
            std::chrono::duration<double, std::milli>(t1 - t0).count(),
            std::chrono::duration<double, std::milli>(t2 - t1).count(),
            cooked.filename().string().c_str(), ok ? double(fs::file_size(cooked, ec)) / 1048576.0 : 0.0);
        if (!ok) ++bad;
    }
    for (const auto& w : staging.warnings()) std::printf("  warning: %s\n", w.c_str());
    // Index-page archives for disk streaming (Metal/Vulkan incremental residency).
    std::vector<std::string> failures;
    if (!staging.writeDiskArchives(out, &failures)) {
        for (const auto& f : failures) std::printf("  page archive failed: %s\n", f.c_str());
        ++bad;
    } else {
        std::printf("index-page archives written to %s\n", out.c_str());
    }
    return bad ? 1 : 0;
}
