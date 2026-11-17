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

inline bool drawDocumentPositionField(MapDoc& map, DocumentEntityRef ref,
                                      const char* label = "Position XYZ") {
    DocumentTransform position;
    if (!readDocumentTransform(map, ref, position)) return false;
    float xyz[3] = {float(position.x), float(position.y), float(position.z)};
    ImGui::PushID(label);
    const bool planar = ref.source == DocumentEntityRef::Source::MapEntity;
    ImGui::TextDisabled("%s", planar ? "Position XZ" : label);
    ImGui::SetNextItemWidth(-1);
    float xz[2] = {xyz[0], xyz[2]};
    const bool changed = planar
        ? ImGui::DragFloat2("##positionXZ", xz, 0.05f, 0, 0, "%.3f")
        : ImGui::DragFloat3("##position", xyz, 0.05f, 0, 0, "%.3f");
    if (planar) { xyz[0] = xz[0]; xyz[2] = xz[1]; }
    if (ImGui::IsItemActivated()) map.beginEdit();
    bool applied = false;
    if (changed) {
        Json component = Json::object(), values = Json::array();
        for (float coordinate : xyz) values.push(Json::number(double(coordinate), 3));
        component.set("position", values);
        applied = writeDocumentComponent(map, ref, DocumentComponentKind::Transform, component);
    }
    ImGui::PopID();
    return applied;
}

} // namespace aiverse::editor
