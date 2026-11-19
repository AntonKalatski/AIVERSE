#include "RtsPortrait.h"
#include <algorithm>
#include <cmath>

namespace aiverse::game {
using namespace render;

bool Portrait::init(gfx::Device& device, int size) {
    return preview_.init(device, size);
}

namespace {
// a unit sphere / box with UVs that show a texture the way it tiles (u around, v top to bottom)
std::unique_ptr<Model> previewShape(int shape) {
    auto m = std::make_unique<Model>();
    m->path = shape == 0 ? "preview:sphere" : "preview:box";
    auto add = [&](float3 p, float3 n, float2 uv) { ModelVertex v; v.pos = p; v.nrm = n; v.uv = uv; m->vertices.push_back(v); };
    if (shape == 0) {
        const int rings = 32, segs = 64;
        for (int r = 0; r <= rings; ++r)
            for (int sg = 0; sg <= segs; ++sg) {
                const float th = float(r) / float(rings) * 3.14159265f, ph = float(sg) / float(segs) * 6.2831853f;
                const float3 n{std::sin(th) * std::cos(ph), std::cos(th), -std::sin(th) * std::sin(ph)};
                add(n * 0.5f + float3{0, 0.5f, 0}, n, {float(sg) / float(segs) * 2, float(r) / float(rings)});
            }
        for (int r = 0; r < rings; ++r)
            for (int sg = 0; sg < segs; ++sg) {
                const uint32_t a = uint32_t(r * (segs + 1) + sg), b = a + uint32_t(segs + 1);
                for (uint32_t i : {a, b, a + 1, a + 1, b, b + 1}) m->indices.push_back(i);
            }
    } else {
        const float3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (const float3& n : dirs) {
            const float3 up = std::fabs(n.y) > 0.5f ? float3{0, 0, -1} : float3{0, 1, 0};
            const float3 right = cross(up, n);
            const uint32_t base = uint32_t(m->vertices.size());
            for (int k = 0; k < 4; ++k) {
                const float su = (k == 1 || k == 2) ? 1.0f : -1.0f, sv = k >= 2 ? 1.0f : -1.0f;
                add((n + right * su + up * sv) * 0.4f + float3{0, 0.4f, 0}, n, {su * 0.5f + 0.5f, 0.5f - sv * 0.5f});
            }
            for (uint32_t i : {0u, 2u, 1u, 0u, 3u, 2u}) m->indices.push_back(base + i);
        }
    }
    generateTangents(m->vertices, m->indices, 0, uint32_t(m->indices.size()));
    Primitive p;
    p.indexCount = uint32_t(m->indices.size());
    p.vertexCount = uint32_t(m->vertices.size());
    MeshDef md;
    md.name = "preview";
    md.primitives.push_back(p);
    m->meshes.push_back(md);
    Node n;
    n.name = "preview";
    n.mesh = 0;
    m->nodes.push_back(n);
    Material mt;
    mt.name = "preview";
    m->materials.push_back(mt);
    m->finalize();
    return m;
}
} // namespace

void Portrait::renderMaterial(RtsView& view, const Json& material, int shape, double time) {
    gfx::Device* device = preview_.device();
    if (!device || !preview_.texture().valid()) return;
    shape = shape == 1 ? 1 : 0;
    if (!shapes_[shape]) {
        shapes_[shape] = previewShape(shape);
        uploadModel(*device, *shapes_[shape], true);
    }
    Model& model = *shapes_[shape];
    static UnitType previewType;
    previewType.id = "material preview";
    materials_ = model.materials;
    view.applyMaterial(*device, previewType, model, materials_[0], material);
    Camera camera;
    camera.target = {0, 0.45f, 0};
    camera.distance = 2.1f;
    camera.minDistance = 0.1f;
    camera.pitch = 0.42f;
    camera.yaw = 0.6f;
    camera.fovY = 0.6f;
    preview_.capture(camera, [&](Renderer3D& renderer) {
        ModelDraw draw;
        draw.model = &model;
        draw.world = mat4::rotateY(float(time) * 0.4f);
        draw.materials = &materials_;
        draw.team = {0.8f, 0.1f, 0.08f};
        renderer.drawModel(draw);
    });
}

void Portrait::renderPose(const Model& model, const Pose& pose, float yaw) {
    preview_.renderPose(model, pose, yaw);
}

void Portrait::shutdown() {
    gfx::Device* device = preview_.device();
    if (device)
        for (auto& shape : shapes_)
            if (shape) { releaseModel(*device, *shape); shape.reset(); }
    materials_.clear();
    preview_.shutdown();
}

void Portrait::render(RtsView& view, uint16_t type, uint8_t owner, double time, float yaw) {
    if (!preview_.device() || !preview_.texture().valid()) return;
    float radius = 0.8f, height = 1.0f;
    if (const Model* model = view.modelOf(type)) {
        const float3 e = model->boundsMax - model->boundsMin;
        height = std::max(0.3f, std::min(e.y, 4.0f));
        radius = std::max({e.x, e.y, e.z, 0.3f}) * 0.6f;
    }
    radius = std::max(radius, view.selectionRadius(type));
    Camera camera;
    camera.target = {0, std::min(height, radius * 1.4f) * 0.45f, 0};
    camera.distance = radius * 3.2f;
    camera.minDistance = 0.1f;
    camera.pitch = 0.32f;
    camera.yaw = 0.5f;
    camera.fovY = 0.6f;
    preview_.capture(camera, [&](Renderer3D& renderer) {
        view.drawPreview(renderer, type, owner, time,
                         yaw < 0 ? float(time) * 0.6f : yaw);
    });
}

} // namespace aiverse::game
