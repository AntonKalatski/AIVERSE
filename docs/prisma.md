# PRISMA — универсальная постобработка AIVERSE

PRISMA — название системы полноэкранных эффектов и обработки изображения. Не путать с **SpectraFX** — системой частиц/VFX. Форматы существующих игр не меняются.

## Границы ответственности: PRISMA и SpectraFX

PRISMA владеет **постобработкой итогового изображения**: HDR/тонмаппинг, Bloom, color grading/LUT, DoF, Motion Blur, FXAA/TAA/upscaling, screen-space GI/AO/SSR, объёмы камеры, screen-space Shader Graph (domain `postfx`), render-pass dependencies и performance budgets.

SpectraFX владеет **мировыми визуальными эффектами до постобработки**: эмиттеры/частицы, дым, огонь, взрывы, ribbons, trail-потоки, CPU/GPU particle simulation, авторинг VFX graph и события ECS. SpectraFX генерирует HDR вклад сцены, PRISMA затем может применить Bloom ко всей сцене. Системы не дублируют друг друга, не зависят от жанра и не включаются в deterministic gameplay state.

Канонические исходники теперь `engine/render/Prisma.{h,cpp}`, `PrismaData.{h,cpp}`, `PrismaShaders.h`, `PrismaVolumes.h`. Editor: `apps/editor/core/PrismaOps.{h,cpp}` и `apps/editor/windows/PrismaWindow.cpp`. Legacy PostFx headers forward to PRISMA, legacy .cpp не содержат реализации. Стабильные JSON `postfx`, MCP `postfx_*` и editor saved id `postfx` **не переименовывать**: это внешние форматы, а не владельцы кода.

## Текущее устройство
- Основной C++ API: `#include "engine/render/Prisma.h"`; класс `aiverse::render::Prisma`.
- Совместимость: `PostFx` и `PostFxData` — исходные alias/forwarding includes; основная компиляция через `Prisma.cpp` и `PrismaData.cpp`. Старые `postfx_*` MCP имена сохранены.
- `Renderer3D` запрашивает `PostProcess::scenePass` и вызывает `resolve()`; `ScreenLighting` при HDR/High выполняет экранное освещение и TAA до финального resolve.
- Параметры: `game.json` / карта, `postfx` с объединением по типу эффекта; панель редактора и MCP-операции уже реализованы.
- GPU/RHI: GLSL/MSL, HDR RGBA16F с ACES и sRGB, fallback RGBA8; текущая цепочка имеет фиксированный порядок. UI отрисовывается **после** PRISMA.

## PrismaRenderGraph — этап 07 (GPU-free planner)

`engine/render/PrismaRenderGraph.h` — первая независимая от RHI реализация **описания проходов и ресурсов**. `PrismaRenderGraph::addResource/addPass/compile` строят стабильный топологический порядок, проверяют один writer на ресурс, ошибки идентификаторов, feedback read/write в одном pass и циклы зависимостей. `PrismaGraphPlan` содержит `order`, `resources` (first/last use), `slots` и число байт выделяемых **внутренних** текстур. Совместимые по формату и размеру временные ресурсы с непересекающимися lifetime используют один slot; history и ресурсы с `allowAlias=false` не разделяют slot. External input/output/history передаются вызывающим кодом и не аллоцируются planner.

**Важно:** это пока только граф зависимостей + lifetime planner, не GPU-executor. Реальный `Prisma::resolve()` пока исполняет текущий фиксированный pipeline. Нельзя заявлять, что RenderGraph уже управляет рендерингом; следующий этап — RHI-resource pool и execution callbacks с реальными pass bindings. Неправильные зависимости намеренно отклоняются, а не исправляются неявным shader blit.

Тестовый код: `tests/render/prisma_graph.cpp`, CTest `prisma_graph`, при `AIVERSE_BUILD_TESTS=ON` включён в стандартную CMake ALL сборку и `./run-tests.sh`. Нативный запуск тестов ещё требуется.

## Целевые интерфейсы
1. GPU-free `PrismaProfile`, `PrismaVolume`, `PrismaVolumeStack` и разрешение параметров по положению камеры.
2. Декларативный `PrismaGraph` с типизированными ресурсами (SceneColor, Depth, Velocity, History, LUT, Output), топологическим порядком и lifetime, без привязки к FPS/RTS.
3. Эффекты DepthOfField, MotionBlur, AutoExposure, GTAO и LensDistortion как отдельные проходы с quality-tier fallback.
4. MotionVectors / reactive masks и temporal upscaling; отдельная политика инвалидирования истории при camera cut/LOD/resize.
5. Профилировщик GPU-проходов, проверка ресурсо-опасных зависимостей, редактор узлов и полная автоматизированная проверка RHI.

