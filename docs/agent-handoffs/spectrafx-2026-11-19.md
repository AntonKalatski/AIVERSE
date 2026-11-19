# SpectraFX — agent handoff

## 2026-11-19: Stage 1 — genre-independent VFX command ingress

- **Branch:** `feature/spectrafx-generic-cue-queue`, based on remote `main` at `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`.
- **Implementation commit:** `be5b54013f422ec2c6fa4b924e03296521e26b00`.
- **Changed files:** `engine/fx/SpectraFxCommandQueue.h` (new).
- **Architecture:** reusable, bounded, FIFO, presentation-only command bridge between any ECS/gameplay host and existing `fx::ParticleSystem`. No genre-specific dependencies or authoritative gameplay state. SpectraFX owns particle/VFX dispatch; PRISMA exclusively owns post-processing.
- **API:** `SpectraFxCommandQueue(capacity)`, `enqueue(Command)`, `play(effect,pos,key,dir,scale,tint)`, `move(key,pos,dir)`, `stop(key,immediate)`, `flush(ParticleSystem&)`, `reset(ParticleSystem&)`, `discardPending()`, `stats()`. Zero key denotes one-shot. Nonzero keys identify attached instances. Repeated keyed play kills/replaces the previous instance. Move/stop for unknown keys are no-ops. Queue overflow is counted and rejected.
- **Update order:** after gameplay tick and read-only ECS cue extraction, enqueue commands; on rendering thread flush before `ParticleSystem::update`. Single-threaded submission, deterministic caller-side merge for multiple producers. Explicit match-local generation-safe keys and reset required.
- **Compatibility:** additive public header only, no existing serialization or FX JSON changes, existing RTS dispatcher unchanged. No compiler/runtime validation.
- **Verification:** remote GitHub `main` source files inspected: `engine/fx/ParticleSystem.h`, `modules/rts/fx/RtsFx.h`, root `AGENTS.md`, `docs/particles.md`. **C++ build NOT RUN, CTest NOT RUN, editor/runtime NOT RUN**, no claims of passing tests or visual correctness.
- **Known risks:** stale keyed handles can persist after natural completion until explicit stop/reset; caller must not concurrently enqueue. On teardown call `reset(particles)` before destroying ParticleSystem. No automatic signal translation, ECS system registration, parameter overrides, GPU compute or editor graph has been implemented yet.

### Next action
1. Add native headless CTest exercising FIFO, queue capacity, generation-safe key replacement, stop vs kill, unknown keys and reset.
2. Define a generic ECS cue/event bridge to this queue and validate from at least two unrelated game modules. Keep deterministic gameplay state independent from float-based VFX.
3. Expose graph/editor authoring and parameter overrides after the generic bridge works. Never move post-process features out of PRISMA.
4. Recheck and merge newest `main` **into this feature branch** before additional edits; document each stage in a separate commit.

### Commands including pull (macOS)

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main
git merge origin/main
cmake -S . -B build/spectrafx-validation -DCMAKE_BUILD_TYPE=Debug
cmake --build build/spectrafx-validation --parallel
ctest --test-dir build/spectrafx-validation --output-on-failure -R 'fx|particle|vfx'
```


## 2026-11-19: Stage 2 — generic gameplay/ECS cue translation

- **Branch:** `feature/spectrafx-generic-cue-queue`; remote `main` still `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` at stage start; branch 0 commits behind main. No merge required.
- **Implementation commit:** `1d83c02841cfb5f9f2fa83a02140476347314239`.
- **Changed files:** `engine/fx/SpectraFxCueBridge.h` (new).
- **Architecture/API:** `SpectraFxCue { id, action, key, position, direction, scale, tint }`; `SpectraFxBinding { effect, scale, tint, enabled }`; `SpectraFxCueBridge::bind/unbind/binding/dispatch/dispatchAll/translate/stats/resetStats`. Caller-provided `translate(event, cue)` maps any game/ECS event record into immutable presentation cues. Cue Play resolves a generic ID to an effect and multiplies binding/runtime size and tint; Move, Stop, Kill operate on generation-safe instance keys even after a binding is removed. An unbound/disabled Play is ignored. Actions with missing instance key are rejected. Calls enqueue into `SpectraFxCommandQueue`; `flush` occurs on render thread before `ParticleSystem::update`.
- **Compatibility:** no changes to SimEvent, ECS snapshots, JSON serialization or existing RTS/FPS game logic. Optional header-only integration, not an installed ECS system. The host must maintain a read-only extraction barrier and invalidate presentation handles on rollback/reset.
- **Tests actually executed:** **NOT RUN** (GitHub connector editing does not provide a native repository checkout/compiler/CTest); no native, CI or macOS editor verification. API is authored, not proven by compilation.
- **Known limitations:** no authored JSON cue-binding loader, no live RTS/FPS call sites, no renderer parameter graph, no thread-safe producer queue or dedup of rollback events; all are future work. For rollback, discard cues from discarded ticks and `reset(particles)` before re-emitting the confirmed presentation state.
- **Next step:** integrate the bridge with one existing game presentation adapter using an explicit event-to-cue mapping; add a second independent example, then compile/test both. Consider an optional generic effect parameter-block API after behavior is verified. Do not copy PRISMA post-processing into SpectraFX.

### Pull / build commands

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main
git merge origin/main
cmake -S . -B build/spectrafx-validation -DCMAKE_BUILD_TYPE=Debug
cmake --build build/spectrafx-validation --parallel
ctest --test-dir build/spectrafx-validation --output-on-failure -R 'fx|particle|vfx'
```


## 2026-11-19: Stage 3 — read-only native Simulation collector

