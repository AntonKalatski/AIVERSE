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
