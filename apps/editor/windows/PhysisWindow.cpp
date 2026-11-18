// PHYSIS debug view — opt-in read-only nature fields in the universal editor.
// Live game ECS is NEVER mutated here. The offline demo owns an independent
// World and can be stepped explicitly, even when the current project has no PHYSIS.
#include "../Editor.h"
#include "../EditorUtil.h"
#include "engine/physis/PhysisDebugPalette.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace aiverse::editor {
namespace {

using namespace aiverse::physis;

constexpr const char* kLayers[] = {
    "Температура, °C", "Влажность", "Давление, hPa", "Дождь",
    "Снег", "Влажность грунта", "Снежный покров", "Лёд",
    "Скорость ветра, м/с", "Вертикальный ветер", "Гравитация, м/с²",
    "Пожар", "Землетрясение", "Локальное затопление PHYSIS"
};
constexpr const char* kPalettes[] = {"Тепловая", "Контрастная", "Вода"};

ImU32 colorToImU32(DebugColor c) { return IM_COL32(c.r, c.g, c.b, c.a); }

} // namespace

class PhysisWindow final : public EditorWindow {
public:
    const char* id() const override { return "physis"; }
    const char* title() const override { return "PHYSIS · Природа"; }
    const char* group() const override { return "world"; }
    const char* dockArea() const override { return "right-bottom"; }
    int order() const override { return 16; }
    bool openByDefault() const override { return false; }