- **Branch:** `feature/spectrafx-generic-cue-queue`, still based on `main` `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` (no newer main identified).
- **Implementation commit:** `d41cbae6ea5ac5c6aa3864125441578c135a77ed`.
- **Changed paths:** `engine/fx/SpectraFxSimulationBridge.h` (new header).
- **API:** `fx::spectraFxPosition(const aiverse::Vec3&)` converts fixed-point positions strictly for presentation; `fx::installSpectraFxCollector(sim, bridge, queue, translator)` registers `spectrafx.collect`, a read-only `Simulation::events()` translator and FIFO command submitter. The system naming suffix `.collect` follows the documented `Simulation::present()` mirror/event collector mechanism. Runtime owner is responsible for binding cue IDs, invoking queue.flush before particle update and for reset on rollback/new match. Modules can provide their own translators without modifying ECS schemas or engine core.
- **Compatibility:** opt-in and additive. No FPS/RTS source rewrites; no save/rollback format or authoritative ECS modifications. No external dependencies beyond existing engine headers.
- **Verification:** **NOT RUN**: C++ compilation/CTest/macOS/editor visual QA/CI. Header was authored through GitHub connector; do not claim it builds until native validation succeeds.
- **Limitations:** registration captures the bridge and queue by reference; they must outlive the Simulation system. Events are not deduplicated across rollback; host must explicitly reset VFX presentation, not replay discarded events. No concrete FPS/RTS consumer wiring or authored config importer yet.
- **Next actionable step:** create host-level integration example and add focused CTest for a real Simulation + two different translators; verify .collect order under `step()` and `present()`. Then wire actual game adapters while preserving gameplay checksum parity.

### Pull / verification

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main
git merge origin/main
cmake -S . -B build/spectrafx-validation -DCMAKE_BUILD_TYPE=Debug
cmake --build build/spectrafx-validation --parallel
ctest --test-dir build/spectrafx-validation --output-on-failure -R 'fx|particle|vfx'
```


## 2026-11-19: Stage 4 — real SimEvent type mapping (unverified)

- **Implementation commit:** `7fb57936d57524b5acb2e9f57d77621c92eda4cb`; branch `feature/spectrafx-generic-cue-queue`; remote main `9ee6ff6`, 0 behind at stage start.
- **Changed:** `engine/fx/SpectraFxEventMap.h` (new).
- **API:** `SpectraFxEventRule` configures cueId/action/scale/tint per native `SimEvent::type`; `SpectraFxEventMap::bind/unbind/clear/rule/translate/translateOneShot` translates genuine `SimEvent` values into `SpectraFxCue` with read-only Fixed to float presentation positions. `installSpectraFxEventMap(sim,map,bridge,queue)` registers the `.collect` adapter for one-shot events. Persistent Move/Stop/Kill need an explicit host key policy via `translate(..., keyFor)`; zero keys are rejected for these actions. No assumptions about RTS/FPS type IDs.
- **Authority:** `SimEvent` remains the game's sole event source, Graph/FX remain presentation, no ECS/save/checksum changes. The map, bridge and queue must outlive the registered Simulation.
- **Tests actually executed:** **NOT RUN** (connector-only changes: no native compile, CTest, CI or editor QA); gameplay integrations are optional and are not wired into live RTS/FPS host loops.
- **Next:** add a minimal graph representation with bounded DAG validation and explicit execution to SpectraFxCueBridge; keep runtime FX effects in existing ParticleSystem. Then add actual native tests/genre host integration and verify compilation before production use.


## 2026-11-19: Stage 5 — bounded cue graph evaluator (unverified)

- **Code commit:** `5d7068dd3a9f581d00e8e189aecfdfdf4539a430`; **branch:** `feature/spectrafx-generic-cue-queue`; `main` had no new commits in the last comparison (base `9ee6ff6`, zero behind).
- **Changed:** `engine/fx/SpectraFxGraph.h` (new).
- **Architecture:** `SpectraFxNode` with `Input`, `Scale`, `Tint`, `Emit` nodes; compile/authoring can produce a vector of backward-referencing node indices. A single Input, up to 128 nodes, no cycles/forward references, bounded scale/tint, at least one Emit. `SpectraFxGraph::validate` returns an error without modifying state. `execute` creates temporary presentation cues, evaluates ordered transforms and uses `SpectraFxCueBridge::dispatch` for output effects. For keyed/attached cues, multiple outputs are rejected to avoid shared instance-key collisions. Unkeyed one-shots can branch into multiple effects.
- **Public API:** `setNodes(vector<SpectraFxNode>)`, `nodes()`, `validate(error*)`, `execute(input, cueBridge, queue)` returning `Result {emitted,rejected,valid}`. Existing `fx::ParticleSystem` handles particles, rendering and budgets; this new graph only orchestrates event/cue transforms. **Not** a GPU simulation graph or visual node editor.
- **Compatibility/authority:** additive header, no changes to JSON/save/net/Simulation checksum or gameplay systems. No PRISMA post-process logic. Existing RTS VFX remains untouched.
- **Tests actually executed:** **NOT RUN**. Compiler, CTest, editor QA, GPU tests and real RTS/FPS integration are unverified: work was authored through the GitHub connector, with no native checkout/compiler in this stage.
- **Limitations:** no graph serialization/parser, type checking beyond ordered topology, effect-parameter expressions, GPU compute, node GUI, module-owned launch points or live game call sites. Node evaluation allocates a transient vector per execute. Rollback needs queue reset.
- **Next:** add explicit native CTest for `SpectraFxCueBridge`, `SpectraFxEventMap`, `SpectraFxGraph`, `SpectraFxCommandQueue` with a headless ParticleSystem, then wire a native game adapter and visual graph serialization. Preserve separate implementation/handoff commits after every stage.

### Commands — always pull branch before testing

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main
git merge origin/main
cmake -S . -B build/spectrafx-validation -DCMAKE_BUILD_TYPE=Debug
cmake --build build/spectrafx-validation --parallel
ctest --test-dir build/spectrafx-validation --output-on-failure -R 'fx|particle|vfx'
```


