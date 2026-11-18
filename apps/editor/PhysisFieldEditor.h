// PHYSIS Stage 19 — editor-owned field markers, move gizmo and Inspector draft.
// Authoring writes ONLY through MapDoc Undo/Redo, never live Simulation World.
// No genre-specific identity or Box3D interaction; existing Scene selection
// stays authoritative unless an explicitly enabled PHYSIS marker was clicked.
#pragma once
#include "Editor.h"
#include "EditorUtil.h"
#include "PhysisAuthoring.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace aiverse::editor {

struct PhysisFieldEditorState {
    bool showMarkers = true;
    bool editMode = false;
    bool dragging = false;
    bool dragEdited = false;
    std::string selectedId;
    std::string draftId;
    std::string error;
    std::string rawDraft;
    Json draftField;
    double grabOffsetX = 0, grabOffsetZ = 0;
    float lastMouseY = 0;
};

inline PhysisFieldEditorState& physisFieldEditorState() {
    static PhysisFieldEditorState state;
    return state;
}

inline bool physisFieldPosition(const Json& field, render::float3& position) {
    const Json& coords = field["center"];
    if (!coords.isArray() || coords.size()!=3) return false;
    for (std::size_t axis=0;axis<3;++axis)
        if (!coords[axis].isNumber() ||
            !std::isfinite(coords[axis].toDouble()) ||
            std::abs(coords[axis].toDouble()) > 1000000) return false;
    position = {float(coords[0].toDouble()),float(coords[1].toDouble()),
                float(coords[2].toDouble())};
    return true;
}

inline void selectPhysisField(const std::string& id) {
    auto& state = physisFieldEditorState();
    if (state.selectedId == id) return;
    state.selectedId = id;
    state.draftId.clear();
    state.error.clear();
    state.dragging = false;
    state.dragEdited = false;
}

inline void drawPhysisFieldMarkers(Editor& ed, const Viewport& vp,
                                   const render::RTSCamera& camera) {
    auto& state = physisFieldEditorState();
    if (!state.showMarkers || !physis_authoring::read(ed.doc()).isObject()) return;
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect({vp.originX,vp.originY},
                       {vp.originX+vp.pointsW,vp.originY+vp.pointsH},true);
    const Json config = physis_authoring::read(ed.doc());
    for (const char* key : {"windZones","storms","gravityFields"}) {
        const auto& entries = config[key];
        if (!entries.isArray()) continue;
        for (const auto& entry : entries.items()) {
            render::float3 position;
            if (!physisFieldPosition(entry,position)) continue;
            render::float2 projected;
            if (!camera.worldToScreen(position,vp.pointsW,vp.pointsH,projected))
                continue;
            const ImVec2 p(vp.originX+projected.x,vp.originY+projected.y);
            if (p.x < vp.originX || p.x > vp.originX+vp.pointsW ||
                p.y < vp.originY || p.y > vp.originY+vp.pointsH) continue;
            const bool selected = entry.getStr("id")==state.selectedId;
            const bool wind = std::string(key)=="windZones";
            const bool storm = std::string(key)=="storms";
            const ImU32 color = wind ? IM_COL32(90,219,243,245) :
                                storm ? IM_COL32(255,192,93,245) :
                                        IM_COL32(186,142,255,245);
            draw->AddCircleFilled(p, selected?9.f:6.f,
                                  IM_COL32(16,22,35,225), 18);
            draw->AddCircle(p, selected?9.f:6.f,color,18,selected?2.7f:1.8f);
            if (selected) {
                draw->AddLine({p.x-14,p.y},{p.x+14,p.y},color,1);
                draw->AddLine({p.x,p.y-14},{p.x,p.y+14},color,1);
            }
            const char* symbol = wind ? "W" : storm ? "S" : "G";
            draw->AddText({p.x+10,p.y-8},color,symbol);
        }
    }
    draw->PopClipRect();
}