    void draw(Editor& ed) override {
        const aiverse::World* live = nullptr;
        if (auto* sim = ed.sim()) {
            const auto& w = sim->world();
            if (w.resourceConst<State>()) live = &w;
        }

        ui::heading("PHYSIS · диагностика природы");
        ImGui::TextDisabled("Состояние из ECS · без изменения игры");
        if (!live && !demo_)
            ui::hint("В текущей игре нет активной PHYSIS. Создайте независимое демо для просмотра.");
        if (ImGui::Button("Создать / сбросить демо"))
            makeDemo();

        if (demo_) {
            if (live) {
                ImGui::SameLine();
                if (ImGui::Checkbox("Показывать демо", &showDemo_)) dirty_ = true;
            } else {
                showDemo_ = true;
            }
        }

        const aiverse::World* world = showDemo_ && demo_ ? demo_.get() : live;
        if (!world) {
            ui::hint("Для отображения реальных данных PHYSIS необходимо подключить её к GameModule::setup.");
            return;
        }
        const bool demonstration = world == demo_.get();
        ImGui::TextColored(demonstration ? ImVec4(0.92f, 0.76f, 0.35f, 1) :
                                           ImVec4(0.35f, 0.78f, 0.73f, 1),
                            "%s", demonstration ? "Изолированная демонстрация" :
                                                   "Живое состояние игрового мира (только чтение)");
        const auto* state = world->resourceConst<State>();
        if (!state) return;

        if (demonstration) {
            if (ImGui::Button("Следующий тик")) {
                advanceEnvironmentWithStormsAndDisasters(*demo_,
                                                         FloodMode::ExternalAuthority);
                dirty_ = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("+60 тиков")) {
                for (int i = 0; i < 60; ++i)
                    advanceEnvironmentWithStormsAndDisasters(*demo_,
                                                             FloodMode::ExternalAuthority);
                dirty_ = true;
            }
            ImGui::TextDisabled("Демо не пишет в карту, World проекта или Box3D");
        }

        int layer = static_cast<int>(layer_);
        if (ImGui::Combo("Поле", &layer, kLayers, IM_ARRAYSIZE(kLayers))) {
            layer_ = static_cast<DiagnosticLayer>(layer);
            dirty_ = true;
        }
        if (ImGui::Combo("Палитра", &palette_, kPalettes, IM_ARRAYSIZE(kPalettes))) {
            dirty_ = true;
        }
        if (ImGui::SliderInt("Масштаб сетки", &stride_, 1, 8)) dirty_ = true;
        if (ImGui::SliderInt("Размер окна (ячейки)", &viewSize_, 8, 64)) dirty_ = true;
        if (ImGui::InputInt("Центр X", &centerX_)) dirty_ = true;
        if (ImGui::InputInt("Центр Z", &centerZ_)) dirty_ = true;
        if (ImGui::Button("Обновить данные")) dirty_ = true;

        // Refresh only when input, World identity or committed tick changed.
        // Mutations without a new tick (editor edits or rollback-to-same-tick)
        // require explicit refresh. No pointer into mutable ECS is cached.
        if (cachedSource_ != world || cachedTick_ != state->tick)
            dirty_ = true;
        if (dirty_) {
            DiagnosticRequest request;
            request.originX = centerX_ - viewSize_ / 2;
            request.originZ = centerZ_ - viewSize_ / 2;
            request.width = viewSize_;
            request.height = viewSize_;
            request.stride = stride_;
            request.layer = layer_;
            // Build a transient COW snapshot for accelerated vector layers.
            // Scalar layers already read ECS cells directly (Stage 15).
            if (diagnosticNeeds3DField(layer_)) {
                FieldSpatialIndex index(*world);
                valid_ = sampleDiagnosticRasterIndexed(index, request, raster_);
            } else {
                valid_ = sampleDiagnosticRaster(*world, request, raster_);
            }
            cachedSource_ = world;
            cachedTick_ = state->tick;
            dirty_ = false;
        }
        if (!valid_) {
            ui::hint("Невозможно прочитать поле: проверьте границы и размер.");
            return;
        }
        ImGui::TextDisabled("Tick %llu · доступно %u/%zu · диапазон %.3f .. %.3f",
                            static_cast<unsigned long long>(raster_.tick),
                            raster_.sampleCount, raster_.valid.size(),
                            Fixed::fromRaw(raster_.minRaw).toDouble(),
                            Fixed::fromRaw(raster_.maxRaw).toDouble());
        drawMap();
        if (raster_.sampleCount == 0)
            ui::hint("В выбранной области нет ячеек PHYSIS.");
        if (selected_ >= 0 && std::size_t(selected_) < raster_.valid.size() &&
            raster_.valid[std::size_t(selected_)]) {
            const auto col = selected_ % raster_.width;
            const auto row = selected_ / raster_.width;
            const auto x = raster_.originX + col * raster_.stride;
            const auto z = raster_.originZ + row * raster_.stride;
            ImGui::Separator();
            ImGui::Text("Ячейка X %d · Z %d", x, z);
            ImGui::Text("Значение: %s", Fixed::fromRaw(
                raster_.rawValues[std::size_t(selected_)]).toString().c_str());
            const auto entity = cellAt(*world, x, z);
            if (const auto* cell = world->tryGetConst<Cell>(entity)) {
                const std::int64_t meters = state->settings.cellSizeMeters;
                const Vec3 position{Fixed::fromInt(std::int64_t(x) * meters + meters / 2),
                                    cell->elevation,
                                    Fixed::fromInt(std::int64_t(z) * meters + meters / 2)};
                NatureFieldSample detail;
                if (sampleNatureField(*world, position, detail)) {
                    ImGui::TextDisabled("Ветер: %.2f, %.2f, %.2f м/с",
                        detail.windVelocity.x.toDouble(), detail.windVelocity.y.toDouble(),
                        detail.windVelocity.z.toDouble());
                    ImGui::TextDisabled("Гравитация: %.2f, %.2f, %.2f м/с²",
                        detail.gravityAcceleration.x.toDouble(),
                        detail.gravityAcceleration.y.toDouble(),
                        detail.gravityAcceleration.z.toDouble());
                }
            }
        }
        ImGui::Spacing();
        ui::hint("PHYSIS не заменяет NEREID: «локальное затопление» — только отдельная модель PHYSIS.");
    }

private:
    void makeDemo() {
        demo_ = std::make_unique<aiverse::World>();
        Settings config;
        config.originX = -16;
        config.originZ = -16;
        config.width = 32;
        config.height = 32;
        config.cellSizeMeters = 100;
        config.seed = 307;
        initialize(*demo_, config);
        initializeDisasters(*demo_);

        // Gradient provides a useful heatmap even without a weather tick.
        for (const auto entity : demo_->resourceConst<State>()->cells) {
            const auto* cell = demo_->tryGetConst<Cell>(entity);
            if (!cell) continue;
            ClimateZone zone;
            zone.temperatureOffset = Fixed::fromInt(cell->x);
            zone.humidityOffset = Fixed::fromRatio(cell->z, 64);
            demo_->add<ClimateZone>(entity, zone);
        }
        StormEvent storm;
        storm.kind = StormKind::Tornado;
        storm.center = {150_fx, 0_fx, 50_fx};
        storm.eyeRadiusMeters = 100_fx;
        storm.outerRadiusMeters = 900_fx;
        storm.heightMeters = 600_fx;
        storm.tangentialWind = 55_fx;
        storm.updraft = 35_fx;
        storm.pressureDrop = 45_fx;
        storm.precipitationBoost = Fixed::fromRatio(3, 4);
        demo_->add<StormEvent>(demo_->create(), storm);
        WindZone zone;
        zone.center = {-600_fx, 0_fx, -350_fx};
        zone.radiusMeters = 1100_fx;
        zone.velocity = {25_fx, 1_fx, -5_fx};
        zone.gustAmplitude = 6_fx;
        demo_->add<WindZone>(demo_->create(), zone);
        GravityField gravity;
        gravity.center = {-200_fx, 0_fx, 350_fx};
        gravity.radiusMeters = 600_fx;
        gravity.mode = GravityMode::Uniform;
        gravity.acceleration = {};
        gravity.overrideBaseline = true;
        demo_->add<GravityField>(demo_->create(), gravity);
        (void)ignite(*demo_, 2, 2);
        for (int i = 0; i < 25; ++i)
            advanceEnvironmentWithStormsAndDisasters(*demo_, FloodMode::ExternalAuthority);
        showDemo_ = true;
        centerX_ = centerZ_ = 0;
        selected_ = -1;
        dirty_ = true;
    }

