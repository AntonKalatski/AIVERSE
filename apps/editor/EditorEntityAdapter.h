// Editor-facing document entity adapter. Genre modules map their native
// serialized objects to this identity; rendering and UI do not own selection.
#pragma once
#include "engine/data/Json.h"
#include <cstddef>
#include <cstdint>
#include <limits>

namespace aiverse::editor {

struct DocumentEntityRef {
    enum class Source : uint8_t { MapEntity, FpsCollider, FpsSpawn };
    Source source = Source::MapEntity;
    size_t index = 0;

    // FPS refs use the high bit for spawns; zero is reserved for no entity.
    uint64_t fpsKey() const {
        if (index >= (uint64_t(1) << 63) - 1) return 0;
        return (source == Source::FpsSpawn ? (uint64_t(1) << 63) : 0)
               | (uint64_t(index) + 1);
    }
    static bool fromFpsKey(uint64_t key, DocumentEntityRef& out) {
        if (!key) return false;
        out.source = (key & (uint64_t(1) << 63)) ? Source::FpsSpawn : Source::FpsCollider;
        out.index = size_t((key & ~(uint64_t(1) << 63)) - 1);
        return true;
    }
};

inline const char* fpsEntityCollection(DocumentEntityRef::Source source) {
    return source == DocumentEntityRef::Source::FpsSpawn ? "spawns" : "colliders";
}

// Shared JSON component access: a genre adapter can supply a different
// component store without requiring a separate Inspector selection model.
inline const Json* fpsDocumentEntity(const Json& fps, DocumentEntityRef ref) {
    if (ref.source == DocumentEntityRef::Source::MapEntity) return nullptr;
    const Json& entries = fps[fpsEntityCollection(ref.source)];
    if (!entries.isArray() || ref.index >= entries.size()) return nullptr;
    return &entries[ref.index];
}
inline Json* fpsDocumentEntity(Json& fps, DocumentEntityRef ref) {
    if (ref.source == DocumentEntityRef::Source::MapEntity) return nullptr;
    const char* collection = fpsEntityCollection(ref.source);
    if (!fps[collection].isArray() || ref.index >= fps[collection].size()) return nullptr;
    return &fps.member(collection)[ref.index];
}

} // namespace aiverse::editor
