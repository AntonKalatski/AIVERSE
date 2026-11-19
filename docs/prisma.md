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

**Ограничения:** body volume ещё не ориентированный box (rotation нет), нет spatial index/приоритетного GPU culling, маска камеры по умолчанию all bits; обычные GameClient/Editor применяют mask all. Интерполяция изменяет **только явно указанные параметры** эффекта (этап 09), остальные параметры, minQuality и пути LUT сохраняют базовые значения. Для native C++ профиля без masks сохраняется legacy full-profile поведение. Дискретные настройки меняются при весе >= 0.5, числовые параметры blend в float на presentation side. Не хранить такой float стек в authoritative ECS state. Новые данные `prismaVolumes` необязательны и не затрагивают старые проекты. Код: `PrismaVolumeData.{h,cpp}`, `PrismaVolumes.h`; CPU tests: `tests/render/prisma_volume.cpp` / CTest `prisma_volume` (проверка запуском ещё требуется).

## Stage 09 — точечное смешивание параметров

`PrismaVolumeEffectMask` в `PrismaVolumes.h` и `PrismaVolumeData.cpp::authoredEffects` сопоставляют каждому эффекту bitmask явно указанных параметров из JSON, включая компоненты цвета, flags `enabled/minQuality` и текстовый путь LUT. Когда volume задаёт только `colorGrade.exposure`, исходные `colorGrade.saturation`, `contrast`, lift/gamma/gain и minQuality **не перезаписываются**. При отсутствии базового эффекта добавляется нейтральный профиль с плавным входом для ColorGrade/Bloom/эффектов с `intensity` или `amount`; для дискретных эффектов — порог 0.5. Для прямого native C++ создания volume без `effectMasks` сохранено старое full-profile поведение. Тесты `prisma_volume` расширены на сохранение несвязанных параметров, но нативный запуск ещё требуется.

## Stage 10 — визуальное редактирование Camera Volumes

Панель `PRISMA — Постобработка` (`apps/editor/windows/PrismaWindow.cpp`) теперь содержит секцию **PRISMA Volumes** в текущем scope: вся игра (`game.json`) или открытая карта. Она умеет создавать глобальный/сферический/box volume; центрировать объём на камеру Scene, редактировать `id`, enabled, размеры, приоритет, `weight`, `blendDistance`, `layerMask` и вложенные эффекты. Для каждого параметра эффекта рядом есть переключатель **наследовать/переопределить**: снятие override удаляет соответствующий ключ JSON, а не записывает дефолт. Это соответствует `PrismaVolumeEffectMask` и предотвращает сброс глобального контраста/насыщенности.

Авторинг использует общий GPU-free `makePrismaVolumeJson(id, shape, center)` из `PrismaVolumeData.h` — тот же формат, что runtime-парсер и CPU-тест `prisma_volume`. Изменения карты идут через `MapDoc::beginEdit/touched` (undo), изменения игры через сохранение manifest, как в уже существующих PostFX-слайдерах. Scene-камера показывает количество применимых эффектов/объёмов и предупреждения парсера.

**Ограничения:** нет gizmo-контуров объёма в viewport и нет редактора Shader Graph payload внутри volume — для него остаётся отдельный Shader Graph Editor. UI использует существующий ImGui и editor layout. Ни компиляция, ни GUI smoke/undo parity, ни GPU screenshots на этом этапе не проверены; агент должен подтвердить их нативно перед объединением. Старое ID панели `postfx` и MCP-команды `postfx_*` сохранены.


## Stage 11 — GPU-free typed RenderGraph dispatch

`engine/render/PrismaGraphDispatch.h` adds a header-only, genre-independent
`dispatchPrismaGraph<Handle>(graph, plan, bindings, callback)` contract.
`PrismaGraphBindings<Handle>` contains one entry per graph resource for
external handles and one entry per compiled transient slot for physical
allocations. `PrismaGraphDispatchView::resource(id)` resolves a logical
resource through the compiled aliasing plan; the host supplies the typed
RHI handles, command encoders and texture transitions.

Validation completes **before any callback**: invalid plans, mismatched
binding counts, unbound handles, duplicate pass IDs and invalid resource
references are rejected without partially drawing a frame. The callback
runs in deterministic topological order, and returning false aborts subsequent
passes with an error and exact number of completed passes.

This is **not an RHI executor** and does not allocate GPU textures, create
encoder barriers, perform render passes or bind PRISMA's current fixed chain.
An external integration must first create and retain physical slots, manage
external/history resource lifetimes, provide synchronization and connect the
existing `Prisma::resolve` calls. Keep the legacy renderer enabled by default
until native Metal/GL validation and image parity tests pass.

Tests are appended to `tests/render/prisma_graph.cpp` / CTest `prisma_graph`.
Native build, CTest and GPU tests have **NOT RUN** in the GitHub-only agent
environment; see `docs/handoffs/prisma-stage-11.md`.


## Stage 12 — Physical Resource Pool (GPU-agnostic)

`engine/render/PrismaGraphResourcePool.h` provides a host-owned pool of
physical render target handles for `PrismaGraphPlan::slots`. The host creates
targets with a `create(format,width,height)` callback and releases them
with `release(handle)`; this header never calls GPU/RHI APIs.

- `prepare(graph, plan, create, release)` reuses allocations with identical
  format/dimensions and matching history resource name, or creates new targets.
  It never selects the same allocation twice within a single frame. On
  allocation failure it releases targets allocated during that prepare call
  and preserves the previous pool entries.
- `markHistoryWritten(name)` marks successfully produced temporal pixels valid;
  `invalidateHistory()` discards temporal *contents* on camera cuts/teleports
  without discarding reusable GPU storage. If `prepare` returns a false
  history-valid bit, the caller must avoid reading the old history and use
  a non-temporal fallback. Resize changes resource descriptors and always
  allocates a fresh history.
- `pruneUnused(release)` frees allocations not selected by the last successful
  prepare; `clear(release)` releases all allocations. **Call both only after
  GPU fence completion**, not while earlier frames might still reference them.
- Use one pool instance per independent camera/view/temporal stream, not one
  global mutable history across cameras. The pool does not allocate external
  resource bindings, compute barriers, issue render passes or integrate itself
  into the existing `Prisma::resolve`. It is an opt-in host contract.

Tests for reusable physical slots, valid/invalid history, resize/pruning,
allocation rollback and clearing are in `tests/render/prisma_graph.cpp`.
Native build/CTest/GPU screenshot verification is **NOT RUN** for Stage 12.
See `docs/handoffs/prisma-stage-12.md`.


## Stage 13 — Opt-in RHI graph adapter (not yet wired into Prisma::resolve)

`engine/render/PrismaRhiExecutor.h` bridges the GPU-free Stage 11 graph and
Stage 12 allocation pool to the existing `gfx::Device` API. It creates
`TextureDesc{renderTarget=true}` targets, maps RGBA8/RGBA16F/Depth32F to
the backend's supported formats, uses `copyColor` for one-input/one-output
matching RGBA8 Copy passes, and executes Raster callbacks inside balanced
`beginPass/endPass` scopes. Compute callbacks run outside render passes
only if `Device::supportsCompute()` reports support.

A native host explicitly creates a per-camera `PrismaRhiExecutor(device)`
and calls `execute(graph,plan,external,raster,compute,optionalDepth)`
**outside any existing pass**. Raster callback signature is
`bool(gfx::Device&, PrismaGraphId, const PrismaGraphDispatchView<gfx::TextureHandle>&)`;
it binds its own pipeline, uniforms and input textures. Compute has the
same callback signature but is responsible for opening/closing any compute
encoder it needs. `optionalDepth` must match raster target dimensions if
the chosen Metal backend needs a depth attachment. The adapter never calls
`endFrame`, presents, performs implicit shader conversions, or modifies ECS.

**Compatibility constraints:** currently no RG16F/R32F target formats in
`gfx::PixelFormat`; these graphs fail preflight. The portable
`copyColor` interface only guarantees same-size RGBA8; HDR/other copy
operations need explicit raster shader passes. `gfx::Device` does not expose
a universal resource-barrier API. GPU target release and pool reset must
happen at a caller-managed safe point after in-flight work finishes. Temporal
history is valid only after the host has successfully produced its pixels
and explicitly marked it written.

