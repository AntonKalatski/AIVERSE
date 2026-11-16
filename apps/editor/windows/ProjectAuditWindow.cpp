// AIVERSE Studio: project asset references audit. Never modifies source files.
// Scans JSON, RML, RCSS and Lua strings for paths to known asset types, then reports missing references.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <string>
#include <vector>
#include "../Editor.h"
#include "../EditorUtil.h"

namespace aiverse::editor {
class ProjectAuditWindow final : public EditorWindow {
public:
    const char* id() const override { return "studio_asset_audit"; }
    const char* title() const override { return "Asset Audit"; }
    const char* group() const override { return "service"; }
    const char* dockArea() const override { return "bottom"; }
    bool openByDefault() const override { return false; }
    void draw(Editor& ed) override {
        if (ed.ws().project.dir.empty()) { ImGui::TextDisabled("Откройте проект."); return; }
        if (project_ != ed.ws().project.dir) { project_ = ed.ws().project.dir; rows_.clear(); scanned_ = 0; truncated_ = false; }
        if (ImGui::Button("Сканировать зависимости")) scan();
        ImGui::SameLine(); ImGui::TextDisabled("Проверено файлов: %zu; подозрительных ссылок: %zu", scanned_, rows_.size());
        if (truncated_) ImGui::TextDisabled("Достигнут предел сканирования; результат неполный.");
        if (!rows_.empty() && ImGui::BeginTable("##assetRefs", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Source"); ImGui::TableSetupColumn("Missing reference"); ImGui::TableHeadersRow();
            for (const auto& row : rows_) {
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(row.source.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(row.reference.c_str());
            }
            ImGui::EndTable();
        } else if (rows_.empty() && scanned_ > 0) ImGui::TextDisabled("Не найдено подозрительных ссылок. Это эвристическая проверка, не полная графовая модель ассетов.");
    }
private:
    struct Missing { std::string source, reference; };
    std::string project_;
    std::vector<Missing> rows_;
    size_t scanned_ = 0;
    bool truncated_ = false;
    static bool assetExt(const std::string& name) {
        const auto ext = std::filesystem::path(name).extension().string();
        static const std::set<std::string> known = {".png", ".jpg", ".jpeg", ".webp", ".gltf", ".glb", ".obj", ".fbx", ".lua", ".rml", ".rcss", ".wav", ".ogg", ".mp3", ".json", ".rtex"};
        return known.count(ext) > 0;
    }
    void scan() {
        namespace fs = std::filesystem;
        rows_.clear(); scanned_ = 0; truncated_ = false;
        std::error_code ec;
        const fs::path root = fs::weakly_canonical(project_, ec);
        if (ec) return;
        // Extract quoted paths; relative paths are resolved from project root and from the referencing file's directory.
        const std::regex quoted(R"RE(["']([^"'\r\n]{3,240})["'])RE");
        const std::set<std::string> inspected = {".json", ".lua", ".rml", ".rcss"};
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end && !ec; it.increment(ec)) {
            if (it->is_directory(ec)) {
                if (it->path().filename() == ".git" || it->path().filename() == "node_modules" || it->path().filename() == "build") it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec) || !inspected.count(it->path().extension().string()) || it->file_size(ec) > 1024*1024) continue;
            if (++scanned_ > 3000) { truncated_ = true; break; }
            std::ifstream file(it->path(), std::ios::binary);
            if (!file) continue;
            const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            for (std::sregex_iterator match(content.begin(), content.end(), quoted), last; match != last; ++match) {
                std::string path = (*match)[1].str();
                if (!assetExt(path) || path.find("://") != std::string::npos || path.find("${") != std::string::npos) continue;
                fs::path ref(path);
                if (ref.is_absolute() || path.find("..") != std::string::npos || path.find('\\') != std::string::npos) continue;
                if (fs::exists(root / ref, ec) || fs::exists(it->path().parent_path() / ref, ec)) continue;
                rows_.push_back({it->path().lexically_relative(root).generic_string(), path});
                if (rows_.size() >= 1000) { truncated_ = true; return; }
            }
        }
    }
};
AIVERSE_EDITOR_WINDOW(ProjectAuditWindow);
} // namespace aiverse::editor
