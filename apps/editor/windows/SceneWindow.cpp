// Scene: the map in 3D with a free camera; select/drag objects and apply the current tool.
//   RMB drag: look around, RMB + WASD/QE: fly, MMB drag: pan, wheel: zoom, F: focus selection.
#include <algorithm>
#include <cmath>
#include "../Editor.h"
#include "StudioContextMenus.h"
#include "StudioLuaExtensions.h"
#include "../EditorGenreUi.h"
#include "../core/PlanetOps.h"
#include "../EditorUtil.h"
#include "../EditorWorldEdit.h"

namespace aiverse::editor {

using namespace render;

class SceneWindow : public EditorWindow {
public:
    const char* id() const override { return "scene"; }
    const char* title() const override { return "Сцена"; }
    const char* group() const override { return "scene"; }
    int order() const override { return 0; }
    const char* dockArea() const override { return "center"; }
    bool zeroPadding() const override { return true; }
    ImGuiWindowFlags flags() const override { return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse; }

    void draw(Editor& ed) override {
        if(ed.ws().project.isFps()) {
            if(ImGui::Button(genreText(ed.ws(),"fps.editor.open").c_str()))ed.focusWindow("fps");
            ImGui::SameLine();ImGui::TextDisabled("%s",genreText(ed.ws(),"fps.editor.camera").c_str());
        } else toolbar(ed);
        ImVec2 avail = ImGui::GetContentRegionAvail();
        const float dpi = ImGui::GetIO().DisplayFramebufferScale.x;
        vp_.ensure(ed.device(), int(avail.x * dpi), int(avail.y * dpi));
        ImVec2 p = ImGui::GetCursorScreenPos();
        vp_.originX = p.x; vp_.originY = p.y; vp_.pointsW = avail.x; vp_.pointsH = avail.y;
        // image first (like the Game window): the screen overlays renderScene adds (bars, labels) stay on top of it
        vp_.image(ed.device(), avail.x, avail.y);
        // Diagnostic rendering is scoped to Scene, never leaked into the Game tab.
        ed.renderer().setStudioDebugMode(studioDebugMode_);
        ed.renderScene(vp_, ed.sceneCam, !ed.playing());
        ed.renderer().setStudioDebugMode(0);
        vp_.drawnFrame = ImGui::GetFrameCount();
        ed.sceneViewport = &vp_;
        if(ed.ws().project.isFps()) {
            ImGuiIO& io = ImGui::GetIO();
            const bool navigation = vp_.hovered || (ImGui::IsWindowFocused() && !io.WantTextInput);
            if (vp_.hovered && io.MouseWheel != 0)
                ed.sceneCam.distance = std::clamp(ed.sceneCam.distance * std::pow(0.85f, io.MouseWheel), 1.0f, 2000.0f);
            if (vp_.hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                ed.sceneCam.yaw -= io.MouseDelta.x * 0.005f;
                ed.sceneCam.pitch = std::clamp(ed.sceneCam.pitch + io.MouseDelta.y * 0.005f, 0.05f, 1.5f);
            }
            if (vp_.hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                const float s = ed.sceneCam.distance * 0.002f;
                const float3 right{std::cos(ed.sceneCam.yaw), 0, -std::sin(ed.sceneCam.yaw)};
                const float3 forward{-std::sin(ed.sceneCam.yaw), 0, -std::cos(ed.sceneCam.yaw)};
                ed.sceneCam.target += right * (-io.MouseDelta.x * s) + forward * (io.MouseDelta.y * s);
            }
            // FPS preview also uses the editor free camera: no RMB hold required for WASD.
            if (navigation && !io.WantTextInput && !io.KeyCtrl && !io.KeySuper) {
                const float speed = ed.sceneCam.distance * 0.9f * io.DeltaTime * (io.KeyShift ? 3.0f : 1.0f);
                const float3 right{std::cos(ed.sceneCam.yaw), 0, -std::sin(ed.sceneCam.yaw)};
                const float3 forward{-std::sin(ed.sceneCam.yaw), 0, -std::cos(ed.sceneCam.yaw)};
                if (ImGui::IsKeyDown(ImGuiKey_W)) ed.sceneCam.target += forward * speed;
                if (ImGui::IsKeyDown(ImGuiKey_S)) ed.sceneCam.target += forward * -speed;
                if (ImGui::IsKeyDown(ImGuiKey_D)) ed.sceneCam.target += right * speed;
                if (ImGui::IsKeyDown(ImGuiKey_A)) ed.sceneCam.target += right * -speed;
                if (ImGui::IsKeyDown(ImGuiKey_Q)) ed.sceneCam.target.y -= speed;
                if (ImGui::IsKeyDown(ImGuiKey_E)) ed.sceneCam.target.y += speed;
                if (ImGui::IsKeyPressed(ImGuiKey_F, false)) ed.fitScene();
            }
        } else {if (ImGui::IsWindowFocused() || vp_.hovered || looking_ || panning_) input(ed);hud(ed);}
        StudioLuaExtensions::get().drawScene(ed, vp_);
        // Shift+RMB opens custom scene actions. Ordinary RMB still navigates.
        if (StudioContextMenus::get().available(ed) && vp_.hovered &&
            ImGui::GetIO().KeyShift && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            ImGui::OpenPopup("##aiverse_scene_context");
        if (ImGui::BeginPopup("##aiverse_scene_context")) {
            StudioContextMenus::get().draw(ed, "scene");
            ImGui::EndPopup();
        }
    }

private:
    void toolbar(Editor& ed) {
        static const char* names[] = {"Выбор", "Объекты", "Клетки", "Уровни", "Высота", "Поля", "Регионы", "Зоны", "Старты", "Дороги", "Ластик"};
        static const char* icons[] = {ICON_SELECT, ICON_OBJECTS, ICON_CELLS, ICON_LEVELS, ICON_HEIGHT, ICON_FIELDS, ICON_REGIONS, ICON_ZONES,
                                      ICON_STARTS, ICON_ROADS, ICON_ERASER};
        static_assert(sizeof names / sizeof *names == size_t(Tool::Count) && sizeof icons / sizeof *icons == size_t(Tool::Count));
        const float bottom = ui::beginToolbar();
        // On narrow viewports expose additional tools from a popup rather than
        // clipping the toolbar. All tools remain available by shortcuts 1..0.
        const bool compact = ImGui::GetContentRegionAvail().x < 760.0f;
        ui::beginSegment();
        for (int i = 0; i < (compact ? 5 : int(Tool::Count)); ++i) {
            if (i == 1 || i == 10) { ui::endSegment(); ImGui::SameLine(0, 8); ui::beginSegment(); } // select | edit tools | eraser
            else if (i) ImGui::SameLine();
            char tip[64];
            if (i < 10) std::snprintf(tip, sizeof tip, "%s  (%d)", names[i], (i + 1) % 10); else std::snprintf(tip, sizeof tip, "%s", names[i]);
            if (ui::toolButton(icons[i], tip, ed.tool == Tool(i))) { ed.tool = Tool(i); ed.roadStart = -1; }
        }
        ui::endSegment();
        if (compact) {
            ImGui::SameLine(0, 5);
            if (ui::toolButton(ICON_MORE, "Другие инструменты")) ImGui::OpenPopup("##scene_more_tools");
            if (ImGui::BeginPopup("##scene_more_tools")) {
                for (int i = 5; i < int(Tool::Count); ++i)
                    if (ImGui::MenuItem(names[i], nullptr, ed.tool == Tool(i))) { ed.tool = Tool(i); ed.roadStart = -1; }
                ImGui::EndPopup();
            }
        }
        ui::toolbarDivider();
        // Studio visual diagnostics: actual built-in GPU shader views.
        static const char* studioModes[] = {"Shaded", "Draw Calls", "Normals", "UV", "Depth", "Albedo", "Roughness", "Metallic", "Emissive", "World Position"};
        ImGui::SetNextItemWidth(155.0f);
        if (ImGui::BeginCombo("##sceneVisualisation", studioModes[studioDebugMode_])) {
            for (int i = 0; i < 10; ++i) {
                if (ImGui::Selectable(studioModes[i], studioDebugMode_ == i)) studioDebugMode_ = i;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(i == 1 ? "Каждый встроенный GPU draw batch — свой цвет (instancing сохраняется)" :
                                                         i >= 2 ? "Диагностика моделей; terrain и callback renderers могут оставаться без раскраски" : "Обычный материал и освещение");
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ui::toolButton(ICON_EYE, "Отладочные gizmo")) ImGui::OpenPopup("##studio_gizmos");
        if (ImGui::BeginPopup("##studio_gizmos")) {
            ImGui::TextDisabled("Scene overlays");
            ImGui::Checkbox("Границы объектов", &studioDrawBounds_);
            ImGui::Checkbox("Оси мира", &studioDrawAxes_);
            ImGui::Checkbox("Выделение", &studioDrawSelection_);
            ImGui::Separator();
            ImGui::TextDisabled("RTS debugging");
            ImGui::Checkbox("Уровни и рампы", &ed.showLevels);
            ImGui::Checkbox("Регионы", &ed.showRegions);
            ImGui::Checkbox("Триггерные зоны", &ed.showAreas);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ui::toolbarDivider();
        if (ui::toolButton(ICON_FIT, "Вся карта (F)")) ed.fitScene();
        ImGui::SameLine();
        if (ui::toolButton(ICON_MAGNET, ed.snap ? "Привязка к сетке: вкл" : "Привязка к сетке: выкл", ed.snap)) ed.snap = !ed.snap;
        ImGui::SameLine();
        if (ui::toolButton(ICON_EYE, "Что показывать")) ImGui::OpenPopup("##show");
        if (ImGui::BeginPopup("##show")) {
            ImGui::MenuItem("Уровни и рампы", nullptr, &ed.showLevels);
            ImGui::MenuItem("Регионы", nullptr, &ed.showRegions);
            ImGui::MenuItem("Зоны триггеров", nullptr, &ed.showAreas);
            ImGui::EndPopup();
        }
        const bool brushTool = ed.tool == Tool::Terrain || ed.tool == Tool::Level || ed.tool == Tool::Height || ed.tool == Tool::Field || ed.tool == Tool::Region;
        if (brushTool) {
            ui::toolbarDivider();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled(ICON_BRUSH);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110);
            ImGui::SliderInt("##brush", &ed.brush, 1, 16, "кисть %d");
        }
        ImGui::SameLine(0, 12);
        ImGui::AlignTextToFramePadding();
        if (int(ed.tool) < int(Tool::Count)) ImGui::TextDisabled("%s", names[int(ed.tool)]);
        ImGui::SameLine(); ImGui::TextDisabled("·");
        ImGui::SameLine(); ImGui::TextDisabled("Grid 1м");
        ImGui::SameLine(); ImGui::TextDisabled("·");
        ImGui::SameLine(); ImGui::TextDisabled(ed.snap ? "Snap ON" : "Snap OFF");
        ImGui::SameLine(); ImGui::TextDisabled("·");
        ImGui::SameLine(); ImGui::TextDisabled("%zu объектов", ed.doc().data.entities.size());
        ui::endToolbar(bottom);
    }

    // planet maps: wheel zoom (surface -> planet -> system), RMB drag turn + pitch, MMB drag / WASD move over
    // the surface, Tab next body, F back to the start body
    void planetInput(Editor& ed) {
        ImGuiIO& io = ImGui::GetIO();
        game::PlanetCamera& pc = ed.view().planetCamera();
        if (vp_.hovered && io.MouseWheel != 0) pc.zoom(std::pow(0.88f, io.MouseWheel));
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && vp_.hovered) looking_ = true;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && vp_.hovered) panning_ = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) looking_ = false;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) panning_ = false;
        if (looking_) {
            pc.rotate(-io.MouseDelta.x * 0.006f);
            pc.pitch = std::clamp(pc.pitch + io.MouseDelta.y * 0.005f, 0.15f, 1.55f);
        }
        if (panning_) pc.pan(io.MouseDelta.x, io.MouseDelta.y, vp_.pointsH);
        if (!io.WantTextInput && (ImGui::IsWindowFocused() || vp_.hovered)) {
            const float speed = std::max(pc.distance, 6.0f) * 0.9f * io.DeltaTime * (io.KeyShift ? 3.0f : 1.0f);
            if (ImGui::IsKeyDown(ImGuiKey_W)) pc.move(0, speed);
            if (ImGui::IsKeyDown(ImGuiKey_S)) pc.move(0, -speed);
            if (ImGui::IsKeyDown(ImGuiKey_D)) pc.move(speed, 0);
            if (ImGui::IsKeyDown(ImGuiKey_A)) pc.move(-speed, 0);
            if (ImGui::IsKeyDown(ImGuiKey_Q)) pc.rotate(io.DeltaTime * 1.6f);
            if (ImGui::IsKeyDown(ImGuiKey_E)) pc.rotate(-io.DeltaTime * 1.6f);
            if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
                const auto& W = ed.view().planets().world();
                for (size_t k = 1; k <= W.bodies.size(); ++k) {
                    const int b = int((size_t(pc.body()) + k) % W.bodies.size());
                    if (W.bodies[size_t(b)].surface()) { pc.focus(b); break; }
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) ed.fitScene();
        }
    }

    void planetDown(Editor& ed, float sx, float sy, ImGuiIO& io) {
        const int n = ed.pickEntity(vp_, ed.sceneCam, sx, sy);
        const Json at = ed.planetPosAt(vp_, sx, sy);
        auto& doc = ed.doc();
        auto& d = doc.data;
        switch (ed.tool) {
        case Tool::Select:
            ed.selectEntity(n);
            if (n >= 0) { doc.beginEdit(); dragging_ = true; }
            break;
        case Tool::Place: {
            if (n >= 0 && !io.KeyShift) { ed.selectEntity(n); break; }
            if (!at.isObject() || ed.rules().types.empty()) break;
            DocEntity e;
            e.type = ed.rules().types[size_t(ed.placeType)].id;
            e.owner = ed.placeOwner;
            e.at = at; e.x = float(at["x"].toDouble()); e.z = float(at["y"].toDouble());
            ed.selectEntity(doc.addEntity(e));
            ed.status = "Добавлено: " + ed.rules().types[size_t(ed.placeType)].name;
            break;
        }
        case Tool::Start:
            if (!at.isObject() || ed.startPlayer < 1 || ed.startPlayer > int(d.players.size())) break;
            doc.beginEdit();
            d.players[size_t(ed.startPlayer - 1)].hasStart = true;
            d.players[size_t(ed.startPlayer - 1)].startAt = at;
            doc.touched();
            break;
        case Tool::Terrain:
            if (!at.isObject()) break;
            doc.beginEdit();
            painting_ = true;
            planetDrag(ed, sx, sy, io);
            break;
        case Tool::Erase:
            if (n >= 0) { doc.removeEntity(n); ed.select({}); }
            break;
        default: ed.status = "На картах планет: выбор, объекты, клетки, старты, ластик; рельеф — окно «Рельеф»"; break;
        }
    }

    void planetDrag(Editor& ed, float sx, float sy, ImGuiIO& io) {
        auto& doc = ed.doc();
        auto& d = doc.data;
        const Json at = ed.planetPosAt(vp_, sx, sy);
        if (!at.isObject()) return;
        const int sel = ed.selectedEntity();
        if (dragging_ && sel >= 0 && sel < int(d.entities.size())) {
            auto& e = d.entities[size_t(sel)];
            e.at = at; e.x = float(at["x"].toDouble()); e.z = float(at["y"].toDouble());
            doc.touched();
        }
        if (painting_ && ed.tool == Tool::Terrain) {
            int body; render::float3 local;
            if (ed.planetLocal(at, body, local)) planetPaint(doc, at.getStr("body"), normalize(local), float(ed.brush) - 0.5f, io.KeyShift ? '.' : ed.paintCell);
        }
    }

    void input(Editor& ed) {
        ImGuiIO& io = ImGui::GetIO();
        RTSCamera& cam = ed.sceneCam;
        const float mx = io.MousePos.x, my = io.MousePos.y;
        if (ed.planetMap()) { // planet camera; tools: select / move / place / erase objects, start, cells
            planetInput(ed);
            hoverX_ = mx; hoverY_ = my;
            if (ed.playing()) return;
            if (!io.WantTextInput && (ImGui::IsWindowFocused() || vp_.hovered)) {
                if ((ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) && !ed.selectedEntities().empty())
                    ed.removeSelectedEntities();
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ed.select({});
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && vp_.hovered) planetDown(ed, mx, my, io);
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && (dragging_ || painting_) && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) planetDrag(ed, mx, my, io);
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) dragging_ = painting_ = false;
            return;
        }
        // camera
        if (vp_.hovered && io.MouseWheel != 0) cam.distance = std::clamp(cam.distance * std::pow(0.88f, io.MouseWheel), cam.minDistance, cam.maxDistance);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && vp_.hovered) looking_ = true;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && vp_.hovered) panning_ = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) looking_ = false;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) panning_ = false;
        if (looking_) {
            cam.yaw -= io.MouseDelta.x * 0.006f;
            cam.pitch = std::clamp(cam.pitch + io.MouseDelta.y * 0.005f, 0.15f, 1.53f);
        }
        // Free fly: WASD/QE move the camera target while Scene is focused/hovered.
        // Right mouse remains orbit/look, middle mouse pans, Shift increases speed.
        const bool cameraKeys = !io.WantTextInput && !io.KeyCtrl && !io.KeySuper &&
                                (vp_.hovered || (ImGui::IsWindowFocused() && looking_));
        if (cameraKeys) {
            const float speed = cam.distance * 0.9f * io.DeltaTime * (io.KeyShift ? 3.0f : 1.0f);
            const float3 fwd{-std::sin(cam.yaw), 0, -std::cos(cam.yaw)};
            const float3 right{std::cos(cam.yaw), 0, -std::sin(cam.yaw)};
            if (ImGui::IsKeyDown(ImGuiKey_W)) cam.target += fwd * speed;
            if (ImGui::IsKeyDown(ImGuiKey_S)) cam.target += fwd * -speed;
            if (ImGui::IsKeyDown(ImGuiKey_D)) cam.target += right * speed;
            if (ImGui::IsKeyDown(ImGuiKey_A)) cam.target += right * -speed;
            if (ImGui::IsKeyDown(ImGuiKey_Q)) cam.target.y -= speed;
            if (ImGui::IsKeyDown(ImGuiKey_E)) cam.target.y += speed;
        }
        if (panning_) {
            float x0, z0, x1, z1;
            if (ed.groundAt(vp_, cam, mx - io.MouseDelta.x, my - io.MouseDelta.y, x0, z0) && ed.groundAt(vp_, cam, mx, my, x1, z1))
                cam.target += float3{x0 - x1, 0, z0 - z1};
        }
        if (!io.WantTextInput && ImGui::IsWindowFocused() && !looking_) {
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                float fx, fz;
                if (ed.selectionPos(ed.selection, fx, fz)) ed.focusScene(fx, fz, 18);
                else ed.fitScene();
            }
            if ((ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) && !ed.selectedEntities().empty())
                ed.removeSelectedEntities();
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false)) ed.brush = std::max(1, ed.brush - 1);
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false)) ed.brush = std::min(16, ed.brush + 1);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { ed.select({}); ed.roadStart = -1; }
            // 1..9, 0: tools in toolbar order (the eraser is the 11th: Delete with nothing selected is not used for it)
            if (!io.KeyCtrl && !io.KeySuper && !io.KeyAlt)
                for (int k = 0; k < 10 && k < int(Tool::Count); ++k)
                    if (ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_1 + (k == 9 ? -1 : k)), false)) { ed.tool = Tool(k); ed.roadStart = -1; }
        }
        // the object under the cursor: highlighted here and in the Hierarchy
        if (vp_.hovered && !looking_ && !panning_ && (ed.tool == Tool::Select || ed.playing())) ed.setHover(ed.pickObject(vp_, cam, mx, my));
        if (ed.playing()) {   // play: the Select tool picks live objects for the Inspector (read-only)
            if (ed.tool == Tool::Select && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && vp_.hovered) pickSelect(ed, ed.pickObject(vp_, cam, mx, my), io);
            hoverX_ = mx; hoverY_ = my;
            return;
        }
        // tools
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && vp_.hovered) down(ed, mx, my, io);
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && (dragging_ || painting_) && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) drag(ed, mx, my, io);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) up(ed, mx, my);
        hoverX_ = mx; hoverY_ = my;
    }

    // Hit-test the projected move handles, independently of entity mesh picking.
    // Map documents currently expose X/Z positions; Y, rotation and scale are
    // not writable through DocEntity and must not pretend to be interactive.
    int moveHandle(Editor& ed, float sx, float sy) {
        if (ed.playing() || ed.tool != Tool::Select || ed.planetMap()) return -1;
        float wx, wz;
        if (!ed.selectionPos(ed.selection, wx, wz)) return -1;
        const float wy = ed.view().heightAt(ed.editGrid(), wx, wz) + 0.4f;
        float2 origin;
        if (!ed.sceneCam.worldToScreen({wx, wy, wz}, vp_.pointsW, vp_.pointsH, origin)) return -1;
        int best = -1;
        float bestDist = 90.0f;
        for (int axis : {0, 2}) {
            float2 end;
            const float3 target = axis == 0 ? float3{wx + 1.5f, wy, wz} : float3{wx, wy, wz + 1.5f};
            if (!ed.sceneCam.worldToScreen(target, vp_.pointsW, vp_.pointsH, end)) continue;
            const float ax = vp_.originX + origin.x, ay = vp_.originY + origin.y;
            const float bx = vp_.originX + end.x, by = vp_.originY + end.y;
            const float vx = bx - ax, vy = by - ay;
            const float len2 = vx * vx + vy * vy;
            if (len2 < 64.0f) continue;
            const float t = std::clamp(((sx - ax) * vx + (sy - ay) * vy) / len2, 0.25f, 1.1f);
            const float dx = sx - (ax + t * vx), dy = sy - (ay + t * vy);
            const float dist2 = dx * dx + dy * dy;
            if (dist2 < bestDist) { bestDist = dist2; best = axis; }
        }
        return best;
    }

    void down(Editor& ed, float sx, float sy, ImGuiIO& io) {
        int n = ed.pickEntity(vp_, ed.sceneCam, sx, sy);
        float x = 0, z = 0;
        bool ground = ed.groundAt(vp_, ed.sceneCam, sx, sy, x, z);
        auto& doc = ed.doc();
        auto& d = doc.data;
        const auto& rules = ed.rules();
        switch (ed.tool) {
        case Tool::Select: {
            const int handle = moveHandle(ed, sx, sy);
            if (handle >= 0 && ed.selectedEntity() >= 0 && ground) {
                gizmoAxis_ = handle;
                dragging_ = true;
                dragMoved_ = false;
                gizmoGroundX_ = x;
                gizmoGroundZ_ = z;
                break;
            }
            const Selection picked = ed.pickObject(vp_, ed.sceneCam, sx, sy);
            const bool keepSet = picked.kind == Selection::Entity && ed.isSelected(picked) && !io.KeyCtrl && !io.KeySuper && !io.KeyShift;
            if (!keepSet) pickSelect(ed, picked, io);   // a click on a selected object keeps the set: drag moves all of it
            else ed.selectAdd(picked);
            keepSetClick_ = keepSet && ed.selected().size() > 1;
            clicked_ = picked;
            dragMoved_ = false;
            if (ground && n >= 0 && picked.kind == Selection::Entity && ed.isSelected(picked) && !io.KeyCtrl && !io.KeySuper && !io.KeyShift) {
                dragging_ = true;
                dragOffX_ = d.entities[size_t(n)].x - x;
                dragOffZ_ = d.entities[size_t(n)].z - z;
                if (io.KeyAlt) { // Alt+drag copies
                    doc.beginEdit();
                    DocEntity copy = d.entities[size_t(n)];
                    copy.id.clear();
                    d.entities.push_back(copy);
                    ed.selectEntity(int(d.entities.size() - 1));
                }
            }
            break;
        }
        case Tool::Place: {
            if (n >= 0 && !io.KeyShift) { ed.selectEntity(n); break; }
            if (!ground || rules.types.empty()) break;
            DocEntity e;
            e.type = rules.types[size_t(ed.placeType)].id;
            e.owner = ed.placeOwner;
            e.x = ed.snapCoord(x, ed.placeType); e.z = ed.snapCoord(z, ed.placeType);
            ed.selectEntity(doc.addEntity(e));
            ed.status = "Добавлено: " + rules.types[size_t(ed.placeType)].name;
            break;
        }
        case Tool::Terrain: case Tool::Level: case Tool::Height: case Tool::Field: case Tool::Region:
            if (!ground) break;
            doc.beginEdit();
            painting_ = true;
            drag(ed, sx, sy, io);
            break;
        case Tool::Area:
            if (!ground) break;
            areaDrag_ = true;
            areaX0_ = std::floor(x); areaZ0_ = std::floor(z);
            break;
        case Tool::Start:
            if (!ground || ed.startPlayer < 1 || ed.startPlayer > int(d.players.size())) break;
            doc.beginEdit();
            d.players[size_t(ed.startPlayer - 1)].hasStart = true;
            d.players[size_t(ed.startPlayer - 1)].startX = std::round(x * 2) / 2;
            d.players[size_t(ed.startPlayer - 1)].startZ = std::round(z * 2) / 2;
            break;
        case Tool::Road:
            if (n < 0) { ed.roadStart = -1; break; }
            if (ed.roadStart < 0) { ed.roadStart = n; break; }
            if (n != ed.roadStart) {
                bool added = doc.toggleRoad(ed.roadStart, n);
                ed.status = added ? "Дорога добавлена" : "Дорога удалена";
                ed.roadStart = io.KeyShift ? n : -1;
            }
            break;
        case Tool::Erase:
            if (n >= 0) { doc.removeEntity(n); ed.select({}); }
            else if (int r = ed.pickRoad(vp_, ed.sceneCam, sx, sy); r >= 0) doc.removeRoad(size_t(r));
            break;
        default: break;
        }
    }

    void drag(Editor& ed, float sx, float sy, ImGuiIO& io) {
        float x, z;
        if (!ed.groundAt(vp_, ed.sceneCam, sx, sy, x, z)) return;
        auto& doc = ed.doc();
        auto& d = doc.data;
        int sel = ed.selectedEntity();
        if (dragging_ && gizmoAxis_ >= 0 && sel >= 0 && sel < int(d.entities.size())) {
            const float dx = gizmoAxis_ == 0 ? x - gizmoGroundX_ : 0.0f;
            const float dz = gizmoAxis_ == 2 ? z - gizmoGroundZ_ : 0.0f;
            if (dx != 0.0f || dz != 0.0f) {
                if (!dragMoved_) doc.beginEdit();
                dragMoved_ = true;
                std::vector<int> idx;
                for (int i : ed.selectedEntities()) if (i >= 0 && size_t(i) < d.entities.size() && !ed.objectLocked(d.entities[size_t(i)].owner, d.entities[size_t(i)].type)) idx.push_back(i);
                worldedit::move(doc, idx, dx, dz);
                gizmoGroundX_ = x;
                gizmoGroundZ_ = z;
            }
        } else if (dragging_ && sel >= 0 && sel < int(d.entities.size())) {   // the primary follows the cursor, the rest of the set keeps its offsets
            auto& e = d.entities[size_t(sel)];
            int t = ed.rules().findType(e.type);
            const float dx = ed.snapCoord(x + dragOffX_, t) - e.x, dz = ed.snapCoord(z + dragOffZ_, t) - e.z;
            if (dx != 0 || dz != 0) {
                if (!dragMoved_ && !io.KeyAlt) doc.beginEdit();
                dragMoved_ = true;
                std::vector<int> idx;
                for (int i : ed.selectedEntities()) if (!ed.objectLocked(d.entities[size_t(i)].owner, d.entities[size_t(i)].type) || i == sel) idx.push_back(i);
                worldedit::move(doc, idx, dx, dz);
            }
        }
        if (!painting_) return;
        int cx = int(std::floor(x)), cz = int(std::floor(z));
        const bool erase = io.KeyShift;
        const int r = ed.brush - 1;
        switch (ed.tool) {
            case Tool::Terrain: doc.paint(cx, cz, r, erase ? '.' : ed.paintCell); break;
            case Tool::Level: doc.setLevel(cx, cz, r, erase ? 0 : ed.paintLevel); break;
            case Tool::Height: doc.raise(cx, cz, ed.brush, erase ? -2 : 2); break;
            case Tool::Field:
                if (!ed.rules().resources.empty())
                    doc.paintField(cx, cz, r, ed.rules().resources[size_t(std::clamp(ed.fieldRes, 0, int(ed.rules().resources.size()) - 1))].id, erase ? 0 : ed.fieldAmount);
                break;
            case Tool::Region: doc.paintRegion(cx, cz, r, erase ? '.' : ed.regionLetter); break;
            default: break;
        }
    }

    void up(Editor& ed, float sx, float sy) {
        if (areaDrag_) {
            areaDrag_ = false;
            float x, z;
            if (ed.groundAt(vp_, ed.sceneCam, sx, sy, x, z)) {
                float x0 = std::min(areaX0_, std::floor(x)), z0 = std::min(areaZ0_, std::floor(z));
                float x1 = std::max(areaX0_, std::floor(x)) + 1, z1 = std::max(areaZ0_, std::floor(z)) + 1;
                ed.doc().beginEdit();
                Json& areas = ed.doc().data.extra.member("areas");
                std::string name;
                for (int i = 1;; ++i) { name = "area" + std::to_string(i); if (!areas.has(name)) break; }
                Json a = Json::array();
                for (float v : {x0, z0, x1 - x0, z1 - z0}) a.push(Json::number(double(v), 1));
                areas.set(name, a);
                ed.select({Selection::Area, -1, name});
                ed.status = "Зона «" + name + "» — переименуйте в Инспекторе";
            }
        }
        if (dragging_ && keepSetClick_ && !dragMoved_) ed.select(clicked_);   // a plain click inside a multi-selection picks that one
        keepSetClick_ = false;
        dragging_ = painting_ = false;
        gizmoAxis_ = -1;
    }

    // brush circle, placement ghost and help text over the viewport
    void hud(Editor& ed) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Project actual selections from the shared Selection model. A click in
        // Scene, Hierarchy or Inspector therefore receives the same feedback.
        if (!ed.planetMap() && studioDrawSelection_) {
            dl->PushClipRect(ImVec2(vp_.originX, vp_.originY),
                             ImVec2(vp_.originX + vp_.pointsW, vp_.originY + vp_.pointsH), true);
            for (const Selection& sel : ed.selected()) {
                float wx, wz;
                if (!ed.selectionPos(sel, wx, wz)) continue;
                const float wy = ed.view().heightAt(ed.editGrid(), wx, wz) + 0.4f;
                float2 p;
                if (!ed.sceneCam.worldToScreen({wx, wy, wz}, vp_.pointsW, vp_.pointsH, p)) continue;
                const ImVec2 c(vp_.originX + p.x, vp_.originY + p.y);
                // Screen-space selection pivot and projected XYZ transform axes.
                // This is an editor overlay; a true occlusion-aware mesh silhouette
                // requires a separate selection-mask pass in the scene renderer.
                dl->AddCircle(c, 10.0f, IM_COL32(88, 174, 255, 225), 32, 1.8f);
                dl->AddCircleFilled(c, 3.0f, IM_COL32(225, 238, 255, 255));
                if (sel.kind == Selection::Entity || sel.kind == Selection::Live) {
                    const float axisLength = 1.5f;
                    const float3 ends[3] = {
                        {wx + axisLength, wy, wz},
                        {wx, wy + axisLength, wz},
                        {wx, wy, wz + axisLength}
                    };
                    const ImU32 colors[3] = {
                        IM_COL32(245, 93, 91, 255),
                        IM_COL32(102, 217, 131, 255),
                        IM_COL32(91, 155, 250, 255)
                    };
                    const char* axisLabels[3] = {"X", "Y", "Z"};
                    for (int axis = 0; axis < 3; ++axis) {
                        float2 projected;
                        if (!ed.sceneCam.worldToScreen(ends[axis], vp_.pointsW, vp_.pointsH, projected)) continue;
                        const ImVec2 endpoint(vp_.originX + projected.x, vp_.originY + projected.y);
                        dl->AddLine(c, endpoint, IM_COL32(14, 20, 29, 215), 5.0f);
                        dl->AddLine(c, endpoint, colors[axis], 2.5f);
                        dl->AddCircleFilled(endpoint, 4.0f, colors[axis]);
                        dl->AddText(ImVec2(endpoint.x + 6, endpoint.y - 7), colors[axis], axisLabels[axis]);
                    }
                }
            }
            dl->PopClipRect();
        }
        if (!ed.planetMap()) studioOverlays(ed, dl);
        float x, z;
        if (ed.planetMap()) { // body / face / cell under the cursor
            int body; float3 local;
            if (vp_.hovered && ed.planetAt(vp_, hoverX_, hoverY_, body, local)) {
                const game::PlanetSurface* s = ed.view().planets().surface(body);
                int f = 0; float a = 0, b = 0;
                if (s) s->faceFromDir(normalize(local), f, a, b);
                char buf[128];
                std::snprintf(buf, sizeof buf, "%s  грань %d  x %.1f  y %.1f  высота %.2f", ed.planetBodyId(body).c_str(), f, a, b, s ? s->heightAt(normalize(local)) : 0.0f);
                ui::viewportLabel(dl, ImVec2(vp_.originX + 12, vp_.originY + vp_.pointsH - 26), buf);
            }
            ui::viewportLabel(dl, ImVec2(vp_.originX + 12, vp_.originY + 10), "Планеты: колесо — масштаб, ПКМ — поворот, СКМ / WASD — движение, Tab — следующее тело", ImVec4(0.86f, 0.9f, 1, 0.9f));
            return;
        }
        if (!ed.playing() && vp_.hovered && ed.groundAt(vp_, ed.sceneCam, hoverX_, hoverY_, x, z)) {
            bool brushTool = ed.tool == Tool::Terrain || ed.tool == Tool::Level || ed.tool == Tool::Height || ed.tool == Tool::Field || ed.tool == Tool::Region;
            const RTSCamera& cam = ed.sceneCam;
            auto toScreen = [&](float wx, float wz, ImVec2& out) {
                float2 s;
                if (!cam.worldToScreen({wx, ed.view().heightAt(ed.editGrid(), wx, wz) + 0.1f, wz}, vp_.pointsW, vp_.pointsH, s)) return false;
                out = ImVec2(vp_.originX + s.x, vp_.originY + s.y);
                return true;
            };
            if (brushTool) {
                float r = float(ed.tool == Tool::Height ? ed.brush : ed.brush - 1) + 0.5f, cx = std::floor(x) + 0.5f, cz = std::floor(z) + 0.5f;
                ImVec2 prev;
                bool havePrev = false;
                for (int i = 0; i <= 40; ++i) {
                    float a = 6.2831853f * float(i) / 40.0f;
                    ImVec2 p;
                    if (toScreen(cx + std::cos(a) * r, cz + std::sin(a) * r, p)) {
                        if (havePrev) dl->AddLine(prev, p, IM_COL32(255, 255, 255, 200), 1.5f);
                        prev = p; havePrev = true;
                    } else havePrev = false;
                }
            }
            if (ed.tool == Tool::Place && !ed.rules().types.empty()) {
                const auto& t = ed.rules().types[size_t(ed.placeType)];
                float cx = ed.snapCoord(x, ed.placeType), cz = ed.snapCoord(z, ed.placeType);
                float hw = t.structure ? t.footprintW * 0.5f : float(t.radius.toDouble()), hh = t.structure ? t.footprintH * 0.5f : hw;
                ImVec2 a, b, c, d;
                if (toScreen(cx - hw, cz - hh, a) && toScreen(cx + hw, cz - hh, b) && toScreen(cx + hw, cz + hh, c) && toScreen(cx - hw, cz + hh, d))
                    dl->AddQuadFilled(a, b, c, d, IM_COL32(255, 255, 255, 60)), dl->AddQuad(a, b, c, d, IM_COL32(255, 255, 255, 200));
            }
            if (areaDrag_) {
                float x0 = std::min(areaX0_, std::floor(x)), z0 = std::min(areaZ0_, std::floor(z));
                float x1 = std::max(areaX0_, std::floor(x)) + 1, z1 = std::max(areaZ0_, std::floor(z)) + 1;
                ImVec2 a, b, c, d;
                if (toScreen(x0, z0, a) && toScreen(x1, z0, b) && toScreen(x1, z1, c) && toScreen(x0, z1, d)) dl->AddQuadFilled(a, b, c, d, IM_COL32(255, 220, 80, 60));
            }
            char buf[96];
            std::snprintf(buf, sizeof buf, "x %.1f  z %.1f  уровень %d", x, z, ed.doc().data.level(int(std::floor(x)), int(std::floor(z))));
            ui::viewportLabel(dl, ImVec2(vp_.originX + 12, vp_.originY + vp_.pointsH - 26), buf);
        }
        if (ed.playing())
            ui::viewportLabel(dl, ImVec2(vp_.originX + 12, vp_.originY + 10), ICON_PLAY "  Идёт тест — правка отключена (F5 — стоп)", ImVec4(1, 0.9f, 0.47f, 1));
    }

    // Studio diagnostics share the Scene's projection and do not change the map or simulation.
    void studioOverlays(Editor& ed, ImDrawList* dl) {
        if (!studioDrawBounds_ && !studioDrawAxes_) return;
        const auto& cam = ed.sceneCam;
        auto project = [&](render::float3 xyz, ImVec2& result) {
            render::float2 xy;
            if (!cam.worldToScreen(xyz, vp_.pointsW, vp_.pointsH, xy)) return false;
            result = ImVec2(vp_.originX + xy.x, vp_.originY + xy.y);
            return true;
        };
        dl->PushClipRect(ImVec2(vp_.originX, vp_.originY),
                         ImVec2(vp_.originX + vp_.pointsW, vp_.originY + vp_.pointsH), true);
        if (studioDrawAxes_) {
            ImVec2 a,b;
            if (project({0,0,0},a)) {
                if (project({4,0,0},b)) dl->AddLine(a,b,IM_COL32(234,96,96,255),2);
                if (project({0,4,0},b)) dl->AddLine(a,b,IM_COL32(105,206,119,255),2);
                if (project({0,0,4},b)) dl->AddLine(a,b,IM_COL32(105,154,239,255),2);
            }
        }
        if (studioDrawBounds_) {
            const auto& entities = ed.doc().data.entities;
            for (size_t i=0; i<entities.size(); ++i) {
                const auto& e = entities[i];
                const int ti = ed.rules().findType(e.type);
                const bool structure = ti>=0 && ed.rules().types[size_t(ti)].structure;
                const float rx = structure ? std::max(.5f, float(ed.rules().types[size_t(ti)].footprintW)*.5f) : .5f;
                const float rz = structure ? std::max(.5f, float(ed.rules().types[size_t(ti)].footprintH)*.5f) : .5f;
                const float ground = ed.view().heightAt(ed.editGrid(), e.x, e.z);
                const float height = structure ? 2.5f : 1.0f;
                ImVec2 corners[8]; bool ok[8];
                for (int j=0;j<8;++j) ok[j]=project({e.x+((j&1)?rx:-rx), ground+((j&2)?height:0.f), e.z+((j&4)?rz:-rz)},corners[j]);
                const ImU32 color=ed.isSelected(Selection{Selection::Entity,int(i),{}}) ? IM_COL32(85,175,255,240) : IM_COL32(149,167,191,165);
                for (int j=0;j<8;++j) for (int bit : {1,2,4}) if (!(j&bit) && ok[j] && ok[j|bit])
                    dl->AddLine(corners[j],corners[j|bit],color,1.25f);
            }
        }
        dl->PopClipRect();
    }

    // Click selection: Ctrl / Cmd toggles, Shift adds, a plain click selects one (nothing: clears).
    static void pickSelect(Editor& ed, const Selection& s, const ImGuiIO& io) {
        if (io.KeyCtrl || io.KeySuper) { if (s.kind != Selection::None) ed.selectToggle(s); }
        else if (io.KeyShift) { if (s.kind != Selection::None) ed.selectAdd(s); }
        else ed.select(s);
    }

    Selection clicked_;
    bool keepSetClick_ = false, dragMoved_ = false;
    Viewport vp_;
    bool looking_ = false, panning_ = false, dragging_ = false, painting_ = false, areaDrag_ = false;
    int studioDebugMode_ = 0;
    int gizmoAxis_ = -1;
    float gizmoGroundX_ = 0.0f, gizmoGroundZ_ = 0.0f;
    bool studioDrawBounds_ = false;
    bool studioDrawAxes_ = false;
    bool studioDrawSelection_ = true;
    float dragOffX_ = 0, dragOffZ_ = 0, areaX0_ = 0, areaZ0_ = 0, hoverX_ = 0, hoverY_ = 0;
};

AIVERSE_EDITOR_WINDOW(SceneWindow);

} // namespace aiverse::editor