**Not a complete render pipeline migration.** `Prisma::resolve()` still
uses the legacy, shipped fixed pass chain by default, with no automatic
RHI graph execution. The header is an opt-in integration point, and the
test addition currently covers only RHI format mappings in
`tests/render/prisma_graph.cpp`. Native build, driver smoke, screenshots
and numerical parity remain **NOT RUN**. See
`docs/handoffs/prisma-stage-13.md`.


## Stage 14 — experimental graph-driven SDR FXAA

The first production call site of `PrismaRhiExecutor` is an **opt-in**
SDR FXAA prepass in `Prisma::resolve()`. Set
`AIVERSE_PRISMA_GRAPH_FXAA=1` to create a two-external-resource graph
(`scene-fxaa` -> `fxaa-output`) and execute the existing FXAA pipeline,
same uniforms and texture bindings, through a single RHI raster pass.

The default code path is unchanged. The experimental path is only used
when FXAA is active, the scene is RGBA8/SDR, the FXAA pipeline is valid,
both color textures exist, and the matching depth attachment was created.
If the graph cannot be compiled/dispatched, the original direct `draw()`
path runs instead. Graph preflight happens before any draw submission;
the shader uses exactly one full-screen draw and writes the complete
destination. The opt-in pass clears neither color nor depth.

The graph has no internally allocated targets; it uses the existing
`Sized` per-view resources. It does **not** switch on graph-rendered
HDR, bloom, temporal AA, or the final composite. There is no new
JSON schema, editor toggle or change to SpectraFX. This is a guarded
runtime foothold, not a whole-chain render-graph migration.

GPU parity and native CMake/CTest remain **NOT RUN**. The test source in
`tests/render/prisma_graph.cpp` checks only graph structure and
single-pass binding/dispatch logic, not Metal/GL pixel correctness.
For reproduction and outstanding validation, read
`docs/handoffs/prisma-stage-14.md`.


## Stage 15 — Bloom Prefilter opt-in and legacy clear parity

`PrismaRhiExecutor::execute` now defaults raster passes to the same
opaque-black color clear and depth clear as `Prisma::draw`. An additional
`clearRasterTargets=false` argument is available for explicitly overwrite-only
pipelines, but the FXAA and Bloom experimental call sites use the default
legacy-compatible behavior. This fixes a known Stage 14 parity risk.

`AIVERSE_PRISMA_GRAPH_BLOOM_PREFILTER=1` opts only the **first Bloom
Prefilter** raster pass into `PrismaRhiExecutor`. It uses the existing
`kPrefilter` shader pipeline, uniform block and one source texture; the
input/output resources are externally managed `Sized` targets (full-resolution
input, lower-resolution output), and the graph does not allocate transient
textures. In HDR mode both resources are declared `RGBA16F`, otherwise
`RGBA8`. Both are supported by the existing conditional RHI path.

The rest of the Bloom downsample/upsample chain remains the original direct
renderer; the FXAA experiment still uses its independent
`AIVERSE_PRISMA_GRAPH_FXAA=1` flag. The default path is unchanged and any
preflight/allocation failure falls back to `Prisma::draw`. The experimental
raster callback returns true after a submitted draw; failure **after** GPU
submission cannot be rolled back. No full-frame parity has been established.

`tests/render/prisma_graph.cpp` now has CPU-only graph contracts for
SDR and HDR prefilter sizes, bindings, single-dispatch and missing output
preflight. These sources have **not** been compiled or run in this agent
environment; Metal/GL screenshots and GPU timings are also **NOT RUN**.
See `docs/handoffs/prisma-stage-15.md`.


## Stage 16 — opt-in full Bloom down/up RenderGraph

`AIVERSE_PRISMA_GRAPH_BLOOM_CHAIN=1` enables an experimental full Bloom
**Prefilter → Downsample → Upsample** graph inside `Prisma::resolve()`.
The flag is off by default. The graph declares one external Scene input and
external per-level down/up targets backed by the existing `Prisma::Sized`
per-view texture cache. Every downsample reads the previous mip; each upsample
reads the lower intermediate plus the matching down mip. All passes use the
existing `kPrefilter`, `kDown` and `kUp` GPU shader programs, exactly
the original size-dependent uniforms, tint/radius normalization, source
bindings and texture formats (SDR RGBA8; HDR RGBA16F). The host creates
the depth attachments for each output size before execution.

The RHI executor now accepts an optional trailing
`std::function<gfx::TextureHandle(PrismaGraphId)> rasterDepthForPass`.
When supplied, that attachment replaces the shared `rasterDepth` for each
pass; this is necessary on Metal for different Bloom mip dimensions.
Existing Stage 14/15 executor users continue to use the previous optional
shared depth and legacy-compatible clear defaults.

**Scope/limitations:** this is real RHI graph scheduling of Bloom raster
passes, but it is **not yet** adoption of Stage 12's transient graph pool:
the host supplies externally cached target textures, which already persist
per viewport. There is no automatic RHI in-flight retirement queue or
resource barrier API. The complete graph is rebuilt on each opt-in frame,
and `Prisma::resolve()` retains the old path as fallback when graph
preflight fails. On any unexpected failure **after** GPU pass submission,
the graph is not replayed (partial rendering must not be double-submitted).
No effect outside Bloom was migrated during this stage. If both
`AIVERSE_PRISMA_GRAPH_BLOOM_CHAIN` and the Stage 15 prefilter-only flag
are set, the full-chain flag takes precedence on success.

New GPU-free graph tests check 1/2/4 downsample levels for SDR/HDR,
topological order, valid binding resolution, and rejection of missing
mip targets before pass execution. Those sources have **NOT RUN** in a
native compiler or on a GPU; see `docs/handoffs/prisma-stage-16.md`.


## Stage 17 — per-view compiled Bloom topology cache

`engine/render/PrismaBloomGraph.h` provides a GPU-free
`PrismaBloomGraphCache`. `configure(sourceWidth,sourceHeight,format,mips)`
compiles the complete Bloom topology once per source size, HDR/SDR format
and mip shape list. Repeated frames with the same structure reuse
`graph()`, `plan()` and `operations()`; changed quality/mip sizes
trigger compilation of a replacement graph. Failed reconfiguration leaves
the previous valid configuration intact, but the renderer never executes
that stale graph for the incompatible current layout.

The main `Prisma::Sized` viewport cache now owns a
`PrismaBloomGraphCache`, so Scene/Game views have separate graph
layouts and no cross-view history. The Stage 16 opt-in path
`AIVERSE_PRISMA_GRAPH_BLOOM_CHAIN=1` uses the cached immutable graph
and rebuilds only the *frame bindings*: threshold/knee/scatter,
shader uniforms, source and output texture handles, and per-mip
depth attachments. The existing fallback and default-off behavior
are preserved. Stage 12 transient target pooling remains a separate
future integration; cached topology itself allocates **no** GPU textures.

This is implementation-only work; CMake, CTest and real GPU validation
were intentionally deferred at the user's request. See
`docs/handoffs/prisma-stage-17.md`.


## Stage 18 — GPU completion-gated retirement and persistent view executor

`engine/render/PrismaGpuRetirement.h` introduces
`PrismaGpuRetirementQueue<Handle>`: `retire(handle,submissionFence)`
transfers an allocation to a pending queue; `collect(completedGpuFence,
release)` invokes the release callback **only when the GPU has reported
completion** through a caller-managed fence/event. `drainAfterGpuIdle`
is valid only after an explicit GPU idle synchronization. A CPU frame
counter or `gfx::Device::frameSerial()` is not a completion fence.

`PrismaGraphResourcePool::retireUnused/retireAll` now transfer
allocations to the caller instead of destroying them immediately.
`PrismaRhiExecutor::retireUnused/retireAll/collectCompleted`
wires those operations to its queue and calls RHI `destroyTexture`
after the supplied completed fence. `resetAfterGpuIdle()` also
releases all pending and current allocations, but **only** if the
host has ensured the GPU is idle.

Every `Prisma::Sized` viewport now holds a lazy persistent
`std::unique_ptr<PrismaRhiExecutor>`, shared by its opt-in FXAA,
Bloom prefilter and full Bloom graph calls. It retains resource
pool state across frames rather than creating a new executor on
every pass. Lifetime is tied to the existing Sized cache. This
does not yet make PRISMA use graph-owned Bloom targets; those remain
external RHI textures in Sized, and graph execution stays opt-in.

