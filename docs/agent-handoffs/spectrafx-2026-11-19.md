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
