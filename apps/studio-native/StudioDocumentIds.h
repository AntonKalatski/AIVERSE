// Stable source KIND identity for authoring document objects; ordinals are
// still provisional until the editor's documents gain stable IDs.
#pragma once
#include "apps/editor/EditorEntityAdapter.h"
#include <string>

namespace aiverse::studio {
inline std::string studioDocumentId(editor::DocumentEntityRef ref) {
    switch (ref.source) {
        case editor::DocumentEntityRef::Source::MapLight:
        case editor::DocumentEntityRef::Source::FpsLight:
            return "light:legacy:"+std::to_string(ref.index);
        case editor::DocumentEntityRef::Source::MapEntity:
            return "entity:" + std::to_string(ref.index);
        case editor::DocumentEntityRef::Source::FpsCollider:
            return "collider:" + std::to_string(ref.index);
        case editor::DocumentEntityRef::Source::FpsSpawn:
            return "spawn:" + std::to_string(ref.index);
    }
    return {};
}
inline std::string studioDocumentId(const editor::MapDoc& map,editor::DocumentEntityRef ref) {
    if(const auto* light=editor::documentLight(map,ref)) {
        const auto id=light->getStr("entityId");if(!id.empty())return "light:"+id;
    }
    return studioDocumentId(ref);
}
} // namespace aiverse::studio