**Backend integration constraint:** Gfx does not yet provide a
portable completed-GPU-fence API. Hosts must pass actual completion
signals to `collectCompleted`. Existing viewport eviction and
renderer shutdown still destroy their legacy textures at the
current existing release points; they are not automatically made
GPU-fence-safe by this opt-in queue. Actual backend fence wiring
and transient Bloom pool migration remain future features.
Testing/build/GPU verification deliberately deferred at user request.
See `docs/handoffs/prisma-stage-18.md`.


## Stage 19 — opt-in graph-owned Bloom mip allocation

With both `AIVERSE_PRISMA_GRAPH_BLOOM_CHAIN=1` and
`AIVERSE_PRISMA_GRAPH_BLOOM_POOL=1`, the complete Bloom RenderGraph
can now allocate its own downsample/upsample targets through
`PrismaGraphResourcePool` in the persistent per-viewport RHI executor.
Only the input Scene/FXAA color is external. The graph declares every
Bloom mip as an **internal** resource, enabling the planner to alias
identical-format/size GPU textures when their lifetimes do not overlap.
Without the new pool flag, the existing Stage 16 graph still uses
`Sized::targets` as external resources. Both experimental flags are
off by default.

`PrismaRhiExecution::resources` now maps each logical graph resource
to its actual external or pooled RHI texture handle. The final Bloom
composite samples `resources[cache.outputId()]` when pooled mode
succeeds. Per-pass depth attachments still come from the established
`Sized::depths` cache; thresholds, scatter, shader pipelines and
source UV conventions are unchanged. On graph preflight/allocation
failure before GPU submission, the code allocates legacy mip targets
and uses the existing Bloom fallback. A failure after GPU submission
does not attempt to replay partial passes.

**Ownership:** pooled RHI textures persist with their viewport executor,
rather than being released after each frame. Reconfiguration keeps
unselected allocations until explicit safe retirement; their eventual
release depends on GPU-completion reporting or a confirmed GPU-idle
shutdown. No portable completed-fence backend is yet wired, so
automatic mid-flight eviction is deliberately not used. Existing
view eviction still follows legacy release assumptions, and memory
benefits are not quantified. This experimental path needs build/GPU
verification before being enabled by default.

Implementation-only stage: testing intentionally postponed at user
request. See `docs/handoffs/prisma-stage-19.md`.

## Stage 20 — Tilt Shift graph opt-in

`AIVERSE_PRISMA_GRAPH_TILT_SHIFT=1` routes the existing three-pass
Tilt Shift (downsample, horizontal Gaussian blur, vertical Gaussian blur)
through `PrismaRhiExecutor`. Four logical SSA resources describe the
source, first A target, B target and final A target. The two versions of
A are deliberately mapped to the same existing physical texture: they
are never simultaneously needed, and the intermediate B is separate.
Per-pass uniforms and original pipelines are preserved. The default
path remains the legacy three draws. Missing resources or failures before
GPU submission fall back to it; partially dispatched passes are not replayed.
This stage does not yet migrate physical Tilt targets to the graph-owned pool,
nor does it activate Tilt Shift by default. Native/GPU tests deliberately
NOT RUN per user request. Handoff: `docs/handoffs/prisma-stage-20.md`.


## Stage 21 — Sun Shafts graph opt-in

`AIVERSE_PRISMA_GRAPH_SUN_SHAFTS=1` enables a two-pass graph for
the existing sunlight prefilter mask and radial scattering operation.
The sun position, offscreen edge fallback, visibility threshold, color,
quality tier, sample count and shader uniforms are unchanged. Three
externally owned textures represent the source, mask and radial output,
with the existing per-size depth attachment. The two-pass graph executes
through the per-viewport RHI executor. Failure before any submission uses
the legacy two-draw path; a partially submitted graph is never replayed.
The path remains off by default and the HUD is still drawn after postprocessing.

No new serialised settings or shader variants were introduced.
This stage is code-only, intentionally NOT compiled or GPU-tested yet.
See `docs/handoffs/prisma-stage-21.md`.


## Stage 22 — experimental depth-based Depth of Field

The new `depthOfField` entry in `postfx.effects` is handled by the shared
PRISMA data catalog (MCP and editor parameter discovery are data-driven).
Parameters are `focusDepth` (0..1 **GPU device depth**, not metres),
`focusRange` (depth interval), `maxBlur` (0..16 output pixels) and
`intensity` (0..1). Defaults: 0.5, 0.08, 8, 1 respectively.
This entry defaults to High post quality and is classified Heavy in
the cost estimation model.

The opt-in `AIVERSE_PRISMA_DOF=1` activates one full-resolution
nine-neighbour blur pass **before** SDR FXAA and Bloom; it reads the
scene color and render-target depth using the existing GLSL/MSL
fullscreen-program path and produces a new color texture used as
the source by subsequent effects. The hardware depth is deliberately
not represented as physical focus distance, since the PostFrame
currently lacks a portable inverse-projection/near-far contract.
The blur radius follows the distance between sampled hardware
depth and `focusDepth`, with a central sharp interval. The shader
is a bounded, approximate defocus, **not** cinematic hexagonal bokeh,
depth-layer separation or physically accurate lens simulation.

The pass samples the original scene depth, but uses its own
scratch depth attachment of matching dimensions, avoiding read/write
feedback on the same depth texture. It can run only when the
configured effect, opt-in flag, necessary GPU resources and
pipeline are present. Without the flag, existing image processing
continues unchanged. DoF is disabled on low/medium quality by its
default `minQuality=high`, unless a project explicitly overrides it.

Example game.json `postfx.effects` entry:
```json
{"type":"depthOfField","focusDepth":0.65,"focusRange":0.08,"maxBlur":6,"intensity":0.8}
```

All native compilation, CTest and visual/GPU smoke are intentionally
**NOT RUN** at the user's request. Real Metal/GL depth texture
sampling compatibility, reverse-Z behavior and scene-depth
interpretation need later validation. The next feature is
true world-distance autofocus via camera projection data, followed
by multi-resolution CoC separation and physically-inspired bokeh.
See `docs/handoffs/prisma-stage-22.md`.


## Stage 23 — HDR Auto Exposure (GPU log luminance + eye adaptation)

`postfx.effects` can now contain `{"type":"autoExposure"}`. The optional
feature remains disabled in rendering unless
`AIVERSE_PRISMA_AUTO_EXPOSURE=1` is set **and** the current scene is
using linear HDR RGBA16F. On SDR/RGBA8, the effect has no GPU action,
because clipped highlights cannot be recovered by metering.

The GPU pipeline in `Prisma::resolve()` is:
1. Read scene-linear RGB and store log2 luminance in a half-resolution
   RGBA16F target (four color taps with Rec.709 luminance coefficients).
2. Repeatedly average log luminance through a 2x2 pyramid to 1x1. This
   estimates geometric mean brightness, not a full per-bin histogram.
3. Determine target exposure `2^(clamp(log2(keyValue)-meanLogL+
   compensation, minEV, maxEV))`. In a dedicated RGBA16F 1x1
   ping-pong pass, blend from previous exposure using
   `1-exp(-speed*deltaSeconds)`. `speedUp` controls increasing
   exposure (dark->bright output); `speedDown` decreasing exposure.
   The time delta is clamped at 0.1s to avoid large jumps after stalls.
4. Sample the resulting exposure multiplier as `TEX8` in the final
   composite **before** HDR tonemapping and sRGB conversion. UI remains
   unprocessed. ColorGrade's manual exposure/compensation still multiplies
   the automatic result.

Parameters (regular `PrismaData` catalog and editor/MCP discovery):

| parameter | default | range | meaning |
|---|---:|---:|---|
| `keyValue` | 0.18 | 0.01–1 | target scene middle grey |
| `minEV` | -6 | -12–0 | lower automatic EV limit |
| `maxEV` | 6 | 0–12 | upper automatic EV limit |
| `speedUp` | 3 | 0.01–20 | brighten response rate, 1/s |
| `speedDown` | 1 | 0.01–20 | darken response rate, 1/s |
| `compensation` | 0 | -5–5 | EV bias |

