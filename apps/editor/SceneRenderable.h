// Genre-independent render submissions for editor Scene View.
// The extractor owns the lifetime of referenced geometry until submit().
#pragma once
#include "../../engine/render/Renderer3D.h"
#include <cstdint>
#include <vector>

namespace aiverse::editor {

struct SceneRenderable {
    uint64_t entity = 0;                 // stable entity reference; 0 = non-entity preview
    render::mat4 world{};
    render::float4 tint{1, 1, 1, 1};
    const render::Mesh* mesh = nullptr;   // editor/debug primitives
    const render::Model* model = nullptr; // authored/skinned geometry
    bool selected = false;
    bool hovered = false;
};

inline void submitSceneRenderables(render::Renderer3D& renderer,
                                   const std::vector<SceneRenderable>& objects) {
    for (const auto& object : objects) {
        if (object.model) {
            render::ModelDraw draw;
            draw.model = object.model;
            draw.world = object.world;
            draw.tint = object.tint;
            if (object.selected) draw.outline = {0.25f, 0.62f, 1.0f, 0.035f};
            else if (object.hovered) draw.outline = {0.75f, 0.8f, 0.85f, 0.025f};
            renderer.drawModel(draw);
        } else if (object.mesh) {
            renderer.drawMesh(*object.mesh, object.world, object.tint);
        }
    }
}

} // namespace aiverse::editor
