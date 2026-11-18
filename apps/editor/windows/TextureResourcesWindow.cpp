// Read-only graphics diagnostics: live texture allocations on the native GPU.
// No editor gameplay logic or ECS state. GPU footprint is an approximation,
// not a GPU driver memory measurement or streaming residency query.
#include "../Editor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <memory>
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
struct TextureReadPreview {
    uint64_t revision = 0;
    uint32_t resourceId = 0;
    int mip = 0, layer = 0, width = 0, height = 0;
    int cellsX = 0, cellsY = 0;
    bool pending = false;
    std::string status;
    std::vector<ImU32> cells; // 32x32 max; never hold the full GPU readback
};

// Read the exact linear 16-bit float texel, including negatives. Tonemap
// positive HDR values only for an approximate on-screen diagnostic.
float diagnosticHalf(const uint8_t* p) {
    const uint16_t bits = uint16_t(p[0]) | (uint16_t(p[1]) << 8);
    const int sign = bits & 0x8000u ? -1 : 1;
    const int exponent = (bits >> 10) & 31;
    const int mantissa = bits & 1023;
    if (exponent == 31) return 0.f; // malformed/non-finite content, preview only
    if (exponent == 0) return float(sign) * std::ldexp(float(mantissa), -24);
    return float(sign) * std::ldexp(1.f + float(mantissa)/1024.f, exponent - 15);
}
uint8_t diagnosticByte(float linear) {
    // Approximate Reinhard exposure then linear->sRGB presentation.
    const float v = std::max(0.f, linear);
    const float mapped = v / (1.f + v);
    const float display = mapped <= .0031308f ? mapped * 12.92f :
        1.055f * std::pow(mapped, 1.f/2.4f) - .055f;
    return uint8_t(std::clamp(int(display * 255.f + .5f),0,255));
}
void decodeReadPreview(TextureReadPreview& state, gfx::TextureReadbackResult image) {
    state.pending = false;
    state.cells.clear();
    state.cellsX = state.cellsY = state.width = state.height = 0;
    if (!image.success()) {
        state.status = "GPU readback failed or resource was released.";
        return;
    }
    const int bpp = image.format == gfx::PixelFormat::RGBA16F ? 8 : 4;
    if (image.format != gfx::PixelFormat::RGBA16F &&
        image.format != gfx::PixelFormat::RGBA8 &&
        image.format != gfx::PixelFormat::RGBA8_sRGB) {
        state.status = "Readback returned an unsupported preview format.";
        return;
    }
    const size_t expected = size_t(image.width) * size_t(image.height) * size_t(bpp);
    if (image.width <= 0 || image.height <= 0 ||
        image.pixels.size() != expected) {
        state.status = "Readback pixel count does not match the texture dimensions.";
        return;
    }
    state.width = image.width;
    state.height = image.height;
    state.cellsX = std::min(32,image.width);
    state.cellsY = std::min(32,image.height);
    state.cells.reserve(size_t(state.cellsX)*size_t(state.cellsY));
    for (int y=0;y<state.cellsY;++y) {
        for (int x=0;x<state.cellsX;++x) {
            const int sx = int((int64_t(x)*image.width)/state.cellsX);
            const int sy = int((int64_t(y)*image.height)/state.cellsY);
            const uint8_t* sample = image.pixels.data() +
                (size_t(sy)*size_t(image.width)+size_t(sx))*size_t(bpp);
            uint8_t rgb[3]{};
            if (bpp == 8) {
                for (int c=0;c<3;++c)
                    rgb[c] = diagnosticByte(diagnosticHalf(sample+c*2));
            } else {
                for (int c=0;c<3;++c) rgb[c]=sample[c];
            }
            state.cells.push_back(IM_COL32(rgb[0],rgb[1],rgb[2],255));
        }
    }
    state.status = "Captured a small CPU preview of the selected GPU subresource.";
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
                ImGui::TextDisabled("Native 2D preview is unavailable for this resource.");
            }

            // Readback is optional per backend; requests must be made on the
            // device thread, outside render/compute encoders. A callback runs
            // later at beginFrame and owns its staging bytes. Never capture
            // EditorWindow 'this' in an asynchronous GPU callback.
            readMip_ = std::clamp(readMip_,0,std::max(0,chosen->mipLevels-1));
            readLayer_ = std::clamp(readLayer_,0,std::max(0,chosen->arrayLayers-1));
            ImGui::SliderInt("Read mip", &readMip_, 0, std::max(0,chosen->mipLevels-1));
            if (chosen->arrayLayers > 1)
                ImGui::SliderInt("Read face / layer", &readLayer_, 0, chosen->arrayLayers-1);
            else readLayer_ = 0;
            if (ImGui::Button("Read GPU mip / face")) {
                auto state = preview_;
                const uint64_t revision = ++state->revision;
                state->resourceId = selectedId_;
                state->mip = readMip_;
                state->layer = readLayer_;
                state->status.clear();
                state->cells.clear();
                state->pending = true;
                std::weak_ptr<TextureReadPreview> weak = state;
                gfx::TextureReadbackDesc request;
                request.mipLevel = readMip_;
                request.arrayLayer = readLayer_;
                if (!editor.device().readTextureAsync(chosen->handle, request,
                    [weak,revision](gfx::TextureReadbackResult result) {
                        if (auto state = weak.lock()) {
                            if (state->revision == revision)
                                decodeReadPreview(*state,std::move(result));
                        }
                    })) {
                    state->pending = false;
                    state->status = "GPU readback unsupported for this format/backend or active pass.";
                } else {
                    state->status = "GPU transfer pending; result arrives in a later frame.";
                }
            }
            if (preview_->resourceId == selectedId_) {
                ImGui::TextDisabled("%s", preview_->status.c_str());
                if (preview_->pending)
                    ImGui::TextDisabled("Pending GPU fence/completion...");
                else if (!preview_->cells.empty()) {
                    ImGui::Text("%dx%d mip %d face/layer %d",
                                preview_->width,preview_->height,
                                preview_->mip,preview_->layer);
                    const float display = 160.f;
                    const float dx = display / float(preview_->cellsX);
                    const float dy = display / float(preview_->cellsY);
                    const ImVec2 start = ImGui::GetCursorScreenPos();
                    ImDrawList* draw = ImGui::GetWindowDrawList();
                    for (int y=0;y<preview_->cellsY;++y)
                        for (int x=0;x<preview_->cellsX;++x) {
                            const ImVec2 a{start.x+dx*x,start.y+dy*y};
                            const ImVec2 b{start.x+dx*(x+1),start.y+dy*(y+1)};
                            draw->AddRectFilled(a,b,
                                preview_->cells[size_t(y)*size_t(preview_->cellsX)+size_t(x)]);
                        }
                    ImGui::Dummy(ImVec2(display,display));
                    ImGui::TextDisabled("CPU 32x32 max diagnostic; HDR approximately tonemapped.");
                }
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
    int readMip_ = 0, readLayer_ = 0;
    std::shared_ptr<TextureReadPreview> preview_ = std::make_shared<TextureReadPreview>();
};

AIVERSE_EDITOR_WINDOW(TextureResourcesWindow);
} // namespace aiverse::editor