Example:
```json
{"type":"autoExposure","keyValue":0.18,"minEV":-4,
 "maxEV":4,"speedUp":2.5,"speedDown":1.0,"compensation":0.0}
```

Per-`Prisma::Sized` temporal history uses two 1x1 HDR textures and
a previous timestamp. Explicit native `Prisma::resetAutoExposure()`
invalidates history for camera cuts. Project reconfiguration,
postprocess toggle and quality changes reset it; a disabled HDR/auto
mode also clears valid history. The first enabled frame snaps to the
current measured exposure, avoiding reading an uninitialized history
texture. No CPU brightness readback is used.

**Limitations:** the algorithm computes mean *log* luminance, **not**
a percentile histogram; bright outliers may still influence adaptation,
and the measurement is not a physically calibrated light meter. The
existing `Sized` cache is keyed by dimensions rather than stable camera
identity, so two viewport cameras of identical size can share their
temporal history. A follow-up should add explicit stable view IDs.
The fixed exposure sampler uses slot 8, which is also available for
the fourth custom ShaderGraph texture (slots 5..8); automatic exposure
is skipped for those graphs rather than clobbering user textures. HDR
support and blending of 1x1 RGBA16F must be verified on real Metal/GL.

This is an opt-in source implementation with **NO native/GPU tests run**,
per the user's features-first request. Do not enable it globally until
verified. Handoff: `docs/handoffs/prisma-stage-23.md`.


## Stage 24 — independent camera history and weighted GPU metering

The presentation-only API `Prisma::setViewId(uint64_t id)` selects a
**stable** camera/view identifier before rendering. `viewId() const`
returns it. A view's two 1x1 RGBA16F exposure-history render targets,
adaptation timestamp, valid bit and ping-pong index now live in
`Prisma::exposureViews_`, keyed by that ID **instead of render dimensions**.
`resetAutoExposure(id)` invalidates one view; the no-argument overload
invalidates every view. The older target caches remain keyed by size
for sharing ordinary temporary textures; exposure *history* no longer
cross-contaminates identically sized Scene and Game cameras.

The editor sets view IDs `1` for Scene and `2` for Game before
`applyCameraVolumes`; `GameClient` uses ID `3` within its
own PRISMA instance. A native project that renders multiple cameras
must assign distinct IDs itself. The default ID remains `0` for
legacy native projects. Exposure histories are retained until
`Prisma::shutdown` (no automatic per-camera removal/fence retirement
yet). IDs must be stable, not increment every frame.

`autoExposure` has four additional configurable parameters:
- `centerWeight` (default 0, range 0..1): 0 = uniform metering,
  1 = strongly center-weighted.
- `centerRadius` (default 0.35, range 0.05..1):
  normalized radius for Gaussian center weighting.
- `minMeterLogL` (default -10, range -16..0) and
  `maxMeterLogL` (default 10, range 0..16):
  clamp individual pixel log2 luminance in the initial HDR
  metering pass, reducing extreme brightness influence.
  This is not percentile-based histogram clipping.

The first pyramid pass now outputs two channels: weighted sum of
log2 luminance (R) and sum of weights (G); each later 2x2 pass reduces
both components identically. The 1x1 adaptation shader divides R/G
before computing target EV. No CPU readback, additional fullscreen
texture or extra raster pass is required compared with Stage 23.
The format stays RGBA16F throughout.

Example `postfx.effects` entry:
```json
{"type":"autoExposure","keyValue":0.18,
 "centerWeight":0.75,"centerRadius":0.35,
 "minMeterLogL":-8,"maxMeterLogL":6}
```

All rendering remains opt-in via `AIVERSE_PRISMA_AUTO_EXPOSURE=1`,
High-tier HDR only. Existing `postfx` schema is extended, not migrated.
CMake, CTest and real Metal/GL validation are **NOT RUN** as requested.
See `docs/handoffs/prisma-stage-24.md`.


## Stage 25 — HDR camera motion vectors and Motion Blur (experimental)

`postfx.effects` now accepts `motionBlur` through the shared,
genre-independent PRISMA catalog. The new effect defaults to
`minQuality: "high"` and is opt-in at runtime via
`AIVERSE_PRISMA_MOTION_BLUR=1`; it additionally requires an HDR
RGBA16F scene (signed RG motion vectors are not representable in the
existing RGBA8 path without encoding).

Parameters: `intensity` (0..1, default 0.65),
`maxPixels` (0..64, default 20), `samples` (2..16, default 8),
`shutterScale` (0..2, default 1). Example:
```json
{"type":"motionBlur","intensity":0.6,"maxPixels":20,
 "samples":8,"shutterScale":1.0}
```

The motion-vector pass samples the current hardware scene depth
and reconstructs the world position from the inverse current
viewProjection. It projects that position with the previous
viewProjection, writing a signed UV velocity
`currentScreenUV - previousScreenUV` into an RGBA16F texture.
The next raster pass gathers current-frame scene color along
this velocity and blends it with the sharp color. Motion blur is
executed **before** depth of field, FXAA, Bloom and final composite;
HUD/UI remains unprocessed. Separate scratch depth attachments avoid
sampling and writing the scene depth simultaneously.

The previous camera matrix, validity flag and render dimensions are
stored in `Prisma::motionViews_` per stable `setViewId()`, using the
same camera identity contract as Auto Exposure. The first rendered
frame, resize, changed quality, postprocess toggle or explicit
`resetMotionHistory(viewId)`/no-argument reset invalidates
motion history and results in an unblurred frame; it then seeds the
matrix for the next frame. `setSettings` also resets history.

**Optional object velocity bridge:** a native renderer may call
`prisma.setObjectMotionVectors(viewId, velocityTexture)` each frame
with a borrowed, full-resolution texture whose R/G values are signed
current-minus-previous normalized screen UV and B is a valid-object
pixel mask (B > 0.5). If present, that velocity overrides camera-only
velocity for tagged pixels. The borrowed handle is cleared after each
resolve. PRISMA does **not yet** generate per-object velocity buffers
from mesh transforms/skinning; the adapter is an integration contract,
not automatic object-motion rendering. These inputs are presentation
state, not deterministic ECS.

Limitations: this is a camera-reprojection motion blur, not TAA or
temporal image accumulation; it does not store previous color, and
therefore does not synthesize missing disoccluded samples. Camera cuts
must be signaled explicitly to avoid blur across teleports; material
transparency and sky (depth≈1) receive zero camera velocity in the
basic shader. Neither Metal/GL depth sampling, signed velocity
rendering, nor image correctness have been tested. All compilation,
CTest and GPU checks remain **NOT RUN**, per user request.
See `docs/handoffs/prisma-stage-25.md`.


## Stage 26 — automatic rigid-object motion vectors (experimental)

When HDR `motionBlur` is active with
`AIVERSE_PRISMA_MOTION_BLUR=1`, Renderer3D now has an optional GPU
overlay pass for **opaque rigid ModelDraw primitives with a stable
`persistentId`**. It runs between the scene pass and
`Prisma::resolve()`, outside any open render pass, then sends the
RGBA16F velocity texture to PRISMA through the new generic
`PostProcess` bridge. It does not modify gameplay/ECS, and the
existing camera-derived vectors remain the fallback for pixels
without valid object velocity.

The renderer tracks the previous world transform of each
`(ModelDraw::persistentId, nodeIndex)` in a per-view map. On the first
frame or after a camera cut, it records current transforms without
drawing a velocity overlay. Once the camera history is valid, it
renders each eligible rigid primitive with a dedicated GLSL/MSL
shader and `ModelVertex` position stream. Current and previous
model-view-projection matrices produce signed current-minus-previous
screen UV motion in RG. B=1 marks visible rigid pixels. A separate
scratch depth texture is attached; the shader *samples* original scene
depth and rejects fragments behind the visible surface, so world
occlusion/terrain can hide moving objects without writing scene depth.

This pass operates on the existing ModelItems after ordinary batching:
individual model node transforms are preserved even if the regular
opaque pass used instancing/MDI. The velocity overlay currently uses
one regular indexed draw per primitive (not an indirect/instanced
velocity submission); it reuses the existing model vertex/index GPU
buffers. The overlay render target and depth attachment are owned
per `PostProcess` view ID by Renderer3D and released on its shutdown.
No resource is shared between cameras with different stable IDs.

