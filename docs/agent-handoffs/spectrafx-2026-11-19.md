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


## 2026-11-19 — Stage 24: generation-safe GPU slot allocation and emission lifecycle
- **Branch:** `feature/spectrafx-generic-cue-queue`; comparison against main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` at this continuation: ahead 53, behind 0.
- **Implementation commits:** `859c348ac52ec978bba8c47b49cb6dd309639d32` and `59c5f3aef0bbce1c6cfa278f3ce12c41b1499c8c`.
- **Paths:** `engine/fx/SpectraFxGpuEvolution.h` (modified), `engine/fx/SpectraFxGpuParticleSlots.h` (new).
- **Architecture/API:** GPU evolution now supports `initCapacity(device,capacity)` using dormant zero-lifetime slots and `writeSlot(index,seed)` for a bounded partial update of two dynamic storage buffers at RHI safe points. `SpectraFxGpuParticleSlots::init/emit/kill/step/shutdown/alive` tracks allocation, expiration and generation-protected handles; free slots recycle without writing to authoritative ECS. All motion continues on the GPU, and CPU only tracks age for reclaiming capacity. Dormant slots have GPU lifetime zero and therefore draw transparent after the next compute update; no direct GPU readback. Slot generation increments when reused. A failed GPU dispatch does not advance the CPU slot clock; an unsafe GPU-buffer update postpones release and prevents early reuse.
- **Contract:** device-thread-only; capacity 1..65536, caller must issue one `step` before first draw to populate the GPU output texture. Call `emit/kill` only when `canUpdateSharedBuffers()` holds and outside passes, ensure emitted particle lifetimes fit the evolving slot clock, and `shutdown()` before destroying the device. This is presentation-only; not yet a complete multi-emitter Niagara GPU backend.
- **Actual validation:** **NOT RUN**: no native compilation, shader compiler, macOS Metal/Vulkan run, CTest, editor/GPU visual QA, CPU parity or CI, per user instruction functions first. Do not treat correctness as verified.
- **Known limitations:** CPU expiry clock intentionally approximates the same age used by GPU; GPU execution timing and stalling must be checked. No sorting, collisions, GPU-side event callbacks, indirect live-count compaction, multi-emitter particle-level configuration. The GPU texture still contains dormant slots and draws the fixed-capacity instance count.
- **Next:** introduce an explicit backend policy/capability decision with CPU fallback. Then wire a GPU-capable emitter to the actual editor/game rendering passes and implement richer spawning/kill/streaming. Tests only after feature completion.


## 2026-11-19 — Stage 25: conservative CPU/GPU capability routing
- **Branch:** `feature/spectrafx-generic-cue-queue`, main merged through `d71c6d47e532b25fb816a8bb1f96c41e5275a165`.
- **Code commit:** `2bb1b3299def58878945341091d1b29b9a804756`.
- **New public header:** `engine/fx/SpectraFxExecutionPolicy.h`.
- **Features:** `SpectraFxExecutionPreference` (Cpu/Auto/Gpu), `SpectraFxFeature` bitmask and `chooseSpectraFxBackend(device,request)` return backend choice, acceptance, and explicit reason. GPU selection requires 1..65536 particles, a simple unsorted billboard-only feature set (alpha/additive), RHI compute and RGBA32F support, and Metal/Vulkan. Collision, trails, mesh, subemitters, lighting, sorting, local-space, flipbook, ribbons and per-particle events remain CPU. Strict GPU requests reject unsupported features when fallback disabled; Auto uses CPU as necessary. No new genre coupling or D3D12 backend.
- **Important limitations:** policy only; it does not automatically rebind existing `ParticleSystem` emitters to GPU. The GPU code has not been proven with native shaders, backend barriers or framebuffer formats. CPU and GPU must never both simulate the same emitter concurrently. Effect-to-feature extraction and host renderer integration are future work.
- **Tests actually executed:** **NOT RUN** by user's explicit all-features-first instruction; no C++ compilation, CTest, GPU runtime or editor visual QA. Treat this as authored API only, not validated support.
- **Next actionable step:** implement one opt-in per-emitter host bridge using the policy, capped GPU slot allocation and actual renderer submission or graceful CPU fallback, then add spawn-rate/quality culling and GPU events. Keep handoff after every stage and reserve validation for final phase.


## 2026-11-19 — Stage 26: hybrid per-emitter CPU/GPU runtime ownership
- **Branch:** `feature/spectrafx-generic-cue-queue`, current merged main base `d71c6d47e532b25fb816a8bb1f96c41e5275a165`.
- **Implementation commit:** `0ddc2ea6072f8694791cf25cf0278df471399972`.
- **New file:** `engine/fx/SpectraFxHybridEmitter.h`.
- **Functionality:** `SpectraFxHybridEmitter::begin(device,cpu,config,origin,direction)` consults the conservative execution policy. GPU path initializes `SpectraFxGpuParticleSlots` and `SpectraFxGpuBillboardRenderer`; CPU path plays the authored `ParticleSystem` effect and explicitly refuses fallback if no valid CPU effect name exists. `emitGpu/killGpu/updateGpu/renderGpu` expose GPU lifecycle/frame boundaries; `stop(killCpu)` tears down the owned resources only. `gpuActive/cpuActive/backend/decision/status/gpuActiveParticles` expose actual execution choice. A failed GPU compute step suspends the emitter instead of silently running CPU and GPU simultaneously. CPU scene-wide `ParticleSystem::update` remains the host's responsibility (one update per frame, not per emitter).
- **Ownership and limitations:** presentation-only, opt-in. The adapter does NOT attach to real RTS/FPS automatically; no automatic transition of existing GPU particle state to CPU if a GPU step fails, no GPU shader-graph modules beyond the simple billboard/evolution subset, no per-particle GPU events/sorting/collision, no D3D12 backend. On GPU init failure, an authored CPU effect provides fallback only if `allowCpuFallback` is true. Device-thread teardown required before device destruction. Do not infer production readiness from source creation.
- **Tests actually run:** **NOT RUN** per user features-first instruction. C++ compilation, Metal/GLSL compilation, Vulkan/Metal draw path, RHI pipeline and framebuffer barriers, interactive editor preview and CTest/CI remain pending. Current `GpuEvolution`/GPU renderer still have unverified shader binding and buffer safety semantics.
- **Next stage:** integrate explicit GPU emitter creation and runtime selection into a real editor/play-test host, add spawn rate/quality budgets, then implement missing modules and GPU particle event feedback. Perform broad compile/test only after functional acceptance, and fix all warnings/errors before merging.


### Stage 26 follow-up — stale GPU handle protection
- **Code commit:** `7fc788f5c7d9db25e5bcd526dd92dc939bc64cc7`; `engine/fx/SpectraFxGpuParticleSlots.h`.
- Changed per-slot generation assignment to a monotonically advancing manager-owned counter that survives `init/shutdown` on the same manager, preventing an ordinary pool reset/reuse from recreating a previously used (index,generation) pair. `uint32_t` wraparound and handles crossing distinct manager objects/sessions still require external session-level identity if used by a host; do not expose raw handles across sessions.
- **Verification:** NOT RUN. The source remains uncompiled and unverified; full targeted/multiplatform checks are deferred by user request.


## 2026-11-19 — Stage 27: bounded GPU automatic emission, rate/distance/burst and quality scaling

- **Branch:** `feature/spectrafx-generic-cue-queue`. Start-of-stage compare to `main` `d71c6d47e532b25fb816a8bb1f96c41e5275a165`: ahead 62, behind 0; no merge needed.
- **Implementation commits:** `6ea338039fe219ebf9d4977d337ac521f7296899`, `c3d209858aa4b7bcbbafbbe74af25d9d30c960fc`.
- **Paths:** new `engine/fx/SpectraFxGpuSpawnScheduler.h`; updated `engine/fx/SpectraFxHybridEmitter.h`.
- **Architecture/API:** opt-in presentation-only `SpectraFxGpuSpawnSettings` supports particlesPerSecond, particlesPerMeter, startupBurst, lifetime, speed, spread, seed, maxSpawnsPerFrame. `SpectraFxGpuSpawnScheduler::configure/reset/tick/pauseDistanceTracking/stats` calculates bounded per-frame requests with fractional emission carry; the first tick avoids long-distance teleport bursts; time spawns follow the current location and distance spawns interpolate along movement. A local xorshift sequence provides inexpensive visual jitter without changing the authoritative simulation RNG. Quality tiers map Off/Low/Medium/High to 0/0.4/0.7/1 spawn multipliers. Frame cap and failed GPU uploads are counted as dropped, not unbounded deferred debt.
- **Hybrid integration:** `SpectraFxHybridEmitter::Config` gains `spawning`, `quality`, `autoSpawnGpu`; `begin` validates settings before allocating either backend, `updateGpu` schedules bounded new GPU seeds at a safe point before GPU evolution, `moveOrigin` updates attachment coordinates (and CPU-owned effect when active), and `spawnStats` exposes counters. GPU update returns false on suspended/invalid state. Existing CPU ParticleSystem emitter logic remains its sole owner; no concurrent CPU+GPU execution of one emitter.
- **Limitations:** spawn scheduler has no authored JSON binding yet and no automatic GPU emitter registration in Editor/Game scene. Quality only scales spawn counts, not shader iterations or screen fill. This is a CPU command producer for GPU slot allocation, not GPU-side append/compaction nor indirect live-count rendering. Slots still render up to capacity with alpha-zero dormant particles. Under GPU errors, scheduler may already have emitted some seeds before the failed evolution; hybrid then suspends instead of silently switching to CPU. Backend-specific safe-point/thread ordering must be verified.
- **Verification actually executed:** **NOT RUN** — feature-only phase by explicit user request. No native C++ compilation, CTest, MSL/GLSL compilation, editor preview, Metal/Vulkan frame, CI or GPU performance validation. Do not present this as tested production behavior.
- **Next:** add an authored GPU emitter configuration format and project-level native playback bridge, plus bounded emitter registration in the Editor/Game rendering path. Then implement GPU events/collisions and async-friendly rendering. Finish remaining functionality before broad testing.

### Pull newest branch before local work
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main && git merge origin/main
```


## 2026-11-19 — Stage 28: versioned JSON GPU/CPU emitter configuration
- **Branch:** `feature/spectrafx-generic-cue-queue`; merged main ancestor `d71c6d47e532b25fb816a8bb1f96c41e5275a165`.
- **Implementation commit:** `6f9ad492c1347d4b82eb7a2c4fbad7a80a42f863`.
- **File:** `engine/fx/SpectraFxGpuEmitterAsset.h` (new).
- **Features:** `SpectraFxGpuEmitterAsset::read/readText` accepts explicit `version:1` authored configuration with `cpuEffect`, `preference` (cpu/auto/gpu), particle capacity, fallback policy, quality, rate/distance/burst spawn profile, seed, lifetime/speed/spread, frame budget, billboard appearance and acceleration/damping. Rejects unsupported fields and wrong types; numeric bounds are checked against `SpectraFxGpuSpawnScheduler::configure` before atomically replacing the destination `SpectraFxHybridEmitter::Config`. CPU fallback effect is required when appropriate, and GPU/Auto requires positive capacity. The document is rendering-only and does not enter deterministic gameplay saves or rollback state.
- **Limitations:** parser/validator only, not yet plugged into live scene asset discovery or editing controls; no writer API. It does not expose advanced modules; those remain on CPU. Incorrectly configured GPU assets are rejected rather than silently switched without an authored CPU fallback.
- **Verification:** **NOT RUN**, per user instruction. No compiler/CTest, native desktop, Metal/Vulkan graphics, JSON negative-fixture or editor UI execution. Source code requires later comprehensive validation.
- **Next:** add project-level asset loader and optional editor preview routing for GPU/CPU assets; continue full-feature GPU modules and eventual game loop integration before tests.

### Pull-first
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main && git merge origin/main
```


## 2026-11-19 — Stage 29: project GPU emitter asset loading
- **Branch:** `feature/spectrafx-generic-cue-queue`, latest GitHub main comparison at start: `d71c6d47e532b25fb816a8bb1f96c41e5275a165`, 0 commits behind.
- **Implementation commit:** `6f3ee511f0e0cbeacb956c2f6f8ef820b04fd00d`.
- **New file:** `engine/fx/SpectraFxGpuEmitterLibrary.h`. A generic caller-owned FileReader resolves `fx/gpu/<safe-name>.gpu.json`, checks path-safe asset names, hashes source bytes for hot reload, and uses the existing v1 `SpectraFxGpuEmitterAsset` parser. `load` performs validation before an atomic cache revision; invalid/missing reload keeps last known good configuration with explicit `fallback` result. `find/invalidate/invalidateAll/clear` expose cache control. No effect handle or GPU resource ownership in this class.
- **Compatibility:** opt-in presentation layer. The native project supplies a file reader; no runtime/global CMake/GameModule rewrite, no RTS/FPS dependency, no PRISMA ownership changes.
- **Actual verification:** **NOT RUN** by user's features-first instruction; no compiler/CTest, asset IO, hot-reload, GPU/Metal/Vulkan, native Editor or CI evidence.
- **Next:** add a renderer-thread manager connecting asset names, cue instances, `SpectraFxHybridEmitter`, and scene frame ordering with explicit safe-point requirements. Defer tests until requested feature completion.


## 2026-11-19 — Stage 30: project hybrid GPU/CPU presentation manager
- **Branch:** `feature/spectrafx-generic-cue-queue`, main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` at last comparison, 0 behind.
- **Implementation commit:** `6fc3dbd720675ae166b4e9a46c9002f0618d3177`.
- **New API:** `engine/fx/SpectraFxHybridPlayback.h`. A project host supplies `gfx::Device`, the shared `ParticleSystem`, and `SpectraFxGpuEmitterLibrary`; route arbitrary cue IDs to `fx/gpu/*.gpu.json` assets. `dispatch/dispatchAll` handles generation-safe, nonzero keyed Play/Move/Stop/Kill; Play reloads/validates an asset, starts a new `SpectraFxHybridEmitter`, and replaces the old instance only on successful startup. Bounded to 64 active emitter objects. `updateGpu` runs their compute stages outside passes, `renderGpu` submits GPU draws inside the current render pass. `reset` releases only managed instances at rollback/scene unload. The host updates its shared CPU ParticleSystem once per frame separately and must explicitly call `reset` before device teardown.
- **Safety/ownership:** refuses zero-key one-shots until an autonomous retirement policy is implemented, preventing unbounded orphaned GPU emitter resources. The library uses stable asset names and last-good fallback; no CPU/GPU double stepping of one managed emitter. No genre hardcoding, no ECS save or simulation state changes. No automatic hook into current RTS/FPS Editor/Game scene loops yet: host must install and drive this API explicitly.
- **Verification:** **NOT RUN**, per the user's functions-first requirement. No compilation, native CI, CTest, RHI GPU work, real editor integration, screenshots or functional game-loop checks executed. This stage is source only.
- **Limitations:** GPU emitter instances each allocate private buffers/shader/pipeline; high emitter count needs a pooled batched backend before performance acceptance. No GPU one-shot automatic retirement, particle collision callbacks or sorting. Render order is unordered_map iteration; alpha-sorted transparent effects remain CPU by current policy. GPU fallback on mid-frame failure currently suspends rather than transfers particle state.
- **Next:** add editor/project authoring integration for `fx/gpu/*.gpu.json` assets and a bounded one-shot expiry owner; then tune GPU pooling/collisions/LOD and integrate an opt-in native scene. Test after features as requested.