## 2026-11-19 — Stage 6: typed VFX graph parameters and branching

- **Branch:** `feature/spectrafx-generic-cue-queue`, compared with `main` `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`: 0 commits behind at stage start; no main merge necessary.
- **Implementation commit:** `e67ad37c7514991c2c62c0d3e8dcf9af45c6c21e`.
- **Affected file:** `engine/fx/SpectraFxGraph.h` (updated existing public graph header).
- **Features:** Added `SpectraFxGraphParameters` with named scalar/color/bool dictionaries and `setScalar/setColor/setFlag`; backward-compatible `execute(input,bridge,queue)` and new `execute(input,params,bridge,queue)`. Extended `SpectraFxNodeKind` with `ParameterScale`, `ParameterTint`, `Offset`, `Gate`, `EventFilter`. Nodes can branch from earlier cue values; a disabled filter/flag suppresses only its branch. Parameter absence/type mismatch suppresses the branch (no implicit values). Validation now rejects non-finite scale, tint and offset, overlong/empty parameter names, and invalid indices. Graph remains bounded to 128 nodes, with exactly one Input and at least one Emit. Keyed instances still reject multiple Emit outputs.
- **Compatibility:** Existing `Input/Scale/Tint/Emit` numeric node IDs and the original `execute` overload preserved. Opt-in presentation only; `fx::ParticleSystem` is the rendering executor, no changes to ECS simulation/save/network/RTS/FPS/PRISMA.
- **Tests:** **NOT RUN**, per user's instruction *functions first, tests later*. No compiler, CTest, CI, live game or GPU execution performed. This is source implementation, not validated production functionality.
- **Known limitations:** Still not a Niagara graph editor or GPU particle simulation. No persistent parameter binding/serialization, shader graph, runtime effect definition edits or automatic hot reload. `execute` allocates one vector per call; future runtime optimization can precompile a graph plan.
- **Next:** implement configurable graph asset interchange (explicit versioned authored data, parser/serializer), and a graph registry dispatching cues to graphs before ParticleSystem; then editor integration and GPU runtime. Only after feature implementation: focused tests, native build and cross-game integration.
- **Pull-first command:**
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main
git merge origin/main
```


## 2026-11-19 — Stage 7: versioned in-memory graph registry and cue routing

- **Branch:** `feature/spectrafx-generic-cue-queue`; remote main `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` still matched branch merge base at the latest comparison (0 behind).
- **Implementation SHA:** `bbc340cb34a30e03a264d05704c621aee2737875`.
- **File changed:** `engine/fx/SpectraFxGraphRegistry.h` (new).
- **New API:** `SpectraFxGraphRegistry::define(name,graph,defaults,error)` performs pre-validation and atomic graph replacement with revision increment; `find/erase` manage graph assets; `route/unroute/routeFor` map arbitrary host cue IDs to named graphs; `dispatch(cue,bridge,queue,overrides)` uses either a routed graph with ephemeral typed parameter overrides or legacy direct cue dispatch when unrouted. Routes whose graph asset has not streamed yet fail explicitly. `mergeParameters` applies scalar/color/bool overrides without mutating registry defaults; `Stats`, `graphCount`, `routeCount` expose basic diagnostics.
- **Compatibility / authority:** no new save format, renderer API, ECS component or genre dependency; only read-only presentation routing. Existing direct cue effects are preserved if no route is set. PRISMA remains post-process owner.
- **Checks actually run:** **NOT RUN** (user explicitly requested features first; source edits via GitHub connector; no native compiler, tests, CI, UI or GPU verification). Do not call this feature production-proven yet.
- **Limitations:** in-memory registration only, no asset-file parsing/serialization or visual graph editor; no GPU backend; no automatic per-entity parameter sync; no hot-reload notification beyond `revision`. Production must validate unresolved asset references, memory budgets and graph execution before integration.
- **Next:** author graph data assets, graph import/export and editor node authoring; then runtime emit/spawn lifecycle and backend abstraction. After the feature pass: compile/test, cross-genre examples, rollout/rollback and real Mac Metal profiling.

### Pull-first commands

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main
git merge origin/main
```


## 2026-11-19 — Stage 8: versioned authoring asset codec

- **Implementation SHA:** `4bbc22c35b99a1bad5a9c7e04139486fc9b6a7ea`; branch `feature/spectrafx-generic-cue-queue`; prior comparison with `main` at `9ee6ff6` showed 0 behind.
- **Changed:** new `engine/fx/SpectraFxGraphAsset.h`. `SpectraFxGraphAssetCodec::read/readText/write/install` serializes node data and typed default parameters into explicit JSON `version:1`. Supports the nine current node kinds, named scalar/color/flag defaults and stable sorted output keys. Strict input type/unknown-field checks, finite numeric/uint32 bounds, DAG validation and transactional parse into a temporary asset before overwriting caller data. `install` validates and commits through registry.define.
- **Compatibility:** entirely optional rendering/presentation data; no ECS save format changes, no FPS/RTS rewrite, no PRISMA ownership overlap. Existing registry/graph APIs remain available.
- **Actual verification:** NOT RUN — user requested feature development first. C++ compilation, headless tests, editor, JSON round-trip and CI are unverified.
- **Limitations:** decimal values serialized to six places; no preservation of editor UI node positions, links beyond single parent source, or custom unknown fields. No file watcher, async streaming or visual editor. The JSON codec relies on the existing `Json` library.
- **Next:** add a feature-gated stage lifecycle / emitter program descriptor and CPU backend adapter, ensuring FX render/update logic remains single-owned by `ParticleSystem`.

