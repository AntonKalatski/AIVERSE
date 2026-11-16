// CPU-side frame capture: frame time, simulation time and draw-call count.
// This is NOT a GPU command capture or per-draw replay.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include "../Editor.h"
#include "../EditorUtil.h"
namespace aiverse::editor {
class StudioFrameCaptureWindow final : public EditorWindow {
public:
    const char* id() const override { return "studio_frame_capture"; }
    const char* title() const override { return "Frame Capture"; }
    const char* group() const override { return "service"; }
    const char* dockArea() const override { return "bottom"; }
    bool openByDefault() const override { return false; }
    void draw(Editor& ed) override {
        int frame = ImGui::GetFrameCount();
        if (capturing_ && frame != lastFrame_ && !ed.frameHistory.empty()) {
            lastFrame_ = frame;
            if (samples_.size() >= 6000) { samples_.erase(samples_.begin()); if (selected_ > 0) --selected_; }
            const auto& s = ed.frameHistory.back();
            samples_.push_back({s.frameMs, s.simMs, s.drawCalls});
            selected_ = int(samples_.size()) - 1;
        }
        if (ImGui::Button(capturing_ ? "Pause capture" : "Start capture")) { capturing_ = !capturing_; lastFrame_ = -1; }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) { samples_.clear(); selected_ = 0; lastFrame_ = -1; }
        ImGui::SameLine();
        ImGui::BeginDisabled(samples_.empty());
        if (ImGui::Button("Copy CSV")) ImGui::SetClipboardText(csv().c_str());
        ImGui::EndDisabled();
        ImGui::TextDisabled("CPU frame / simulation / draw calls; GPU timings require GPU queries");
        if (samples_.empty()) return;
        selected_ = std::clamp(selected_, 0, int(samples_.size()) - 1);
        const auto& s = samples_[size_t(selected_)];
        ImGui::Text("Frame %d / %zu  CPU %.2f ms  Sim %.2f ms  Draws %u", selected_, samples_.size(), s.frameMs, s.simMs, s.drawCalls);
        if (samples_.size() > 1) ImGui::SliderInt("Scrub", &selected_, 0, int(samples_.size()) - 1);
        std::vector<float> cpu, sim, draws;
        cpu.reserve(samples_.size()); sim.reserve(samples_.size()); draws.reserve(samples_.size());
        float maxMs = 16.67f, maxDraws = 1.0f;
        for (const auto& sample : samples_) {
            cpu.push_back(sample.frameMs); sim.push_back(sample.simMs); draws.push_back(float(sample.drawCalls));
            maxMs = std::max(maxMs, std::max(sample.frameMs, sample.simMs));
            maxDraws = std::max(maxDraws, float(sample.drawCalls));
        }
        ImGui::PlotLines("CPU frame ms", cpu.data(), int(cpu.size()), 0, nullptr, 0, maxMs * 1.1f, ImVec2(-1, 62));
        ImGui::PlotLines("Simulation ms", sim.data(), int(sim.size()), 0, nullptr, 0, maxMs * 1.1f, ImVec2(-1, 46));
        ImGui::PlotLines("Draw calls", draws.data(), int(draws.size()), 0, nullptr, 0, maxDraws * 1.1f, ImVec2(-1, 46));
    }
private:
    struct Sample { float frameMs, simMs; uint32_t drawCalls; };
    std::vector<Sample> samples_;
    bool capturing_ = false;
    int selected_ = 0, lastFrame_ = -1;
    std::string csv() const {
        std::string out = "frame,cpu_ms,simulation_ms,draw_calls\n";
        char line[128];
        for (size_t i = 0; i < samples_.size(); ++i) {
            const auto& s = samples_[i];
            std::snprintf(line, sizeof(line), "%zu,%.4f,%.4f,%u\n", i, s.frameMs, s.simMs, s.drawCalls);
            out += line;
        }
        return out;
    }
};
AIVERSE_EDITOR_WINDOW(StudioFrameCaptureWindow);
} // namespace aiverse::editor
