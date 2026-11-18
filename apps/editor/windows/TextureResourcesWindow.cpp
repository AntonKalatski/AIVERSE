// Read-only graphics diagnostics: live texture allocations on the native GPU.
// No editor gameplay logic or ECS state. GPU footprint is an approximation,
// not a GPU driver memory measurement or streaming residency query.
#include "../Editor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace aiverse::editor {
namespace {

const char* textureDimensionName(gfx::TextureDimension d) {
    switch (d) {
    case gfx::TextureDimension::Texture1D: return "1D";
    case gfx::TextureDimension::Texture2D: return "2D";
    case gfx::TextureDimension::Texture3D: return "3D";
    case gfx::TextureDimension::Texture2DArray: return "2D Array";
    case gfx::TextureDimension::Cube: return "Cube";
    case gfx::TextureDimension::CubeArray: return "Cube Array";
    }
    return "?";
}
const char* textureFormatName(gfx::PixelFormat f) {
    switch (f) {
    case gfx::PixelFormat::RGBA8: return "RGBA8";
    case gfx::PixelFormat::R8: return "R8";
    case gfx::PixelFormat::Depth: return "Depth";
    case gfx::PixelFormat::RGBA32F: return "RGBA32F";
    case gfx::PixelFormat::ETC2_RGB8: return "ETC2 RGB8";
    case gfx::PixelFormat::ETC2_RGBA8: return "ETC2 RGBA8";
    case gfx::PixelFormat::BC1: return "BC1";
    case gfx::PixelFormat::BC3: return "BC3";
    case gfx::PixelFormat::BC7: return "BC7";
    case gfx::PixelFormat::ASTC_4x4: return "ASTC 4x4";
    case gfx::PixelFormat::ASTC_6x6: return "ASTC 6x6";
    case gfx::PixelFormat::RGBA8_sRGB: return "RGBA8 sRGB";
    case gfx::PixelFormat::RGBA16F: return "RGBA16F";
    case gfx::PixelFormat::BC4: return "BC4";
    case gfx::PixelFormat::BC5: return "BC5";
    case gfx::PixelFormat::EAC_R11: return "EAC R11";
    case gfx::PixelFormat::RG8: return "RG8";
    case gfx::PixelFormat::R16F: return "R16F";
    case gfx::PixelFormat::RG16F: return "RG16F";
    case gfx::PixelFormat::R32F: return "R32F";
    case gfx::PixelFormat::RG32F: return "RG32F";
    case gfx::PixelFormat::BC6H_UFloat: return "BC6H UF";
    case gfx::PixelFormat::BC6H_SFloat: return "BC6H SF";
    }
    return "Unknown";
}
bool matchesTextureFilter(const gfx::TextureResourceInfo& r, const char* value) {
    if (!value || !*value) return true;
    char id[32]{};
    std::snprintf(id, sizeof id, "%u", r.handle.id);
    std::string text = std::string(id) + " " + textureDimensionName(r.dimension) +
                       " " + textureFormatName(r.format);
    for (char& c : text)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    std::string filter = value;
    for (char& c : filter)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return text.find(filter) != std::string::npos;
}
} // namespace

class TextureResourcesWindow final : public EditorWindow {
public:
    const char* id() const override { return "texture_resources"; }
    const char* title() const override { return "GPU Текстуры"; }
    const char* group() const override { return "look"; }
    const char* dockArea() const override { return "right"; }
    int order() const override { return 65; }
    bool openByDefault() const override { return false; }

