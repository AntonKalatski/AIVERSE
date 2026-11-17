// Unified *view* of runtime objects for the editor, irrespective of genre.
// This intentionally distinguishes real ECS Entities (RTS) from deterministic
// World resource records (FPS): changing the editor view must not invent an ECS
// identity or silently migrate FPS snapshot/rollback serialization.
#pragma once
#include "Editor.h"
#include <string>
#include <vector>

namespace aiverse::editor {

struct EditorRuntimeEntity {
    Selection selection;
    std::string title;
    render::float3 position{};
    bool hasPosition = false;
    bool ecsEntity = false;  // true for RTS World::Entity, false for FPS State actor record
    Json fields;
};

inline std::vector<EditorRuntimeEntity> enumerateEditorRuntimeEntities(Editor& ed) {
    std::vector<EditorRuntimeEntity> result;
    if (ed.ws().project.isFps()) {
        const Json& runtime = ed.fpsRuntimeSnapshot();
        const Json& actors = runtime["actors"];
        if (!actors.isArray()) return result;
        result.reserve(actors.size());
        for (size_t i = 0; i < actors.size(); ++i) {
            const Json& actor = actors[i];
            EditorRuntimeEntity info;
            info.selection.kind = Selection::FpsActor;
            info.selection.index = int(i);
            info.title = "Actor #" + std::to_string(
                actor["slot"].integer(int64_t(i + 1)));
            info.fields = actor;
            const Json& pos = actor["position"];
            if (pos.isArray() && pos.size() == 3 &&
                pos[0].isNumber() && pos[1].isNumber() && pos[2].isNumber()) {
                info.position = {float(pos[0].toDouble()),
                                 float(pos[1].toDouble()),
                                 float(pos[2].toDouble())};
                info.hasPosition = std::isfinite(info.position.x) &&
                                   std::isfinite(info.position.y) &&
                                   std::isfinite(info.position.z);
            }
            result.push_back(std::move(info));
        }
        return result;
    }

    const WorldModel& world = ed.world();
    result.reserve(world.rows.size());
    for (const WorldRow& row : world.rows) {
        EditorRuntimeEntity info;
        info.selection = ed.worldSelection(row);
        info.title = worldRowName(world, ed.rules(),
                                  &ed.doc().data, row);
        info.ecsEntity = true;
        info.position = {row.x, row.y, row.z};
        info.hasPosition = row.hasPos;
        result.push_back(std::move(info));
    }
    return result;
}

} // namespace aiverse::editor