Pull-first:
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main && git merge origin/main
```


## 2026-11-19 — Stage 9: emitter lifecycle and CPU adapter

- **Implementation commit:** `6ccda1813bfc433214aadc316870851416c2dbf0`; branch `feature/spectrafx-generic-cue-queue`; remote main comparison at stage start: base `9ee6ff6`, 0 behind.
- **Changed:** `engine/fx/SpectraFxEmitterRuntime.h` (new).
- **Functionality:** `SpectraFxStage` declares ordered Spawn, Initialize, Update, Render stages. `SpectraFxEmitterProgram::validate/find` validates uniqueness, order, legal scale/color and required Spawn effect name. Spawn delegates to existing `ParticleSystem::play`; Initialize applies scale/tint. Update/Render are descriptive contracts owned by existing `ParticleSystem` and **not** an independent executable particle update/shader pipeline. `SpectraFxCpuEmitterRuntime::start/move/stop/reset/owns/activeCount/stats` controls session-keyed instances, supports atomic replacement after a new handle was obtained, and resets only owned instances. Keys must be generation-safe across despawn and rollback.
- **Ownership/compatibility:** only presentation state; no gameplay authority, existing particles/collision/sub-emitter/graphics path remains the sole CPU backend. No changes to saves, ECS data formats, PRISMA, RTS or FPS.
- **Tests actually run:** **NOT RUN** (user instruction: implement features before tests); native compilation, CTest, CI, GPU and editor runtime all unverified.
- **Limitations:** no stage program JSON format, no sub-graph bindings, no command queue integration, no GPU compute execution, no per-stage update/render custom behaviors. CPU runtime's Stop stops emitting but particles can continue fading as existing API specifies.
- **Next:** add an emitter asset codec and configurable effect-stage rendering backend contract; then authoring/editor integrations. After feature phase, compile/test all headers and verify real host integrations.
- **Pull commands:**
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main && git merge origin/main
```


## 2026-11-19 — Stage 10: bounded cross-emitter event routing

- **Implementation:** `63319c7c0d85935875eb33adfb40565f40649df9`, branch `feature/spectrafx-generic-cue-queue`.
- **Affected:** `engine/fx/SpectraFxEmitterEvents.h` (new).
- **API/behavior:** `SpectraFxEmitterEvent` enum Spawned/Completed/Impact/Marker/Manual; `SpectraFxEmitterEventRecord` event payload; `SpectraFxEmitterEventRouter::link/unlink/publish/drain/clear/stats`. Bounded 2,048-event FIFO and per-drain cap (default 512) prevent unbounded presentation-event work; dispatch uses `SpectraFxCommandQueue::play`, not a second particle simulator. Events published during a drain are deferred to the next drain. Depth metadata is limited to 3 for future chained producers; no automatic child event publishing exists yet.
- **Authority/compatibility:** presentation-only layer, no gameplay state or serialization changes. Existing RTS/FPS modules, FX JSON and PRISMA unchanged.
- **Tests actually run:** **NOT RUN** by user request; native compilation, CTest, editor and GPU QA unverified.
- **Known limitations:** this is an external event router, not automatic particle death/collision callbacks from ParticleSystem. `sourceMarker` in the link is presently reserved and not matched by `drain`; thus marker-specific filtering and event-origin propagation must be implemented before claiming full Niagara event-handler parity. External caller is responsible for publishing appropriately ordered signals and generation-safe emitter keys.
- **Next:** wire emitter lifecycle and ParticleSystem callbacks to router, implement marker filtering and explicit origin propagation, then authored graph/editor controls. Finally tests/CI/Mac Metal profiling, after feature phase.