Only *rigid*, visible, opaque, non-deformed, stable-ID model primitives
are eligible. Missing IDs, transparent/masked materials, skinned or
baked animated meshes, morph targets, custom geometry and transient
immediate draws are skipped. Their pixels continue using the
camera-only motion reconstruction. The scene depth test is an
approximate epsilon comparison, not a full material/alpha/deformation
replay; fast disocclusions and edge artifacts remain possible.

Existing `Prisma::setObjectMotionVectors(viewId,texture)` stays
available for custom native renderers; automatic Renderer3D
generation uses the same borrowed-handle bridge via virtual
`PostProcess::acceptObjectMotionVectors`. There is no ECS scheduling
or genre-specific integration. Default behavior is unchanged when
MotionBlur is not active.

**NOT RUN:** native build, CTest, Metal/GL shader compilation,
screen comparisons and profiling, per user's features-first request.
A GPU fence-completed retirement path is still required for robust
target resizing and teardown while work is in flight. See
`docs/handoffs/prisma-stage-26.md`.


## Stage 27 — deformation-aware and instanced object motion vectors

The optional HDR `motionBlur` path now contains two new
`Renderer3D` velocity render variants, both active only with
`AIVERSE_PRISMA_MOTION_BLUR=1`:

**Rigid velocity instancing.** Geometry-equivalent visible opaque rigid
`ModelItem` instances are grouped by source model pointer, first index
and index count. A separate 16-instance GLSL/MSL pipeline consumes
per-instance *current and previous* MVP matrices, submitting one
`drawIndexed(..., instanceCount)` per compatible group (up to 16
instances per group). The uniforms occupy at most 2064 bytes; a single
item or missing batched pipeline still uses the Stage 26 singleton
shader. Matching shader/geometry groups are presentation-only; the
existing color draw batching is not altered.

**Skinned and morph velocity.** Palette-skinned objects and morph-target
primitives with a nonzero stable `persistentId` can produce actual
deformed vertex velocities. The renderer snapshots the current joint
palette and eight morph weights per
`(persistentId, nodeIndex, geometryPrimitive)`, retaining the previous
frame's values independently for each camera. A separate shader deforms
each mesh vertex for **both** the current and previous pose, computes
two clip positions and stores screen UV displacement. Current and
previous U3/U4 blocks hold up to 80 joints (3 rows each); U5 contains
the two 8-weight morph arrays and morph atlas parameters; tex8 reuses
the existing morph delta texture. GPU skinning/morph math is kept
separate from the deterministic simulation and is not a CPU-skinned
mesh upload. A missing previous pose or changed source model identity
causes the deformable primitive to skip object-velocity override rather
than invent movement.

**Depth-edge motion blur.** Motion Blur now samples the original
scene depth at each color gather position and reduces contributions
across large depth discontinuities. This protects foreground outlines
against unrelated background colors, although the rejection threshold
is based on non-linear hardware depth, so it is a conservative
approximation, not physically exact depth-layer motion reconstruction.
`Prisma::buildProgram` supports `depth2d<float>` for explicitly marked
depth samplers on Metal while keeping GLSL sampler2D conventions.

Eligibility remains intentionally conservative: visible, opaque
non-displaced primitives with stable IDs. Baked animation (whose pose
comes from clip textures), material ShaderGraph vertex displacement,
transparent/masked models and custom geometry are not yet handled by
the new deformed-velocity pipeline. Velocity target resize, first
frame and camera cuts seed history rather than extrapolating invalid
motion. Renderer3D owns lazy velocity shader/pipeline and target
lifetime.

**Verification deferred** by user request: native compilation,
Metal/GL shader validation, visual comparison, CTest and GPU profiling
have NOT RUN. Do not enable these experimental paths by default before
they are validated. Details: `docs/handoffs/prisma-stage-27.md`.


## Stage 28 — baked clip motion, alpha-cutout velocity and depth thresholds

The experimental `motionBlur` pipeline now includes a **baked-clips
vertex velocity** shader variant. For a `ModelItem` with `baked=true`,
the renderer records the current `anim` values (clip A row, clip B row,
blend) and world matrix by stable `(persistentId, nodeIndex,
geometryPrimitive)`. The new GLSL/MSL vertex shader reads the original
baked bone atlas (`tex7`) and original per-primitive joint palette.
Current and previous clip rows and interpolation are sampled separately
to reconstruct both deformed vertex positions. The velocity fragment
then writes current-minus-previous normalized screen UV motion. The
history snapshot is presentation-only; the animation and authoritative
simulation aren't modified. This path intentionally skips baked +
morph simultaneous deformation until a mixed permutation is available.

An optional **alpha-cutout rigid** velocity variant now replays the
material's base texture alpha cutoff with KHR texture transform
(offset/scale/rotation), base alpha and vertex-color alpha (when
enabled). Because cutout surfaces contain empty pixels, the dedicated
variant samples the base alpha texture and discards rejected fragments
before depth-occlusion testing. These materials are kept out of the
ordinary opaque instanced velocity draw and rendered separately.
ShaderGraph-displaced surfaces and deformable alpha-cutout models are
not included, since their visible shape requires matching additional
material stages. Fully blended/transparent models remain excluded.

The rigid history now stores the source Model identity alongside its
world transform, preventing a reused `persistentId` from picking up
a different model's previous transform. Existing per-view reset, initial
frame and resize behavior stays intact.

`motionBlur` also accepts `depthThreshold` (default 0.012, range
0.0005..0.05) for rejecting unrelated depth layers during the
directional color gather. The shader falls back to the sharp sample if
all gathered samples are rejected instead of blending black into the
image. The threshold is in **non-linear device depth units**, not metres.
Example:
```json
{"type":"motionBlur","intensity":0.65,"maxPixels":20,
 "samples":8,"shutterScale":1,"depthThreshold":0.012}
```

All additional paths remain behind the pre-existing
`AIVERSE_PRISMA_MOTION_BLUR=1` and HDR/quality gates. They use the
same masked velocity texture bridge and do not change gameplay ECS or
SpectraFX. Baked clip pose and rigid alpha-cutout motion use new
GLSL/MSL variants; no backend-specific testing has yet verified
their image parity. CPU/GPU builds, CTest, shader compilation,
screenshots and benchmarks are **NOT RUN** at the user's request.
See `docs/handoffs/prisma-stage-28.md`.


## Stage 29 — camera-cut epochs, reactive transparency and baked+morph velocity

PRISMA's experimental HDR motionBlur path now has three additional
presentation-only capabilities, all gated by the existing
`AIVERSE_PRISMA_MOTION_BLUR=1` runtime flag and configured quality.

**Explicit camera/view lifecycle.** `Prisma::resetMotionHistory(viewId)`
increments a monotonically increasing per-view camera-cut epoch exposed by
`PostProcess::objectMotionEpoch(viewId)`. Renderer3D reads this epoch on
each opt-in velocity pass and discards prior rigid and deformable transform
snapshots when it changes. This keeps the camera reprojection and
per-object pose histories in sync across camera cuts, teleports, quality
changes and map reconfigurations without modifying gameplay ECS. The
renderer also provides `resetObjectMotionHistory(viewId)` and
`resetAllObjectMotionHistory()` for explicit standalone control.
`releaseObjectMotionViewAfterGpuIdle(viewId)` releases the per-view
GPU velocity, reactive and depth textures **only** after the host has
confirmed GPU idle; CPU frame serials are NOT a GPU completion signal.
No automatic viewport closure or backend GPU fences are wired yet.

A new read-only `Renderer3D::objectMotionStats()` returns the last
velocity pass's view ID, submitted state and rigid instance/draw, cutout,
skinned, baked, reactive and rejected draw counters. These values are
CPU submission diagnostics, not measured GPU time or performance data.

**Reactive masks.** Renderer3D can emit a separate, current-frame RGBA8
reactive coverage texture for visible **rigid, translucent ModelDraw**
primitives. It samples the final scene depth and uses a dedicated
scratch depth attachment rather than modifying scene depth. The red
channel is coverage strength, while an all-zero background means no
reactive pixels. `PostProcess::acceptMotionReactiveMask` forwards the
borrowed handle to PRISMA. Native renderers can instead provide a
borrowed R=0..1 texture through
`Prisma::setMotionReactiveMask(viewId,texture)` before resolve.
The motion blur shader samples this mask at texture slot 3 and reduces
local blur strength by `1 - reactive * reactiveStrength`. A missing
mask uses the black fallback, so motion blur remains unchanged.

