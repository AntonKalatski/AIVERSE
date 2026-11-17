// Explicit persisted bindings for fields observed in a real ECS snapshot.
// This is deliberately not a generic write into the preview's Simulation:
// the preview is rebuilt from MapDoc and must not be used as storage.
#pragma once
#include "EditorEntityAdapter.h"
#include "EditorWorldEdit.h"
#include <limits>
#include <string>

namespace aiverse::editor {

// A field may be edited only if the selected entity actually exposes the
// component, and a known authoring schema can persist that precise field.
inline bool canWriteAuthoredEcsField(const MapDoc& map, DocumentEntityRef ref,
                                    const std::string& componentId,
                                    const std::string& fieldId) {
    if (!documentMapEntity(map, ref)) return false;
    if (componentId == "Transform" && fieldId == "pos") return true;
    if (componentId == "Owner" && fieldId == "player") return true;
    return false;
}

inline bool writeAuthoredEcsField(MapDoc& map, DocumentEntityRef ref,
                                 const std::string& componentId,
                                 const std::string& fieldId, const Json& value) {
    if (!canWriteAuthoredEcsField(map, ref, componentId, fieldId)) return false;
    if (componentId == "Transform" && fieldId == "pos") {
        if (!value.isArray() || value.size() != 3) return false;
        for (size_t axis = 0; axis < 3; ++axis)
            if (!value[axis].isNumber()) return false;
        DocumentTransform current;
        if (!readDocumentTransform(map, ref, current)) return false;
        const double x = value[0].toDouble(), z = value[2].toDouble();
        if (!std::isfinite(x) || !std::isfinite(z)) return false;
        // Y comes from the real world; the serialized RTS map has no Y slot.
        return setDocumentPosition(map, ref, {x, current.y, z});
    }
    if (componentId == "Owner" && fieldId == "player") {
        if (!value.isNumber()) return false;
        const double v = value.toDouble();
        if (!std::isfinite(v) || v < 0.0 ||
            v > double(map.data.players.size()) ||
            v != std::floor(v) || v > 255.0) return false;
        auto* entity = documentMapEntity(map, ref);
        if (!entity || entity->owner == int(v) ||
            ref.index > size_t(std::numeric_limits<int>::max())) return false;
        worldedit::setOwner(map, {int(ref.index)}, int(v));
        return true;
    }
    return false;
}

} // namespace aiverse::editor
