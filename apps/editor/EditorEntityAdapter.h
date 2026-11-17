// Editor-facing document entity adapter. Genre modules map their native
// serialized objects to this identity; rendering and UI do not own selection.
#pragma once
#include "core/MapDoc.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <cmath>
#include <vector>
#include <set>

namespace aiverse::editor {

struct DocumentEntityRef {
    enum class Source : uint8_t { MapEntity, FpsCollider, FpsSpawn };
    Source source = Source::MapEntity;
    size_t index = 0;

    static DocumentEntityRef mapEntity(size_t i) { return {Source::MapEntity, i}; }
    static DocumentEntityRef fpsCollider(size_t i) { return {Source::FpsCollider, i}; }
    static DocumentEntityRef fpsSpawn(size_t i) { return {Source::FpsSpawn, i}; }

    // FPS refs use the high bit for spawns; zero is reserved for no entity.
    uint64_t fpsKey() const {
        if (source == Source::MapEntity) return 0;
        if (uint64_t(index) >= (uint64_t(1) << 63) - 1) return 0;
        return (source == Source::FpsSpawn ? (uint64_t(1) << 63) : 0)
               | (uint64_t(index) + 1);
    }
    static bool fromFpsKey(uint64_t key, DocumentEntityRef& out) {
        if (!key || (key & ~(uint64_t(1) << 63)) == 0) return false;
        out.source = (key & (uint64_t(1) << 63)) ? Source::FpsSpawn : Source::FpsCollider;
        out.index = size_t((key & ~(uint64_t(1) << 63)) - 1);
        return true;
    }
};

// RTS map objects and FPS JSON objects resolve through the same document
// reference, without changing either game's existing serialization format.
inline const DocEntity* documentMapEntity(const MapDoc& map, DocumentEntityRef ref) {
    if (ref.source != DocumentEntityRef::Source::MapEntity ||
        ref.index >= map.data.entities.size()) return nullptr;
    return &map.data.entities[ref.index];
}
inline DocEntity* documentMapEntity(MapDoc& map, DocumentEntityRef ref) {
    if (ref.source != DocumentEntityRef::Source::MapEntity ||
        ref.index >= map.data.entities.size()) return nullptr;
    return &map.data.entities[ref.index];
}

inline const char* fpsEntityCollection(DocumentEntityRef::Source source) {
    return source == DocumentEntityRef::Source::FpsSpawn ? "spawns" : "colliders";
}

// Shared JSON component access: a genre adapter can supply a different
// component store without requiring a separate Inspector selection model.
inline const Json* fpsDocumentEntity(const Json& fps, DocumentEntityRef ref) {
    if (ref.source != DocumentEntityRef::Source::FpsCollider &&
        ref.source != DocumentEntityRef::Source::FpsSpawn) return nullptr;
    const Json& entries = fps[fpsEntityCollection(ref.source)];
    if (!entries.isArray() || ref.index >= entries.size()) return nullptr;
    return &entries[ref.index];
}
inline Json* fpsDocumentEntity(Json& fps, DocumentEntityRef ref) {
    if (ref.source != DocumentEntityRef::Source::FpsCollider &&
        ref.source != DocumentEntityRef::Source::FpsSpawn) return nullptr;
    const char* collection = fpsEntityCollection(ref.source);
    if (!fps[collection].isArray() || ref.index >= fps[collection].size()) return nullptr;
    return &fps.member(collection).at(ref.index);
}

struct DocumentTransform {
    double x = 0, y = 0, z = 0;
};

// All genre-specific document layouts expose position through this API.
// The returned value is a copy, never a pointer into a mutable JSON array.
inline bool readDocumentTransform(const MapDoc& map, DocumentEntityRef ref,
                                  DocumentTransform& out) {
    if (ref.source == DocumentEntityRef::Source::MapEntity) {
        const DocEntity* entity = documentMapEntity(map, ref);
        if (!entity) return false;
        if (!std::isfinite(entity->x) || !std::isfinite(entity->z)) return false;
        out = {entity->x, 0.0, entity->z};
    } else {
        const Json* object = fpsDocumentEntity(map.data.fps, ref);
        if (!object) return false;
        if (ref.source == DocumentEntityRef::Source::FpsSpawn) {
            if (!object->isArray() || object->size() < 3 ||
                !(*object)[0].isNumber() || !(*object)[1].isNumber() || !(*object)[2].isNumber()) return false;
            out = {(*object)[0].toDouble(), (*object)[1].toDouble(), (*object)[2].toDouble()};
        } else {
            const Json& lo = (*object)["min"], &hi = (*object)["max"];
            if (!lo.isArray() || !hi.isArray() || lo.size() < 3 || hi.size() < 3) return false;
            for (size_t axis = 0; axis < 3; ++axis) {
                if (!lo[axis].isNumber() || !hi[axis].isNumber()) return false;
                const double minimum = lo[axis].toDouble(), maximum = hi[axis].toDouble();
                if (!std::isfinite(minimum) || !std::isfinite(maximum) ||
                    minimum >= maximum) return false;
            }
            // Midpoint without summing two potentially huge finite endpoints.
            // (lo + hi) / 2 can overflow even when the midpoint is finite.
            out = {lo[0].toDouble() * 0.5 + hi[0].toDouble() * 0.5,
                   lo[1].toDouble() * 0.5 + hi[1].toDouble() * 0.5,
                   lo[2].toDouble() * 0.5 + hi[2].toDouble() * 0.5};
        }
    }
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

inline bool translateDocumentEntity(MapDoc& map, DocumentEntityRef ref,
                                    DocumentTransform delta, bool markDirty = true) {
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(delta.z))
        return false;
    DocumentTransform before;
    if (!readDocumentTransform(map, ref, before)) return false;
    const DocumentTransform after{before.x + delta.x, before.y + delta.y, before.z + delta.z};
    if (!std::isfinite(after.x) || !std::isfinite(after.y) || !std::isfinite(after.z))
        return false;
    if (delta.x == 0 && delta.y == 0 && delta.z == 0) return false;
    if (ref.source == DocumentEntityRef::Source::MapEntity) {
        // RTS maps are 2D in their serialized format. Never silently discard Y.
        if (delta.y != 0) return false;
        DocEntity* entity = documentMapEntity(map, ref);
        if (!entity) return false;
        const float x = float(after.x), z = float(after.z);
        if (!std::isfinite(x) || !std::isfinite(z)) return false;
        entity->x = x;
        entity->z = z;
        if (entity->at.isObject()) {
            entity->at.set("x", Json::number(double(x), 3));
            entity->at.set("y", Json::number(double(z), 3));
        }
    } else {
        Json* entity = fpsDocumentEntity(map.data.fps, ref);
        if (!entity) return false;
        auto shifted = [&](const Json& xyz, Json& result) {
            if (!xyz.isArray() || xyz.size() < 3) return false;
            for (size_t i = 0; i < 3; ++i)
                if (!xyz[i].isNumber()) return false;
            const double coords[3] = {xyz[0].toDouble() + delta.x,
                                      xyz[1].toDouble() + delta.y,
                                      xyz[2].toDouble() + delta.z};
            result = Json::array();
            for (double value : coords) {
                if (!std::isfinite(value)) return false;
                result.push(Json::number(value, 3));
            }
            return true;
        };
        if (ref.source == DocumentEntityRef::Source::FpsSpawn) {
            Json moved;
            if (!shifted(*entity, moved)) return false;
            *entity = moved;
        } else {
            const Json& originalMin = (*entity)["min"];
            const Json& originalMax = (*entity)["max"];
            if (!originalMin.isArray() || !originalMax.isArray() ||
                originalMin.size() < 3 || originalMax.size() < 3) return false;
            for (size_t axis = 0; axis < 3; ++axis) {
                if (!originalMin[axis].isNumber() || !originalMax[axis].isNumber() ||
                    !std::isfinite(originalMin[axis].toDouble()) ||
                    !std::isfinite(originalMax[axis].toDouble()) ||
                    !(originalMin[axis].toDouble() < originalMax[axis].toDouble()))
                    return false;
            }
            Json lo, hi;
            if (!shifted(originalMin, lo) || !shifted(originalMax, hi))
                return false;
            entity->member("min") = lo;
            entity->member("max") = hi;
        }
    }
    if (markDirty) map.touched();
    return true;
}

inline bool setDocumentPosition(MapDoc& map, DocumentEntityRef ref,
                                DocumentTransform position) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z)) return false;
    DocumentTransform current;
    if (!readDocumentTransform(map, ref, current)) return false;
    return translateDocumentEntity(map, ref,
        {position.x - current.x, position.y - current.y, position.z - current.z});
}