The new optional effect parameter `reactiveStrength` defaults to 0.85
and has a range of 0..1. The renderer's reactive pass is a conservative
geometric silhouette of translucent rigid models: it does **not**
reproduce per-pixel blended texture alpha, alpha-test materials,
skinning, particles or water drawn by other renderer modules.
False-positive reactive coverage may reduce blur inside glass geometry,
which is preferable to inventing object velocities. It remains an
experimental approximation.

**Baked clip + morph.** The baked-velocity vertex shader now applies
both the current and previous morph weights against immutable morph
deltas (tex8) **before** sampling current and previous baked clip rows
from tex7, matching the existing color shader's deformation order.
Palette, animation and morph parameters are all per primitive and
per view; a missing previous frame or changed model identity still
suppresses object-velocity override. This does not add per-vertex CPU
readback or change deterministic animation state.

LOD transition geometry that requires screen-space dithering is now
conservatively excluded from object velocity until a matching LOD fade
shader is implemented; camera motion reconstruction remains the
fallback there.

Example:
```json
{"type":"motionBlur","intensity":0.65,"maxPixels":20,
 "samples":8,"shutterScale":1,"depthThreshold":0.012,
 "reactiveStrength":0.85}
```

No native C++ compilation, shader validation, GPU tests, screenshots
or CTest were run at the user's request. The new GPU passes and
lifetime APIs remain opt-in and are not proven production-ready.
See `docs/handoffs/prisma-stage-29.md`.


## Stage 30 — animated reactive coverage, extensible water/particle masks, LOD fallback and velocity dilation

The existing experimental `motionBlur` chain now supports animated,
current-frame reactive coverage for translucent skinned, morph and
baked-clip `ModelDraw` geometry. These **reactive** passes do not
invent previous animation poses or feed object motion velocity: they
evaluate the *current* GPU-deformed vertex position using the same
vertex path as the Stage 27/29 motion shaders, sample the read-only
scene depth for visibility, and mark covered pixels as reactive (R=1).
The animated Metal shaders reuse the established skinned/baked vertex
shader source while substituting a coverage-only fragment. This handles
baked+morph current-pose silhouettes, but does **not** reproduce
per-pixel transparent material alpha, ShaderGraph vertex displacement,
screen-space water, or particles automatically. These may be more
accurately supplied by the owning renderer.

### Reactive coverage extension for all renderers

`Renderer3D::addMotionReactiveDraw(fn)` registers a presentation-only,
current-scene callback with signature

```cpp
void(gfx::Device& device, gfx::TextureHandle sceneDepth,
     int width, int height)
```

Register **after** `beginScene()` and before `resolvePrisma()` (typically
after the main scene/color pass has been submitted); callback registration
is cleared automatically after the current scene's resolve. The
callback is invoked **inside an already open RGBA8 reactive render
pass** with a separate scratch depth target; it must never call
`beginPass/endPass`. The provider binds its own shader, geometry,
texture/constant state and writes red channel R in [0,1] (0 = no
reaction, 1 = suppress blur according to the configured strength).
Use coverage-union-friendly blending, such as
`gfx::BlendMode::Screen`, so several water/particle/object providers
combine rather than erase one another. The current sceneDepth handle
may only be sampled: never rebind that texture as a writable depth
attachment. The provider must keep any borrowed buffers/textures valid
through the callback and cannot modify gameplay ECS state.

The generic callback is available for rendering engines and projects
implementing water, particles, weather or custom presentation. Stage 30
does **not** automatically instrument existing water/VFX packages;
those owners must register their respective reactive render callbacks
to render the correct coverage with their own material/geometry
semantics. No callbacks are invoked unless HDR MotionBlur is active and
a valid camera history exists; registration by itself cannot force the
postprocessing chain to run.

Built-in reactive pipelines now use Screen blending for accumulation
of multiple overlapping masks. The rigid fallback also reproduces
the pixel-dither coverage rule of `ModelShaders` for dithered LOD
crossfades: these pixels mark themselves reactive rather than
incorrectly receiving normal rigid-object velocities. This is
coverage-aware blur suppression, **not** full motion-vector
reconstruction across LOD changes.

### Depth-matched velocity dilation

The new `motionBlur.velocityDilation` parameter (default 1.0,
range 0..2, 0 disables) samples four neighbouring velocity values at
the requested radius, selecting a stronger velocity only when its
device-depth closely matches the center pixel. This helps preserve
motion around silhouettes without importing motion from clearly
different depths. It is a local approximate depth check (hardware
depth units, not metres) and costs up to four additional velocity and
depth reads per blurred pixel. The cost estimator includes this
additional work when enabled.

Example:
```json
{"type":"motionBlur","intensity":0.65,"maxPixels":20,
 "samples":8,"depthThreshold":0.012,"reactiveStrength":0.85,
 "velocityDilation":1}
```

All additions use the existing `AIVERSE_PRISMA_MOTION_BLUR=1` opt-in
flag, HDR RGBA16F and appropriate quality tier. Reactive mask
callbacks are a `Renderer3D` presentation extension, independent of
FPS, RTS, water or SpectraFX ownership.

**NOT RUN:** CMake/native C++ compilation, CTest, Metal/GLES/GL shader
compilation, screenshots, memory/performance profiling or CI, per
the user's features-first instruction. No hardware parity or shipping
readiness is claimed. See `docs/handoffs/prisma-stage-30.md`.


## Stage 31 — real RTS water/particles reactive integration and optional linear depth

Stage 30 introduced the generic `Renderer3D::addMotionReactiveDraw()`
producer interface. Stage 31 connects **existing runtime producers**:

- **RTS terrain water and generic mesh translucency:** `RtsTerrainRender.cpp`
  queues water with `Renderer3D::drawMesh(water_, ..., waterColor)`.
  `Renderer3D::renderObjectMotionVectors` now picks up the real
  translucent `DrawItem` mesh buffers (including those water sheets).
  A dedicated `MeshVertex` pipeline draws that same geometry into the
  RGBA8 reactive mask with read-only scene depth, no gameplay geometry
  regeneration. The mask strength is the draw item's alpha. The
  fallback shader is two-sided for water sheets. Other semi-transparent
  immediate MeshVertex draws benefit from the same path.
- **Real particle billboards/trails/ribbons/mesh particles:**
  `fx::ParticleSystem::render()` records the non-opaque runs that it
  *actually rendered*, including source alpha texture and vertex ranges.
  `hasReactiveGeometry()` reports whether those runs are available.
  `renderReactive(gfx::Device&,sceneDepth,w,h)` runs **inside the
  already-open** Stage 30 mask pass, using exactly the uploaded
  current-frame particle vertex/index buffer and alpha texture. It
  samples scene depth, combines source alpha with particle vertex
  alpha, and uses Screen blending to union overlapping coverage.
  Quads use the existing 16-bit indexed ranges; mesh particles use
  their nonindexed triangle ranges. There is no second particle
  simulation, CPU vertex expansion or additional vertex upload.
- **Client and editor:** `GameClient.cpp` and `Editor.cpp` now
  register callbacks immediately after `fx_.render` /
  `gameFx_.render` and before `r3d_.resolvePrisma()`. The callbacks
  consume the just-recorded particle geometry synchronously during
  the current scene's reactive-pass. They are not retained between
  view frames.

These paths remain **behind** the regular `motionBlur` effect,
`AIVERSE_PRISMA_MOTION_BLUR=1`, required HDR format and minQuality
gating. Water and particles are genre-independent *producers* of
current-frame presentation coverage; PRISMA still owns the full-screen
postprocessing and does not depend on any specific water/FX module.
Specialized water/particle renderers not using these two concrete
paths can continue using the existing generic callback.

### Optional linear view-depth filtering

`PostFrame` now carries the camera's actual projection near/far
planes and perspective/orthographic mode. The renderer populates
these from its `RTSCamera`; orthographic near/far match its
explicit -200..1000 projection.

`motionBlur` adds two optional settings:
- `linearDepth` (default false): if enabled and camera projection
  planes are valid, compares **linear view distance** for velocity
  dilation and blur gather rather than non-linear window depth.
