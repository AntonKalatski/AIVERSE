// AIVERSE Studio: compact project asset browser for the left shelf.
// Keeps the existing Project editor intact (it remains a full-size bottom tab).
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
#include "../Editor.h"
#include "../EditorUtil.h"

namespace fs = std::filesystem;
namespace aiverse::editor {

class AssetsWindow final : public EditorWindow {
public:
    const char* id() const override { return "assets"; }
    const char* title() const override { return "Ресурсы"; }
    const char* group() const override { return "scene"; }
    const char* dockArea() const override { return "left-bottom"; }
    int order() const override { return 0; }

    void draw(Editor& ed) override {
        const fs::path base(ed.ws().project.dir);
        if (base.empty()) { ui::emptyStateHere(ICON_PROJECT, "Проект не открыт", "Файл → Открыть проект"); return; }
        if (relative_.empty()) relative_ = ".";
        fs::path current = (base / relative_).lexically_normal();
        std::error_code ec;
        if (!fs::exists(current, ec) || !fs::is_directory(current, ec)) { relative_ = "."; current = base; }

        ImGui::TextDisabled(ICON_PROJECT "  ASSETS");
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_HOME "##assetHome")) relative_ = ".";
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Корень проекта");
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FOLDER_UP "##assetUp")) {
            fs::path up = fs::path(relative_).parent_path();
            relative_ = up.empty() ? "." : up.generic_string();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("На уровень выше");
        ImGui::TextDisabled("%s", relative_.c_str());
        ui::searchField("##assetSearch", filter_, "Поиск файлов");
        ImGui::Separator();
        std::vector<fs::directory_entry> items;
        for (fs::directory_iterator it(current, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (!filter_.empty() && !ui::contains(name, filter_)) continue;
            items.push_back(*it);
        }
        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
            std::error_code ea, eb;
            const bool da = a.is_directory(ea), db = b.is_directory(eb);
            return da != db ? da : a.path().filename().string() < b.path().filename().string();
        });
        ImGui::BeginChild("##assetList", ImVec2(0, 0), ImGuiChildFlags_None);
        for (const auto& item : items) {
            std::error_code err;
            const bool dir = item.is_directory(err);
            const std::string name = item.path().filename().string();
            const std::string label = std::string(dir ? ICON_PROJECT : ICON_FILE) + "  " + name;
            const std::string abs = item.path().string();
            ImGui::PushID(abs.c_str());
            if (ImGui::Selectable(label.c_str(), selected_ == abs, ImGuiSelectableFlags_AllowDoubleClick)) {
                selected_ = abs;
                if (dir && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    relative_ = item.path().lexically_relative(base).generic_string();
                    filter_.clear();
                    ImGui::PopID();
                    break;
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nДвойной щелчок — открыть папку", abs.c_str());
            if (ImGui::BeginPopupContextItem("##assetCtx")) {
                if (ImGui::MenuItem("Копировать путь")) ImGui::SetClipboardText(abs.c_str());
                if (dir && ImGui::MenuItem("Открыть папку")) {
                    relative_ = item.path().lexically_relative(base).generic_string();
                    filter_.clear();
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
private:
    std::string relative_ = ".";
    std::string selected_;
    std::string filter_;
};
AIVERSE_EDITOR_WINDOW(AssetsWindow);
} // namespace aiverse::editor
