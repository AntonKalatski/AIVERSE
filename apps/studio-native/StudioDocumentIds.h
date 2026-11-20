// Stable source KIND identity for authoring document objects; ordinals are
// still provisional until the editor's documents gain stable IDs.
#pragma once
#include "apps/editor/EditorEntityAdapter.h"
#include <string>

namespace aiverse::studio {
inline std::string studioDocumentId(editor::DocumentEntityRef ref) {
    switch (ref.source) {
        case editor::DocumentEntityRef::Source::MapEntity:
            return "entity:" + std::to_string(ref.index);
        case editor::DocumentEntityRef::Source::FpsCollider:
            return "collider:" + std::to_string(ref.index);
        case editor::DocumentEntityRef::Source::FpsSpawn:
            return "spawn:" + std::to_string(ref.index);
    }
    return {};
}
} // namespace aiverse::studio