// Return true only for mouse interactions owned by an opted-in PHYSIS marker.
// This keeps the legacy FPS and RTS selection/drag behavior intact otherwise.
inline bool processPhysisFieldGizmo(Editor& ed,const Viewport& vp,
                                     const render::RTSCamera& camera) {
    auto& state = physisFieldEditorState();
    const ImGuiIO& io = ImGui::GetIO();
    if (!state.editMode || ed.playing()) {
        state.dragging = false;
        return false;
    }
    if (state.dragging) {
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
            !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            state.dragging = false;
            state.dragEdited = false;
            return true;
        }
        const auto& config = physis_authoring::read(ed.doc());
        std::string collection;
        std::size_t index=0;
        Json field;
        if (!physis_authoring::findField(config,state.selectedId,collection,index,&field)) {
            state.dragging = false;
            return true;
        }
        render::float3 current;
        if (!physisFieldPosition(field,current)) {
            state.dragging = false;
            return true;
        }
        double x = current.x,y=current.y,z=current.z;
        if (io.KeyShift) {
            y += double(state.lastMouseY - io.MousePos.y) *
                 std::max(0.01,double(camera.distance)*0.002);
        } else {
            render::float3 hit;
            if (camera.screenToGround(io.MousePos.x-vp.originX,
                    io.MousePos.y-vp.originY,vp.pointsW,vp.pointsH,
                    hit,current.y)) {
                x=double(hit.x)+state.grabOffsetX;
                z=double(hit.z)+state.grabOffsetZ;
            }
        }
        state.lastMouseY = io.MousePos.y;
        if ((std::abs(x-current.x)>0.001 || std::abs(y-current.y)>0.001 ||
             std::abs(z-current.z)>0.001) &&
            physis_authoring::moveField(ed.doc(),state.selectedId,x,y,z,
                                         !state.dragEdited,&state.error)) {
            state.dragEdited = true;
            state.draftId.clear(); // draft follows the document after drag
        }
        return true;
    }
    if (!vp.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
        io.KeyCtrl || io.KeySuper || io.KeyAlt || ImGui::IsMouseDown(ImGuiMouseButton_Right))
        return false;

    const Json config = physis_authoring::read(ed.doc());
    std::string picked;
    float bestDistanceSq = 15.f*15.f;
    render::float3 chosenPosition{};
    for (const char* key : {"windZones","storms","gravityFields"}) {
        const auto& fields = config[key];
        if (!fields.isArray()) continue;
        for (const auto& entry : fields.items()) {
            render::float3 position;
            render::float2 screen;
            if (!physisFieldPosition(entry,position) ||
                !camera.worldToScreen(position,vp.pointsW,vp.pointsH,screen))
                continue;
            const float dx = screen.x+vp.originX-io.MousePos.x;
            const float dy = screen.y+vp.originY-io.MousePos.y;
            const float distanceSq = dx*dx+dy*dy;
            if (distanceSq < bestDistanceSq) {
                bestDistanceSq=distanceSq;
                chosenPosition=position;
                picked=entry.getStr("id");
            }
        }
    }
    if (picked.empty()) return false;
    selectPhysisField(picked);
    // Open the common Inspector's MapProps panel for the selected PHYSIS field.
    // The PHYSIS field is authored JSON, not a fake runtime ECS Selection.
    ed.select({Selection::MapProps,-1,{}});
    render::float3 ground;
    if (camera.screenToGround(io.MousePos.x-vp.originX,
        io.MousePos.y-vp.originY,vp.pointsW,vp.pointsH,ground,
        chosenPosition.y)) {
        state.grabOffsetX = double(chosenPosition.x-ground.x);
        state.grabOffsetZ = double(chosenPosition.z-ground.z);
    } else {
        state.grabOffsetX = state.grabOffsetZ = 0;
    }
    state.dragging = true;
    state.dragEdited = false;
    state.lastMouseY = io.MousePos.y;
    return true;
}

