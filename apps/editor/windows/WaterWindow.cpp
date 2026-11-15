// «Вода»: water from splines (docs/water.md § Editor). Lists the map's straits, rivers and seas with the shore
// segments of every bank (embankment, cliffs, beach, natural bank), lets a segment take another shore type (kept over
// "auto"), puts every bank back to auto, deletes a body and bakes the water into the map again. Bodies are drawn by
// agents and scripts through the same operations (water_set, water_shores, water_delete, water_bake).
#include <algorithm>
#include <cstdio>
#include "../Editor.h"
#include "../EditorUtil.h"
#include "../core/WaterOps.h"

namespace aiverse::editor {

class WaterWindow : public EditorWindow {
public:
    const char* id() const override { return "water"; }
    const char* title() const override { return "Вода"; }
    const char* group() const override { return "world"; }
    const char* dockArea() const override { return "right-bottom"; }
    int order() const override { return 4; }
    const char* icon() const override { return ICON_ROADS; }

    void draw(Editor& ed) override {
        const Json& net = ed.ws().map.data.extra["waterNetwork"];
        if (ImGui::Button("Пересчитать берега")) result_ = ed.ws().call("water_bake", Json::object());
        ImGui::SetItemTooltip("Заново разметить клетки воды и скал и пересчитать «авто»-участки берега (после правки дорог или правил)");
        if (result_.has("error")) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", result_.getStr("error").c_str());
        else if (result_.has("warning")) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%s", result_.getStr("warning").c_str());
        if (!net.isObject() || net["bodies"].size() == 0) {
            ImGui::TextWrapped("На карте нет воды из сплайнов. Пролив, реку или море добавляет операция water_set (агент, MCP).");
            return;
        }
        Json types;
        try { types = ed.ws().call("water_types", Json::object())["types"]; } catch (...) {}
        for (auto& b : net["bodies"].items()) {
            const int id = int(b.getInt("id"));
            const std::string kind = b.getStr("kind");
            const char* kindRu = kind == "strait" ? "Пролив" : kind == "sea" ? "Море" : "Река";
            char head[64];
            std::snprintf(head, sizeof head, "%s %d##water%d", kindRu, id, id);
            if (!ImGui::CollapsingHeader(head, ImGuiTreeNodeFlags_DefaultOpen)) continue;
            ImGui::PushID(id);
            if (ImGui::SmallButton("Все берега — авто")) { Json q = Json::object(); q.set("id", Json::number(int64_t(id))); q.set("reset", Json::fromBool(true)); result_ = ed.ws().call("water_shores", q); }
            ImGui::SameLine();
            if (ImGui::SmallButton("Удалить")) { Json q = Json::object(); q.set("id", Json::number(int64_t(id))); result_ = ed.ws().call("water_delete", q); ImGui::PopID(); return; }
            if (ImGui::BeginTable("##shores", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Берег"); ImGui::TableSetupColumn("От, м"); ImGui::TableSetupColumn("До, м"); ImGui::TableSetupColumn("Тип");
                ImGui::TableHeadersRow();
                int row = 0;
                for (auto& s : b["shores"].items()) {
                    ImGui::PushID(row++);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(kind == "sea" ? "—" : s.getInt("side") < 0 ? "левый" : "правый");
                    ImGui::TableNextColumn(); ImGui::Text("%.0f", s["from"].toDouble());
                    ImGui::TableNextColumn(); ImGui::Text("%.0f", s["to"].toDouble());
                    ImGui::TableNextColumn();
                    const std::string cur = s.getStr("type");
                    std::string label = cur;
                    for (auto& t : types.items()) if (t.getStr("id") == cur) label = t.getStr("name");
                    if (!s.getBool("auto")) label += " (вручную)";
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::BeginCombo("##type", label.c_str())) {
                        for (auto& t : types.items()) {
                            if (!ImGui::Selectable(t.getStr("name").c_str(), t.getStr("id") == cur)) continue;
                            Json q = Json::object();
                            q.set("id", Json::number(int64_t(id)));
                            q.set("side", Json::number(s.getInt("side", 1)));
                            q.set("from", s["from"]); q.set("to", s["to"]);
                            q.set("type", Json::string(t.getStr("id")));
                            result_ = ed.ws().call("water_shores", q);
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::PopID();
        }
    }

private:
    Json result_ = Json::object();
};

AIVERSE_EDITOR_WINDOW(WaterWindow);

} // namespace aiverse::editor
