// Central conversion between authoring scene source IDs and Editor selections.
// Existing Selection::Entity and Selection::FpsObject representations remain
// compatible with other editor tools during the universal-entity migration.
#pragma once
#include "Editor.h"
#include "EditorEntityAdapter.h"
#include <limits>

namespace aiverse::editor {

inline Selection selectionForDocumentEntity(DocumentEntityRef ref) {
    Selection selection;
    if (ref.source == DocumentEntityRef::Source::MapEntity) {
        if (ref.index > size_t(std::numeric_limits<int>::max())) return selection;
        selection.kind = Selection::Entity;
        selection.index = int(ref.index);
    } else {
        const uint64_t key = ref.fpsKey();
        if (!key) return selection;
        selection.kind = Selection::FpsObject;
        selection.ref = key;
    }
    return selection;
}

inline bool documentEntityFromSelection(const Selection& selection,
                                        DocumentEntityRef& out) {
    if (selection.kind == Selection::Entity && selection.index >= 0) {
        out = DocumentEntityRef::mapEntity(size_t(selection.index));
        return true;
    }
    if (selection.kind == Selection::FpsObject)
        return DocumentEntityRef::fromFpsKey(selection.ref, out);
    return false;
}

} // namespace aiverse::editor
