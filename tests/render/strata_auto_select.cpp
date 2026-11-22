// Engine regression: strata.select auto keeps foliage, small and many-copy
// assets on the LOD path and sends large unique opaque meshes to STRATA.
#include "engine/render/StrataAutoSelect.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "strata auto select failed: %s at %d\n", #x, __LINE__); std::exit(1); } } while (0)
using namespace aiverse::render;

static Model mesh(uint32_t tris, AlphaMode alpha, bool lods) {
    Model m;
    Material mat; mat.alpha = alpha; m.materials.push_back(mat);
    MeshDef d; Primitive p; p.indexCount = tris * 3; p.material = 0; d.primitives.push_back(p);
    m.meshes.push_back(d);
    if (lods) { MeshDef c = d; c.primitives[0].indexCount = tris; m.meshes.push_back(c); m.meshes[0].lods.push_back(ModelLod{1, 64.f}); }
    Node n; n.mesh = 0; m.nodes.push_back(n);
    return m;
}

int main() {
    std::string why;
    // Photogrammetry castle: 730k opaque triangles, one placement.
    CHECK(strataPreferred(strataSourceFacts(mesh(730000, AlphaMode::Opaque, false), 1), {}, &why));
    // Tree: 20k triangles, alpha-cut leaves, authored chain, hundreds of copies.
    {
        Model tree = mesh(6000, AlphaMode::Opaque, true);
        Primitive leaves; leaves.indexCount = 14000 * 3; leaves.material = 1;
        Material cut; cut.alpha = AlphaMode::Mask; tree.materials.push_back(cut);
        tree.meshes[0].primitives.push_back(leaves);
        CHECK(!strataPreferred(strataSourceFacts(tree, 900), {}, &why));
        CHECK(why.find("alpha") != std::string::npos);
    }
    // Rock: small.
    CHECK(!strataPreferred(strataSourceFacts(mesh(1280, AlphaMode::Opaque, true), 40), {}, &why));
    // Medium building with 20 copies: instancing.
    CHECK(!strataPreferred(strataSourceFacts(mesh(120000, AlphaMode::Opaque, false), 20), {}, &why));
    CHECK(why.find("copies") != std::string::npos);
    // Same building once, with an authored chain: LODs; without: STRATA only if large.
    CHECK(!strataPreferred(strataSourceFacts(mesh(120000, AlphaMode::Opaque, true), 1), {}, &why));
    CHECK(strataPreferred(strataSourceFacts(mesh(400000, AlphaMode::Opaque, true), 1), {}, &why));
    // Threshold override.
    StrataAutoSelectConfig low; low.minTriangles = 1000;
    CHECK(strataPreferred(strataSourceFacts(mesh(5000, AlphaMode::Opaque, false), 1), low, &why));
    std::printf("strata auto select: ok\n");
    return 0;
}
