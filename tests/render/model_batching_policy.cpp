// Engine regression: static batching preserves runtime-mutable subtrees.
// This is intentionally independent from any particular project/genre.
#include "engine/render/Model.h"
#include "engine/render/ModelMerge.h"
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "model batching check failed: %s at %d\\n", #x, __LINE__); std::exit(1); } } while (0)
#include <cstdio>
#include <string>

using namespace aiverse::render;

int main() {
    Model m;
    m.materials.resize(1);
    constexpr int kNodes = 5;
    m.vertices.resize(kNodes * 3);
    m.indices.resize(kNodes * 3);
    const char* names[kNodes] = {"building_wall", "building_roof", "hinged_panel", "panel_surface", "static_sign"};
    for (int n = 0; n < kNodes; ++n) {
        for (int j = 0; j < 3; ++j) {
            const int v = n * 3 + j;
            m.indices[size_t(v)] = uint32_t(v);
            m.vertices[size_t(v)].pos = {float(n * 2 + (j == 1)), float(j == 2), 0};
            m.vertices[size_t(v)].nrm = {0, 0, 1};
        }
        Primitive p;
        p.firstIndex = uint32_t(n * 3);
        p.indexCount = 3;
        p.material = 0;
        MeshDef mesh;
        mesh.name = names[n];
        mesh.primitives.push_back(p);
        m.meshes.push_back(mesh);
        Node node;
        node.name = names[n];
        node.mesh = n;
        node.parent = n == 3 ? 2 : -1;
        node.runtimeMutable = n == 2;
        m.nodes.push_back(node);
    }
    m.finalize();
    auto oldParent = m.nodes[3].parent;
    const auto oldMatrices = m.bindWorld;
    m.batching.mergeRigid = true;
    prepareModelUpload(m);
    CHECK(m.uploadPrepared);
    CHECK(m.nodes.size() == 6);
    CHECK(m.nodes.back().name == "merged");
    CHECK(m.nodes[2].name == "hinged_panel");
    CHECK(m.nodes[3].name == "panel_surface");
    CHECK(m.nodes[3].parent == oldParent);
    for (int n : {2, 3}) {
        CHECK(m.nodes[size_t(n)].mesh >= 0);
        CHECK(m.meshes[size_t(m.nodes[size_t(n)].mesh)].primitives.size() == 1);
        for (int row = 0; row < 16; ++row)
            CHECK(m.bindWorld[size_t(n)].m[row] == oldMatrices[size_t(n)].m[row]);
    }

    Model skip;
    skip.materials.resize(1);
    skip.batching.mergeRigid = false;
    skip.nodes.resize(1);
    skip.nodes[0].name = "untouched";
    skip.vertices.resize(3);
    skip.indices = {0, 1, 2};
    skip.meshes.resize(1);
    skip.meshes[0].primitives.resize(1);
    skip.meshes[0].primitives[0].indexCount = 3;
    skip.nodes[0].mesh = 0;
    skip.finalize();
    prepareModelUpload(skip);
    CHECK(skip.nodes.size() == 1);
    std::puts("engine model batching policy: PASS");
    return 0;
}