inline void drawPhysisFieldProperties(Editor& ed) {
    using namespace physis_authoring;
    auto& state = physisFieldEditorState();
    std::string collection;
    std::size_t index = 0;
    Json selected;
    if (!findField(read(ed.doc()), state.selectedId, collection, index, &selected))
        return;
    ui::section(ICON_PENCIL,"PHYSIS — параметры поля");

    if (state.draftId!=state.selectedId) {
        state.draftId=state.selectedId;
        state.draftField=selected;
        state.rawDraft=selected.dump(2);
    }
    ImGui::Text("Выбрано: %s",state.selectedId.c_str());
    Json draft=state.draftField;
    render::float3 position;
    if (physisFieldPosition(draft,position)) {
        float coords[]={position.x,position.y,position.z};
        if (ImGui::InputFloat3("Позиция XYZ, м",coords))
            draft.set("center",point(coords[0],coords[1],coords[2]));
    }
    float radius=float(draft["radiusMeters"].toDouble(
                 draft["outerRadiusMeters"].toDouble(100)));
    const bool storm=collection=="storms";
    if (ImGui::DragFloat("Радиус, м",&radius,1.f,1.f,100000.f)) {
        if (storm) draft.set("outerRadiusMeters",Json::number(double(radius),3));
        else draft.set("radiusMeters",Json::number(double(radius),3));
    }
    if (collection=="windZones") {
        float velocity[3]={};
        const auto& v=draft["velocity"];
        if (v.isArray() && v.size()==3)
            for(int i=0;i<3;++i)velocity[i]=float(v[size_t(i)].toDouble());
        if (ImGui::DragFloat3("Ветер, м/с",velocity,0.2f,-200.f,200.f))
            draft.set("velocity",point(velocity[0],velocity[1],velocity[2]));
    }
    if (collection=="storms") {
        const char* kinds[]={"Tornado","Hurricane"};
        int kind=draft.getStr("kind")=="Hurricane" ? 1 : 0;
        if (ImGui::Combo("Тип шторма",&kind,kinds,2))
            draft.set("kind",Json::string(kinds[kind]));
        for (const char* key : {"eyeRadiusMeters","heightMeters","tangentialWind","updraft"}) {
            float value=float(draft[key].toDouble());
            if (ImGui::DragFloat(key,&value,0.2f,0.f,100000.f))
                draft.set(key,Json::number(double(value),3));
        }
    }
    if (collection=="gravityFields") {
        const char* modes[]={"Uniform","Attract","Repel"};
        int kind=draft.getStr("mode")=="Attract" ? 1 :
                 draft.getStr("mode")=="Repel" ? 2 : 0;
        if (ImGui::Combo("Тип гравитации",&kind,modes,3))
            draft.set("mode",Json::string(modes[kind]));
        float acceleration[3]={};
        const auto& v=draft["acceleration"];
        if (v.isArray() && v.size()==3)
            for(int i=0;i<3;++i)acceleration[i]=float(v[size_t(i)].toDouble());
        if (ImGui::DragFloat3("Ускорение м/с²",acceleration,0.1f,-200.f,200.f))
            draft.set("acceleration",point(acceleration[0],acceleration[1],acceleration[2]));
        float strength=float(draft["radialMagnitude"].toDouble(10));
        if (ImGui::DragFloat("Радиальная сила",&strength,0.1f,0.f,200.f))
            draft.set("radialMagnitude",Json::number(double(strength),3));
    }
    if (!(draft==state.draftField)) {
        state.draftField=draft;
        state.rawDraft=draft.dump(2);
    }

    if (ImGui::CollapsingHeader("Дополнительные параметры JSON")) {
        ui::inputMultiline("##physisFieldJson",state.rawDraft,ImVec2(-1,150));
    }
    if (ImGui::Button("Применить изменения поля")) {
        try {
            const Json value=Json::parse(state.rawDraft);
            if (replaceField(ed.doc(),state.selectedId,value,&state.error)) {
                state.draftId.clear();
                state.error.clear();
            }
        } catch(const std::exception& error) {
            state.error=error.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Удалить поле")) {
        if (removeField(ed.doc(),state.selectedId,&state.error))
            selectPhysisField("");
    }
}

inline void drawPhysisFieldsPanel(Editor& ed) {
    using namespace physis_authoring;
    auto& state = physisFieldEditorState();
    const Json config = read(ed.doc());
    ui::section(ICON_GLOBE,"PHYSIS — объекты и Gizmo");
    if (!config.isObject()) {
        ui::hint("Создайте PHYSIS в свойствах карты, затем добавляйте поля.");
        if (ImGui::Button("Открыть свойства карты##physisProps")) {
            ed.select({Selection::MapProps,-1,{}});
            ed.focusWindow("inspector");
        }
        return;
    }
    ImGui::Checkbox("Показать маркеры в Сцене", &state.showMarkers);
    ImGui::Checkbox("Редактировать маркеры мышью", &state.editMode);
    if (state.editMode)
        ImGui::TextDisabled("Перетащите маркер в Сцене (Shift — высота). Один drag = одно Undo.");

    const double x = std::clamp(double(ed.sceneCam.target.x),-1000000.0,1000000.0);
    const double y = std::clamp(double(ed.sceneCam.target.y),-1000000.0,1000000.0);
    const double z = std::clamp(double(ed.sceneCam.target.z),-1000000.0,1000000.0);
    const auto add = [&](aiverse::physis::AuthoredFieldKind kind) {
        std::string id;
        if (addField(ed.doc(),kind,x,y,z,id,&state.error)) {
            selectPhysisField(id);
            state.editMode = true;
            state.showMarkers = true;
        }
    };
    if (ImGui::SmallButton("+ Ветер")) add(aiverse::physis::AuthoredFieldKind::Wind);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Смерч/ураган")) add(aiverse::physis::AuthoredFieldKind::Storm);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Гравитация")) add(aiverse::physis::AuthoredFieldKind::Gravity);
    if (!state.error.empty()) ui::toneText(ui::Tone::Error,"%s",state.error.c_str());

    if (ImGui::BeginChild("##physisFields",ImVec2(0,140),ImGuiChildFlags_Borders)) {
        for (const char* key : {"windZones","storms","gravityFields"}) {
            const auto& fields=read(ed.doc())[key];
            if (!fields.isArray()) continue;
            for (const auto& entry : fields.items()) {
                const std::string id=entry.getStr("id");
                if (ImGui::Selectable((id+"##physisField").c_str(),
                                      id==state.selectedId))
                    selectPhysisField(id);
            }
        }
    }
    ImGui::EndChild();
    if (!state.selectedId.empty()) {
        std::string collection;
        std::size_t index=0;
        if (!findField(read(ed.doc()),state.selectedId,collection,index))
            selectPhysisField("");
    }
    drawPhysisFieldProperties(ed);

}

} // namespace aiverse::editor
