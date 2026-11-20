# SpectraFX Stage 94 — genre-neutral camera/view input (2026-11-19)

**Branch:** `feature/spectrafx-generic-cue-queue`.
**Implementation commits:** `82970d7aa99490a0ff6bbcb07a2f2989e43f2a7c`, `3be028674abc221cc391d58f41f34802cf07b050`.
**Ownership:** SpectraFX is an engine presentation subsystem. No FPS/RTS/editor camera, game-rule or scene dependency is permitted in `engine/fx`.

## What changed

- New `engine/fx/SpectraFxView.h`: camera-independent view packet with world eye, forward/right/up, combined view-projection, Y projection scale, viewport size and validation. Native engines can fill it directly from any camera model; `fromCamera` is an optional template adapter for legacy cameras exposing `eye/view/proj`.
- `engine/fx/ParticleSystem.h/.cpp`: primary `setView(const SpectraFxView&)` now reads only view data, including camera-independent projection scale and transform into particle-local space. A template preserves existing camera-shaped `setView(camera,viewProj,w,h)` calls. The source no longer names the old `RTSCamera`.
- `engine/fx/SpectraFxSceneHost.h`: primary `prepareGpu(dt,view)`, `prepareCpu(dt,view)` and `render(lighting,view)`. Legacy camera-shaped overloads remain templates, not dependencies on specific classes. `render::Lighting` / Renderer3D remain generic rendering infrastructure.
- `tests/spectrafx_compile_smoke.cpp`: compile-only use of a fictional `CinemaCamera` outside any game module, plus direct `SpectraFxView` calls, exercises both API shapes. Marked helper `[[maybe_unused]]` to avoid warnings-as-errors.
- `tools/spectrafx/check_boundaries.py`, `CMakeLists.txt`: new Python-based `spectrafx_architecture` CTest with `SpectraFX;Architecture` labels; rejects explicit `RTSCamera` symbols and direct genre/Editor/client includes in SpectraFX and ParticleSystem source.
- `tools/spectrafx/acceptance.sh`: independent `spectrafx_core_build`, `architecture`, and `host_integration_build` stages. Core tests can execute after their build succeeds even if a host build fails; the complete acceptance still reports host/full-build failures honestly.

## Public API sketch

```cpp
aiverse::fx::SpectraFxView view;
view.eye = worldCameraPosition;
view.forward = worldCameraForward;
view.right = worldCameraRight;
view.up = worldCameraUp;
view.viewProj = viewProjection;
view.projectionYScale = projection.at(1, 1);
view.width = framebufferWidth;
view.height = framebufferHeight;

fxHost.prepareGpu(dt, view);    // safe point, outside render encoder
fxHost.prepareCpu(dt, view);    // after scene setup
fxHost.render(lighting, view);  // inside compatible scene pass
```

No game-genre-specific enum or class is needed. Existing native host code remains source-compatible through the template camera adapter. The `SpectraFxView` packet is presentation-only: its values never enter deterministic gameplay or save-state checksums.

## Actual checks performed

- GitHub connector fetched actual stage-93 source and the branch head; constructed and committed the modifications with exact old-text replacement assertions.
- Commit objects and branch ref writes succeeded on GitHub. A code inspection identified explicit RTS camera type references in `ParticleSystem` and `SpectraFxSceneHost`; those were removed in the committed source.
- **NOT RUN on this stage:** native Clang/C++ build, Python boundary script, `ctest`, Metal GPU shader/raster test, Editor/FPS/RTS render, full acceptance and screenshot/performance verification. There is no local Mac Studio execution access. A source change is not a native test PASS.
- Earlier user evidence is from older code/SHAs: 4/4 CPU SpectraFX CTests PASS, 1/1 actual M3 Ultra Metal compute PASS, host/header target builds PASS. Do not attribute these results to stage 94.
- Full GitHub run `44582216785` was last observed **queued** on an older commit, not evidence that this stage passed.

## Limitations and next action

1. Build the newest branch on macOS/Apple Silicon with one command `bash tools/spectrafx/acceptance.sh build/spectrafx-validation`; save `spectrafx-acceptance/summary.md`, complete logs, exact SHA and `spectrafx_architecture` CTest result. A clean/native app run is required before marking acceptance.
2. On any compiler failure, fix the offending generic camera conversion or host call and append a new handoff. Check CPU particle screen-size/culling/alpha sort parity and overhead/orthographic cameras against stage-93 behavior.
3. Execute real Metal billboard offscreen acceptance (still unverified), then integrated RHI scene pass, Editor preview, three-frame retirement and event callbacks. These are integration consumers, not core dependencies.
4. Do not merge `main` into the validated detached worktree or push this feature to `main` until divergent branches/conflicts and all regression tests are resolved.

**Git commands (pull and detached validation worktree):**

```bash
cd ~/Projects/AIVERSE-spectrafx-validation
git status --short
git fetch origin
git switch --detach origin/feature/spectrafx-generic-cue-queue
bash tools/spectrafx/acceptance.sh build/spectrafx-validation
```

If the detached worktree has local changes, do not switch across them; save them or use a fresh worktree instead.