inline bool setDocumentColliderBounds(MapDoc& map, DocumentEntityRef ref,
                                      DocumentTransform minimum,
                                      DocumentTransform maximum) {
    if (ref.source != DocumentEntityRef::Source::FpsCollider) return false;
    const double lo[3] = {minimum.x, minimum.y, minimum.z};
    const double hi[3] = {maximum.x, maximum.y, maximum.z};
    for (size_t axis = 0; axis < 3; ++axis)
        if (!std::isfinite(lo[axis]) || !std::isfinite(hi[axis]) ||
            !(lo[axis] < hi[axis])) return false;
    Json* collider = fpsDocumentEntity(map.data.fps, ref);
    if (!collider || !(*collider)["min"].isArray() ||
        !(*collider)["max"].isArray() ||
        (*collider)["min"].size() < 3 || (*collider)["max"].size() < 3)
        return false;
    Json minJson = Json::array(), maxJson = Json::array();
    for (size_t axis = 0; axis < 3; ++axis) {
        minJson.push(Json::number(lo[axis], 3));
        maxJson.push(Json::number(hi[axis], 3));
    }
    if ((*collider)["min"] == minJson && (*collider)["max"] == maxJson)
        return false;
    collider->member("min") = minJson;
    collider->member("max") = maxJson;
    map.touched();
    return true;
}