Pull-first:
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main && git merge origin/main
```


## 2026-11-19 — Stage 11: source marker dispatch correctness
- **Code SHA**: `22b3cfb6c715d69e251d2dea18d3005fc0d20daf`; branch `feature/spectrafx-generic-cue-queue`. Remote main `9ee6ff6` is ancestor; comparison showed zero behind.
- **Changed**: `engine/fx/SpectraFxEmitterEvents.h`. Marker links now filter matching `sourceMarker` when the event type is Marker (zero matches all). `discardPending()` drops unconfirmed presentation events on rollback without clearing configured event links. Clarified explicit parent-producer responsibility for bounded event depth. Existing queue budget and deferred drains preserved.
- **Build/CTest/CI**: **NOT RUN** by user request (all features first, tests later). No claims of renderer verification.
- **Known limitations**: no direct particle collision/death callback plumbing, no RHI GPU simulation; event depth relies on external event provenance. `Completed` must not be conflated with particle extinction.
- **Next**: connect lifecycle events from tracked CPU emitters to event router on explicit starts/stops and natural emission shutdown, without altering existing particle simulation.


## 2026-11-19 — Stage 12: tracked CPU emitter lifecycle event bridge
- **Code commits**: `63e2a2d8a70baa38046723f53e3836659cace7cc` and `9d083de0a40307db2464f951aee905ea126684b5`, branch `feature/spectrafx-generic-cue-queue`. Main comparison: 0 commits behind `9ee6ff6` at this continuation's start.
- **Changes**: `SpectraFxCpuEmitterRuntime::emitting(instance, const ParticleSystem&)` exposes whether a tracked handle is still *emitting*. New `engine/fx/SpectraFxEmitterController.h` orchestrates tracked `start/move/stop/poll/reset` across the existing CPU runtime and `SpectraFxEmitterEventRouter`. Start publishes Spawned, poll (after `ParticleSystem::update`) publishes Completed once after emission ceases, explicit stop/kill suppresses false natural-completion signals. Reset clears tracked handles and pending presentation events without touching unrelated FX.
- **Semantics**: Completed means **emission ended**, not that all particles have died. Existing ParticleSystem owns their remaining lifetime. This is an opt-in presentation adapter; existing RTS/FPS are not switched over. Controller and its referenced runtime/router must have coherent lifetimes.
- **Actual checks**: **NOT RUN**; no local build/CTest/CI/GPU/editor QA because user explicitly requested all feature implementation before testing.
- **Limits**: no direct per-particle collision/death callbacks (ParticleSystem continues handling its own subemitters). Completion routing must use a key bound by the host; generation-safe keys needed; bounded router may drop events under overload. No automatic graph authoring UI or GPU compute backend.
- **Next step**: implement graph authoring and stage-program serialization, then integrate configurable stage orchestration into native editor/host. Keep a dedicated handoff for each stage.


## 2026-11-19 — Stage 13: stage-program JSON codec
- **Code SHA** `e47b28a51a55f69b828e39f913d0bc4a68655dc3`; branch `feature/spectrafx-generic-cue-queue`.
- **File added:** `engine/fx/SpectraFxEmitterAsset.h`.
- **API:** `SpectraFxEmitterAssetCodec::read/readText/write`. Versioned JSON v1 with ordered `stages` array and `spawn/initialize/update/render` names; optional enabled/effect/scale/tint. Strict field/type checking, full `SpectraFxEmitterProgram::validate` before atomic commit to output; deferred effect name resolution by existing ParticleSystem. `write` emits normalized v1 JSON for editor authoring/export.
- **Scope:** feature-only authoring data. No ECS deterministic state changes, no JSON graph schema change, no new CPU particle solver, no post-processing ownership changes.
- **Verification:** **NOT RUN** (user requested all functions first, tests later). Native build, CTest, editor integration and asset roundtrip are unverified.
- **Current limitations:** stage Update/Render are declarative placeholders delegating to legacy ParticleSystem, not configurable stages or new GPU shader passes; no automatic file watcher/editor panel yet. The new asset parser must be covered by the later compile+negative-validation pass.
- **Next actionable feature step:** runtime asset loader, editor graph UI and GPU capability-specific implementations; preserve old CPU fallback. All future functionality requires per-stage handoff commits and an eventual full focused/native validation pass.


## 2026-11-19 — Stage 14: transactional graph authoring API
- **Code commit:** `d3f41176457a956d75564bafda83f90025c6e4c4`, branch `feature/spectrafx-generic-cue-queue`.
- **Changed:** `engine/fx/SpectraFxGraphAuthoring.h` (new). `SpectraFxGraphAuthoring::replace/append/update/remove/connect/createDefault/revision` supplies the eventual editor's graph mutation surface. Each mutation validates a temporary graph before replacing the canonical authored graph; failed topology edits leave the previous graph and revision untouched. Remove refuses referenced nodes and shifts later source indices; graph requires exactly one Input, topologically ordered nodes and at least one Emit. Empty initial graph is not editable through append; start with `createDefault()` and `replace()`.
- **Integration boundaries:** declarative editor/data layer, no GUI window yet. Optional header, no changes to ECS, gameplay, CPU particles, RHI, PRISMA or serialization IDs.
- **Tests actually run:** **NOT RUN**, per user's features-first instruction; native C++ compile, CTest, UI operation and cross-platform quality checks are still pending. This stage is not verified.
- **Limitations:** indexed single-parent connections rather than full arbitrary typed dataflow; no node position/selection/Undo history persisted; insert/reorder require complete graph replacement.
- **Next:** build a genuine dockable editor graph UI using existing editor window APIs, persist graph assets with editor metadata and implement a GPU compute path without duplicating CPU particle authority. Only then perform native test and QA phase.


## 2026-11-19 — Stage 15: synchronize 409 main commits, preserve SpectraFX and legacy graph work
- **Merge commit** `de0872a1f9a48a9f384b3d5ff320e4314b1f6c60`, first parent previous SpectraFX HEAD `9be28f6`, second parent main `d71c6d47e532b25fb816a8bb1f96c41e5275a165`; branch `feature/spectrafx-generic-cue-queue`.
- GitHub compare previously showed 409 behind main. A two-parent merge tree uses the exact main tree `b5b659a` plus all 12 independently added `engine/fx/SpectraFx*.h` files and this living handoff from the feature branch, without overwriting any main source. No overlapping path existed for those new files.
- **Important discovery:** main already includes `engine/fx/SpectraFX.h`, `VfxGraph.h`, `VfxGraphAssets.h` with a Niagara-style Spawn→Initialize→Update→Output graph compiler, CPU ParticleSystem integration, and variant hot reload. DO NOT create another competing particle graph or duplicate its compiler. The feature branch's `SpectraFxGraph` is a separate presentation *cue* routing DAG, not the canonical particle graph. Future editor UI should edit the existing `VfxGraph.h` schema (`fx/graphs/<name>.vfx.json`) and use `SpectraGraphAssets` for actual runtime loading.
- **Verification:** GitHub merge object and branch ref update completed; native build, CTest, editor execution, GPU behavior **NOT RUN**. No claim of runtime compatibility yet.
- **Next:** add one editor window for the canonical particle graph format, reusing main's `FxWindow` and `ShaderGraphWindow` patterns; keep cue routing assets separate. Continue stages/handoffs without tests until requested feature pass is finished.


## 2026-11-19 — Stage 16: SpectraFX particle graph editor window

- **Branch:** `feature/spectrafx-generic-cue-queue`. **Code commits:** `679b29193c08e1efac62fe790134e63865851ac8` and `674d898a7231b0be85160f6e35775acc443dccfb` (connected-node insertion correction). Latest merged main ancestor is `d71c6d47e532b25fb816a8bb1f96c41e5275a165`, via `de0872a`.
- **Changed:** new `apps/editor/windows/SpectraFxGraphWindow.cpp`, registered via existing `AIVERSE_EDITOR_WINDOW` mechanism. The window owns **canonical particle graph JSON**, not the separate presentation-cue graph and not PRISMA post-process effects. Works with main's actual `fx::compileSpectraGraph` and `fx/graphs/<name>.vfx.json` used by `SpectraGraphAssets`.
- **Features:** project-scoped graph discovery/create/open, version-1 starter graph Spawn→Initialize→SpriteOutput, topological Update insertion, additional Sprite/Mesh/Ribbon outputs, node removal and rewiring through source selector, per-node JSON properties, full-document JSON editing, compiler-backed transactional validation, bounded Undo/Redo, explicit dirty/discard flow and save to fx/graphs followed by Editor::reloadFx. Output creation is connected to an Initialize or Update node; Update insertion rewires an existing output. Existing effect runtime remains the canonical CPU particle solver.
- **Compatibility:** additive editor window; no existing FX files or ECS authority changed; effect graph compiler and existing asset hot-reload implementation are reused rather than replaced. Keeps SpectraFX and PRISMA ownership separate.
- **Tests actually executed:** **NOT RUN** (user specifically requested finishing functionality before tests). No C++ compilation, CTest, macOS editor window verification, UI interaction, GPU or visual screenshot checks have been performed. **Do not mark this editor production-ready.**
- **Known limitations:** graph view is currently a structured node/properties editor, not freely draggable canvas; no GPU compute backend, no automatic live preview image in this window (FX window remains separate), no typed port UX or multi-parent graph support. Persistence writes version-1 canonical graph JSON only. The new window must be checked against current auto-registration/CMake and editor ImGui API in the later build phase. Existing `SpectraFxGraph*.h` cue-routing DAG remains distinct; documentation should prevent confusion.
- **Next step:** add typed visual node canvas/selection/links and embedded preview reusing FX window resources; create an RHI-backed GPU particle execution pipeline with explicit CPU fallback/capability checks, then genre-level connections. Run all tests and fix errors after the complete functionality phase as requested.

### Pull branch (always fetch first)

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main
git merge origin/main
```


