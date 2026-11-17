// Shared Inspector component inventory for document-backed entities.
// Specialized editable drawers remain in their respective panels until
// a native ECS reflection/writeback registry is available.
#pragma once
#include "EditorEntityAdapter.h"
#include "imgui.h"

namespace aiverse::editor {

inline void drawDocumentComponentInventory(const MapDoc& map,
                                           DocumentEntityRef ref) {
    const auto components = documentComponents(map, ref);
    if (components.empty()) return;
    ImGui::TextDisabled("COMPONENTS  %d", int(components.size()));
    for (const auto& component : components) {
        Json snapshot;
        if (!readDocumentComponent(map, ref, component.kind, snapshot)) continue;
        ImGui::BulletText("%s%s", component.title,
                          component.editable ? "" : "  (read-only)");
    }
    ImGui::Separator();
}

} // namespace aiverse::editor