inline bool translateDocumentEntities(MapDoc& map,
                                      const std::vector<DocumentEntityRef>& refs,
                                      DocumentTransform delta) {
    if (refs.empty() || !std::isfinite(delta.x) || !std::isfinite(delta.y) ||
        !std::isfinite(delta.z) ||
        (delta.x == 0 && delta.y == 0 && delta.z == 0)) return false;
    // A selection may contain duplicate references; move each source once.
    std::set<std::pair<uint8_t, size_t>> seen;
    std::vector<DocumentEntityRef> unique;
    unique.reserve(refs.size());
    for (const auto& ref : refs)
        if (seen.insert({uint8_t(ref.source), ref.index}).second) unique.push_back(ref);
    // Validate the entire selection before modifying the map. No partial moves.
    for (const auto& ref : unique) {
        DocumentTransform current;
        if (!readDocumentTransform(map, ref, current)) return false;
        const DocumentTransform next{current.x + delta.x, current.y + delta.y,
                                     current.z + delta.z};
        if (!std::isfinite(next.x) || !std::isfinite(next.y) || !std::isfinite(next.z))
            return false;
        if (ref.source == DocumentEntityRef::Source::MapEntity) {
            if (delta.y != 0 || !std::isfinite(float(next.x)) ||
                !std::isfinite(float(next.z))) return false;
        } else {
            const Json* object = fpsDocumentEntity(map.data.fps, ref);
            if (!object) return false;
            auto validCoords = [&](const Json& coords) {
                if (!coords.isArray() || coords.size() < 3) return false;
                const double offsets[3] = {delta.x, delta.y, delta.z};
                for (size_t axis = 0; axis < 3; ++axis) {
                    if (!coords[axis].isNumber()) return false;
                    const double value = coords[axis].toDouble();
                    if (!std::isfinite(value) ||
                        !std::isfinite(value + offsets[axis])) return false;
                }
                return true;
            };
            if (ref.source == DocumentEntityRef::Source::FpsSpawn) {
                if (!validCoords(*object)) return false;
            } else {
                const Json& lo = (*object)["min"];
                const Json& hi = (*object)["max"];
                if (!validCoords(lo) || !validCoords(hi)) return false;
                for (size_t axis = 0; axis < 3; ++axis)
                    if (lo[axis].toDouble() >= hi[axis].toDouble()) return false;
            }
        }
    }
    bool moved = false;
    for (const auto& ref : unique)
        moved = translateDocumentEntity(map, ref, delta, false) || moved;
    if (moved) map.touched();
    return moved;
}

enum class DocumentComponentKind : uint8_t {
    Transform, Renderable, Collider, Spawn, Gameplay
};
struct DocumentComponentDescriptor {
    DocumentComponentKind kind;
    const char* id;
    const char* title;
    bool editable;
};