- `depthToleranceMeters` (default 0.35; accepted range 0.01..10):
  near/far-layer rejection tolerance in scene world units (metres
  when the project uses metre scale).

The renderer uses the ordinary non-reversed 0..1 **window** depth
on both GL and Metal; the helper handles perspective and
orthographic projections separately. If `linearDepth` is false,
`depthThreshold` retains its existing meaning in hardware depth
units, so previously authored scenes retain their original
behavior. Both paths sample only the current frame and are
presentation-only. Example:

```json
{"type":"motionBlur","intensity":0.65,"maxPixels":20,
 "samples":8,"depthThreshold":0.012,
 "linearDepth":true,"depthToleranceMeters":0.35,
 "velocityDilation":1,"reactiveStrength":0.85}
```

**Resource ownership:** the real particle producer owns its shader
and two reactive pipelines and destroys them with `ParticleSystem`.
`Renderer3D` owns the generic MeshVertex reactive pipeline and
destroys it with other object-motion resources. The reactive pass
owns the output texture; producers must not begin/end GPU passes or
destroy its source depth. Raw externally supplied reactive/velocity
textures still have no portable RHI introspection for dimensions
and formats and must be validated by their owner.

**NOT RUN** (user requested features before tests): native C++
compiler, CMake, CTest, Metal/GL/GLES shader compilation, editor
smoke, water/particle image comparison, timing and GPU profiler.
No verified parity or speedup is claimed. See
`docs/handoffs/prisma-stage-31.md`.


## Stage 32 — specialized SpriteBatch water masks, live RHI texture metadata, and GPU-fence retirement contract

Stage 31 connected actual RTS terrain water through `Renderer3D::drawMesh`.
For the **separate** sprite/decal water path, Stage 32 adds
`SpriteBatch::drawWater` alpha-aware reactive rendering: the original
static `SpriteMesh`, water texture, and view-projection transform are
recorded and can be registered with the current scene via
`SpriteBatch::registerWaterReactive(renderer)`. The callback replays
only the existing mesh buffers into the open reactive pass. A dedicated
GLSL/MSL shader samples the water sheet's alpha and per-vertex tint
alpha and discards pixels hidden behind original scene depth.
It uses `gfx::BlendMode::Screen` so water combines with existing
model/particle masks rather than erasing them. The specialized
`drawWater` fallback to lit decals remains unchanged.

**Important integration scope:** repository search found no current
calls to `SpriteBatch::drawWater` in this branch. The producer support
is complete at the API/source level, but a native game using specialized
sprite water must explicitly call `registerWaterReactive` after its
`drawWater` calls and before `Renderer3D::resolvePrisma()`.
Ordinary RTS water through `drawMesh` already registers itself
automatically. `SpriteBatch::begin` clears its captured water draws
every frame, and registration consumes them; never destroy its textures,
mesh buffers or SpriteBatch before the synchronous callback finishes.

### Validating externally owned velocity and reactive textures

`PrismaMotionTextureInput.h` adds `MotionTextureInput`, a non-owning
handle with optional declared width, height and `gfx::PixelFormat`.
The `PostProcess` bridge has new typed overloads of
`acceptObjectMotionVectors` and `acceptMotionReactiveMask`; the
original handle-only overloads remain for existing consumers.

`Renderer3D` now submits typed inputs for its own full-resolution
RGBA16F velocity and RGBA8 reactive targets.
In `Prisma::resolve`, both inputs must match the active viewport
dimensions and required formats. If a mismatch is found, the object
velocity is ignored and camera reprojection is used; a mismatched
reactive mask is treated as absent (black fallback). This prevents a
stale Scene/Game texture of another size being sampled as a valid
input. The owning renderer retains GPU lifetime responsibility.

The RHI adds `gfx::Device::textureInfo(handle,out)`, a CPU-only
metadata query that is implemented by GL, Metal, and Vulkan from
their own live resource tables. PRISMA checks **actual RHI metadata**
where available, even for a handle-only legacy input. Providers
whose backends do not expose metadata are checked against any
declared metadata; the existing untyped legacy path otherwise remains
trusted for compatibility. This API performs no GPU readback and
does not make input textures globally self-describing when passed
outside the RHI.

### Safe handoff for retiring old motion targets

`Renderer3D` now uses the Stage 18
`PrismaGpuRetirementQueue<gfx::TextureHandle>` for view-motion
textures. `setObjectMotionSubmittedFence(token)` registers an
**actual backend-issued** last-submitted fence for subsequent resizes.
Old velocity/depth/reactive targets are moved to a retirement queue
rather than immediately destroyed. `collectCompletedObjectMotion(
completedGpuFence)` destroys only handles whose submission fence
was **confirmed GPU-complete** by the host. The manual
`retireObjectMotionView(viewId,lastSubmittedFence)` transfers one
view's current targets to this queue and discards its CPU history.

If the renderer has no valid submission fence, old targets are put
into an **unfenced quarantine**, where they are retained until
`drainObjectMotionAfterGpuIdle()` is called with a proven idle GPU.
Normal engine shutdown currently assumes the host has reached its
existing safe teardown point. There is no automatic backend fence
emission or polling yet, so this conservative default can retain
GPU memory across many viewport resizes until idle. **CPU frame
counters and arbitrary N-frame delays do not count as GPU fences.**

The older `releaseObjectMotionViewAfterGpuIdle` remains a separate
synchronous API and must only be invoked after the GPU is idle.
These changes do not automatically repair unrelated PRISMA `Sized`
pool or other rendering-resource release points; those require
their own GPU-completion-aware integrations.

All added GPU paths are opt-in under
`AIVERSE_PRISMA_MOTION_BLUR=1`; nothing new is enabled by default.
**NOT RUN:** native compilation, CTest, GLSL/MSL shader compilation,
Metal/GL/Vulkan images, GPU-performance measurements, and CI. The
source is not verified ready for production.
See `docs/handoffs/prisma-stage-32.md`.


## Stage 33 — GPU-completed fences for safe PRISMA resource retirement

The renderer now exposes a separate **GPU-completion timeline** in the
genre-independent `gfx::Device` interface:

```cpp
uint64_t submitted = dev.gpuSubmissionFence(); // current GPU frame, 0 if unavailable
uint64_t completed = dev.gpuCompletedFence();  // GPU-confirmed completion only
```

The submitted token covers work encoded in the *currently active* GPU
frame and is not the CPU frame counter. It can be attached to a target
retirement before that frame is actually submitted; the backend will
complete it only after GPU execution. A nonzero `gpuCompletedFence()`
result is a real, contiguous completed GPU timeline token. Unsupported
devices return zero. All values are per-device; never compare values
from different devices, contexts or GPU queues.

**Metal:** `GfxMetal.mm` uses the current command buffer's frame token
and updates an atomic completed counter in its
`addCompletedHandler` **only** for
`MTLCommandBufferStatusCompleted`. It does not turn
`frameSerial()` into a completion fence. The asynchronous callback
captures shared lifetime-safe state and the graphics owner thread
polls it by reading `gpuCompletedFence()`.

**GL33 / GLES3 / WebGL2:** after the frame's render commands the backend
inserts `glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0)` and flushes
the command stream without waiting. At the next `beginFrame`, it
calls `glClientWaitSync(..., timeout=0)` on its oldest pending fence,
advancing completed tokens strictly in submission order and deleting
signalled GL sync objects. The Windows GL33 dynamic loader now loads
`glFenceSync`, `glClientWaitSync`, `glDeleteSync`.
A failed fence is never treated as successful completion.
No per-frame `glFinish` is added.

**Vulkan:** uses its already existing per-frame `VkFence` and
`serial_`. `gpuSubmissionFence` is the currently recording GPU
frame's serial; `gpuCompletedFence` returns only the highest serial
whose slot fence has been successfully confirmed by
`vkWaitForFences`. A lost device or unsuccessful wait does not
advance the completed fence. This is the existing slot-wait path, not
a new polling implementation that blocks extra frames.

### Automatic motion target reclamation

