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

## Целевые интерфейсы
1. GPU-free `PrismaProfile`, `PrismaVolume`, `PrismaVolumeStack` и разрешение параметров по положению камеры.
2. Декларативный `PrismaGraph` с типизированными ресурсами (SceneColor, Depth, Velocity, History, LUT, Output), топологическим порядком и lifetime, без привязки к FPS/RTS.
3. Эффекты DepthOfField, MotionBlur, AutoExposure, GTAO и LensDistortion как отдельные проходы с quality-tier fallback.
4. MotionVectors / reactive masks и temporal upscaling; отдельная политика инвалидирования истории при camera cut/LOD/resize.
5. Профилировщик GPU-проходов, проверка ресурсо-опасных зависимостей, редактор узлов и полная автоматизированная проверка RHI.

## Агентам и интеграциям

Официальный скилл: `skills/visuals/aiverse-prisma/SKILL.md`, зарегистрирован в `tools/skills/catalog.py` и `skills/catalog.json`. Старые команды `postfx_*` принадлежат PRISMA как совместимый внешний API. Generic `ShaderGraph` compiler — общая инфраструктура, но full-screen `postfx` domain и его шейдеры относятся к PRISMA. Старые исходники `PostFx*.cpp` и `ScreenLighting.cpp` — пустые compatibility translation units; изменения в них не вносить. Следовать границам из `AGENTS.md` и писать handoff после каждого этапа.

## Требования
- Универсальные render API для разных жанров; никакой зависимости от `modules/rts`/`modules/fps` в PRISMA.
- Переход постепенный: каждый новый путь отключаемый, исходный `postfx` формат не ломать.
- Не смешивать внутреннее HDR linear пространство с sRGB/UI; не запускать TAA и FXAA одновременно в полном режиме.
- `PostFxData` остаётся доступен headless/native без GPU; новый объёмный слой так же не должен требовать устройства.
- Для каждого этапа нужен `docs/handoffs/prisma-stage-XX.md` с проверяемыми фактами; результаты тестов фиксировать честно.

## Статус
- Этап 01: имя C++ класса и совместимость. Этап 02: GPU-free `PrismaVolumeStack` (opt-in, ещё не подключён к камерному loop). Этап 03: физический перенос ядра postprocess в PRISMA. Этап 04: редактор/MCP и документирование владения системами. Новые GPU эффекты/RenderGraph ещё в плане, а не завершены.
- Дальнейшие стадии смотреть в handoff-документах.