    void drawMap() {
        const float available = ImGui::GetContentRegionAvail().x;
        if (available < 32 || raster_.width <= 0 || raster_.height <= 0) return;
        const float w = available;
        const float h = w * float(raster_.height) / float(raster_.width);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##physisHeatmap", ImVec2(w, h));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, ImVec2(origin.x + w, origin.y + h),
                            IM_COL32(24, 28, 38, 255), 3);
        const float cw = w / float(raster_.width);
        const float ch = h / float(raster_.height);
        for (int y = 0; y < raster_.height; ++y)
            for (int x = 0; x < raster_.width; ++x) {
                const std::size_t index = std::size_t(y) *
                                          std::size_t(raster_.width) + std::size_t(x);
                const DebugColor c = diagnosticColor(raster_, index,
                                                     static_cast<DebugPalette>(palette_));
                if (!c.a) continue;
                draw->AddRectFilled(ImVec2(origin.x + x * cw, origin.y + y * ch),
                                    ImVec2(origin.x + (x + 1) * cw, origin.y + (y + 1) * ch),
                                    colorToImU32(c));
            }
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const int x = std::clamp(int((mouse.x - origin.x) / cw), 0, raster_.width - 1);
            const int y = std::clamp(int((mouse.y - origin.y) / ch), 0, raster_.height - 1);
            selected_ = y * raster_.width + x;
        }
        if (selected_ >= 0 && std::size_t(selected_) < raster_.valid.size()) {
            const int x = selected_ % raster_.width;
            const int y = selected_ / raster_.width;
            const ImVec2 a(origin.x + x * cw, origin.y + y * ch);
            const ImVec2 b(a.x + cw, a.y + ch);
            draw->AddRect(a, b, IM_COL32(255, 255, 255, 235), 0, 0, 2.0f);
        }
        draw->AddRect(origin, ImVec2(origin.x + w, origin.y + h),
                      ImGui::GetColorU32(ImGuiCol_Border), 3);
    }

    std::unique_ptr<aiverse::World> demo_;
    DiagnosticRaster raster_{};
    DiagnosticLayer layer_ = DiagnosticLayer::TemperatureCelsius;
    const aiverse::World* cachedSource_ = nullptr;
    std::uint64_t cachedTick_ = ~std::uint64_t(0);
    bool showDemo_ = false, dirty_ = true, valid_ = false;
    int viewSize_ = 32, stride_ = 1, centerX_ = 0, centerZ_ = 0;
    int palette_ = 0, selected_ = -1;
};

AIVERSE_EDITOR_WINDOW(PhysisWindow);

} // namespace aiverse::editor