## 2026-11-19 — Stage 17: canonical particle-graph playback from ECS presentation cues

- **Branch:** `feature/spectrafx-generic-cue-queue`; prior main merge `de0872a1f9a48a9f384b3d5ff320e4314b1f6c60` includes main `d71c6d47e532b25fb816a8bb1f96c41e5275a165`.
- **Code commit:** `ac79ac5f7a5b10a7a090f5c1a2394e9c1b147f48`.
- **Changed:** new `engine/fx/SpectraFxGraphPlayback.h`.
- **New functionality:** `SpectraFxGraphPlayback` connects generic `SpectraFxCue` (Play/Move/Stop/Kill, ID, generation-safe key, position, direction, scale, tint) to main's **canonical** `SpectraGraphAssets::load(name,overrides)` and `ParticleSystem::play(effectId,...)`. `route/unroute/clearRoutes/find` select particle graph assets by gameplay cue ID; `dispatch/dispatchAll` translate events; `invalidateAssets/invalidateAsset` expose cache invalidation, `lastLoad` retains compiler errors or safe fallback indication; `prune` clears completed emitter attachments, `reset` kills tracked FX on a rollback/scene reset and leaves unrelated effects untouched. Play replaces an existing keyed instance only after the new effect starts successfully. No second particle simulation.
- **Compatibility:** additive header; no changes to existing FPS/RTS runtimes, ECS deterministic checksum, save, post-processing or VFX graph file schemas. This is opt-in host integration; users still must install an actual Simulation presenter/collector and call `dispatch` from the presentation frame. Gameplay state is never written back.
- **Tests actually executed:** **NOT RUN** per user's features-first instruction. Native compile, CTest, live editor preview, effect rendering and GPU backend verification remain pending.
- **Limitations:** `SpectraFxGraphPlayback` is separate from earlier `SpectraFxCommandQueue` until a unified dispatcher bridge is authored; no direct callback from ParticleSystem per-particle collision/death yet. New graph requests compile/load on Play, which should be cached/preloaded in production. Ownership of particle definitions and handles stays in `SpectraGraphAssets`/`ParticleSystem`.
- **Next:** connect `SpectraFxGraphPlayback` to the existing `spectrafx.collect` producer without making a second gameplay authority; add editor preview and performance-aware GPU path later. Tests after the complete functionality pass.

