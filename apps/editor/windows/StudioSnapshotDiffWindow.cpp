// AIVERSE Studio: deterministic JSON snapshot comparison, offline analysis only.
// This is not a live dual-simulation runner or a binary ECS memory comparator.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include "../Editor.h"
#include "../EditorUtil.h"

namespace aiverse::editor {

class StudioSnapshotDiffWindow final : public EditorWindow {
public:
    const char* id() const override { return "studio_snapshot_diff"; }
    const char* title() const override { return "Snapshot Diff"; }
    const char* group() const override { return "service"; }
    const char* dockArea() const override { return "bottom"; }
    bool openByDefault() const override { return false; }

    void draw(Editor& ed) override {
        ImGui::TextDisabled("Compare two JSON snapshots or saved states (offline)");
        ImGui::InputTextWithHint("##left", "First snapshot .json", left_, sizeof(left_));
        ImGui::InputTextWithHint("##right", "Second snapshot .json", right_, sizeof(right_));
        if (ImGui::Button("Compare")) compare(ed);
        ImGui::SameLine();
        ImGui::BeginDisabled(rows_.empty());
        if (ImGui::Button("Copy report")) ImGui::SetClipboardText(report().c_str());
        ImGui::EndDisabled();
        if (!error_.empty()) { ImGui::TextWrapped("%s", error_.c_str()); return; }
        if (!compared_) return;
        if (rows_.empty()) { ImGui::TextDisabled("Snapshots match structurally and by scalar values."); return; }
        ImGui::Text("Differences: %zu%s", rows_.size(), truncated_ ? " (limit reached)" : "");
        if (ImGui::BeginTable("##snapshotdiff", 3,
                              ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_ScrollY, ImVec2(0, 0))) {
            ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, .36f);
            ImGui::TableSetupColumn("First", ImGuiTableColumnFlags_WidthStretch, .32f);
            ImGui::TableSetupColumn("Second", ImGuiTableColumnFlags_WidthStretch, .32f);
            ImGui::TableHeadersRow();
            for (const auto& row : rows_) {
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(row.path.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(row.left.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(row.right.c_str());
            }
            ImGui::EndTable();
        }
    }
private:
    struct Diff { std::string path, left, right; };
    char left_[512] = {}, right_[512] = {};
    std::vector<Diff> rows_;
    std::string error_;
    bool compared_ = false, truncated_ = false;
    static constexpr size_t kMaxDiffs = 512;
    static std::string shortValue(const Json& j) {
        std::string text = j.dump(-1);
        if (text.size() > 180) text = text.substr(0, 177) + "...";
        return text;
    }
    void add(const std::string& path, const Json& a, const Json& b) {
        if (rows_.size() == kMaxDiffs) { truncated_ = true; return; }
        rows_.push_back({path, shortValue(a), shortValue(b)});
    }
    void walk(const Json& a, const Json& b, const std::string& path, int depth) {
        if (rows_.size() >= kMaxDiffs) { truncated_ = true; return; }
        if (depth >= 48 || a.type() != b.type()) { if (!(a == b)) add(path, a, b); return; }
        if (a.isObject()) {
            for (const auto& [key, av] : a.members()) {
                const std::string child = path + "." + key;
                if (!b.has(key)) add(child, av, Json());
                else walk(av, b[key], child, depth + 1);
            }
            for (const auto& [key, bv] : b.members()) if (!a.has(key)) add(path + "." + key, Json(), bv);
        } else if (a.isArray()) {
            const size_t count = std::min(a.size(), b.size());
            for (size_t i = 0; i < count; ++i) walk(a[i], b[i], path + "[" + std::to_string(i) + "]", depth + 1);
            for (size_t i = count; i < a.size(); ++i) add(path + "[" + std::to_string(i) + "]", a[i], Json());
            for (size_t i = count; i < b.size(); ++i) add(path + "[" + std::to_string(i) + "]", Json(), b[i]);
        } else if (!(a == b)) add(path, a, b);
    }
    void compare(Editor& ed) {
        namespace fs = std::filesystem;
        rows_.clear(); error_.clear(); compared_ = false; truncated_ = false;
        try {
            const auto root = fs::path(ed.ws().project.dir);
            auto resolve = [&](const char* name) -> fs::path {
                if (!*name) throw std::runtime_error("Specify both snapshot paths");
                const fs::path input(name);
                const fs::path file = input.is_absolute() ? input : root / input;
                std::error_code ec;
                if (!fs::is_regular_file(file, ec)) throw std::runtime_error("Not a file: " + file.string());
                if (fs::file_size(file, ec) > 16 * 1024 * 1024 || ec) throw std::runtime_error("Snapshot exceeds 16 MiB: " + file.string());
                return file;
            };
            const auto a = Json::parseFile(resolve(left_).string());
            const auto b = Json::parseFile(resolve(right_).string());
            walk(a, b, "$", 0);
            compared_ = true;
        } catch (const std::exception& e) { error_ = e.what(); }
    }
    std::string report() const {
        std::string out = "path\tfirst\tsecond\n";
        for (const auto& row : rows_) out += row.path + "\t" + row.left + "\t" + row.right + "\n";
        if (truncated_) out += "[truncated]\n";
        return out;
    }
};
AIVERSE_EDITOR_WINDOW(StudioSnapshotDiffWindow);
} // namespace aiverse::editor
