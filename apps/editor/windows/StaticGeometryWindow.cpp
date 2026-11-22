// Static geometry of the open game (game.json "geometryScene", geometry.json by default):
// the objects drawn by GeometryScene, and per object how it is drawn: automatically
// (STRATA or the ordinary LOD path, chosen per model), forced STRATA or forced LOD.
// Writes through the geometry_strata_set operation (validated, atomic, reloads the view).
#include <filesystem>
#include <fstream>
#include <sstream>
#include "../Editor.h"
#include "../EditorUtil.h"

namespace aiverse::editor {

class StaticGeometryWindow : public EditorWindow {
public:
    const char* id() const override { return "static-geometry"; }
    const char* title() const override { return "Статическая геометрия"; }
    const char* group() const override { return "world"; }
    const char* dockArea() const override { return "right-bottom"; }
    int order() const override { return 7; }
    bool openByDefault() const override { return false; }

    void draw(Editor& ed) override {
        auto& ws = ed.ws();
        if (ws.project.dir.empty()) { ui::emptyStateHere(ICON_OBJECTS, "Игра не открыта", "Файл → Открыть проект"); return; }
        const std::string rel = ws.project.manifest.getStr("geometryScene", "geometry.json");
        const auto path = std::filesystem::path(ws.project.dir) / rel;
        std::error_code ec;
        const auto stamp = std::filesystem::last_write_time(path, ec);
        if (ec) {
            ui::emptyStateHere(ICON_OBJECTS, "Нет статической геометрии", "Сцена задаётся в game.json → geometryScene");
            return;
        }
        if (rel != file_ || stamp != stamp_) reload(path, rel, stamp);

        const auto& view = ed.view().geometryScene();
        const auto& g = view.stats();
        ui::section(ICON_OBJECTS, "Сцена");
        ImGui::TextUnformatted(rel.c_str());
        ImGui::Text("Объектов: %zu   STRATA: %s   источников STRATA: %u, LOD: %u",
                    scene_["instances"].size(), view.strataPortableRenderingEnabled() ? "вкл" : "выкл",
                    unsigned(g.strataAutoStrataSources), unsigned(g.strataAutoLodSources));
        if (g.strataCookedSources || g.strataCookStaleSources)
            ImGui::Text("Подготовлено на диске: %u, устарело: %u", unsigned(g.strataCookedSources), unsigned(g.strataCookStaleSources));
        ui::help("Авто: STRATA или обычные LOD выбираются по модели (плотные модели — STRATA). "
                 "«STRATA» и «LOD» принудительно задают путь отрисовки для этого объекта.");

        ui::section(ICON_LEVELS, "Объекты");
        static const char* modes[] = {"Авто", "STRATA", "LOD"};
        if (ImGui::BeginTable("##objects", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
                              ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 14))) {
            ImGui::TableSetupColumn("Объект");
            ImGui::TableSetupColumn("Модель");
            ImGui::TableSetupColumn("Отрисовка", ImGuiTableColumnFlags_WidthFixed, 230);
            ImGui::TableHeadersRow();
            int row = 0;
            for (auto& i : scene_["instances"].items()) {
                ImGui::TableNextRow();
                ImGui::PushID(row++);
                const std::string id = i.getStr("id");
                ImGui::TableNextColumn(); ImGui::TextUnformatted(id.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(std::filesystem::path(i.getStr("model")).filename().string().c_str());
                ImGui::TableNextColumn();
                int mode = 0;
                if (i["strata"].type() == Json::Type::Bool) mode = i["strata"].boolean() ? 1 : 2;
                if (ui::segmented("##mode", mode, modes, 3)) {
                    Json p = Json::object();
                    p.set("id", Json::string(id));
                    p.set("mode", Json::string(mode == 1 ? "strata" : mode == 2 ? "lod" : "auto"));
                    const Json r = ws.call("geometry_strata_set", p);
                    message_ = r.has("error") ? r.getStr("error") : "";
                    file_.clear(); // re-read the scene
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (!message_.empty()) ImGui::TextColored(ImVec4(1, .45f, .4f, 1), "%s", message_.c_str());

        const auto& decisions = view.strataSelectDecisions();
        if (!decisions.empty()) {
            ui::section(ICON_EYE, "Решения по моделям");
            for (const auto& d : decisions) ImGui::TextWrapped("%s", d.c_str());
        }
    }

private:
    void reload(const std::filesystem::path& path, const std::string& rel, std::filesystem::file_time_type stamp) {
        file_ = rel;
        stamp_ = stamp;
        try {
            std::ifstream f(path, std::ios::binary);
            std::stringstream s;
            s << f.rdbuf();
            scene_ = Json::parse(s.str());
        } catch (const std::exception& e) {
            scene_ = Json::object();
            message_ = e.what();
        }
    }
    std::string file_, message_;
    std::filesystem::file_time_type stamp_{};
    Json scene_ = Json::object();
};

AIVERSE_EDITOR_WINDOW(StaticGeometryWindow);

} // namespace aiverse::editor