Pull newest branch:
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main && git merge origin/main
```


## 2026-11-19 — Stage 18: interactive canvas for canonical particle graphs
- **Branch:** `feature/spectrafx-generic-cue-queue`, 0 commits behind main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` at start.
- **Implementation commits:** `0dce554832b3b7c358e53c8920c5ea530a714285` and `fb64f2a94c9313d0be5bc48e359980715d9d1b1b`.
- **Changed:** `apps/editor/windows/SpectraFxGraphWindow.cpp`. Added visual node canvas with grid, selectable title bars, draggable node positions, middle-mouse panning, Bezier links, output/input port clicks for rewiring (existing `relink` compiles a temporary graph before mutation), and cancel via Escape. The second commit defers rewiring until node iteration ends to avoid invalidating the referenced JSON vector mid-loop. Node screen positions are editor-only transient state, not part of canonical `fx/graphs/*.vfx.json` schema.
- **Compatibility:** existing AIVERSE particle-graph compiler `compileSpectraGraph` remains the source of truth. No ECS, CPU particle simulation, RHI, PRISMA or gameplay modifications.
- **Actual validation:** **NOT RUN**, per user's all-features-before-tests instruction. Native editor build, ImGui interaction, actual graph save/reload, UI smoke, GPU and CI are unverified. Do not claim the canvas works until host execution.
- **Limitations:** node positions are not persisted across restart, no drag-connect wire preview, typed ports, box selection or built-in GPU preview; topological constraints still enforced by the canonical compiler.
- **Next:** persist editor-only canvas layout separately from the canonical VFX graph; then embedded preview in the editor using the existing ParticleSystem/renderer pattern. Finish required functional features before broad test execution.

Pull-first command:
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main && git merge origin/main
```


## 2026-11-19 — Stage 19: editor-only SpectraFX canvas layout persistence
- **Branch:** `feature/spectrafx-generic-cue-queue`; `main` still at merged ancestor `d71c6d47e532b25fb816a8bb1f96c41e5275a165` at stage start.
- **Code commits:** `6ed70a8b485d6126216b02b4d2b299cf924faa19`, `b8738ba8ba91264c541b55f1de4ef53d0e04c890`, `69a183c6cff7ec9b0154b7e090ece3920d2c9598`. Last two correct JSON line terminator and new-document layout dirty state; the final commit uses C++ escaped newline, not literal backslash+n JSON garbage.
- **Changed file:** `apps/editor/windows/SpectraFxGraphWindow.cpp`. Saves positions/pan into `fx/graphs/<name>.layout.json` version 1, separately from canonical particle graph `<name>.vfx.json`. Reads the sidecar on load, bounds finite coordinates, gracefully discards corrupted optional metadata, tracks layout dirtiness during node dragging/panning, and writes the sidecar via a temp file rename. Does not change `compileSpectraGraph` schema or CPU ParticleSystem. Saving main graph invokes saving layout.
- **Compatibility:** additive editor metadata, no ECS gameplay state/network/save format changes. Canonical FX assets remain loadable without a layout file.
- **Tests actually run:** **NOT RUN**, explicitly deferred by user until all features are implemented. Native compile, CTest, editor UI, file IO, Metal rendering and CI all unverified. No production-ready claim.
- **Limitations:** editor's graph and layout are two separate files, not transactionally committed together; an IO failure can save one but not the other. Layout metadata is not shared through `fx::SpectraGraphAssets`. Graph node id rename does not migrate layout positions automatically.
- **Next:** embedded live ParticleSystem preview in SpectraFX editor using existing FxWindow render target patterns, then an opt-in RHI GPU particle path with CPU fallback; integrate host cue producer and perform native tests only after functional implementation.

Pull-first:
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main && git merge origin/main
```


## 2026-11-19 — Stage 20: embedded SpectraFX live CPU preview
- **Branch:** `feature/spectrafx-generic-cue-queue`; current main had 0 missing commits at stage start (`d71c6d47e532b25fb816a8bb1f96c41e5275a165` merged).
- **Implementation commit:** `0ceb6e2dcff376e729d3478a4dbb61689d217a07`.
- **Affected file:** `apps/editor/windows/SpectraFxGraphWindow.cpp`.
- **New functionality:** window initializes a private `fx::ParticleSystem`, `TextureCache`, `Viewport`, and `RTSCamera` following existing `FxWindow` conventions. Recompiles in-memory `fx::compileSpectraGraph(document_)` when the source JSON hash changes; defines a preview effect, plays/replays it, moves the camera with mouse drag/zoom and renders directly to an off-screen editor viewport with a reference grid. Adds manual Restart preview button. Texture/model file readers resolve against the current project; preview never writes gameplay ECS state or duplicates particle simulation logic.
- **Compatibility/ownership:** private editor-only visual preview using existing CPU `ParticleSystem` and RHI. Not a new GPU compute pipeline; PRISMA stays separate. Existing `fx/graphs/<name>.vfx.json` schema unchanged.
- **Actual tests:** **NOT RUN**. Native compilation, editor initialization, GPU device/render target behavior and visual correctness are unverified because user explicitly requested completing functionality before tests. Do not claim the preview is operational on macOS until native UI execution.
- **Limitations and risks:** repeated editor preview render uses the shared renderer's scene/pass lifecycle; must validate with real Editor viewport ordering. Editor-only preview does not expose collision/death callback events, no GPU sim, no CPU-vs-GPU parity. The following test phase must inspect RT lifetime, device teardown, fps delta, and source hot-reload.
- **Next:** integrate opt-in host cue routing with canonical graph playback; implement GPU capability-gated particle execution and editor GPU/CPU mode selection, then tests after feature completion.

