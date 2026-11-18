#include "engine/render/TerraWeaveModelAdapter.h"
#include <cassert>
#include <cmath>
#include <cstdint>

using namespace aiverse::terraweave;

int main() {
    TreeProfile oak;
    oak.speciesId = 1001;
    oak.primaryBranches = 24;
    const auto model = makeArborModel(oak, 0x123456789ULL);
    assert(model);
    assert(model->nodes.size() == 1 && model->nodes[0].mesh == 0);
    assert(model->materials.size() == 2);
    assert(model->meshes.size() == 3);
    assert(model->meshes[0].lods.size() == 2);
    assert(model->meshes[0].lods[0].mesh == 1);
    assert(model->meshes[0].lods[1].mesh == 2);
    assert(!model->batching.autoLod);
    assert(model->boundsMax.y > 5.0f && model->boundsMax.y < 30.0f);
    assert(model->boundsMin.y >= -1.0f);
    assert(model->materials[1].doubleSided);
    assert(model->materials[1].alpha == aiverse::render::AlphaMode::Opaque);
    assert(model->vertices.size() > 100);
    assert(model->indices.size() > 100);
    assert(model->order.size() == 1);
    for (const auto& mesh : model->meshes) {
        assert(mesh.primitives.size() == 2);
        for (const auto& primitive : mesh.primitives) {
            assert(primitive.indexCount > 0 && primitive.indexCount % 3 == 0);
            assert(primitive.firstIndex + primitive.indexCount <= model->indices.size());
            for (uint32_t k = primitive.firstIndex;
                 k < primitive.firstIndex + primitive.indexCount; ++k)
                assert(model->indices[k] < model->vertices.size());
        }
    }
    for (const auto& v : model->vertices) {
        assert(std::isfinite(v.pos.x));
        assert(std::isfinite(v.pos.y));
        assert(std::isfinite(v.pos.z));
    }
    const auto again = makeArborModel(oak, 0x123456789ULL);
    assert(model->vertices.size() == again->vertices.size());
    assert(model->indices == again->indices);
}
