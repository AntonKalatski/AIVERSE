// Shared Inspector component inventory for document-backed entities.
// Specialized editable drawers remain in their respective panels until
// a native ECS reflection/writeback registry is available.
#pragma once
#include "EditorEntityAdapter.h"
#include "imgui.h"
#include <functional>
#include <string>
#include <unordered_map>

namespace aiverse::editor {


using ComponentDrawer = std::function<void(MapDoc&, DocumentEntityRef, const Json&)>;
using ComponentFieldDrawer = std::function<bool(MapDoc&, DocumentEntityRef, const char*, const Json&)>;

// Optional native project/editor extensions can replace a whole component or
// override individual fields. Unregistered fields use the built-in drawer.
struct InspectorDrawerRegistry {
    std::unordered_map<std::string, ComponentDrawer> components;
    std::unordered_map<std::string, ComponentFieldDrawer> fields;
    static InspectorDrawerRegistry& instance() {
        static InspectorDrawerRegistry registry;
        return registry;
    }
    void registerComponent(std::string id, ComponentDrawer drawer) {
        components[std::move(id)] = std::move(drawer);
    }
    void registerField(std::string componentId, std::string fieldId,
                       ComponentFieldDrawer drawer) {
        fields[componentId + "." + fieldId] = std::move(drawer);
    }
};

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

// Rotation/scale are exposed ONLY when the authored object's real schema
// contains them (FPS oriented-box center/half/yaw). AABB colliders and RTS
// DocEntity have no generic 3D rotation/scale serialization.
inline bool drawDocumentRotationScaleFields(MapDoc& map,
                                             DocumentEntityRef ref) {
    Json transform;
    if (!readDocumentComponent(map, ref, DocumentComponentKind::Transform,
                               transform) ||
        !transform["rotationY"].isNumber() ||
        !transform["scale"].isArray()) return false;
    float rotation = float(transform["rotationY"].toDouble());
    ImGui::TextDisabled("Rotation Y");
    ImGui::SetNextItemWidth(-1);
    const bool rotateChanged = ImGui::DragFloat("##rotationY", &rotation,
                                                0.5f, 0, 0, "%.2f deg");
    if (ImGui::IsItemActivated()) map.beginEdit();

    const Json& scaleValues = transform["scale"];
    if (scaleValues.size() != 3) return false;
    float scale[3] = {float(scaleValues[0].toDouble()),
                      float(scaleValues[1].toDouble()),
                      float(scaleValues[2].toDouble())};
    ImGui::TextDisabled("Scale XYZ");
    ImGui::SetNextItemWidth(-1);
    const bool scaleChanged = ImGui::DragFloat3("##scaleXYZ", scale,
                                                0.05f, 0, 0, "%.3f");
    if (ImGui::IsItemActivated()) map.beginEdit();
    if (!rotateChanged && !scaleChanged) return false;
    Json write = Json::object();
    if (rotateChanged)
        write.set("rotationY", Json::number(double(rotation), 3));
    if (scaleChanged) {
        Json values = Json::array();
        for (float x : scale) values.push(Json::number(double(x), 3));
        write.set("scale", values);
    }
    return writeDocumentComponent(map, ref, DocumentComponentKind::Transform,
                                  write);
}

inline void drawDocumentEditableComponents(MapDoc& map, DocumentEntityRef ref) {
    const auto components = documentComponents(map, ref);
    for (const auto& component : components) {
        ImGui::PushID(component.id);
        if (ImGui::CollapsingHeader(component.title, ImGuiTreeNodeFlags_DefaultOpen)) {
            Json snapshot;
            readDocumentComponent(map, ref, component.kind, snapshot);
            auto& registry = InspectorDrawerRegistry::instance();
            const auto custom = registry.components.find(component.id);
            if (custom != registry.components.end()) {
                custom->second(map, ref, snapshot);
                ImGui::PopID();
                continue;
            }
            switch (component.kind) {
            case DocumentComponentKind::Transform:
                drawDocumentPositionField(map, ref);
                drawDocumentRotationScaleFields(map, ref);
                break;
            case DocumentComponentKind::Collider: {
                const Json* object = fpsDocumentEntity(map.data.fps, ref);
                if (object && (*object)["yaw"].isNumber()) {
                    ImGui::TextDisabled("Oriented box uses the Transform rotation and scale.");
                    if ((*object)["id"].isNumber())
                        ImGui::TextDisabled("Collider ID: %lld",
                            static_cast<long long>((*object)["id"].integer()));
                } else {
                    drawDocumentColliderBoundsField(map, ref);
                    ImGui::TextDisabled("Collider bounds are defined by Min/Max.");
                }
                break;
            }
            case DocumentComponentKind::Spawn:
                ImGui::TextDisabled("Spawn orientation and scale are not part of the current map schema.");
                break;
            case DocumentComponentKind::Renderable:
            case DocumentComponentKind::Gameplay: {
                Json snapshot;
                if (readDocumentComponent(map, ref, component.kind, snapshot) &&
                    snapshot.isObject()) {
                    for (const auto& entry : snapshot.members()) {
                        const Json& value = entry.second;
                        const std::string display = value.isString()
                            ? value.str() : value.dump(-1);
                        const auto field = registry.fields.find(
                            std::string(component.id) + "." + entry.first);
                        if (field != registry.fields.end() &&
                            field->second(map, ref, entry.first.c_str(), value))
                            continue;
                        ImGui::TextDisabled("%s", entry.first.c_str());
                        ImGui::SameLine();
                        ImGui::TextWrapped("%s", display.c_str());
                    }
                }
                break;
            }
            default:
                break;
            }
        }
        ImGui::PopID();
    }
}

} // namespace aiverse::editor