    void draw(Editor& editor) override {
        auto resources = editor.device().textureResources();
        const size_t allCount = resources.size();
        size_t sumBytes = 0, renderTargets = 0, multisampled = 0;
        for (const auto& info : resources) {
            if (sumBytes <= SIZE_MAX - info.estimatedBytes)
                sumBytes += info.estimatedBytes;
            else sumBytes = SIZE_MAX;
            if (info.renderTarget) ++renderTargets;
            if (info.sampleCount > 1) ++multisampled;
        }
        ImGui::Text("Live: %zu   Targets: %zu   MSAA: %zu",
                    allCount, renderTargets, multisampled);
        ImGui::Text("Estimated GPU storage: %.2f MiB",
                    double(sumBytes) / (1024.0 * 1024.0));
        ImGui::TextDisabled("Estimate only: excludes driver padding, swapchain and transient memory.");
        ImGui::Separator();
        ImGui::InputText("Filter ID / dimension / format", filter_.data(), filter_.size());
        ImGui::Checkbox("Render targets only", &renderTargetsOnly_);
        ImGui::SameLine();
        ImGui::Checkbox("Largest first", &sortByBytes_);

        // Preview is read-only: use the same texture-handle ImGui binding
        // used by Viewport::image, never create/destroy the selected texture.
        // Restrict to ordinary 2D single-sampled formats with a known
        // portable ImGui sampler2D path. Arrays/cubes/depth need readback.
        const gfx::TextureResourceInfo* chosen = nullptr;
        for (const auto& item : resources)
            if (item.handle.id == selectedId_) { chosen = &item; break; }
        if (!chosen) selectedId_ = 0;
        if (chosen) {
            ImGui::Separator();
            ImGui::Text("Selected #%u", selectedId_);
            const bool previewable =
                chosen->dimension == gfx::TextureDimension::Texture2D &&
                chosen->sampleCount == 1 &&
                !gfx::isCompressedTextureFormat(chosen->format) &&
                chosen->format != gfx::PixelFormat::Depth;
            if (previewable) {
                const bool flip = chosen->renderTarget &&
                                  !editor.device().info().renderTargetFlipY;
                ImGui::Image(ImTextureRef(ImTextureID(chosen->handle.id)),
                             ImVec2(160.f, 160.f),
                             ImVec2(0.f, flip ? 1.f : 0.f),
                             ImVec2(1.f, flip ? 0.f : 1.f));
            } else {
                ImGui::TextDisabled("Preview needs a 2D single-sample color resource.");
                ImGui::TextDisabled("Arrays, cubes and MSAA require an explicit readback.");
            }
            ImGui::Separator();
        }

        if (sortByBytes_)
            std::sort(resources.begin(), resources.end(), [](const auto& a, const auto& b) {
                if (a.estimatedBytes != b.estimatedBytes)
                    return a.estimatedBytes > b.estimatedBytes;
                return a.handle.id < b.handle.id;
            });
        else
            std::sort(resources.begin(), resources.end(), [](const auto& a, const auto& b) {
                return a.handle.id < b.handle.id;
            });

        ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV |
                                ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_Resizable |
                                ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("##gpu_textures", 6, flags)) {
            ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed, 62.f);
            ImGui::TableSetupColumn("Dimension", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Size / Layers", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Mips / MSAA", ImGuiTableColumnFlags_WidthFixed, 92.f);
            ImGui::TableSetupColumn("Est. KiB", ImGuiTableColumnFlags_WidthFixed, 86.f);
            ImGui::TableHeadersRow();
            for (const auto& info : resources) {
                if (renderTargetsOnly_ && !info.renderTarget) continue;
                if (!matchesTextureFilter(info, filter_.data())) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                char handleLabel[32]{};
                std::snprintf(handleLabel, sizeof handleLabel, "%u", info.handle.id);
                if (ImGui::Selectable(handleLabel, selectedId_ == info.handle.id))
                    selectedId_ = info.handle.id;
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(textureDimensionName(info.dimension));
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(textureFormatName(info.format));
                ImGui::TableSetColumnIndex(3);
                if (info.dimension == gfx::TextureDimension::Texture3D)
                    ImGui::Text("%dx%dx%d", info.width, info.height, info.depth);
                else if (info.arrayLayers > 1)
                    ImGui::Text("%dx%d [%d]", info.width, info.height, info.arrayLayers);
                else
                    ImGui::Text("%dx%d", info.width, info.height);
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%d / %dx", info.mipLevels, info.sampleCount);
                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%.1f", double(info.estimatedBytes) / 1024.0);
            }
            ImGui::EndTable();
        }
    }

private:
    std::array<char, 96> filter_{};
    bool renderTargetsOnly_ = false;
    bool sortByBytes_ = true;
    uint32_t selectedId_ = 0;
};

AIVERSE_EDITOR_WINDOW(TextureResourcesWindow);
} // namespace aiverse::editor