Pull-first:
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main && git merge origin/main
```


## 2026-11-19 — Stage 31: bounded one-shot hybrid emitter ownership
- **Branch** `feature/spectrafx-generic-cue-queue`. At stage start, compared with remote main `d71c6d47e532b25fb816a8bb1f96c41e5275a165`: 0 behind, 71 ahead; no merge required.
- **Implementation commit** `ee4d0e852ef9e44f43e53cf098499a57e01adceb`.
- **File** `engine/fx/SpectraFxHybridPlayback.h` (modified).
- **Behavior**: zero-key Play now creates an internally owned one-shot instead of being rejected; `Route::oneShotSeconds` (default 3, valid 0..60 exclusive of zero) provides a hard retirement deadline. One-shot and keyed instances jointly respect `kMaxEmitters=64`. `updateGpu(dt)` also advances one-shot ownership time and retires owned CPU/GPU resources; `renderGpu` draws their GPU batches; `reset` clears both types, and destructor calls `reset` (caller MUST destroy playback before Device/ParticleSystem). `oneShotCount` and `Stats::oneShots/retired` expose ownership. Invalid delta-time is rejected instead of advancing expiries.
- **Semantics**: one-shot retirement calls Stop with kill semantics; CPU particle tails are cut at the hard deadline instead of guaranteed to fade naturally. This is a safety bound, not an effect-duration detector. GPU effects continue spawning until deadline unless configured otherwise. Keyed Play still preserves a live prior instance until a successful replacement starts.
- **Compatibility**: additive API, no ECS/gameplay save changes and no PRISMA ownership overlap.
- **Actually executed**: **NOT RUN** — C++ build, CTest, native editor, CPU/GPU integration, shader compilation, CI and visual QA deliberately deferred per user request. Code is not confirmed production-safe.
- **Next step**: expose authored GPU hybrid assets in a real editor window. Then implement optional authoring/route configuration and scene runtime lifecycle. Keep testing deferred until feature completion.


## 2026-11-19 — Stage 32: GPU emitter assets editing in native AIVERSE Editor
- **Branch** `feature/spectrafx-generic-cue-queue`; stage began with `main` at `d71c6d47e532b25fb816a8bb1f96c41e5275a165`, 0 commits behind. Existing SpectraFX stages and all main changes preserved.
- **Implementation commits** `4a7a9b227f907eb271947662d8647fe6a6bbce53` and `81878ea57b0851451aadca7b18c58a5255c23336` (missing library include corrected).
- **Changed** new `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp`. Dockable look-group editor window registered via `AIVERSE_EDITOR_WINDOW`; lists project `fx/gpu/*.gpu.json`, creates default v1 documents, selects/loads files, permits raw JSON editing, performs mandatory `SpectraFxGpuEmitterAsset::readText` validation before save, tracks dirty documents, prompts before discarding on selection/create, performs temp-file rename and calls `Editor::reloadFx`. It intentionally does not take over the existing SpectraFX particle-graph editor or PRISMA postprocessing.
- **Scope/ownership** user-authored GPU/CPU emitter configurations only. The actual runtime remains `SpectraFxHybridPlayback` and `ParticleSystem`; no simulation ECS authoring, gameplay/network save or duplicated particle solver. Current UI intentionally does not promise GPU preview: this stage is asset authoring.
- **Actual validation** **NOT RUN**, as user requested finishing functions before tests. No C++ compiler, native Editor startup, ImGui interactions, actual file IO, CTest, CI, shader compilation, Metal/Vulkan rendering or visual verification. User must not infer the editor window has passed QA.
- **Known limitations** structured controls for individual GPU parameters are not implemented; uses JSON text editor. No preview toggle, no undo/redo for GPU config edits, no direct emitter scene placement, and `fx/gpu` list only refreshes manually or on project switch. Actual host has not been wired to drive hybrid playback every frame.
- **Next** implement opt-in scene/game host frame integration and authored cue-to-GPU asset mappings, scene placement, further GPU effect modules and GPU render-pass integration. After all functional stages, run native build/CTest and GPU/UI validation and fix failures.
- **Pull before continuation**
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main && git merge origin/main
```


## 2026-11-19 — Stage 33: opt-in GPU event routes with visibility guard
- **Branch:** `feature/spectrafx-generic-cue-queue`; verified start-of-stage comparison against main `d71c6d47e532b25fb816a8bb1f96c41e5275a165`: ahead 76, behind 0.
- **Implementation SHA:** `ddf52b1449202bd1bc97730335ac166bbfcd924e`.
- **Changed:** new `engine/fx/SpectraFxGpuRoutes.h`. `SpectraFxGpuRouteAsset::read/readText` parses versioned `fx/gpu/routes.json` containing 0–64 `{eventType,asset,enabled,oneShotSeconds,ownPlayerOnly}` entries, with strict types, unique uint16 event IDs, safe asset names and bounded one-shot lifetime. Atomic configuration replacement prevents malformed reloads from partially changing routes.
- **Visibility contract:** `ownPlayerOnly=true` is mandatory in this generic RTS bridge. It does not infer enemy visibility from hidden simulation events; requests for `ownPlayerOnly=false` are rejected until an explicit game/view visibility callback exists. This prevents rendering otherwise hidden enemy combat cues solely from reading authoritative `sim.events()`. Host must also avoid duplicating legacy RtsFx cues when authoring routes.
- **Compatibility:** feature-gated and standalone, no GameClient/Editor runtime modified in this stage, no ECS simulation/save changes, PRISMA untouched.
- **Verification actually performed:** **NOT RUN**, per user's features-first request. No compilation, CTest, application/editor or GPU validation. No production-ready claim.
- **Next actionable step:** integrate the authored routes in `apps/client/ClientFx` with a weak-owned, bounded per-tick event collector, then dispatch only at the safe GPU frame point before `Renderer3D::beginScene`; render after scene render while the pass is open. Protect against duplicate tick deliveries, paused/planet views and device teardown. Commit code and append next handoff separately.


## 2026-11-19 — Stage 34: live native RTS GameClient GPU frame integration (opt-in)
- **Branch:** `feature/spectrafx-generic-cue-queue`; latest compare at the stage start: `main` `d71c6d47e532b25fb816a8bb1f96c41e5275a165`, 0 behind, 76 ahead. No unrelated main edits replaced.
- **Code SHAs:** `e830f62ed6a0e320e103e2dc5fc004b878a3925f` (ClientFx public hooks), `2da27c262198200b39255d72a24f4e88a692da78` (ClientFx implementation), `5ffa492d8777ddb167df7873174899b6005e2df3` (real GameClient frame calls), `e08b35ceb900a2ea5887c1dcc8f19fb5b0220553` (planet-view guard order).
- **Changed:** `apps/client/ClientFx.h`, `apps/client/ClientFx.cpp`, `apps/client/GameClient.cpp`. **Existing** client rendering loop now optionally loads `fx/gpu/routes.json`, constructs `SpectraFxGpuEmitterLibrary` and `SpectraFxHybridPlayback` only when at least one configured route exists. A weak-pointer-owned `Simulation::addSystem("spectrafx_gpu.collect",...)` captures only per-tick `SimEvent` records owned by the local viewer, and only the explicitly configured event IDs. Queue is bounded to 1024; events are **not** dispatched into GPU within deterministic simulation. A new `ClientFx::prepareGpuFrame(dt)` drains the cue queue and invokes GPU compute inside the device frame, before `Renderer3D::beginScene`; `renderGpu(viewProj,camera)` draws the GPU-only billboards in the already-open scene pass, after existing CPU `ParticleSystem::render`. Camera right/up are extracted from the view matrix. No generic GPU effect routes = unchanged prior CPU RTS FX behavior. Planetary views reset/skip GPU effects until a dedicated body-local coordinate adapter exists, and this gate is applied before GPU prepare. Match stop and shutdown release hybrid GPU resources before device/ParticleSystem destruction; weak inbox makes stale Simulation collectors inert after match detach.
- **User-facing config:** `fx/gpu/routes.json` version 1, `routes` up to 64 entries of `{eventType: uint16, asset: "safe-name", enabled: bool, ownPlayerOnly: true, oneShotSeconds: float}`. Event ownership filter is deliberately conservative. Native host code still needs to prevent duplicate legacy RtsFx visuals if project authors assign the same cue to both old and new banks. A network spectator (viewer 0) does not receive GPU-event FX from this opt-in collector.
- **Scope limits:** Integrated **RTS GameClient only**; FPS external renderer, editor SceneViewport and generic native project hosts remain opt-in/manual. Not a production claim. No untrusted `ownPlayerOnly=false` routing, no visibility leaks by deliberately reading hidden World data, no new ECS persistent components/net formats. Each GPU emitter still owns private buffers and shader pipeline; no batching across emitters yet.
- **Verification actually run:** **NOT RUN** (explicit user instruction: complete functionality before tests). No C++ compilation, CTest, UI startup, RHI Metal/Vulkan shader compilation, render-pass/barrier visual check, performance profiling, CI or end-to-end event playback evidence. GitHub accepted commits, not proof that runtime works. Do not merge or enable by default until these gates pass.
- **Next:** editor UI for GPU cue route config and optional project-scoped preview; then cross-emitter batching, GPU collisions, sorting/LOD, and native host/fallback robustness. Finally compile, run focused tests/visual acceptance, fix all errors, and only then merge. Before each stage, fetch/pull branch and compare to latest main.

### Pull and sync (macOS)
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main
git merge origin/main
```


## 2026-11-19 — Stage 35: GPU route authoring in AIVERSE Editor
- **Branch:** `feature/spectrafx-generic-cue-queue`, synchronized with main through `d71c6d47e532b25fb816a8bb1f96c41e5275a165` at stage start.
- **Implementation SHA:** `eae04e062a196456c261cac530d4cd76a7244334`; **changed** `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp`.
- **Functionality:** the existing SpectraFX GPU Assets editor now has a **Event routes (routes.json)** entry alongside `*.gpu.json` configurations. It creates safe default `{version:1,routes:[]}` on demand, validates via the exact engine `SpectraFxGpuRouteAsset::readText` implementation, preserves dirty-discard confirmation, saves with temp rename, and warns authors that only own-player events may be displayed, hidden simulation events must never be leaked, existing `fx/fx.json` effects may duplicate these optional routes, and the standalone/hosted GameClient needs restart to apply changed routes. UI deliberately does not claim routes are hot-reloaded by an already-running GameClient (its `ClientFx::init` currently loads once).
- **Scope:** purely authoring/UI, no ECS changes. Project without `fx/gpu/routes.json` continues the existing CPU VFX path. No PRISMA ownership change.
- **Tests actually run:** **NOT RUN** (functions first, tests later); no native editor build, CTest, UI interaction, project save/reload, GameClient launch or Metal/Vulkan GPU verification. GitHub source commit only.
- **Known limitations:** routes editor remains raw JSON (typed form/list controls are not yet built). ClientFx loads routes at startup, not on game hot reload; no route UI auto-complete for module event IDs; CPU RTS effects can duplicate opt-in routed cues if project authors specify both. Real GPU emitter visuals and stability remain unverified.
- **Next:** inspect and implement a pooled/shared GPU batch for multiple emitters, reduce duplicated GPU pipeline/buffer allocations, and add the editor's opt-in GPU preview path; continue documenting each stage. Keep native tests and comprehensive QA deferred until feature-complete as requested.


## 2026-11-19 — Stage 36: shared GPU billboard pipelines
- **Branch:** `feature/spectrafx-generic-cue-queue`; main merged through `d71c6d47e532b25fb816a8bb1f96c41e5275a165` at last comparison. Implementation SHAs: `4c331bf89eeabd737497201a8b98b9d564cee914`, `d6c80e948099171e9d2ba8a29779472b290b06c5`.
- **Changed:** `engine/fx/SpectraFxHybridEmitter.h` and `engine/fx/SpectraFxHybridPlayback.h`. `SpectraFxHybridEmitter::begin` now optionally accepts a non-owning `SpectraFxGpuBillboardRenderer*`. When provided, the emitter uses that already-initialized renderer while retaining private particle state buffers. Local renderer creation remains supported for standalone callers. `SpectraFxHybridPlayback` lazily initializes **one** billboard shader/static quad/alpha+additive pipeline pair, shares it across managed GPU emitters, does not repeatedly retry failed GPU resource initialization in the same playback lifetime, and explicitly releases the shared renderer after stopping all emitter instances in its destructor. If resource acquisition fails, the existing authored CPU fallback is used only when allowed by the emitter's policy; there is never one effect driven by both CPU and GPU owners.
- **Scope and performance:** reduces redundant shader/pipeline and static quad allocation across active GPU emitters. GPU particle storage buffers/position textures remain per emitter; does **not** implement one global particle buffer, compute dispatch compaction, indirect live-count or cross-emitter sorting. No changes to ECS state, network saves, Graph v1 format or PRISMA.
- **Validation actually performed:** **NOT RUN** (user requested all functions before tests). GitHub commits succeeded, but no compiler, GPU shader validation, native game/editor launch, CTest, Metal/Vulkan timing, CI, or frame capture. Do not mark this as production-ready.
- **Known risks:** shared renderer pointer is borrowed: owners must not destroy it before all referencing emitter instances. `SpectraFxHybridPlayback` destructor now deliberately orders shutdown (emitter.reset -> shared renderer.release) before RHI device teardown. The fallback renderer state is not automatically retried after a device reset; recreate the playback object.
- **Next:** add an explicit project/example route and preview hooks, GPU particle collision/LOD/quality with a safe CPU fallback, and full native acceptance only after features are finished.

Pull:
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main
git merge origin/main
```


## 2026-11-19 — Stage 37: project-facing GPU runtime guide and client adapter cleanup
- **Branch:** `feature/spectrafx-generic-cue-queue`; latest main comparison for this continuation: main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` (0 commits behind at start).
- **Implementation/cleanup commits:** `fcf91926ef439af33808228070f2f7609291845c` (unused non-owning ClientFx device field removed), `9052e1886ff7e5f762496d6b07fcd28fbf39f4d4` (unused assignments removed), `8d946e63b80dd72333e0e614eb0b3b87018fc32f` (new `docs/spectrafx-gpu-runtime.md`).
- **Documentation:** demonstrates `fx/gpu/<safe-name>.gpu.json` CPU/GPU config and `fx/gpu/routes.json` project opt-in; labels example eventType 42 explicitly **illustrative only**, not an actual game module event code. Explains conservative local-player event visibility, potential duplication with `fx/fx.json`, backend policy and lack of D3D12, editor route editing + client restart, game `beginFrame -> GPU compute -> Renderer3D scene pass -> CPU and GPU particles -> PRISMA` order, exact ownership/reset requirements and the currently incomplete GPU modules.
- **Scope:** cleaning header/cpp warning exposure and source documentation only; no ECS/system/save version changes. Earlier Stage 34 and Stage 35 source changes remain committed intact, with handoffs. No tests or compilation were performed.
- **Verification actually executed:** **NOT RUN**, by explicit user's instructions (all functions, then tests). GitHub accepted all changes; no C++ compile, CTest, editor/game smoke, screenshot, Mac Metal/Vulkan rendering or perf evidence. Route documentation is not proof of implementation correctness.
- **Next feature priorities:** native Scene Editor GPU preview, pooled GPU particle storage across emitters, GPU collision/event feedback, LOD/quality controls, additional cross-genre host integration. Follow the mandatory per-stage handoff protocol; only after feature completion move to compiling and testing every implemented path.


## 2026-11-19 — Stage 38: GPU ground-plane collision (unverified)

- **Branch** `feature/spectrafx-generic-cue-queue`; at stage start main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` was ancestor (0 behind, 92 ahead).
- **Implementation SHAs:** `ac1ed124d4c3c8d56c6a4ff2beac9f22eb2e3630`, `fc0a572842baca02898a8db45048e8b93b8046bd`, `631afffb6adea240e35769c391d3235c246de657`, `d3da586f7b144f1f2ba664846d09f0d05a975b44`.
- **Changed:** `engine/fx/SpectraFxComputeShaders.h`, `engine/fx/SpectraFxGpuEvolution.h`, `engine/fx/SpectraFxExecutionPolicy.h`, `engine/fx/SpectraFxGpuEmitterAsset.h`.
- **API:** `SpectraFxGpuEnvironment` gains `collideGround` (off by default), `groundHeight`, `restitution`, `tangentRetention`; the Metal/GLSL compute shaders apply world-up Y plane collision and reflect downward vertical velocity, damping tangent velocity. GPU compute uniform block grows from 32 to 48 bytes and remains entirely within RHI uniform limits. JSON emitter assets accept optional `environment.groundCollision = {enabled,height,restitution,tangentRetention}`, with strict validation. Conservative execution policy gains `GroundPlaneCollision` flag; generic Collision/mesh/depth/terrain interactions still force CPU.
- **Constraints:** flat Y plane only (not curved planets or actual sampled terrain), not a general GPU particle collision system; no gameplay physics or damage events are published, and no GPU->CPU feedback yet. This remains a presentation feature. Existing default assets without `groundCollision` keep prior motion.
- **Tests actually performed:** **NOT RUN**. No local compiler, shaders, Metal/Vulkan GPU frame, native editor/GameClient, CTest or CI verified; the user's request is to finish functionality first. GitHub commits accepted, not evidence of collision correctness.
- **Next:** append optional compact per-particle GPU event records and nonblocking readback path; preserve generation/session guards and never feed events into the authoritative ECS. Then add opt-in presentation callbacks.


## 2026-11-19 — Stage 39: nonblocking GPU ground-impact presentation feedback

- **Branch:** `feature/spectrafx-generic-cue-queue`; main compared at stage start: `d71c6d47e532b25fb816a8bb1f96c41e5275a165`, zero behind.
- **Implementation commits:** `90625d64a09e37b1a6e63671020a8394010dd0be` (Metal/GLSL kernel event records), `668aa7a20f559554a5c0764a63ae90d7c9852aa4` (RHI impact buffer), `79e39a91cf153aa3f9c297bc2dff41debb39a362` (generation snapshot), `3bc630edfc7eb786030d77e0aef4308d300e6e65` (new `SpectraFxGpuImpactFeedback.h`), `1296cb431fb7dcb7ee5e3025f0be9fa82826a056`, `012be8ad8b21bfc7f7bcc9b7258b8569fd59cd57`, `450e191ee5c6947e5be9a4b5a8a3e7bbb50a2b46` (opt-in schema/capability routing), `d72a16947f231af86b805643962f29e7e4592a7e` (hybrid emitter wiring), and `2404c6d6aeffe200c944b168f33918a72f346d91` (presentation event dispatch).
- **Paths:** `SpectraFxComputeShaders.h`, `SpectraFxGpuEvolution.h`, `SpectraFxGpuParticleSlots.h`, `SpectraFxGpuImpactFeedback.h`, `SpectraFxExecutionPolicy.h`, `SpectraFxGpuEmitterAsset.h`, `SpectraFxHybridEmitter.h`, `SpectraFxHybridPlayback.h` (all under `engine/fx/`).
- **How it works:** MSL/GLSL compute writes one per-particle `float4` impact event record (xyz world position, w hit flag) to storage buffer slot 2. `SpectraFxGpuEvolution` allocates and binds this buffer; opt-in async RHI readback is limited to 4096 slots × 16 bytes = 64 KiB, matching `Device::readBufferAsync` Metal limit. `SpectraFxGpuImpactFeedback::request` uses `gfx::Device::readBufferAsync` with weak-owned callback state, a reset epoch and generation snapshot for the RHI frame; a bounded 512-entry queue is later drained on the render/presentation thread with current generation validation so stale recycled slots cannot send events. `HybridEmitter::updateGpu` requests async feedback only when `environment.reportImpacts` is enabled; `HybridPlayback::publishGpuImpacts(router,maxEvents)` converts the results into `SpectraFxEmitterEventRecord{Impact}` for the existing bounded *presentation* router. **Never feed these approximate visual events into deterministic gameplay/Simulation authority.**
- **Authored JSON:** `environment.groundCollision = {enabled:true,height:0,restitution:0.35,tangentRetention:0.8,emitImpactEvents:true}`. Feedback is explicit, disabled by default and requires collision enabled; `SpectraFxFeature::GpuImpactFeedback` chooses the GPU only on Metal and <=4096 slots. Vulkan/other RHI implementations currently return false from async readback, so the policy refuses the feedback feature (and uses CPU fallback if allowed); Metal runtime readback may also return false due to device state/full callback slots and is treated as optional telemetry loss.
- **Boundaries:** CPU effects retain their existing simulation. The event router's public `publishGpuImpacts` is an opt-in host call, not invoked automatically by ClientFx. GPU events are limited to bounces on the configured horizontal world plane (no mesh, terrain or depth collisions, no CPU gameplay hit detection). One packet in flight at a time; RHI may deliver later. The API drops or ignores overflow, stale generations, unsupported readbacks and reset epochs; it does not block/synchronously read GPU buffers.
- **Tests actually executed:** **NOT RUN**, per user instruction. No native compiler, CTest, shader compiler, Metal/Vulkan dispatch, editor/game scene, CI, screenshot or GPU performance validation; code is unverified and must not be merged as tested.
- **Next:** wire optional GPU impact router consumption into ClientFx with an explicit FX binding schema, add capacity/LOD and safe GPU impact budget policy, then finish renderer/backend features before native build/CTest/Metal GPU QA. Maintain handoff per stage.


## 2026-11-19 — Stage 40: authored GPU collision sub-effects in native client

- **Code commits:** `5115e66cfb10d4b706b666f597224211b22327c1` (`HybridEmitter::Config::impactEffect`), `49fdb719a662a9379fefb8a0e387bec141201e79` (validated v1 JSON `impactEffect`), `c3f81d8ec936d8160b9b6c63deb6d72e658e836e` (bounded `HybridPlayback::playGpuImpactEffects`), `b44489698688b1d3f5b7f8603ce5778724cb6eec` (`ClientFx::update` opt-in call).
- **Branch:** `feature/spectrafx-generic-cue-queue`, last main comparison 0 behind `d71c6d47e532b25fb816a8bb1f96c41e5275a165`.
- **Changed:** `engine/fx/SpectraFxHybridEmitter.h`, `engine/fx/SpectraFxGpuEmitterAsset.h`, `engine/fx/SpectraFxHybridPlayback.h`, `apps/client/ClientFx.cpp`. The optional `impactEffect` name in `fx/gpu/<asset>.gpu.json` must be a safe effect name and requires `environment.groundCollision.emitImpactEvents:true`. The emitter exposes its runtime config, and playback can drain asynchronous GPU impacts into CPU `ParticleSystem::play(impactEffect, impact.position)` with a global 32/frame default budget. The native client calls this **after** `ParticleSystem::setView` and **before** `ParticleSystem::update`, so impacts use the current presentation camera. The separate `publishGpuImpacts(router)` API remains available to hosts that want custom per-impact presentation callbacks; do not consume the same events via both APIs.
- **Scope:** strictly visual chain; GPU impacts do not write to simulation, network, physics, damage or ECS and must never be treated as authoritative hit detection. Only applies to optional routed GPU/CPU hybrid emitters, and Metal async feedback with <=4096 capacity as selected by policy. Non-supporting devices use the selected CPU fallback effect (if configured); event sub-effect parity on CPU is not guaranteed.
- **Actual verification:** **NOT RUN** by user's functions-first instruction. Native C++ compilation/CTest, MSL/GLSL compilation, Metal GPU readback, editor/game playback and performance remain unverified. No production release claim.
- **Current limitations:** each GPU emitter maintains its own feedback buffer/readback state; this stage does not introduce batched GPU feedback or arbitrary scene mesh collision. Readback events may be delayed/dropped if slots expire/recycle or asynchronous RHI callbacks are unsupported. Zero-key global event links are separate from authored on-impact effect names. Continue feature implementation first, validate later.
- **Next:** implement quality/scalability and distance-based GPU rendering LOD; consider pooled feedback buffers and GPU-side compaction, then test native editor and host once all functions are complete.


## 2026-11-19 — Stage 41: camera-distance GPU particle LOD and spawn quality

- **Code commits:** `a12b55e8b932d3138ac2daf73d3fb15e920d3daf` (new `engine/fx/SpectraFxGpuLod.h`), `9e45bbf77fa7dd3f071b9fd5f93dfd64f07c9c24` (burst obeys spawn scale and avoids uint32 overflow), `655c9d1bb595bb12785b0cc059eb662b80b06b7b` (hybrid emitter LOD), `3e6b8717bd530e91d7903da0cf82152da4c1df0f` (camera-aware playback), `3e928c9b2213e14188606dc973434c62ef758602` (v1 asset `lod` fields), `9a16987ef1e53d05a3ee4b959fff01026fe6ee46`, `8fb5065e17db9fcee3a8d944b512ce205e202f2e`, `bf8278939696f57e13a530f8b20c00c407e84a97` (ClientFx.h/ClientFx.cpp/GameClient.cpp).
- **Branch:** `feature/spectrafx-generic-cue-queue`, main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` was ancestor at last compare.
- **Functionality:** `SpectraFxGpuLodSettings` is pure presentation math, providing near/far distance interpolation of spawn intensity and optional visibility culling. JSON v1 emitter assets accept `lod: {near,far,farSpawnScale,cullBeyondFar}` with strict finite/bounds checks; `far:0` disables distance LOD by default, preserving prior behavior. Hybrid emitter combines distance-LOD spawn multiplier with FxQuality spawn multiplier and skips GPU billboard draw beyond far distance, returning success for intentional culls. GPU motion/lifetime continues even when not visible so slot recycling stays correct. Startup bursts now obey quality/LOD, including quality Off, with uint64 intermediates. `HybridPlayback::updateGpu(dt,cameraEye)` applies LOD to attached and one-shot batches, while the old `updateGpu(dt)` overload remains usable for hosts with no camera; native GameClient passes `cam_.eye()` via `ClientFx::prepareGpuFrame(dt,camera)`.
- **Scope:** no render-quality claim, no ECS gameplay changes and no PRISMA ownership overlap. This is GPU view-distance scalability, not full particle-level GPU culling, occlusion, indirect live-count compaction, dynamic GPU-budget feedback or cross-emitter sorting. Fixed-capacity GPU draw still submits all slots inside the visible LOD range, with dormant/expired slots transparent.
- **Tests actually run:** **NOT RUN** per user's explicit features-first request. Source authored/committed only; no native C++ compile, unit tests/CTest, client/editor launch, MSL/GLSL shader validation, Metal/Vulkan GPU scene, screenshots or performance measurements. The GPU and LOD paths are not verified for production.
- **Next:** add per-view GPU-budget/overdraw policy and spatial GPU pool optimizations; integrate more advanced particle event handlers and collision shapes, then run complete native compilation, tests, visual performance checks, warning cleanup and merge only when validation succeeds.


## 2026-11-19 — Stage 42: GPU live-slot compaction + Metal indirect command generation

- **Branch:** `feature/spectrafx-generic-cue-queue`, started 0 commits behind main `d71c6d47e532b25fb816a8bb1f96c41e5275a165`; 123 ahead at pre-edit comparison. No main merge required.
- **Code commit:** `bb67f01946c8aeb319113b3c887be3078d6c747b`.
- **Added:** `engine/fx/SpectraFxGpuCompaction.h`: `SpectraFxGpuCompaction::init/prepare/preparedFor/denseIndexTexture/indirectArguments/release`. Experimental MSL compute kernel atomically builds an unsorted, dense mapping of live slot indices (the evolution output texture's alpha is >0) and produces a 20-byte `DrawIndexedIndirectCommand` with `indexCount=6` and *GPU-authored instanceCount*. Three buffered frame slots contain a packed-index RGBA32F texture, 4-byte atomic counter and Storage-type indirect argument buffer. Each source slot index (<=65535) is represented exactly as float32. `prepare` requires an earlier successful `GpuEvolution::step`, calls two sequential compute dispatches outside render passes, and never reads the counter back to the CPU. Source texture/frame serial prevents using stale compaction after another scene/frame. `release` must precede device destruction.
- **Important RHI discovery:** `gfx::Device::drawIndexedGpuIndirect` is implemented for **Metal** in `GfxMetal.mm`, and requires its GPU-authored arguments buffer to be `BufferType::Storage`. The current Vulkan implementation only supports CPU-authored indirect batches through a separate API; Vulkan is not advertised as supporting this new compacted draw. `Device::frameSerial()` is used as a Metal-only, 3-frame-in-flight reuse guard.
- **Scope:** an experimental *compaction producer*, not yet connected to the billboard vertex shader/draw submission in this stage, and NOT a shared global multi-emitter GPU particle pool. It compacts each emitter's already GPU-updated position texture; CPU ECS/net/save state and PRISMA untouched.
- **Actual tests:** **NOT RUN**, user's functions-first instruction. No C++ build, MSL compiler, Metal GPU execution, validation of encoder hazards, draw/visual parity, CTest/CI or perf benchmarks. No GPU-indirect support claim until native testing.
- **Next step:** wire the renderer vertex shader to sample packed slot indices, submit Metal GPU-authored indexed indirect draw and fall back to the normal full-capacity instanced draw when optional compaction preparation or RHI indirect submission fails. Preserve one code commit + one handoff per stage.


## 2026-11-19 — Stage 43: compaction-aware GPU billboard draw

- **Implementation SHA:** `571d44af8031beac22ecb30d50ed755286416965` on `feature/spectrafx-generic-cue-queue`.
- **Changed:** `engine/fx/SpectraFxGpuBillboardRenderer.h`. The existing MSL/GLSL vertex shader can optionally fetch packed live-slot indices from GPU-produced RGBA32F texture 1 and then read real particle positions from texture 0. `draw(..., const SpectraFxGpuCompaction* = nullptr)` preserves all existing callers; when the optional Metal compaction object has matching source texture/current frame serial, it binds both textures, sets `u_meta.y=1`, and calls `gfx::Device::drawIndexedGpuIndirect(compaction.indirectArguments(),0)`. On failure, it resets mapping mode and executes the original full-capacity `drawIndexed(6,...,count)`, with transparent dormant particles. The normal path always binds both declared textures to satisfy native RHI resource binding.
- **Compatibility:** no engine RHI ABI modification, no ECS save/state/network changes, PRISMA untouched. Compaction remains an **opt-in parameter**; current HybridEmitter has not been wired to create/prepare a compaction object yet. The indirect backend is Metal-only, Vulkan CPU-authored indirect is not claimed as supported for GPU-written commands.
- **Validation actually performed:** **NOT RUN** per user's functions-first instruction. Neither native compilation nor GPU shader/Metal execution, RHI hazard validation, fallback/quality/performance comparison, CTest nor editor screenshots have been conducted.
- **Next:** integrate optional compaction object into the existing HybridEmitter `begin/updateGpu/renderGpu/stop` lifecycle, with JSON asset toggle and fallback if compaction resources/pipeline are unavailable. Maintain separate handoff commit after that stage.


## 2026-11-19 — Stage 44: opt-in compaction lifecycle and authored config

- **Code commits:** `3ea35910c319f4deee4e9c0a6090e4590f63c98e` (hybrid emitter), `175802747b0cf5e1922965dad588790095506412` (GPU emitter JSON parser), branch `feature/spectrafx-generic-cue-queue`, main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` had 0 missing commits at start.
- **Files changed:** `engine/fx/SpectraFxHybridEmitter.h`, `engine/fx/SpectraFxGpuEmitterAsset.h`. The v1 emitter asset now accepts optional `gpuIndirectCompaction: bool` (default **false**). `HybridEmitter::begin` attempts a private Metal `SpectraFxGpuCompaction` resource set when this switch is true and a GPU backend was selected. Compaction init errors increment an explicit counter but never break the normal GPU instanced draw; `updateGpu` calls `GpuEvolution::step` then optional `compaction.prepare` outside passes, `renderGpu` passes a compaction pointer to the shared billboard renderer only if compaction is initialized. The renderer tests current frame/source texture and uses a GPU-authored indirect draw only when valid, otherwise returns to `drawIndexed`. `stop()` releases compaction resources before destruction of emitter GPU buffers. `gpuCompactedFrames/gpuCompactionFallbacks` provide diagnostics; the first counts prepared compaction frames, not confirmed indirect draw submissions.
- **Backwards compatibility:** no graph/save/authority changes, PRISMA untouched. Existing projects without `gpuIndirectCompaction` continue full-capacity draw. The capability is **Metal-only** and explicitly experimental. Vulkan retains full-capacity draw with transparent dormant slots (not GPU-authored indirect). There is no global/shared particle memory pool across emitter objects yet.
- **Tests actually run:** **NOT RUN** by user request (functions before tests). Source edits and handoff commits alone do not establish that Metal shaders compile or that the RHI barriers/indirect ABI are correct.
- **Next:** add a CPU-owned *shared GPU particle arena* that suballocates stable ranges to multiple emitters where compatible, or at minimum a shared GPU memory budget and per-emitter allocation policy; add frame safety against retired resources and optical/alpha sorting limitations. Then expand modules, and test everything only after functionality completion.


## 2026-11-19 — Stage 45: homogeneous shared GPU particle arena

- **Implementation SHA:** `1c84becb5110b9288feb97e08f947f501695a106`, branch `feature/spectrafx-generic-cue-queue`.
- **Added:** `engine/fx/SpectraFxGpuSharedArena.h`; `SpectraFxGpuSharedArena::init/addEmitter/moveEmitter/removeEmitter/update/render/shutdown`. Unlike existing `SpectraFxHybridPlayback` where each effect has its own GPU buffers, the new **opt-in homogeneous arena** lets up to 64 independently spawned presentation sources share **one** `SpectraFxGpuParticleSlots` store, **one** `GpuEvolution` compute pass, **one** `SpectraFxGpuBillboardRenderer` and one optional Metal `SpectraFxGpuCompaction` draw. Sources retain independent SpawnScheduler, distance LOD, quality tier, positions, direction and tracked generation-safe particle handles. Source iteration uses ordered `std::map`; stale handles are pruned every update; removing a source can either let already-spawned particles fade until their lifetime expires or kill its active particles at a buffer-safe point. Global capacity and source-count bounds enforce predictable presentation memory. `shutdown` explicitly releases GPU resources before device teardown.
- **Important limitation:** all sources in an arena must share identical `Appearance` and `GpuEnvironment`; it **does not** pack heterogeneous materials, collision policies or rendering modes into one draw. It is a new opt-in API, **not yet** the default backend for project-authored `HybridPlayback`. Existing game sessions continue their current hybrid per-emitter behavior unless a host explicitly creates this arena.
- **Fallback:** Metal GPU compaction is optional; failure retains full-capacity instanced rendering. The shared arena itself requires GPU capabilities; the **existing** `ParticleSystem`/`HybridPlayback` remains the CPU fallback at a higher host level, not part of this arena. The arena doesn't publish authoritative gameplay state or PRISMA postprocessing.
- **Validation actually performed:** **NOT RUN** per user request. C++ compilation, shader compilation, Metal indirect draw, Vulkan GPU evolution, allocator generation and visual/performance QA are unverified. No CPU/GPU parity assertion.
- **Next:** add project-level homogeneous batch routing and a feature flag for the shared arena, plus separate authored groups. In the full validation phase, compile/test lifetime control, frame serial reuse, GPU/CPU path fallback and shader ABI before merging.


## 2026-11-19 — Stage 46: user-facing GPU indirect / shared arena integration guide

- **Documentation commit:** `d88a1f55234c241ec586d3cd3b07f26d2a435201`, branch `feature/spectrafx-generic-cue-queue`.
- **Changed file:** `docs/spectrafx-gpu-runtime.md`; describes v1 GPU asset toggle `gpuIndirectCompaction:true` (default off, **Metal-only**), packed alive-index texture, 20-byte GPU indirect command and full-capacity fallback when unsupported. Adds a native C++ usage sample showing `SpectraFxGpuSharedArena::init/addEmitter/update/render/shutdown` and explicit GPU frame/pass/device-lifetime ordering. The shared arena requires homogeneous appearance/physics, up to 64 sources and 65536 particles; it is not yet automatically selected by `HybridPlayback`. Alpha sorting, multi-material batches, terrain collision and production Metal/Vulkan qualification remain future work.
- **Tests actually performed:** **NOT RUN** per user requirement: feature work before tests. No native compile, Metal shader or GPU indirect run, performance data, CTest/CI, editor UX. This document is contract guidance, not a validation report.
- **Next:** optionally wire shared homogeneous arenas into project-level scene host routing, add explicit GPU sorting policy and multi-frame buffer lifetime QA. Only after full functional scope is implemented, run build/test and fix failures; never claim current experimental GPU path is production-ready.


## 2026-11-19 — Stage 47: explicit project routes for homogeneous GPU pools
- **Branch:** `feature/spectrafx-generic-cue-queue`; comparison at the start of this continuation: main `d71c6d47e532b25fb816a8bb1f96c41e5275a165`, 0 behind / 134 ahead.
- **Code commits:** `31baf541b03e1f24568bf0d4360cd97cc6951f93` (`engine/fx/SpectraFxHybridPlayback.h`) and `56afffd8ff862ca7e7e7212fe82faa9e345618e4` (`engine/fx/SpectraFxGpuRoutes.h`).
- **New authored route fields:** `sharedPool` (optional safe nonempty name) and `sharedPoolCapacity` (integer 1..65536; default 8192 when a name is provided). The routes validator checks types, safe names, requires a pool name if capacity is supplied, and rejects conflicting capacities for multiple routes targeting the same pool. Omission retains the prior dedicated HybridEmitter/CPU fallback.
- **Compatibility:** existing `fx/gpu/routes.json` files remain valid and unchanged. Stage 47 only adds validated route intent; it does **not yet attach** these routes to a running `SpectraFxGpuSharedArena`. A shared group must have homogeneous appearance/environment; feedback-enabled/complex effects cannot share the current arena and will use the existing dedicated path.
- **Tests actually run:** **NOT RUN**; no C++ compile, CTest, shader/backend or editor testing, per the explicit features-first requirement.
- **Next:** connect pooled groups in `SpectraFxHybridPlayback` with safe keyed + one-shot lifecycles, aggregate capacity/source limits, homogeneous configuration matching and atomic fallback to the existing path. Add a separate handoff immediately after the implementation stage.


## 2026-11-19 — Stage 48: project GPU routes now use homogeneous shared arenas
- **Branch:** `feature/spectrafx-generic-cue-queue`, merged main ancestor `d71c6d47e532b25fb816a8bb1f96c41e5275a165` at start (0 behind).
- **Implementation commits:** `ffc82f0c3f1fa2f2b0711e34b1e5199122dd33de`, `86639580338fd65e79fdb17440d29cb5e66434e4`.
- **Modified:** `engine/fx/SpectraFxHybridPlayback.h`. Existing `route(cueId,{asset,...})` now accepts optional `sharedPool` and `sharedPoolCapacity` from Stage 47. On Play, it validates the authored GPU asset and execution policy, then lazily creates/reuses a named `SpectraFxGpuSharedArena` using the configured homogeneous appearance/environment. Up to 8 named pools with **at most 65536 slots total**, and up to 64 active keyed/one-shot effect sources across dedicated and pooled paths. Pooled sources have independent Spawn, LOD, quality and generation-protected internal keys. One-shot `cue.key==0` gets a unique internal ticket and the configured `oneShotSeconds` deadline. For keyed Play, staging succeeds before the previous pooled or dedicated source is retired. Move/Stop/Kill are dispatched to either a pooled or a dedicated source, preserving existing route semantics. A pooled Kill destroys its particles, while Stop detaches the source and allows living particles to expire normally. `updateGpu(dt[,cameraEye])` updates each pooled GPU arena once per frame, retires one-shots and runs existing dedicated backends. `renderGpu` adds one billboard draw per active homogeneous arena after dedicated draws. `reset` tears down all groups and owned GPU resources before RHI device destruction.
- **Homogeneity guards:** exact agreement on material color/radius/alpha-vs-additive, acceleration/damping, optional ground-plane collision and compaction toggle. No GPU impact feedback/impactEffect in shared arenas, and `autoSpawnGpu` must be enabled. Unsupported features, policy disqualification, group settings mismatch, GPU allocation failure or group/source capacity exhaustion fall back to the **existing dedicated `SpectraFxHybridEmitter` path**, which retains its authored CPU fallback. No implicit pooling for routes without `sharedPool`.
- **Diagnostics:** `Stats::pooledPlays/pooledFallbacks/pooledFailures/pooledRetired`, `sharedPoolCount()`, `sharedPoolCapacity()`; `attached()` and `oneShotCount()` include both dedicated and pooled owners. Pool memory is retained until `reset()`, bounded by explicit group and aggregate capacity limits.
- **Actual testing:** **NOT RUN** per user's features-first directive. No native compilation, CTest, Metal/Vulkan shader execution, end-to-end ClientFx event play, editor, GPU timing or visual acceptance. Source is integrated but **not yet verified**.
- **Known limits:** GPU pooling remains optional and restricted to homogeneous billboard configurations; different materials or per-particle impact feedback continue dedicated. There is no GPU alpha depth sorting or cross-material batching, and the per-pool source-count cap is 64. Pool resources are deliberately retained between effects until scene reset. Need native checks of lifecycle, CPU fallback, group memory and resource submission before production use.
- **Next:** document sharedPool routes for project authors and preserve a safe opt-out, then tighten GPU memory/overdraw policy and robust handling of unsupported alpha ordering. Continue handoffs; only run tests after feature completion.


## 2026-11-19 — Stage 49: aggregate GPU slot budget and conservative alpha pooling policy
- **Code SHA:** `126fbf05de7667fc87cae7a79769aa76c7deef4b`, branch `feature/spectrafx-generic-cue-queue`.
- **Changed:** `engine/fx/SpectraFxHybridPlayback.h`. `kMaxPlaybackGpuSlots=65536` applies to the total capacities of *retained shared arenas plus active dedicated GPU emitters*, not just individual pooled groups. Before creating a dedicated GPU emitter, the runtime computes `usedGpuSlots()` and either selects the authored CPU fallback (when allowed and named) or refuses the Play if the capacity cap would be exceeded. This does not mutate the loaded asset configuration. Creating a new shared arena also checks aggregate capacity. Counters `gpuBudgetCpuFallbacks`/`gpuBudgetRefusals` expose budget decisions.
- **Transparency guard:** automatic shared-pool routing now requires `Appearance::additive=true`. Alpha-blended GPU effects continue their prior dedicated HybridEmitter path (with CPU fallback according to policy) because unordered cross-emitter particle interleaving is not an acceptable substitute for back-to-front transparency sorting. Shared arena is still individually usable via its native C++ API when a host intentionally opts into unsorted alpha.
- **Boundaries:** the aggregate budget belongs to one `SpectraFxHybridPlayback`; other game render systems and editor previews are not charged. Old GPU resources remain counted until the staged replacement succeeds and is retired; that can cause conservative budget-based fallback on exact-capacity replacements. Shared arena capacity remains reserved until scene reset, even if it currently has no emitters.
- **Actual tests:** **NOT RUN** (functions before tests, per user); C++ compilation, CI, CTest, native editor, GPU shader compilation, Metal draw/indirect and allocation behavior not verified. Source accepted by GitHub only.
- **Next:** extend project documentation with `sharedPool` route examples and support boundaries, then choose between adding sorted transparency through a verified GPU implementation or keeping transparent effects on CPU. Ensure test and perf benchmarking covers pool saturation, keyed replacement, one-shot retire and GPU budget fallback after functionality is complete.


## 2026-11-19 — Stage 50: idle pool reclamation and safe render-frame gating
- **Code SHA:** `dc1a839c99ea90690196e39812b04d0559aebc25`, branch `feature/spectrafx-generic-cue-queue`.
- **Modified:** `engine/fx/SpectraFxHybridPlayback.h`. Each `PoolGroup` tracks `idleSeconds` and `drawReady`. Groups with no registered emitter sources and no alive particles do not submit GPU compute or draw. After five seconds of unpaused/valid presentation delta (0..0.1s per frame) without sources or particles, a group releases its own GPU resources, returns reserved capacity to the aggregate budget and is removed. This avoids holding empty burst-only pools for an entire session. `Stats::unusedPoolsReleased` tracks reclamation. A GPU arena that fails to update this frame is not drawn using stale GPU state; failures are still counted. A new event recreates the named group lazily.
- **Scope/limits:** additive-only homogeneous pools, no ECS authority or PRISMA modification. Idle reclamation requires the host to call `updateGpu`; paused delta zero does not age the idle timer. Existing named pool routes remain loaded; only their GPU allocations are reclaimed. Release at a safe RHI presentation stage is expected by contract but still needs native GPU-fence/lifetime testing.
- **Tests actually executed:** **NOT RUN** per user's feature-first request. Native compilation, CTest, actual Metal/Vulkan frame, resource-lifetime/fence validation and performance measurement remain unverified.
- **Next:** finish project documentation/examples, then implement correct optional alpha-sort policy (or keep CPU fallback for transparency) and additional GPU budget controls. Run comprehensive tests, shader/RHI/GUI checks after functional completion, fixing all warnings/errors before merge.


## 2026-11-19 — Stage 51: correct transparency fallback until GPU sorting exists
- **Code SHAs:** `11907d31a8f412d500c647cbc438d243083142b1` (`SpectraFxHybridEmitter.h`), `828465aa9a13b7aba7815c95688a43f9286853ec` (`SpectraFxHybridPlayback.h`), `36f9aa335499a668b811700f596f01f02765c5e9` (`SpectraFxGpuEmitterAsset.h`).
- **Behavior:** `SpectraFxHybridEmitter::Config` adds `allowUnsortedAlpha=false`. `SpectraFxHybridEmitter::effectiveRequest(config)` adds the already-existing unsupported `SpectraFxFeature::Sorting` requirement for alpha effects unless the author explicitly opts into unsorted GPU alpha. Both the runtime's actual backend selection and the project playback's preflight/pool eligibility now use this same effective request to avoid disagreement. JSON v1 adds optional boolean `allowUnsortedAlpha` (default false) with strict validation.
- **Safety:** Additive effects continue the simple unsorted GPU path, including homogeneous shared arenas. Normal alpha effects default to the existing CPU fallback (which has legacy effect sorting), or reject strict GPU with fallback disabled, rather than silently presenting incorrect GPU alpha blending. Authors can deliberately set `allowUnsortedAlpha:true` only for effects that visually tolerate unsorted alpha; homogeneous automatic pooling still requires additive, even if this flag is set.
- **Limits:** This is a conservative routing rule, **not** a GPU alpha sort implementation. Arbitrary material/mesh/depth sorting, CPU/GPU visual parity, Vulkan/Metal GPU pipeline correctness and runtime feedback are still unverified.
- **Actual tests:** **NOT RUN** per user's features-first request; no native compiler, shader validation, CTest, GPU or editor execution. No production readiness claimed.
- **Next:** document the opt-in unsorted-alpha switch in the user-facing GPU guide and continue GPU sorting/multi-emitter resource/performance work before validation.


## 2026-11-19 — Stage 52: opt-in Metal GPU depth sorter for alpha particles
- **Branch:** `feature/spectrafx-generic-cue-queue`; at start main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` was already merged, behind 0.
- **Code SHA:** `3ab1fe2ec746e339f71d4182b4623701bd913765`.
- **New file:** `engine/fx/SpectraFxGpuAlphaSort.h`, class `SpectraFxGpuAlphaSort::init/prepare/preparedFor/sortedIndexTexture/release` (Metal-specific RHI compute). It snapshots a GPU-produced RGBA32F particle position/opacity texture after `GpuEvolution::step`, initializes `SortItem {depth,slot}` including padded next-power-of-two slots, runs back-to-front bitonic sort passes using separate compute encoders (RHI has no in-encoder dispatch memory barrier primitive), and writes sorted source-slot indices to a GPU RGBA32F texture. The depth key uses dot(position-cameraEye,cameraForward); fully transparent/dead slots sort to the tail. Equal-depth ties are ordered by lower source-slot index, so iteration is stable for a given particle state. No CPU readback.
- **Boundaries:** requires Metal, `Device::frameSerial`, RHI compute, RGBA32F and <=4096 source slots. Sorting remains **opt-in** and is not yet connected to the normal BillboardRenderer or HybridEmitter in this stage. It does not offer cross-emitter global transparency ordering or Vulkan sorting. Worst-case cost is multiple bitonic dispatches per frame and must be benchmarked; don't automatically enable.
- **Native tests actually performed:** **NOT RUN**, per user's features-before-tests directive. No shader compilation, Metal draw, CTest or GUI/visual validation; source code merged into feature branch only.
- **Next:** attach sorted index texture to GPU billboard shader and add conservative execution policy/authoring switch. Do not claim GPU alpha support or performance until later native checks.


## 2026-11-19 — Stage 53: Metal GPU alpha sort wired into RHI rendering, execution policy and client
- **Branch** `feature/spectrafx-generic-cue-queue`, main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` merged; 0 behind at start.
- **Code commits** `03b9c730cc524c2c1870d29add0838c915145d57` (BillboardRenderer samples sorted source-slot indices), `33a8cb3ff57aad6adcc39c6d6619fd97237a4f16` (Metal-only depth sort feature policy), `12cc73ca83f12778a3beb23ba96a4b35ffc74592` (HybridEmitter alpha sorting lifetime), `09c27f7712a6daff28653fddd523b1b35f67d2c8` (explicit GPU -> authored CPU fallback on runtime sort failure), `c958dbd81c1d3dfb8d4fd1d2e1ab4d79e9748843` (v1 GPU asset `gpuSortAlpha`), `241ce675f7b789938b63203081f3fa044cfff611` (HybridPlayback camera forward overload) and `23603c7d3479c5d164dde994695b163ae0b0c454` (ClientFx real camera forward).
- **Files** `engine/fx/SpectraFxGpuBillboardRenderer.h`, `SpectraFxExecutionPolicy.h`, `SpectraFxHybridEmitter.h`, `SpectraFxGpuEmitterAsset.h`, `SpectraFxHybridPlayback.h`, `apps/client/ClientFx.cpp`.
- **Public API:** `SpectraFxHybridEmitter::Config::gpuSortAlpha=false`, JSON `"gpuSortAlpha":true`, `SpectraFxFeature::GpuDepthSort`, `HybridEmitter::setSortView(eye,forward)`, `HybridPlayback::updateGpu(dt,eye,forward)`; the old update overloads remain for backward compatibility but cannot correctly enable a new sorted-alpha GPU emitter without the camera forward, and instead the requested sorted effect falls back to its authored CPU effect when possible. GPU support requires Metal + <=4096 particles + compute + RGBA32F + frame serial. Renderer binds sorted source indices on texture slot 1 and executes ordinary `drawIndexed` for count particles; GPU compaction indirect remains a mutually exclusive unsorted optimization. Sorted alpha GPU buffers are allocated on beginning an emitter and its bitonic sort is prepared **after particle evolution, before scene render pass**, not from deterministic simulation. RHI no-readback contract maintained.
- **Safety:** default alpha still uses CPU due missing `GpuDepthSort` unless author deliberately sets `gpuSortAlpha:true`. GPU sorted alpha and `gpuIndirectCompaction:true` cannot coexist; sorted alpha on additive is rejected. If sort fails during an update, the emitter destroys its GPU simulation resources and switches to the authored CPU fallback if available, without concurrent simulation/draw; otherwise it suspends rather than drawing incorrect unsorted alpha. Shared homogeneous project pools remain **additive-only**, independent of the new sorted-alpha mode.
- **Limits:** current sort is **per GPU emitter**, not cross-emitter or cross-material global transparency sorting. Sorting network uses multiple Metal compute dispatches/encoders and may cost much more than CPU sorting for small batches. CPU fallback does not preserve exact positions/ages of previous GPU particles when the backend changes; presentation is restarted at the configured effect origin. Vulkan and Android/WebGL retain CPU sorted alpha, unless unsorted GPU alpha is explicitly allowed by the author.
- **Native tests actually run:** **NOT RUN**, by explicit functions-first instruction. No C++ build, native MSL compiler, Metal GPU draw/correctness/performance, CTest, Editor/Client launch, screenshots or CI validation. No production-ready claim.
- **Next:** update project docs and look for feature-level improvements (bounds/quality profiles, sorted+compacted GPU path or native preview) before the deferred complete validation pass. Keep separate code/handoff commits each stage.


## 2026-11-19 — Stage 54: pause/empty GPU dispatch guards and authored sorted-alpha guide
- **Code commit:** `4d8e196830007fcaa7133cddb587e48cf0467fb3`. **Documentation commit:** `88db328a2f492cd8681493dce0342969586fcb01`.
- **Files:** `engine/fx/SpectraFxHybridEmitter.h` and `docs/spectrafx-gpu-runtime.md`, branch `feature/spectrafx-generic-cue-queue`.
- **Functional adjustment:** `HybridEmitter::updateGpu(dt)` now suppresses automatic spawn scheduler ticks when `dt==0`, avoiding initial bursts while paused. GPU alpha sorting skips its costly bitonic network when the emitter has zero allocated live particles, while GPU evolution can still refresh its transparent output. `renderGpu` returns success without draw if the batch has zero live particles or is distance-culled. These paths avoid some redundant GPU submissions without modifying gameplay authority or requiring CPU particle readback.
- **Authored workflow:** updated project guide covers `gpuSortAlpha:true` and incompatibility with `gpuIndirectCompaction:true`, the requirement for `appearance.additive:false`, default CPU-sorted alpha behavior, explicit `allowUnsortedAlpha:true` for visual exceptions, Metal-only <=4096 GPU sorting, performance costs and absence of cross-emitter/global alpha sorting. Documentation distinguishes GPU indirect compaction and GPU alpha sorting as alternative implementations, not simultaneously active.
- **Verification actually run:** **NOT RUN**, following user's features-first instruction. The 3 new code stages (52–54) are source-authored and saved to GitHub but have no native C++ compilation, MSL shader validation, CTest, editor/client rendering or GPU frame/performance check.
- **Next:** finish remaining GPU features and stability contracts, then run a dedicated native acceptance phase: C++ header compilation, MSL compile, sorting correctness for overlapping transparents, pause/timing, camera rotations, strict-GPU-no-fallback behavior, Metal resource barriers/lifetime and CPU fallback on GPU faults. Do not call sorted GPU alpha production-ready until validated. Preserve a separate handoff for every follow-up stage.


## 2026-11-19 — Stage 55: half-width Metal alpha sort compare dispatch and zero-work guards
- **Branch** `feature/spectrafx-generic-cue-queue`, main `d71c6d47e532b25fb816a8bb1f96c41e5275a165`, 0 commits behind at beginning.
- **Code SHAs** `a24a074b2e0e6ced3933938be610880389348d17` (`engine/fx/SpectraFxGpuAlphaSort.h`) and `3663b739f5d90c97aa00684ad26b5aa020310a18` (`engine/fx/SpectraFxHybridEmitter.h`).
- **Functionality:** Metal bitonic compare stage launches `paddedCount/2` GPU threads instead of `paddedCount`. Each dispatch thread calculates its own unique pair indices `left=(tid/pass)*(2*pass)+(tid%pass)`, `peer=left+pass`, then performs both compare/swap writes; there are no duplicate writes by two threads in a pair. Initialization and final sorted-index-output dispatches still use their own full logical thread counts. Frame-to-frame sort algorithm and sorted index asset schema unchanged.
- **Runtime:** the hybrid emitter skips GPU alpha sorting for LOD-culled emitters and batches with 0 or 1 live particle (ordering is immaterial), and skips optional Metal indirect compaction for culled or empty batches. GPU motion and lifetime still progress. When an emitter becomes visible again with >1 particle, the full sort resumes before drawing.
- **Safety:** presentation-only; no ECS/gameplay/network mutation, no extra platform feature claim. In the 1-live-particle case the billboard renderer uses its existing unsorted instanced draw, but only one slot has nonzero alpha.
- **Actual tests:** **NOT RUN**, per user instruction to finish all functional stages first. No C++ compile, MSL shader compiler, Metal GPU frame, sorting output comparison, CTest, UI, CI or performance measurement. Any speed improvement remains a theoretical reduction in launched comparison threads, not measured latency.
- **Next:** implement optional adaptive GPU emission budgets based on existing `Device::gpuFrameMs()` and a fixed host-provided frame budget. Make it opt-in and explicitly avoid treating whole-frame GPU timing as SpectraFX-only work. Document both fallback and quality behavior; preserve code/handoff stages separately.


## 2026-11-19 — Stage 56: opt-in GPU frame-time spawn governor and project budget controls
- **Branch:** `feature/spectrafx-generic-cue-queue`, latest checked main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` was already merged, behind 0 at start.
- **Code commits:** `f3c29395c36cf5c0ceb5948629ee1b15f1b735ed` (new `engine/fx/SpectraFxGpuBudgetGovernor.h`), `9895edd50a874cfd6669a9f14197bb9849c9c13b` (dedicated emitter scale), `ad9572a631f8b8cbc212870f63fc444fb6b90b88` (shared arena scale), `919591659f72868fce3e0e7b6ed98580fd139f7c` (hybrid playback), `8d2f186a185dc199eed7d3b5729d498444537cd4` (route asset schema), `e62db3502d41400698ed271c48ca6e905b3f2053` (real ClientFx configuration).
- **Functionality:** new `SpectraFxGpuBudgetGovernor::configure/observe/spawnScale/stats` provides opt-in whole-frame GPU-time feedback with EWMA smoothing, 110% throttle / 80% recovery thresholds, bounded ramp rates and minimum spawn scale. A missing/zero/invalid GPU timer sample leaves scale unchanged (initially 1); paused dt=0 never alters quality. The governor affects **only the rate of NEW visual particles** in dedicated HybridEmitters and homogeneous SharedArenas, multiplied by existing authored quality and distance LOD. Existing live particles, GPU physics/age, gameplay ECS, network, rollback and persistent content are unaffected. `SpectraFxHybridPlayback::configureGpuBudget` and `gpuBudgetStats` expose explicit control/diagnostics.
- **Authored configuration:** optional root keys `gpuFrameBudgetMs` (0..1000; **0 disables**, default) and `minGpuSpawnScale` (0..1; default 0.25) in `fx/gpu/routes.json`. The existing route parser validates all supplied values even if the caller does not request the parsed budget; its `read/readText` accept an optional fourth output argument `SpectraFxGpuBudgetGovernor::Config*` without changing old call sites. Native `apps/client/ClientFx.cpp` reads and passes the settings into the existing playback manager only when authored routes are loaded.
- **Honest metrics:** `Device::gpuFrameMs()` measures the whole GPU frame; it is **not** a SpectraFX-only timing query. GPU contention from all other rendering systems may cause scaling. On backends without completed timing samples the governor cannot adapt. This is a best-effort visual quality policy and disabled by default.
- **Actual tests:** **NOT RUN**, explicitly per user's features-first request. No native C++ compilation, CTest, MSL/GLSL compile, Metal/Vulkan execution, GUI test, hardware timing or perf profiling, CI or visual acceptance. GitHub commit accepted only.
- **Next:** expose nominal GPU buffer-footprint estimates and per-backend diagnostic counters, document the authored budget controls, then continue remaining feature work; run comprehensive platform acceptance only after functional implementation.


## 2026-11-19 — Stage 57: on-demand GPU impact buffers and nominal GPU memory diagnostics
- **Branch:** `feature/spectrafx-generic-cue-queue`, last checked main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` was an ancestor (0 behind).
- **Code commits:** `b586f32a861cbe71f196bd99dc7996e77f6f150e` (shader guarded feedback writes), `f4307cf04fa096600bb9c78e429b74076a6c1944` (on-demand GPU impact buffers), `d2b20d4a31f6cc90da0945974e6c63b1b7551b1b`, `d340891db8dc167af504cec499513efc5c56545e`, `967a89f3e0b2bc6a9d8c341bf36f22427cbd921e` (caller propagation), `8891044af4ffe6ef32944a3f6337e99d3c3152f4`, `fe98ec96bf300fc8c37a4c39395edab86f22fbe0`, `83ae21d151febc9f81dab89aee71b313660a6786` (per-module nominal byte counters), `38fe19322b99d32123fa4671a6cc67174f4799f3`, `3529517d143e1b75cc5924cc6608a64f6e6c7983`, `5f9fdf251ad41680f4d3420cc84d9d63d6df9ee5` (aggregate bytes), `0fce57b851829d85da83b3d4f2b9338d34dcc33b` (ClientFx GPU diagnostics).
- **Implementation paths:** `engine/fx/SpectraFxComputeShaders.h`, `SpectraFxGpuEvolution.h`, `SpectraFxGpuParticleSlots.h`, `SpectraFxHybridEmitter.h`, `SpectraFxGpuSharedArena.h`, `SpectraFxGpuCompaction.h`, `SpectraFxGpuAlphaSort.h`, `SpectraFxHybridPlayback.h`, `apps/client/ClientFx.cpp`.
- **Functional memory saving:** `GpuEvolution::init/initCapacity` gain optional `enableImpactEvents=false` and allocate impact-buffer storage **on demand**. With feedback disabled the declared Metal/GLSL storage-buffer binding remains valid using a single 16-byte dummy record; the GPU compute shader checks `Params.pad` (now the feedback-enable flag) before writing any per-particle event records. With feedback enabled the full `count * 16` buffer remains available. Step rejects attempts to turn on feedback without allocating the full buffer at initialization. `impactReadbackEligible` now requires allocated feedback as well as count <=4096. This saves **16*(capacity-1) nominal bytes** for each emitter without feedback; at 65,536 slots ~1 MiB. Dedicated emitters and shared arenas pass their configured `environment.reportImpacts` through the GPU slot factory.
- **Diagnostics:** `GpuEvolution::nominalGpuBytes`, `GpuCompaction::nominalGpuBytes`, `GpuAlphaSort::nominalGpuBytes`, `HybridEmitter::nominalGpuBytes`, `GpuSharedArena::nominalGpuBytes`, `HybridPlayback::nominalGpuBytes` estimate allocated RHI payload bytes including rounded RGBA32F textures and optional GPU sort/compact buffers. They intentionally exclude driver GPU memory heap overhead, staging, pipeline caches and unrelated renderer resources. Under existing `AIVERSE_FX_DEBUG` the native client logs attached/one-shot counts, pools, reserved slot count, estimate KiB, filtered GPU frame ms, spawn scale and completed timing samples.
- **User guide:** `ce041571bbad353f41df651aa38568594cd2454e` updates `docs/spectrafx-gpu-runtime.md` with explicit `gpuFrameBudgetMs`/`minGpuSpawnScale` project route keys, opt-in/disabled defaults, whole-frame-timing caveat, diagnostics and impact memory optimization.
- **Validation actually performed:** **NOT RUN**, per user's explicit functions-first instruction. No C++ compilation, Metal or GLSL shader checks, CTest/CI, editor/game launch, GPU timing/profile measurements or visual comparison. The memory saving is derived from RHI buffer sizes in source, not measured on a device.
- **Next stage:** continue GPU pipeline/footprint optimization and controlled project host adoption; before production enablement, run native header/build checks, Metal shader compiler and render tests, check dummy buffer branch for out-of-bounds writes, compare GPU frame/memory counters across feedback OFF/ON, and resolve warnings/errors. Keep separate implementation/handoff commits.


## 2026-11-19 — Stage 58: one borrowed GPU evolution compute program per playback
- **Branch:** `feature/spectrafx-generic-cue-queue`. At the start of this continuation `main` `d71c6d47e532b25fb816a8bb1f96c41e5275a165` was fully merged (0 behind, 187 ahead).
- **Implementation commits:** `3e78b695a736a0471681dd18edda467fb1c2ebe7` (`engine/fx/SpectraFxGpuEvolution.h`), `ef90082ebe8baa325dd64db0a76974a3510f7a44` (`SpectraFxGpuParticleSlots.h`), `1fd7096e53146f63190b609fbfd9be188e6bb4de` (`SpectraFxHybridEmitter.h`), `faa2cd17db8ff7780f6a7c8cf9fb14efab357926` (`SpectraFxGpuSharedArena.h`), and `b60a66c05c9bf4e67bbf1dd3cabeea05baa2e09d` (`SpectraFxHybridPlayback.h`).
- **Implementation:** `SpectraFxGpuEvolution::init/initCapacity` accept an optional `gfx::ComputeHandle sharedCompute={}`. If valid, the shader is **borrowed**, and `shutdown` destroys only private buffers/textures, never a shared program; without a handle, the historical owned per-batch shader creation/destroy path remains. `SpectraFxGpuParticleSlots`, `HybridEmitter::begin` and `GpuSharedArena::init` pass this optional resource without requiring existing callers to change. `SpectraFxHybridPlayback::ensureSharedEvolution` lazily creates a single compute pipeline from existing Metal/GLSL sources; it shares the handle across dedicated GPU effects and all named homogeneous arenas. On pipeline creation failure, individual effects continue their previous owned-pipeline path (or authored CPU fallback), without assuming a working GPU backend. The playback destructor releases all borrowers first via `reset()`, then billboard renderer and shared compute handle before `gfx::Device` teardown.
- **Diagnostics:** `Stats::sharedComputePrograms` and `sharedComputeFailures`; counter 1 indicates this playback successfully created the shared program at most once. This avoids redundant compute program compilation per GPU emitter; GPU particle storage and collision feedback remain independently owned per batch.
- **Safety and limitations:** borrowed shader handles require strict device/owner lifetime discipline and render-thread-only usage. No cross-device caching, global process-level static cache, or GPU backend synchronization is claimed. The overall GPU capability policy remains opt-in and the underlying Metal/Vulkan path is still experimental.
- **Verification actually performed:** **NOT RUN**, as requested (complete features before tests). No native C++ compilation, shader compiler, GPU Metal/Vulkan render, RHI lifetime test, CTest, CI or profiler evidence. GitHub source commits succeeded, not proof of native execution.
- **Next:** implement opt-in CPU-side frame command-encoding telemetry to distinguish SpectraFX submission overhead from whole-frame GPU timing, then document and expose diagnostics in the native client. Never present CPU encoder timings as GPU execution times.


## 2026-11-19 — Stage 59: opt-in CPU command-encoding telemetry (not GPU timing)
- **Branch:** `feature/spectrafx-generic-cue-queue`, latest known main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` was fully merged (0 behind at start).
- **Implementation commits:** `3af19b913ac9998c3c70966c563dfa7e96a85b31` (`engine/fx/SpectraFxHybridPlayback.h`) and `11535d8f38dc8538398f9431b41c90592b40fd6f` (`apps/client/ClientFx.cpp`).
- **Functionality:** `SpectraFxHybridPlayback::setCpuEncodeProfiling(bool)` (default false) wraps the adapter's GPU work submission `updateGpu(dt)` and render submission `renderGpu(...)` in `steady_clock` timestamps, recording `Stats::cpuUpdateSamples/cpuDrawSamples`, cumulative elapsed microseconds and peaks. This measures CPU elapsed wall time spent **encoding and issuing calls**, including RHI API overhead, NOT GPU kernel time, GPU render execution, GPU-clock timestamps or asynchronous completion. Frame-graph work outside those methods and some camera LOD setup overhead is not captured. The existing `AIVERSE_FX_DEBUG` flag enables the counters in the native client and logs average/peak microseconds alongside already existing whole-frame GPU time and whether the shared compute program was created successfully. No new public RHI ABI or platform dependency is required.
- **Actual validation:** **NOT RUN**, user requested all features first. No native compilation, CTest, real game frame, Metal or Vulkan GPU timing, device mock, CI, profiling or GPU/memory measurement. Logging code/source is authored only.
- **Next:** share additional identical Metal GPU sort and compaction pipelines across dedicated emitters and shared arenas, with borrow/own lifetime rules; expose their cache counters. Keep per-stage handoffs, and defer native validation until all required functions are complete.


## 2026-11-19 — Stage 60: shared Metal compute pipelines for alpha sort and indirect compaction
- **Branch:** `feature/spectrafx-generic-cue-queue`; main was merged through `d71c6d47e532b25fb816a8bb1f96c41e5275a165` (0 behind at start).
- **Code SHAs:** `53bce5f96bb65772acb3aa25e64c27641f659a15` (`engine/fx/SpectraFxGpuAlphaSort.h`), `66794f272577542001c10fb96f934ef2b17a35ae` (`SpectraFxGpuCompaction.h`), `1b7e3b6ca90cd1d92f487c16485393ed0a081cb9` (`SpectraFxHybridEmitter.h`), `74fe5946ec0f648383a3c1d0d2e76be58d24fd22` (`SpectraFxGpuSharedArena.h`), `6ae7bd56a39d98022f68efbf46724f4721466c77` (`SpectraFxHybridPlayback.h`), `8936f893743d5af22e3fce64ff176290ad211448` (ClientFx debug counters).
- **Feature:** `GpuAlphaSort::init` and `GpuCompaction::init` accept an optional `gfx::ComputeHandle` for a shared, non-owning pipeline. When none is supplied, each unit retains the historical privately compiled/owned pipeline and destruction behavior. `HybridEmitter::begin` now forwards optional `sharedAlphaSort`/`sharedCompaction` handles, and `GpuSharedArena::init` forwards optional `sharedCompaction`. The playback lazily compiles/caches at most one Metal alpha sort and one Metal live-slot compaction compute pipeline, shares them across all eligible dedicated emitters and homogeneous arenas, and destroys them **only after reset() has torn down all borrowers**, along with the previously shared evolution compute pipeline. Errors are explicitly counted; if shared creation fails, the private compute creation path remains available (or authored CPU fallback for mandatory sort).
- **Diagnostics:** `Stats::sharedAlphaPrograms`, `sharedCompactPrograms`, `sharedMetalProgramFailures` and existing `sharedComputePrograms/sharedComputeFailures` are emitted under `AIVERSE_FX_DEBUG`. They count shared program creations/errors and do not measure shader compile times or GPU execution cost.
- **Contracts:** all shared handles belong to one `gfx::Device` and one `SpectraFxHybridPlayback` lifetime; borrowing a handle across device/session ownership would be invalid. No static global shader cache, no extra genre module dependency, no ECS/network/save or PRISMA changes. GPU resources other than the actual compute program (storage buffers, textures, one per-emitter sort scratch/compaction scratch) remain independently allocated.
- **Tests actually run:** **NOT RUN**, per user's functions-first request. GitHub commits succeeded but no native compiler, MSL/GLSL shader compilation, GPU execution, CTest, editor/game smoke, frame timing or memory benchmarks. Do not claim measured performance uplift or production readiness.
- **Next:** add bounded/triple-slot GPU resource lifetime diagnostics and safe device-state guarding, or improve native feature uptake in additional genre hosts without duplicating gameplay FX. Then finish source features before comprehensive compilation/Metal-Vulkan/CPU-fallback validation and fixing warnings.


## 2026-11-19 — Stage 61: recover dedicated VFX after GPU compute/device failure
- **Branch:** `feature/spectrafx-generic-cue-queue`; main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` was merged at start (0 behind).
- **Implementation code SHAs:** `d36616cdd021fe4274db1cd2f492bd709c58bd6d` (`engine/fx/SpectraFxHybridEmitter.h`), `27fb809b9ff68abbe6f7f6ab2398575b00de2f44` (`engine/fx/SpectraFxHybridPlayback.h`), `f6537a77a862f06cc999b489067cf85bc4e9a05b` (`apps/client/ClientFx.cpp`).
- **Public behavior:** `HybridEmitter::updateGpu` now checks `gfx::Device::gpuHealthy()` before any new GPU work, and on an unhealthy device or failed `GpuEvolution::step` tries `switchToCpuFallback(reason)` as already used for GPU alpha-sort failure. This creates the authored CPU effect at the emitter origin before releasing GPU particle storage, optional alpha/compaction buffers and owned pipeline handles; it does **not** double-simulate/draw the emitter in that frame. If CPU fallback is forbidden or no usable effect is authored, the GPU emitter is suspended with an explanatory status instead of drawing possibly invalid data. This is presentation-only; particles are **restarted** on CPU rather than transferred from GPU (no state parity claim).
- **Diagnostics:** `HybridEmitter::gpuRuntimeFallbacks`, `HybridPlayback::Stats::gpuRuntimeCpuFallbacks` (incremented when a former GPU emitter becomes CPU-owned during an update), and `[spectrafx-cpu-encode]` log field `gpuCpuRecoveries` under `AIVERSE_FX_DEBUG`.
- **User documentation SHA:** `8219331559386cdf968af1ac9ba1e0f5dd5d23ac`, `docs/spectrafx-gpu-runtime.md`, covering Stages 58–61: shared compute program ownership, CPU-vs-GPU telemetry semantics and runtime fallback constraints.
- **Known limitations:** the shared homogeneous `SpectraFxGpuSharedArena` currently counts failed pool updates and suppresses draws; this stage **does not migrate multiple pooled sources to CPU** automatically. If the overall graphics device is unhealthy, CPU effect rendering may also fail to display because that still requires the RHI. Borrowed shader/resource lifetime is entirely device-thread/host-owned.
- **Actual tests:** **NOT RUN**, per user's directive to complete functions before testing. No native C++ build, CTest, Metal/Vulkan shader check, editor/client launch, GPU fault injection, resource-lifetime race test, CI, GPU timing or performance evidence. Do not represent runtime recovery as verified.
- **Next feature priority:** implement pooled-source recovery or safe per-pool reset on failed GPU evolution, add bounded GPU buffer/frame-hazard policy and optional scene-driven integration beyond RTS, then only after feature completion perform native compile/CTV/GPU tests and fix every error/warning before merge.


## 2026-11-19 — Stage 62: pooled GPU failure recovery and deferred RHI resource retirement
- **Branch** `feature/spectrafx-generic-cue-queue`, at stage start main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` merged; behind 0.
- **Implementation commits** `17972bff1a0aeccccc64ee4f1932aa06b49f4a43` and `74f065d9d99d6269c2487a0104a5a89ea8de35b1`; both change `engine/fx/SpectraFxHybridPlayback.h`.
- **Runtime behavior:** `PooledRef` retains the authoring config, latest presentation origin/direction, pool name and internal generation-safe ticket. `Move` updates the stored pose as well as the arena source. An unrecoverable `GpuSharedArena::update` failure, unhealthy RHI, or earlier flagged render submission failure invokes `recoverPooledSources(name)` **only in the next frame's safe, outside-pass `updateGpu`**. Sources with allowed and valid `cpuEffect` create new CPU-owned `SpectraFxHybridEmitter` instances at their last known poses, preserving keyed instances and existing one-shot elapsed/duration bookkeeping. Other sources are dropped explicitly and counted. No GPU particle state readback or authoritative ECS replay; the CPU visual effect restarts from the origin. Failed pools do not submit further draws; a GPU render error only sets `failurePending`, deferring CPU replacement/resource handling until the next update.
- **Frame-safe lifetime:** `retirePoolResources` transfers old GPU arenas to a `retiringPools_` ownership list, excludes them from active pool draws, and continues counting their reserved capacity in `usedGpuSlots()` and nominal GPU bytes. `collectRetiredPools` runs at update-time outside render/compute encoders and destroys resources only once the Metal `Device::frameSerial()` has advanced by at least 3 slots after retirement. A device without a reliable nonzero serial retains resources until `reset()` rather than guessing GPU completion. This is a conservative safety policy; actual Metal completion/encoder semantics still need native verification.
- **Pool idle retirement:** wholly empty pools now use the same deferred resource path after their five-second inactivity window. Existing `unusedPoolsReleased` counts retirement initiation, while `pooledResourceReleased` counts actual GPU resource destruction. Reset/host shutdown releases both active and retiring pools before destroying shared compute shader handles and before the RHI Device. New counters `pooledCpuRecoveries`, `pooledRecoveryDropped`, `pooledRenderFaults`, `pooledResourceRetired`, `pooledResourceReleased` distinguish recovery, loss and retirement.
- **API/limits:** `SpectraFxHybridPlayback::Route` and JSON `sharedPool` fields unchanged; no new editor schema or genre-specific coupling. The 65,536-slot aggregate budget includes retired pools until their resources are actually released. No automatic GPU retry of a previously failed pool in the same frame; CPU fallback only when allowed. Vulkan lacks `frameSerial` completion tracking, so retained allocations conservatively count toward budget until scene reset.
- **Tests actually run:** **NOT RUN**, per user's feature-first directive. No native compilation, CTest, MSL/Vulkan execution, RHI fence or semaphore verification, fault injection, CPU/playback regression or visual acceptance. Do not treat the 3-frame delay as a proven hardware fence.
- **Next:** harden guardrails for failure loops and stalled/paused frames, publish recovery/retirement statistics in `AIVERSE_FX_DEBUG`, then add a device-thread resource drain strategy and generic host integration. Run native test suite only after functional completion as requested.


## 2026-11-19 — Stage 63: deferred dedicated GPU emitter destruction and unhealthy-device routing
- **Branch:** `feature/spectrafx-generic-cue-queue`, latest verified ancestor main `d71c6d47e532b25fb816a8bb1f96c41e5275a165`; 0 behind when continuation began.
- **Implementation commits:** `183c925194c6738a5eb627170d9db42c9f40696d` (`engine/fx/SpectraFxExecutionPolicy.h`) and `de9ecc030721f922aae838c46f54dfbbd35762bd` (`engine/fx/SpectraFxHybridPlayback.h`).
- **Policy:** `chooseSpectraFxBackend` refuses a new GPU emitter on an unhealthy `gfx::Device::gpuHealthy()==false`; existing authored CPU fallback policy applies, preserving the strict-GPU refusal when fallback is disabled. This prevents repeated GPU initialization on a known failed device.
- **Dedicated resource lifecycle:** keyed Stop/Kill, one-shot deadline, and keyed replacement now move a GPU-owned `HybridEmitter` to `retiringDedicated_` rather than destroying its GPU buffers/texture immediately. CPU-owned effects still stop directly. Dedicated GPU retirement uses the same conservative Metal frame-serial 3-slot rule as the Stage-62 pool resource queue; backend with unknown serial keeps GPU resources until `reset`. `retiringGpuSlots_` includes both retired dedicated and pooled capacities, and `nominalGpuBytes` includes both retired types. `Stats::dedicatedGpuRetired/dedicatedGpuReleased` reflect scheduled and completed reclamation. The **visible effect stops immediately** by removing it from active playback, even though GPU resources remain alive for in-flight command safety.
- **Owner lifetime:** cleanup is render-thread-only and never performed from a render pass. The playback destructor calls reset on all active/retiring GPU objects before destroying shared Metal and evolution compute programs. Neither GPU buffer completion nor Metal driver behavior has been tested. Actual safe resource reuse relies on the Metal backend's existing semaphore and frameSerial contract. Unknown Vulkan serial implies conservative retention until reset, potentially exhausting the per-playback GPU capacity; CPU fallback prevents unbounded extra GPU allocation.
- **Known limitation:** the internal `HybridEmitter::switchToCpuFallback` transition still destroys its GPU resources at the update safe point; it does not hand those resources to the playback retirement queue. A broader per-emitter transition object is needed to defer that specific pathway. RHI device teardown while GPU commands are still active must be ordered by the host. Do not claim fully GPU-fence-safe without validation.
- **Actual tests:** **NOT RUN** (explicit feature-first user requirement). No C++ build, CTest, Metal/Vulkan GPU frame, deferred-resource race test, fault injection, device lifecycle/ASan/CI validation.
- **Next:** guard pooled Move/Stop/Kill during failed-pool state, handle new Play while recovery is pending, expose counters and update native runbook. Then consider transferring GPU-to-CPU fallback resources to a retirement owner before the final acceptance test phase.


## 2026-11-19 — Stage 64: fault-safe pooled cue semantics and visible GPU resource diagnostics
- **Branch:** `feature/spectrafx-generic-cue-queue`; main merged at least through `d71c6d47e532b25fb816a8bb1f96c41e5275a165`.
- **Code commits:** `1d01b4b7c5c1be89bbf435c80cecbeb7584ee764`, `44952b75d2babf3ecf075d2951d7a2b8394d9b04` (`engine/fx/SpectraFxHybridPlayback.h`), `677c0d906bd27aa2146977db64ebad17a567c7e1` (`apps/client/ClientFx.cpp`).
- **Behavior:** when a pooled GPU group is marked `failurePending` or `gfx::Device::gpuHealthy()` is false, `dispatch` no longer issues GPU writes for Move/Stop/Kill. Move validates finite pose then only records it in the fallback metadata; the later CPU restart inherits this latest pose. Stop/Kill remove the source immediately so pool failure recovery does not resurrect a stopped effect. New Play for a failed named group is prevented from rejoining the known-bad arena; it switches to the authored CPU fallback when permitted and rejects strict GPU requests without fallback. `tryStartPooled` rejects pending-failure groups. The route schema, engine authoring API, nonauthoritative nature and 64-source capacity stay unchanged.
- **Diagnostics:** `retiringGpuSlots()` and `retiringGpuResourceCount()` expose the capacities and count of GPU pool or dedicated emitter resources waiting for a completed GPU frame. Native `AIVERSE_FX_DEBUG` now prints `[spectrafx-gpu-lifetime]`: `pooledCpuRecoveries`, drops, render faults, pending/reclaimed pool resources, pending/reclaimed dedicated resources and retained slot counts. Such retained slots continue to count toward the per-playback 65536 GPU-slot budget; the stats are per playback object, not global driver allocation.
- **Safety limits:** render-pass errors are only flagged there; CPU recovery and GPU retirement are deferred to subsequent outside-pass updates. Metal's frameSerial + three in-flight frame slots determine reclamation; Vulkan/unknown frame serial retains old resources until scene reset. This is conservative source logic, not proof of native GPU fence behavior. GPU failures are presentation-only and do not alter ECS/network/gameplay.
- **Actual verification:** **NOT RUN**, per user's features-first instruction. No C++ compiler, CTest, macOS Metal GPU, Vulkan, editor, stress/fault injection, CI, RHI lifetime checks, visual scene checks or performance timings. Handoff is code/documentation only.
- **Next:** harden the GPU-to-CPU dedicated transition's resource retirement, improve RHI callback lifetime handling, and inspect actual native host integration for any other unresolved GPU issue before moving to the deferred complete test phase.


## 2026-11-19 — Stage 65: deferred GPU memory after dedicated GPU-to-CPU failover
- **Branch:** `feature/spectrafx-generic-cue-queue`, main merged through `d71c6d47e532b25fb816a8bb1f96c41e5275a165` at start.
- **Code SHAs:** `c8bb691b8010be362d0c01b63ec1562200ee8a39` (`engine/fx/SpectraFxHybridEmitter.h`), `622b88e0bdf4ba5a328f0ad618bb9259ac56eb1d` (`engine/fx/SpectraFxHybridPlayback.h`).
- **Behavior:** the existing dedicated `switchToCpuFallback(reason)` now starts the authored CPU effect and immediately cancels pending GPU feedback callbacks but **retains GPU buffers/compaction/sort scratch and local pipelines** rather than destroying them while an earlier GPU command may still be in flight. It records `deferredGpuSince` using the RHI frame serial, clears GPU execution ownership (so no GPU draw/update takes place), and keeps GPU memory included in the runtime budget until safe retirement. `HybridEmitter::collectDeferredGpuRelease()` is called by the playback on every subsequent active/one-shot frame and frees the GPU resources only outside render/compute encoders and after at least three further Metal frameSerial values. When the RHI lacks a reliable nonzero serial, retained memory remains until explicit host reset.
- **Stop/Kill:** `HybridEmitter::gpuResourcesHeld()` reports whether GPU state remains allocated even if `backend()==Cpu`. `HybridPlayback::retireDedicatedEmitter` now queues such former GPU emitters for deferred retirement; it uses `stopCpuOnly` to stop the CPU fallback effect immediately without forcing premature GPU state destruction. The `usedGpuSlots()` budget includes all such retained GPU allocations in addition to active and retiring pools. `nominalGpuBytes()` likewise reports GPU payloads held after CPU transition. `Stats::gpuDeferredReleaseCompleted` is a completed resource-retirement counter, not the number of GPU/CPU recoveries.
- **Public API changes:** new `HybridEmitter::gpuResourcesHeld/hasDeferredGpuRelease/deferredGpuSince/collectDeferredGpuRelease/stopCpuOnly/deferredGpuReleases` for engine-host lifetime management. The CPU fallback's old particle age/position data is not transferred; effects restart at the last known emitter origin/direction and remain presentation-only. The renderer must still own `gfx::Device` until all active/retained GPU resources are discarded.
- **Remaining limitation:** `reset()`/host device shutdown explicitly tears down all resources regardless of a completed frame serial; host must stop rendering and follow RHI shutdown synchronization semantics. Metal frame-serial completion assumption depends on the backend's three-slot semaphore; Vulkan returns zero and retains allocations until reset. GPU resource destruction during a GPU/CPU transition is no longer immediate during normal frame updates, but this remains **unverified** until native acceptance.
- **Tests actually run:** **NOT RUN** per user's request to finish features first. No compiler, sanitizer, CTest, native editor/client, Metal/Vulkan GPU run, RHI fault simulation or performance/memory evidence. GitHub commits reflect source changes only.
- **Next:** expose the remaining fallback/retirement diagnostics in the native debug log and implement a genre-independent scene playback adapter that can be reused in FPS, RTS and other custom native game projects, before the deferred comprehensive testing phase.


## 2026-11-19 — Stage 66: genre-independent native scene FX host
- **Branch:** `feature/spectrafx-generic-cue-queue`, last main comparison 0 behind `d71c6d47e532b25fb816a8bb1f96c41e5275a165`.
- **Code SHA:** `80b52f6ffa18628b5c86d21fae5e01f15cdfeeda`, new `engine/fx/SpectraFxSceneHost.h`.
- **API:** `SpectraFxSceneHost::init(device, FileReader,textureBase)`, `enabled/lastError/dispatch/resetScene/prepareGpu/prepareCpu/render/shutdown/playback`. This is a reusable **genre-neutral** opt-in wrapper: project `fx/gpu/routes.json` provides routes, GPU frame budget and authored GPU emitter asset lookup; a missing file creates **no** new VFX resources. If routes are valid and nonempty, the host constructs its own existing CPU `ParticleSystem` with project file/texture/mesh loaders, the existing `SpectraFxGpuEmitterLibrary` and `SpectraFxHybridPlayback`; it owns these presentation resources but never an RHI device, gameplay ECS, authoritative Simulation or net replication. It accepts arbitrary `SpectraFxCue`s supplied by the client instead of hardcoding FPS/RTS events into the shared engine.
- **Frame ordering:** outside render passes after `Device::beginFrame`, call `prepareGpu(dt,camera)`; after `Renderer3D::beginScene`, call `prepareCpu(dt,camera,viewProj,w,h)` once; after `Renderer3D::render` while the main scene pass is open, call `render(lighting,viewProj,camera)`; call `shutdown` before RHI Device destruction. `resetScene` drops presentation effects and outstanding event keys on rollback/new world/floating-origin shift without mutating authoritative state.
- **Fallback and limits:** only opt-in routes enable the host, GPU policy/CPU fallback remains in existing HybridPlayback. The host is a reusable integration surface rather than a genre-specific event translator. Optional CPU mesh/texture fallback uses existing render loader infrastructure; asset paths remain project owned.
- **Actual validation:** **NOT RUN**, per user's features-first directive. No compile, CTest, Metal/Vulkan, FPS client launch, visuals, quality/performance benchmarks or other native tests; implementation is source only.
- **Next:** integrate the generic host into the native FPS client at correct Device/Renderer3D frame boundaries and emit a bounded presentation-only local shot cue. Preserve floating-origin reset and link aiverse_fx explicitly in FPS CMake without changing server/Simulation modules. Then write a separate handoff.


## 2026-11-19 — Stage 67: opt-in SpectraFX runtime in actual FPS reference client
- **Branch:** `feature/spectrafx-generic-cue-queue`, main ancestor `d71c6d47e532b25fb816a8bb1f96c41e5275a165` (0 behind at continuation start).
- **Code commits:** `869a0fc9994491e314e00724332b2556faae6cc5` (`apps/fps/client.cpp`), `f8b7a6e1aecb98bf343b5a5683fd5c1490a33c20` (`modules/fps/CMakeLists.txt`), `641f502aad0a72ccefa53585cb16b7daf814cd20` (FPS startup log escaping fix).
- **Native FPS integration:** The actual FPS reference `App` owns a `fx::SpectraFxSceneHost` and initializes it with a project-scoped file reader and texture root. No project `fx/gpu/routes.json` => the host stays disabled and creates no GPU/CPU VFX resources. The FPS presentation adapter dispatches cue **eventType/id 1** on newly observed LOCAL predicted actor weapon-fire count, capped at four FX requests per frame. Its position and direction are derived from the local FP camera in **world-cell-relative render coordinates**; it does not publish a network command or mutate the authoritative FPS Simulation. Other FPS gameplay events (remote fire, explosions, damage) are not implicitly mapped. Games must explicitly author a route for eventType 1; do not treat it as a universal `SimEvent` type. On a floating-origin `world::Cell` change, `fxScene.resetScene()` drops the old presentation particles so they cannot be drawn in the new coordinate basis.
- **Render pass ordering:** after `Device::beginFrame` and before `Renderer3D::beginScene`, call `fxScene.prepareGpu(dt,camera)`; after renderer scene setup, call `prepareCpu(dt,camera,viewProj,w,h)`; after `Renderer3D::render(pass)` while the scene pass is still open, draw `fxScene.render(lighting,viewProj,camera)`. FPS `App::shutdown` tears down SpectraFX before the renderer/device, matching borrowed-RHI resource lifetime contracts.
- **Build:** `aiverse_fps_client` now explicitly links `aiverse_fx`. No server, authoritative FPS module, network wire or gameplay save change. This is the first actual cross-genre native host integration (the existing RTS client uses ClientFx and the new FPS client uses generic SceneHost).
- **Actual tests:** **NOT RUN**, per user's feature-first instruction. No CMake configure/build, FPS client launch, hosted editor Game tab, CTest, Metal/Vulkan shader check, screenshots or performance measurement; code committed but not verified. Specific risks: mesh particle fallback dependencies and local FPS world-cell origin changes need native tests.
- **Next:** document project opt-in FPS example for `fx/gpu/routes.json` and `fx/gpu/<name>.gpu.json`, record how eventType 1 is local weapon fire, then complete any further input/route controls or GPU callbacks before final native acceptance.


## 2026-11-19 — Stage 68: project opt-in FPS weapon-fire authoring guide
- **Branch:** `feature/spectrafx-generic-cue-queue`; main merged through `d71c6d47e532b25fb816a8bb1f96c41e5275a165`.
- **Documentation commit:** `11515489bdcaebdbe37a83a9cab00bc8378d0ce2`, file `docs/spectrafx-gpu-runtime.md`.
- **Content:** explains FPS client's newly integrated genre-neutral `SpectraFxSceneHost` and its opt-in `fx/gpu/routes.json` file. FPS presentation route `eventType:1` is LOCAL weapon fire, *not* a universal `SimEvent` ID or network-authoritative weapon event. Demonstrates FPS `fx/gpu/fps_muzzle.gpu.json` source settings for a short, additive muzzle flash with burst particles and optional CPU fallback. Explains the requirement to author/reuse a real `fx/muzzle_flash.json` effect; none is installed or invented by the integration. Clarifies visual event cap of four local shot cues/frame, no remote shot cues, project opt-in disabled by default, world-cell-relative FX positions, reset on a floating-origin shift and exact game render pass order. The existing FPS server, net protocol, weapon damage, RTS effects and PRISMA are untouched.
- **Actual tests:** **NOT RUN** by user request. Neither C++ build/link of aiverse_fps_client, project file reading, asset validation fixtures, native Metal/Vulkan GPU rendering, FPS screenshots, hosting, UI/Editor or CTest has been performed.
- **Next:** consider remaining cross-genre event-to-cue authoring and robust GPU/CPU renderer lifecycle failure modes, then run comprehensive native build/test/visual acceptance after feature completion. Check the FPS target link graph (new `aiverse_fx`) when testing and fix warnings/errors before merge.


## 2026-11-19 — Stage 69: deferred-safe live scene/floating-origin reset
- **Branch:** `feature/spectrafx-generic-cue-queue`, latest main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` was merged at stage start.
- **Code SHAs:** `8e3d9da801d026f2d050ddf10a1352255666ab6f` (`engine/fx/SpectraFxHybridPlayback.h`) and `435a6ab966b96582705cd704bb993e0e3f245be3` (`engine/fx/SpectraFxSceneHost.h`). Project guide `0345ff725856649fa92b9dd7cfe6cbbe90e2b5c0` (`docs/spectrafx-gpu-runtime.md`).
- **Architecture:** `SpectraFxHybridPlayback::resetPresentation() -> bool` is the live-frame-safe alternative to the teardown-only `reset()`. It first checks `Device::canUpdateSharedBuffers()`, returning false without changing state if called during an open compute/render encoder. Outside passes it removes all currently attached/one-shot keyed sources and visible particle effects, sends GPU dedicated emitters and all homogeneous GPU pools through the Stage-62/63 deferred retirement queues, preserves cached project routes/shared shaders and keeps retired capacities charged to `usedGpuSlots()` until completion. CPU effects can be stopped immediately. The separate `reset()` remains for complete host/device shutdown when rendering has ceased.
- **Host:** `SpectraFxSceneHost::resetScene() -> bool` now uses the safe `resetPresentation`, clearing its own CPU particle instances only if the RHI is at a safe point. This is used by the integrated FPS client for world-cell-relative floating-origin changes, preventing immediate destruction of Metal resources potentially still referenced by earlier frames. The returned bool lets other host integrations defer a reset requested inside a pass.
- **Limits:** `frameSerial()`-based Metal completion remains a three-slot heuristic grounded in current RHI semaphore behavior and **not native verified**. Unknown backend completion serial retains resources until shutdown; GPU resource pressure may select the already-authored CPU fallback. Host still must terminate render submission before teardown. No ECS/network gameplay state changed. This does not provide a global cross-genre ECS event bridge or GPU fence API.
- **Tests actually run:** **NOT RUN**, by user's instruction to finish features first. No native C++ build, CTest, FPS/RTS GameClient execution, GPU shader validator, hardware Metal/Vulkan frame, screenshots or fault-injection/lifetime testing.
- **Next:** ensure every native host uses safe outside-pass live reset, finish optional multi-genre event sources/quality hooks, and then run a comprehensive validation phase (CMake configure, warning-clean compilation, focused SpectraFX CTests, Metal GPU acceptance, Vulkan fallback, FPS/RTS scene screenshots and resource-lifetime regressions).


## 2026-11-19 — Stage 70: bounded genre-independent presentation cue inbox
- **Branch:** `feature/spectrafx-generic-cue-queue`, at start `main` `d71c6d47e532b25fb816a8bb1f96c41e5275a165` was already merged (0 behind, 233 ahead).
- **Code commits:** `0a16758430fe159eb7e17cc4dea42c2476703b0b` (new `engine/fx/SpectraFxCueInbox.h`), `378d831525cefaf07cdbbb0dc954ed97ab4fd28d` (`engine/fx/SpectraFxSceneHost.h`), `b4fbd51b16d903f361eedb8d0355d2c019275c5c` (`apps/fps/client.cpp`).
- **Feature:** `SpectraFxCueInbox` is a genre/graphics/ECS-independent, bounded FIFO inbox for presentation `SpectraFxCue`: default 1024 pending cues, 1..65536 capacity, and `submit(cue,visibleToViewer,expectedEpoch)`, `translate(gameEvents,translator,epoch)`, `drain(callback,maxCues)`, `reset/epoch/stats`. It rejects stale scene epochs, invisible-to-viewer cues, nonfinite poses/directions/tints/scales, and unkeyed Move/Stop/Kill. Repeated keyed Move events coalesce until a Play/Stop/Kill barrier; when full, Stop/Kill may evict an older cosmetic Play/Move before dropping a cleanup command. Drain snapshots the initial queue length (bounded to max 256 by default in SceneHost); callback-created events run later. Stats count submitted/accepted/dropped/hidden/stale/invalid/evicted/coalesced/drained/failed dispatches; queue is deliberately **not thread safe**. Games merge producer streams and apply fog/team visibility filters before calling submit.
- **Native host integration:** `SpectraFxSceneHost` adds `cueEpoch/cueStats/pendingCues/enqueueCue/translateCues/setMaxCuesPerFrame`. `prepareGpu` drains queued cues through existing project `HybridPlayback` on the RHI owner thread before GPU evolution, not from simulation callbacks; `resetScene` and `shutdown` invalidate pending cues and change the epoch. The original `dispatch` immediate API remains for advanced callers already at a device-safe point. FPS reference `apps/fps/client.cpp` now enqueues (instead of immediately dispatching) local-fire cue ID 1 and passes the current scene epoch. No FPS networking, damage, physics, RTS or authoritative ECS change.
- **Resource/visibility contracts:** Queued events are transient presentation data only; content visibility is explicit from the host, not inferred by the queue. A scene reset that fails the safe-point guard does not clear the CPU particle state or advance the queue epoch. A valid scene reset discards the old queue before another GPU frame.
- **Verification actually executed:** **NOT RUN** as instructed (functional features first). No C++ compilation, CTest, native FPS client, Metal/Vulkan render, queue concurrency or fuzz tests, CI/performance. GitHub accepted source commits, not evidence of native operation.
- **Next:** implement a common CPU/GPU quality policy in `SpectraFxSceneHost` and playback (without overriding effect-specific authored settings unnecessarily); document quality behaviors and extend game-host usage, then finish features before native acceptance.


## 2026-11-19 — Stage 71: unified native CPU/GPU FX quality cap and authored scene tier
- **Branch:** `feature/spectrafx-generic-cue-queue`; main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` was ancestor (0 behind at continuation start).
- **Implementation commits:** `587e793c18467f7e4396cc2e4b93b1d63538a78a` (`engine/fx/SpectraFxHybridEmitter.h`), `07bf90a4db3116e215f65c93f25bf5a1cafbb819` (`engine/fx/SpectraFxGpuSharedArena.h`), `d4db4c110ca721c80ed052a07a1581628c1521b3` (`engine/fx/SpectraFxHybridPlayback.h`), `bd4fd32156b5a5a032fc8b5a9e292929e60dafb4` (`engine/fx/SpectraFxSceneHost.h`), `b313d4bcfb02f34ef2f92c1275dc996c74c8cd8a` (`engine/fx/SpectraFxGpuRoutes.h`), `7651a83ff3704c133d6457a399baa3493328d60e` (generic host authoring) and `afa7e7b40731452be8ca576f0d01f2e750e9ee5c` (RTS native client).
- **Public quality API:** `HybridEmitter::setGlobalQuality`, `GpuSharedArena::setGlobalQuality`, `HybridPlayback::setGlobalQuality`, and `SceneHost::setQuality`. Quality levels are existing `FxQuality::Off/Low/Medium/High`; GPU spawn scale uses `min(authoredEmitterQuality,globalQuality)` so the scene can lower but never increase the author's per-effect limit. Quality changes do **not** change already emitted GPU particle ages, velocities or gameplay state. Shared arenas and newly initialized/dedicated emitters inherit the current global quality; `SceneHost` synchronizes it with the existing CPU `ParticleSystem::setQuality`. Direct HybridPlayback callers must continue to set quality on their shared CPU ParticleSystem themselves, since the playback does not own that CPU object.
- **Project schema:** optional root `"sceneQuality":"off|low|medium|high"` in `fx/gpu/routes.json`. The route parser's optional fifth out-argument `std::optional<FxQuality>*` distinguishes a missing key (no override) from a deliberately authored quality tier; unknown strings and incorrect types are rejected without mutating the caller's previous route/quality results. Generic `SpectraFxSceneHost` applies the authored quality on successful load, and the real RTS `ClientFx` applies the setting only when a configured route file is present (leaves historical CPU quality alone otherwise).
- **Scope:** generic presentation quality only, no FPS/RTS gameplay or persistent ECS components, no PRISMA postprocessing changes. Existing per-effect GPU quality, LOD and adaptive GPU budget governor multiply with this scene cap. `FxQuality::Off` prevents new GPU spawns while old GPU particles age toward expiration.
- **Actual verification:** **NOT RUN**, explicit features-first request. No C++ compile/CTest, native scene/editor/FPS or RTS startup, Metal/Vulkan shader check, screenshots or GPU benchmark. Source commits + handoff only.
- **Next:** update user-facing project routes docs and then focus the remaining cross-frame RHI resource/callback safety and FPS floating-origin edge cases. Keep per-stage handoff and run a dedicated native validation after feature-complete.


## 2026-11-19 — Stage 72: FPS floating-origin reset guard and project-facing event/quality guide
- **Branch:** `feature/spectrafx-generic-cue-queue`; last confirmed `main` ancestor `d71c6d47e532b25fb816a8bb1f96c41e5275a165`.
- **Code commits:** `d751f45078e2a4c7638394206314ceb434fd1f5a` (`apps/fps/client.cpp`), `5c617155ef5e50d595d098b591c2d5118e4db5e4` (`engine/fx/SpectraFxCueInbox.h`: explicit `<utility>` include), `c628740685cd508e97bcbfd294fd22f3c4c5bba1` (`engine/fx/SpectraFxSceneHost.h`: explicit `<optional>` include), `3bd12e53666319463a8bb94efaffd05a417db7f1` (`apps/client/ClientFx.cpp`: explicit `<optional>` include).
- **FPS bug fixed in source:** the FPS reference client previously called `fxScene.resetScene()` on a floating-origin `world::Cell` transition but ignored its `bool` safe-point failure result. It could then enqueue and draw effects in a new relative coordinate basis while old particles remained alive in a previous origin. Now it tracks `fxOriginReady`; on failed reset it does not advance the stored last-known origin, and suppresses GPU/CPU FX frame preparation, enqueue and FX render until a later frame successfully resets. The retry occurs before opening the scene pass. Non-FX gameplay, networking and world rendering continue as before.
- **User documentation commit:** `0661cf09e8386afa39565cd0e619bb48fc2d131f`, `docs/spectrafx-gpu-runtime.md` now explains the genre-neutral bounded cue inbox, required host visibility filter, scene-epoch reset protection, FIFO draining and Move coalescing, project route `sceneQuality`, CPU and GPU runtime quality synchronization, and safe FPS floating-origin reset.
- **Semantics and known boundaries:** the inbox is not MPSC/thread-safe or an ECS event bus; producers merge events on the host thread and supply visibility explicitly. Quality caps presentation spawn intensity and does not modify gameplay authority or already-simulated particles. Source-only FPS reset behavior requires testing with floating-origin movement under real Metal render submission and GPU latency before acceptance.
- **Actual verification:** **NOT RUN**, following the user's explicit functions-first policy. No native C++ build, CTest, FPS game execution, editor, MSL shader compile, Metal/Vulkan GPU frame, CI, benchmark or stress test; these are source commits and implementation descriptions, not evidence of runtime correctness.
- **Next:** continue closure of remaining genre-neutral feature requirements, then carry out the deferred full compile/test/warning-clean/GPU acceptance phase before considering merge to main. Handoff must be updated per stage.


## 2026-11-19 — Stage 73: structured SpectraFX GPU emitter and project route quick settings
- **Branch:** `feature/spectrafx-generic-cue-queue`. Latest branch compare at start: 251 commits ahead of main `d71c6d47e532b25fb816a8bb1f96c41e5275a165` and 0 behind.
- **Implementation commit:** `06913c7f9c3e687323515e418d73233128a475d9`; modified `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp`.
- **Editor functionality:** the existing dockable `SpectraFX GPU Assets` window now has validated ImGui **Quick settings** above the raw JSON editor. On `routes.json` the author can change scene-quality override (including removing the override), whole-GPU-frame budget ms and minimum GPU spawn multiplier; the older per-event route list remains fully editable in JSON. On individual `*.gpu.json` definitions the author can change CPU/Auto/GPU execution preference, emitter quality, allocated GPU particle capacity, CPU fallback permission, Metal indirect compaction, Metal sorted-alpha opt-in and unsorted-alpha override. Each UI action modifies a parsed copy and re-validates the entire document via the same `SpectraFxGpuRouteAsset::read` or `SpectraFxGpuEmitterAsset::read` parser used at runtime **before** updating the draft. Invalid combinations (e.g., sorted alpha together with indirect compaction or additive) leave existing text and unsaved edits intact, reporting the parser error. JSON remains canonical and arbitrary advanced fields are retained through serialization.
- **Scope/limitations:** this is an authoring UI convenience, not a live GPU preview or a claim that Metal sorted alpha works. It does not replace the editor's existing graph editor or PRISMA. Scene/project route changes still require client restart. The new controls do not directly edit routes in a list view or expose every GPU emitter module; the raw JSON remains available. `file(GLOB AIVERSE_EDITOR_WINDOWS CONFIGURE_DEPENDS ...)` in root CMake automatically discovers this .cpp when configuring the editor.
- **Actual validation:** **NOT RUN** by user instruction. No native C++ compilation, CTest, editor startup, ImGui click interaction, JSON roundtrip, Metal/Vulkan GPU, CI or screenshots. A GitHub source commit is not compilation evidence.
- **Next:** implement safe cross-platform editor asset replacement with verified backup/rollback and avoid overwriting another process's edits, then update a separate agent handoff. After feature completion, do dedicated native editor/FX acceptance and warning fixes.


## 2026-11-19 — Stage 74: transactional editor asset saves with disk conflict detection
- **Branch:** `feature/spectrafx-generic-cue-queue`. Last checked base main `d71c6d47e532b25fb816a8bb1f96c41e5275a165`; zero behind at stage start.
- **Implementation commit:** `dcde0e7d39c766378deefd180045f540262b6992`. Modified `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp` (save path).
- **Behavior:** the editor now compares the existing on-disk GPU emitter file or `fx/gpu/routes.json` byte-for-byte with the exact contents previously loaded/saved (`saved_`). If the asset was changed externally or the editor opened a brand-new draft that collides with a file created later, saving is refused with an explicit reload instruction rather than overwriting an agent/other editor's changes. After complete validation, content is written to `*.tmp` and flushed before touching the existing asset. An existing target is first moved to `*.bak`, then the temporary file is moved to the target, supporting platforms where `std::filesystem::rename` cannot overwrite an existing destination (notably Windows). If the final rename fails, the code tries to restore the backup and reports the backup path on rollback failure. A successful save retains the prior original as `*.bak`, updates the tracked `saved_` bytes, clears the dirty flag and reloads the editor FX authoring cache. Failed file IO leaves unsaved JSON in the editor.
- **Known limits:** not a full directory fsync/durable atomic database transaction; simultaneous external edits in the tiny window after the compare can still race, and the `*.bak` is a single rotating backup rather than version history. File-system symlinks and read-only project paths are governed by the host OS. GPU routes are still loaded by an already-running game only at project startup; saving does not hot-reload active GPU resources.
- **Actual verification performed:** **NOT RUN** by explicit user request (all functionality first). No C++ compile, native editor UI, cross-platform file-IO tests, disk error injection, Windows rename, CTest/CI or GPU check. No source-only change may be presented as verified production behavior.
- **Next:** tighten RHI callback and GPU resource retirement guards, then review full SpectraFX editor and game host code for remaining feature gaps before scheduled native validation; maintain handoff after each stage.


## 2026-11-19 — Stage 75: validated visual authoring for GPU event routes
- **Branch:** `feature/spectrafx-generic-cue-queue`; latest main comparison at stage start: `d71c6d47e532b25fb816a8bb1f96c41e5275a165`, behind 0.
- **Implementation commit:** `ec732c93ce0097d4fb490588f6f1fefa6c2393d2`; changed `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp`.
- **Editor functionality:** `SpectraFX GPU Assets` now has a visual event-route list when `routes.json` is selected. Editors can add a route with a nonconflicting eventType, choose an existing named GPU emitter asset from a dropdown, change numeric event ID, toggle enabled, edit one-shot duration or remove the route. The list shows a warning if a referenced asset is not present in this project's `fx/gpu/*.gpu.json`, and displays existing `sharedPool` and capacity when authored (advanced pool fields remain directly editable in JSON). Every change uses `applyDraft(Json)`: the entire proposed `routes.json` goes through the strict `SpectraFxGpuRouteAsset::read` parser, including unique IDs, pool capacities and scene budget fields, before the editor's text and dirty state change. Neither gameplay nor a running client's GPU routes are modified automatically; saving still requires explicit click, and game restart remains necessary to load new route mappings.
- **Limits:** the visual controls do not yet support manipulating every route JSON field (`ownPlayerOnly` is deliberately fixed true in the current RTS visibility guard; shared pool names/capacities editable via raw JSON). A route that refers to a not-yet-created GPU asset can still be saved; the native runtime will reject its Play until that asset exists. FPS and other genres supply their own cue semantics; numeric IDs are not globally standardized.
- **Tests actually run:** **NOT RUN** (features first). No compilation, editor/ImGui tests, JSON roundtrip, CTest, CI, Metal/Vulkan or end-to-end native client validation. Source code and GitHub commits only.
- **Next:** address remaining GPU storage/transfer consistency hazards, especially Vulkan's host-visible storage buffer versioning for GPU-written particle state; avoid claiming Vulkan GPU correctness before actual device validation. Preserve the per-stage agent handoff.


## 2026-11-19 — Stage 76: explicit Metal-only GPU execution gate until Vulkan RHI supports compute
- **Branch:** `feature/spectrafx-generic-cue-queue`, compared against main `d71c6d47e532b25fb816a8bb1f96c41e5275a165`: 0 behind at stage start.
- **Implementation commit:** `85e080e813bbb69078d1caa2194c3a30b57a39b6` — `engine/fx/SpectraFxExecutionPolicy.h`. **Documentation commit:** `bc89bbdbfa13d832b50fb3084cdca1d6519e6519` — `docs/spectrafx-gpu-runtime.md`.
- **Source inspection findings (not runtime tests):** AIVERSE `engine/gfx/vulkan/VkDevice.h`/`VkDraw.cpp` do not override `gfx::Device::supportsCompute` or any compute entry point, so the current Vulkan RHI inherits the default unsupported compute API. Vulkan `VkResources.cpp` also uses separate CPU host-visible dynamic storage buffer versions, which would require further transfer/compute synchronization and persistent GPU-written state validation before new GPU particle simulation can be declared safe. Metal implements the current RHI compute and GPU-authored indirect APIs. To prevent a future Vulkan compute implementation from accidentally enabling this unverified GPU FX path, `chooseSpectraFxBackend` explicitly refuses Vulkan GPU evolution before the generic supportsCompute check, selecting the authored CPU effect if fallback is allowed; strict GPU preference with `allowCpuFallback=false` remains rejected. Metal policy and authored effect JSON are unchanged. OpenGL/WebGL remain on CPU.
- **Why it matters:** earlier user documentation broadly described Metal/Vulkan GPU paths, which overstated the currently executable backend. The project guide now accurately calls Metal the only authored GPU-particle path and Vulkan a CPU fallback. Enabling Vulkan in the future requires real compute API, GPU-writable storage correctness across frames, compute-to-render barriers and RHI acceptance tests; no unsafe Vulkan override was added here.
- **Actual validation:** **NOT RUN** (functions first per user). Only GitHub source inspection and code commits; no CMake build, CTest, graphics backend initialization, MSL/Vulkan shaders, native editor or macOS hardware rendering was executed. In particular, Metal FX remains experimental/unverified.
- **Next:** complete remaining editor authoring affordances and GPU resource/fault lifetime ownership, then run the deferred native C++/Metal acceptance pass and fix errors/warnings before merging. Keep handoff after every stage.