`Renderer3D::beginScene` now calls
`collectCompletedObjectMotion(dev.gpuCompletedFence())` on the
graphics owner thread. When a viewport changes size,
`queueObjectMotionTexture` takes the current
`dev.gpuSubmissionFence()` if available and associates it with the
old object velocity, reactive or scratch-depth target. On completion
the old targets are destroyed; before then they are retained.
`retireObjectMotionView` and `setObjectMotionSubmittedFence`
remain available for externally managed submission lifecycles.
On devices without submission-fence support, old targets remain in
the Stage 32 quarantine until explicit GPU idle.

### Deferred PRISMA Sized / RenderGraph targets

`Prisma::sized` still keeps three active size entries for Scene/Game
views, but when evicting the least-recently-used entry it now
moves the **whole** `Sized` context — legacy raster targets, depth
targets, cached Bloom graph and persistent `PrismaRhiExecutor`
resource pool — into a fence-tagged retirement list instead of
releasing it immediately. When the GPU reports that the last
referencing submission completed, `collectCompletedSizes` frees
the retired targets and safely resets the graph executor pool.
In addition, `Prisma::target` now fence-retires targets replaced by
size/HDR format changes, avoiding immediate destruction of a texture
that may be in use by an earlier in-flight GPU command buffer.

`Prisma::releaseRetiredSizesAfterGpuIdle()` is the escape hatch for
hosts that have explicitly waited for an idle GPU; the function also
drains unfenced/quarantined individual targets. The `shutdown` path
assumes the platform's normal device-teardown GPU-idle condition.

This stage is about **resource lifetime correctness**, not a change
to image output, bloom topology, exposed `postfx` JSON, particle
simulation or gameplay ECS. Existing experimental flags remain off
by default.

### Limits

The new timeline only covers commands on the backend's regular
graphics submission queue. External GPU queues or devices need their
own fence tracking. Metal handles are released after the command
buffer completion callback; GLES/WebGL sync capabilities and actual
device behavior still require runtime validation. Vulkan may only
advance its completion counter when rotating to a frame slot whose
fence was waited. Without a GPU-completed token, retired resources
are **not** released opportunistically and quarantine can grow.

Auto Exposure per-view 1×1 history, PRISMA LUTs, externally owned
Sprite/FX buffers and other unrelated render-resource caches have
their own existing teardown policies; they are not all migrated to
this queue. RenderGraph internal pool cleanup is deferred together
with the evicted `Sized` context, rather than by assuming every
pixel pass finished at the end of the CPU frame.

Native compilation, CTest, Metal/GL/Vulkan GPU completion tests,
images and memory profiling have **NOT RUN**, per the user's
features-first instruction. See `docs/handoffs/prisma-stage-33.md`.


## Stage 34 — GPU-fenced per-camera history lifecycle and editor diagnostics

PRISMA temporal history is now explicitly **owned by stable view IDs**
instead of render-target size. The Stage 34 public APIs are:

```cpp
prisma.retireView(viewId);               // close only this camera's history
prisma.collectGpuRetirements();          // nonblocking, GPU-completed fence
auto post = prisma.retirementStats();    // read-only CPU diagnostics
auto motion = renderer.motionRetirementStats();
```

`retireView(id)` removes the per-view Auto Exposure ping-pong 1×1
RGBA16F texture handles from the active exposure map and queues them
via the existing GPU-fence-aware `retireTarget()` mechanism. An
unfenced backend retains them until a host-confirmed GPU-idle drain;
a completed GPU token permits collection without stalling.
Per-view MotionBlur matrices, validity and borrowed current-frame
velocity/reactive handles are discarded without destroying those
**externally owned** borrowed resources. Importantly, `retireView`
does **not** delete the shared `Prisma::Sized` scene render targets:
multiple Scene/Game cameras of equal resolution may still use them.

`Renderer3D::retireObjectMotionView(id, fence=0)` is now callable
without a manually supplied fence; the queue first queries the live
backend `gpuSubmissionFence()`. Its separate current-frame GPU
velocity, reactive and depth targets are retired independently from
PRISMA exposure. `Renderer3D::motionRetirementStats()` reports
retained per-view context count, fenced/unfenced texture count,
backend completed GPU fence and known pending bytes.

`Prisma::retirementStats()` reports active/retired size contexts,
exposure/motion view counts, fenced/unfenced textures, retired graph
pool entries, the last GPU-completed fence and **known minimum**
held GPU bytes. The byte estimate uses `Device::textureInfo()` on
live RHI allocations, including the archive of `Sized` targets and
depth attachments; it deliberately does not invent memory for
internal graph retired allocations whose handle metadata has
already been discarded. Statistics are **not** total VRAM use.

### Scene/Game viewport transitions

In the editor `Editor::frame` tracks which view **actually rendered
postprocess** this GUI frame. When a Scene or Game tab is hidden,
closed, switched to non-postprocessed diagnostic mode, or stops
rendering, the editor retires that camera's PRISMA state plus its
Renderer3D motion targets **once**, then continues collecting
backend-confirmed GPU completions on subsequent editor frames even
with no 3D viewport rendered. Reopening a view seeds fresh temporal
history instead of reusing stale exposure after a long hidden period.

The Scene view uses stable ID `1` and Game uses `2`. The editor
previously used Game ID `2` even when the Scene tab reused the
game renderer during play; Stage 34 passes explicit ID `1`
in that path to prevent simultaneous same-resolution Scene/Game
motion and exposure contamination. A hosted standalone game client
uses its own PRISMA instance and view ID `3`.

The native `GameClient` similarly retires view `3` on a new match
before camera and map replacement, and continues checking
pending GPU work on menu/loading frames when the 3D renderer is not
rendering.

The editor's **Statistics** tab now shows a new
`PRISMA · GPU resource retirement` section for completed GPU fence,
active/retired size contexts, view histories, counts of fenced and
unfenced textures, outstanding graph allocations, and the
**minimum known MiB** retained by both PRISMA and Renderer3D.
This is observational and creates no GPU readback or synchronizing
GPU wait.

### Project asset reloads

`Prisma::setImageLoader` now GPU-fence-retires superseded LUT and
user ShaderGraph textures rather than immediately destroying them
while another frame might still reference them. Shared shader
pipelines and graphs keep their existing ownership; this only
changes the texture retirement policy.

### Limitations / compatibility

The API leaves existing `postfx` JSON, editor configuration, game
behavior and ECS unchanged. No new postprocessing effect is enabled
by default. Active `Prisma::Sized` contexts remain keyed by output
dimensions, **not** individual camera IDs, because they contain
reusable frame-local targets; only private exposure and motion history
is camera-keyed.

`Viewport::color`/`Viewport::depth` (the editor's external
framebuffers) and other engine-wide texture resources are **not**
automatically released by `retireView`; the editor's own framebuffer
resource lifetime requires a separate safe owner-side migration.
If the GPU never exposes a completed fence, pending allocations
remain quarantined until explicit GPU idle. GPU bytes shown in the
Statistics tab are lower bounds, not comprehensive VRAM accounting.

**NOT RUN** as requested: CMake/C++, CTest, editor/client runtime,
Metal/GL/Vulkan GPU images, fence/pixel smoke, CI, profiler and
memory leak tests. Code and API integrations are source-level only
and not validated on real hardware.
See `docs/handoffs/prisma-stage-34.md`.

## Требования
- Универсальные render API для разных жанров; никакой зависимости от `modules/rts`/`modules/fps` в PRISMA.
- Переход постепенный: каждый новый путь отключаемый, исходный `postfx` формат не ломать.
- Не смешивать внутреннее HDR linear пространство с sRGB/UI; не запускать TAA и FXAA одновременно в полном режиме.
- `PostFxData` остаётся доступен headless/native без GPU; новый объёмный слой так же не должен требовать устройства.
- Для каждого этапа нужен `docs/handoffs/prisma-stage-XX.md` с проверяемыми фактами; результаты тестов фиксировать честно.

## Статус
- Этап 01: имя C++ класса и совместимость. Этап 02: GPU-free `PrismaVolumeStack` (opt-in, ещё не подключён к камерному loop). Этап 03: физический перенос ядра postprocess в PRISMA. Этап 04: редактор/MCP и документирование владения системами. Этап 07: GPU-free RenderGraph planner (пока не executor). Этап 08: профили камер в рантайме и редакторе. DoF/AutoExposure/motion vectors и GPU execution графа ещё не реализованы.
- Дальнейшие стадии смотреть в handoff-документах.