// Editor-facing component inventory. This describes document-backed data,
// not live ECS pools; runtime reflection/writeback is a separate next stage.
inline std::vector<DocumentComponentDescriptor> documentComponents(
    const MapDoc& map, DocumentEntityRef ref) {
    std::vector<DocumentComponentDescriptor> components;
    DocumentTransform transform;
    if (!readDocumentTransform(map, ref, transform)) return components;
    components.push_back({DocumentComponentKind::Transform, "Transform", "Transform", true});
    switch (ref.source) {
    case DocumentEntityRef::Source::MapEntity: {
        const DocEntity* entity = documentMapEntity(map, ref);
        if (!entity) return {};
        components.push_back({DocumentComponentKind::Renderable, "Renderable", "Renderable", false});
        components.push_back({DocumentComponentKind::Gameplay, "RtsObject", "RTS Object", false});
        break;
    }
    case DocumentEntityRef::Source::FpsCollider:
        components.push_back({DocumentComponentKind::Collider, "BoxCollider", "Box Collider", true});
        break;
    case DocumentEntityRef::Source::FpsSpawn:
        components.push_back({DocumentComponentKind::Spawn, "SpawnPoint", "Spawn Point", true});
        break;
    }
    return components;
}

inline bool readDocumentComponent(const MapDoc& map, DocumentEntityRef ref,
                                  DocumentComponentKind kind, Json& result) {
    DocumentTransform position;
    if (!readDocumentTransform(map, ref, position)) return false;
    result = Json::object();
    if (kind == DocumentComponentKind::Transform) {
        Json xyz = Json::array();
        xyz.push(Json::number(position.x, 3));
        xyz.push(Json::number(position.y, 3));
        xyz.push(Json::number(position.z, 3));
        result.set("position", xyz);
        return true;
    }
    if (ref.source == DocumentEntityRef::Source::MapEntity) {
        const DocEntity* entity = documentMapEntity(map, ref);
        if (!entity) return false;
        if (kind == DocumentComponentKind::Renderable) {
            result.set("type", Json::string(entity->type));
            return true;
        }
        if (kind == DocumentComponentKind::Gameplay) {
            result.set("type", Json::string(entity->type));
            result.set("owner", Json::number(int64_t(entity->owner)));
            return true;
        }
        return false;
    }
    const Json* object = fpsDocumentEntity(map.data.fps, ref);
    if (!object) return false;
    if (kind == DocumentComponentKind::Collider &&
        ref.source == DocumentEntityRef::Source::FpsCollider) {
        result.set("min", (*object)["min"]);
        result.set("max", (*object)["max"]);
        return true;
    }
    if (kind == DocumentComponentKind::Spawn &&
        ref.source == DocumentEntityRef::Source::FpsSpawn) {
        result.set("position", *object);
        return true;
    }
    return false;
}

inline bool writeDocumentComponent(MapDoc& map, DocumentEntityRef ref,
                                   DocumentComponentKind kind, const Json& value) {
    if (!value.isObject()) return false;
    auto readVec3 = [](const Json& array, DocumentTransform& out) {
        if (!array.isArray() || array.size() != 3) return false;
        for (size_t axis = 0; axis < 3; ++axis)
            if (!array[axis].isNumber() || !std::isfinite(array[axis].toDouble()))
                return false;
        out = {array[0].toDouble(), array[1].toDouble(), array[2].toDouble()};
        return true;
    };
    if (kind == DocumentComponentKind::Transform) {
        DocumentTransform position;
        return readVec3(value["position"], position) &&
               setDocumentPosition(map, ref, position);
    }
    if (kind == DocumentComponentKind::Collider &&
        ref.source == DocumentEntityRef::Source::FpsCollider) {
        DocumentTransform lo, hi;
        return readVec3(value["min"], lo) && readVec3(value["max"], hi) &&
               setDocumentColliderBounds(map, ref, lo, hi);
    }
    if (kind == DocumentComponentKind::Spawn &&
        ref.source == DocumentEntityRef::Source::FpsSpawn) {
        DocumentTransform position;
        return readVec3(value["position"], position) &&
               setDocumentPosition(map, ref, position);
    }
    // Renderable and RTS gameplay are deliberately read-only until their
    // runtime/schema writeback contract is implemented and tested.
    return false;
}

} // namespace aiverse::editor
