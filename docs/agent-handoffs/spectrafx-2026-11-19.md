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