Pull-first:
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main && git merge origin/main
```


## 2026-11-19 — Stage 21: Simulation events to canonical particle graph playback
- **Implementation commit:** `83a07a485b5f7a3b728d2188f2cabd89424e25a0`, branch `feature/spectrafx-generic-cue-queue`, based on merged current main `d71c6d47e532b25fb816a8bb1f96c41e5275a165`.
- **File:** `engine/fx/SpectraFxGraphCollector.h` (new).
- **API:** `fx::installSpectraFxGraphCollector(simulation,playback,translator)` installs read-only `spectrafx.graph.collect` after gameplay systems; uses the existing `Simulation::events()` presentation records, calls host's converter to `SpectraFxCue` and sends them into `SpectraFxGraphPlayback` (canonical `SpectraGraphAssets` and `ParticleSystem` backend). `installSpectraFxOneShotGraphCollector` uses `SpectraFxEventMap::translateOneShot` for built-in event mapping. Both are opt-in, no RTS/FPS hardcoding. Header documents presentation frame order and rollback reset obligations.
- **Boundaries:** this new collector must not be registered alongside duplicate FX consumers for the same events; caller retains map/playback and translator resources until Simulation teardown. This is source integration API, not an automatic switch of existing RTS/FPS host modules.
- **Checks actually run:** **NOT RUN** per user's features-before-tests request. No native compilation, CTest, CI, editor screenshots, hardware GPU validation; implementation remains unverified until the dedicated validation phase.
- **Next functionality:** production host opt-in configuration, particle callback forwarding, GPU backend implementations and a richer editor canvas; once all features are finished run targeted then full CTests and native editor QA.

Pull-first:
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main && git merge origin/main
```


## 2026-11-19 — Stage 22: RHI-backed GPU particle evolution foundation
- **Branch:** `feature/spectrafx-generic-cue-queue`. Last branch comparison to main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` showed zero behind.
- **Code commits:** `b41c845aee3f8021ddcac72139119210b6dd7ed9` and `a757b545d02d624eba8358ec37828b06380831f6`.
- **Added files:** `engine/fx/SpectraFxComputeShaders.h`, `engine/fx/SpectraFxGpuEvolution.h`.
- **Functionality:** GPU kernels implemented for Metal MSL and GLSL 4.30: bounded particle position/age and velocity/lifetime evolution with acceleration/damping and a GPU-written RGBA32F position texture. `SpectraFxGpuEvolution::init` capability-gates via `supportsCompute()` and RGBA32F format support, validates finite seeds and max 65,536 particles, allocates RHI storage buffers and a storage texture, and creates compute pipeline. `step` validates 0..0.1-second dt/environment, binds storage, dispatches through existing RHI compute API outside rendering passes; `shutdown` releases device handles. Public accessors expose buffer/texture handles to a future GPU renderer. CPU `ParticleSystem` remains sole full-featured particle simulation/render path on devices without GPU support.
- **Compatibility:** opt-in GPU data evolution only. No changes to deterministic ECS, existing FX format, PRISMA, CMake or CPU ParticleSystem render order. Vulkan shader route is GLSL where supported; **no D3D12 RHI exists in inspected Gfx.h** and no D3D12 backend was added.
- **Actual verification:** **NOT RUN**, by user instruction (all functions first, tests later). No compile, shader compilation, RHI dispatch, real GPU render, CPU/GPU parity, CI, macOS Metal or Vulkan smoke. GPU kernel correctness is source-authored only, not verified.
- **Honest capability boundary:** this stage does NOT implement GPU particle spawning, collision, sorting, draw/indirect renderer or Niagara-level GPU parity. It is a real compute API path and position-buffer producer that must be consumed by a GPU drawing pipeline before it replaces any full CPU FX. Do not route active production effects onto it until renderer/fallback/lifetime handling are complete. GPU-owned buffers should be shut down on the device thread before device destruction.
- **Next:** add GPU billboard draw path (sample GPU-written positions), render-side budgeting and CPU fallback, then broader GPU particle modules. Add a frontend/backend chooser only when both execution and draw paths exist. Tests remain deferred.

Pull latest branch:
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main && git merge origin/main
```


## 2026-11-19 — Stage 23: GPU billboard draw path
- **Branch:** `feature/spectrafx-generic-cue-queue`, current merged main `d71c6d47e532b25fb816a8bb1f96c41e5275a165`; no missing main commits at preceding comparison.
- **Implementation commits:** `1c7af99bb444656e04a008f4ec36cf5ae5faf9cc` and `f8f15289d04b37697175a35142e15d0d8e6c723d`.
- **Paths:** updated `engine/fx/SpectraFxComputeShaders.h`, new `engine/fx/SpectraFxGpuBillboardRenderer.h`.
- **Functionality:** GPU compute stage now exports xyz and normalized remaining-lifetime alpha to RGBA32F storage texture while preserving internal age/lifetime state. `SpectraFxGpuBillboardRenderer::init/draw/release` creates a shared instanced quad, shader/pipelines for alpha and additive blending, samples GPU-written particle positions directly in MSL/GLSL vertex shader with one drawIndexed call and instance_count==particle count. Billboard orientation comes from caller's camera-right/camera-up vectors, radius/color supplied by `Appearance`. Expired particles blend with alpha zero. Drawing requires a compatible open render pass AFTER compute completes, no CPU position readback; engine RHI owns all native backend specifics.
- **Semantics:** opt-in, isolated GPU point-billboard visual path. Existing CPU ParticleSystem still owns all production FX and is the fallback; this stage does NOT integrate GPU emitters or select a backend automatically. No new D3D12 backend. This is NOT equivalent to GPU sorting, collisions, subemitters, ribbons, transparent depth sorting, density/soft-particle effects, or complete Niagara-style renderer.
- **Validation actually performed:** **NOT RUN** by user request (all features first). Source authored and pushed only; no native compile, GLSL/MSL compiler, Metal/Vulkan frame, RHI state, CTest, editor QA or performance evidence. Renderer must be checked for texture resource barriers, shader bindings and pipeline formats on target GPU before enabling.
- **Next:** GPU emitter batch lifecycle, spawn/kill/recycle and capability policy with CPU fallback; renderer integration into the editor viewport and game render pass; verify after feature pass. Keep handoff on every stage, never claim GPU support as proven until native tests.

Pull-first commands:
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main && git merge origin/main
```
