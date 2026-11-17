// Unified *view* of runtime objects for the editor, irrespective of genre.
// Runtime RTS rows and FPS Actor/Projectile/Loot/Vehicle previews now expose
// real ECS entity components. FPS still mirrors its authoritative World State
// vectors into ECS until the gameplay/netcode migration is completed.
#pragma once
#include "Editor.h"
#include <string>
#include <cmath>
#include <utility>
#include <vector>

namespace aiverse::editor {

struct EditorRuntimeEntity {
    Selection selection;
    std::string title;
    render::float3 position{};
    bool hasPosition = false;
    bool ecsEntity = false;  // true for real ECS entities from RTS and FPS worlds
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
            info.ecsEntity = actor["ecsEntity"].isNumber();
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
        const Json& objects = runtime["objects"];
        if (objects.isArray()) for (const auto& object : objects.items()) {
            if (!object["key"].isNumber()) continue;
            EditorRuntimeEntity info;
            info.selection.kind = Selection::FpsRuntimeObject;
            info.selection.ref = uint64_t(object["key"].integer());
            info.title = object.getStr("title", "FPS Object");
            info.fields = object;
            info.ecsEntity = object["ecsEntity"].isNumber();
            const Json& pos = object["position"];
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