## Агентам и интеграциям

Официальный скилл: `skills/visuals/aiverse-prisma/SKILL.md`, зарегистрирован в `tools/skills/catalog.py` и `skills/catalog.json`. Старые команды `postfx_*` принадлежат PRISMA как совместимый внешний API. Generic `ShaderGraph` compiler — общая инфраструктура, но full-screen `postfx` domain и его шейдеры относятся к PRISMA. Старые исходники `PostFx*.cpp` и `ScreenLighting.cpp` — пустые compatibility translation units; изменения в них не вносить. Следовать границам из `AGENTS.md` и писать handoff после каждого этапа.

## Camera Post Process Volumes — этап 08

`PrismaVolumeStack` теперь используется в игровом клиенте и в редакторских Scene/Game viewport. На этапе старта карты клиент читает `game.json["prismaVolumes"]` и `map["prismaVolumes"]` через `parsePrismaVolumes`; перед `Renderer3D::render(...,&prisma)` вызывает `prisma.applyCameraVolumes(stack,camera.eye())`, после `resolvePrisma()` сбрасывает настройки к базовому профилю. Editor кэширует профиль по JSON-сигнатуре и разрешает его отдельно для каждой камеры; изменение `postfx` не мутируется.

Пример `game.json` (тот же верхнеуровневый блок можно указать на карте):

```json
"prismaVolumes": {
  "volumes": [
    {
      "id": "underwater",
      "shape": "box",
      "center": [12, -5, 20],
      "halfExtents": [15, 6, 12],
      "blendDistance": 3,
      "priority": 10,
      "layerMask": 4294967295,
      "weight": 1,
      "profile": {
        "effects": [
          {"type": "colorGrade", "saturation": 0.5, "temperature": -0.3},
          {"type": "vignette", "intensity": 0.4}
        ]
      }
    }
  ]
}
```

Формы: `global`, `sphere` (`center`, `radius`) и `box` (`center`, **axis-aligned** `halfExtents`). `weight` в [0,1], `blendDistance` — дистанция затухания **за внешней границей**; приоритеты обрабатываются по возрастанию и одинаковые приоритеты сохраняют порядок добавления. `layerMask` фильтрует камеры, `enabled` отключает объём.

Профиль внутри volume использует тот же **existing** `postfx` schema, не новую библиотеку шейдеров. Указание `"hdr"` или `"enabled"` явно включает соответствующий override; отсутствие этих ключей сохраняет значение базового профиля. На карте volume с тем же `id` заменяет игровой; `"replace":true` в блоке карты удаляет все game volumes, прежде чем применить map list. Неверные записи пропускаются с `issues`, authored JSON не меняется.

**Ограничения:** body volume ещё не ориентированный box (rotation нет), нет spatial index/приоритетного GPU culling, маска камеры по умолчанию all bits; обычные GameClient/Editor применяют mask all. Интерполяция действует на **полные значения эффекта**; индивидуальный per-parameter override mask ещё не реализован. Дискретные настройки меняются при весе >= 0.5, числовые параметры blend в float на presentation side. Не хранить такой float стек в authoritative ECS state. Новые данные `prismaVolumes` необязательны и не затрагивают старые проекты. Код: `PrismaVolumeData.{h,cpp}`, `PrismaVolumes.h`; CPU tests: `tests/render/prisma_volume.cpp` / CTest `prisma_volume` (проверка запуском ещё требуется).

## Требования
- Универсальные render API для разных жанров; никакой зависимости от `modules/rts`/`modules/fps` в PRISMA.
- Переход постепенный: каждый новый путь отключаемый, исходный `postfx` формат не ломать.
- Не смешивать внутреннее HDR linear пространство с sRGB/UI; не запускать TAA и FXAA одновременно в полном режиме.
- `PostFxData` остаётся доступен headless/native без GPU; новый объёмный слой так же не должен требовать устройства.
- Для каждого этапа нужен `docs/handoffs/prisma-stage-XX.md` с проверяемыми фактами; результаты тестов фиксировать честно.

## Статус
- Этап 01: имя C++ класса и совместимость. Этап 02: GPU-free `PrismaVolumeStack` (opt-in, ещё не подключён к камерному loop). Этап 03: физический перенос ядра postprocess в PRISMA. Этап 04: редактор/MCP и документирование владения системами. Этап 07: GPU-free RenderGraph planner (пока не executor). Этап 08: профили камер в рантайме и редакторе. DoF/AutoExposure/motion vectors и GPU execution графа ещё не реализованы.
- Дальнейшие стадии смотреть в handoff-документах.
