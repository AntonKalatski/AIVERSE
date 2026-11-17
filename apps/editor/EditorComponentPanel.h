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

inline bool drawDocumentColliderBoundsField(MapDoc& map, DocumentEntityRef ref) {
    if (ref.source != DocumentEntityRef::Source::FpsCollider) return false;
    Json component;
    if (!readDocumentComponent(map, ref, DocumentComponentKind::Collider, component))
        return false;
    const Json& lo = component["min"];
    const Json& hi = component["max"];
    if (!lo.isArray() || !hi.isArray() || lo.size() < 3 || hi.size() < 3)
        return false;
    float minimum[3], maximum[3];
    for (size_t axis = 0; axis < 3; ++axis) {
        if (!lo[axis].isNumber() || !hi[axis].isNumber()) return false;
        minimum[axis] = float(lo[axis].toDouble());
        maximum[axis] = float(hi[axis].toDouble());
    }
    ImGui::PushID("BoxCollider");
    ImGui::TextDisabled("Min XYZ");
    ImGui::SetNextItemWidth(-1);
    const bool changedMin = ImGui::DragFloat3("##min", minimum, 0.05f, 0, 0, "%.3f");
    if (ImGui::IsItemActivated()) map.beginEdit();
    ImGui::TextDisabled("Max XYZ");
    ImGui::SetNextItemWidth(-1);
    const bool changedMax = ImGui::DragFloat3("##max", maximum, 0.05f, 0, 0, "%.3f");
    if (ImGui::IsItemActivated()) map.beginEdit();
    bool applied = false;
    if (changedMin || changedMax) {
        Json updated = Json::object(), minArray = Json::array(), maxArray = Json::array();
        for (float coordinate : minimum) minArray.push(Json::number(double(coordinate), 3));
        for (float coordinate : maximum) maxArray.push(Json::number(double(coordinate), 3));
        updated.set("min", minArray);
        updated.set("max", maxArray);
        applied = writeDocumentComponent(map, ref, DocumentComponentKind::Collider, updated);
    }
    ImGui::PopID();
    return applied;
}

} // namespace aiverse::editor
