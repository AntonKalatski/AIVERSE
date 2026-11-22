# PHYSIS next steps — CI, automatic integration, parallel update, streaming (2026-11-22)

**Branch:** `agent-physis-next` (worktree `/Volumes/KINGSTON/AIVERSE-builds/worktrees/agent-physis-next`),
based on `origin/main` 319807ffd, merged `origin/main` once (f4292033e). Not merged into main.

## Commits

| SHA | What |
|---|---|
| `196d5060a` | `PhysisAutoInstall.{h,cpp}` (Simulation::setup installs PHYSIS from map data), `PhysisNatureRuntime{,Config}.h`, aggregate CMake target `aiverse_physis_tests`, test `physis_nature_runtime` |
| `834614f6a` | Game client (`GameClient.{h,cpp}`, `Lobby.cpp`) drives the nature runtime; audio bank gets PHYSIS synth fallbacks |
| `0351c4251` | `PhysisParallel.h`, parallel atmosphere/surface/disaster passes, `workerThreads`, test `physis_parallel` |
| `505552b03` | `PhysisStreaming.h`, `PhysisChunkMask.h`, map `streaming`/`anchors`, test `physis_streaming`, new `.github/workflows/physis.yml` |
| `a443fae46` | docs/physis.md section, AGENTS.md bullet, skill note |

## Results actually run

- **macOS (user's Mac, AppleClang 17, Release, Ninja, build dir `/Volumes/KINGSTON/AIVERSE-builds/physis-next/mac`)**:
  `aiverse_physis_tests` builds; `ctest -R ^physis_` **39/39 PASS** (incl. editor/MCP tests);
  `aiverse_client`, `aiverse_headless` build.
  - Parallel bench 1024×768, storms+disasters, 10 ticks, 3 runs (load ≈24 from other builds):
    1 lane 161.6–162.5 ms/tick; ×1.92–1.95 (2), ×3.69–3.73 (4), ×5.33–5.40 (6), ×6.62–7.00 (8).
  - Streaming bench 1024×1024, 2 % of cells active, 2 runs: full serial 116–117 ms/tick,
    streamed serial 32 ms (×3.6 — remaining O(cells) scans dominate), streamed + auto lanes 6.3 ms (×18.4–18.6).
  - Headless, copy of `templates/skirmish` with a `physis` block (`/Volumes/KINGSTON/AIVERSE-builds/physis-next/proj`):
    match finishes (winner Blue, checksum 00e9d5adbe5f512c). The match differs from the template without the block
    (more entities change entity indices → different but deterministic AI play). Original template still finishes.
  - Client `AiverseGame --map 0 --spectate`, 300 frames, `AIVERSE_PHYSIS_DEBUG=1`: "[physis] nature presentation on",
    PHYSIS ticks advance in the running game; screenshot renders normally. No fire was ignited (nothing in data
    ignites), so flames/smoke/sound were not seen in the game.
- **Linux VM (g++ 11.4, Debug, aarch64, 4 cores, tests-only config)**: `ctest -R ^physis_` **35/35 PASS**;
  parallel ×3.54 on 4 lanes (512×384). Serial checksum after 30 ticks with the old vs new Physis.h/PhysisDisasters.h:
  equal (5884285504486908324). `PhysisNatureRuntime.h` compiles `-Wall -Wextra` (syntax-only).
- **CI**: GitHub API (anonymous, from the VM) showed `physis.yml` failing on main with ctest exit 8 on Ubuntu and
  macOS (run 44956413363) — the hand-written target list did not build `physis_nature_presentation`/`physis_nature_cues`.
  The main `ci.yml` "Build" step was failing on every platform at the time (run 44934455522, not PHYSIS-specific).
  The new workflow has **NOT run** (branch not pushed by me; no push rights from this session). Windows MSVC and the
  Emscripten/Node job are **unverified**.

## NOT done / limits

- Grass burn overlay is not in the game client: `WorldScene` does not expose a `GrassPatchRenderer`.
- No data-driven ignition (fire only starts from gameplay code / `ignite()`); a game needs a command or script hook.
- Streaming keeps all cells in memory (no disk paging); dormant chunks are frozen, not caught up.
- `aiversebuild` engine-module selection does not know PHYSIS yet (only CMake `AIVERSE_SIM_PHYSIS`).
- FPS host (`modules/fps`) has no `PhysisNatureRuntime` call yet; Studio editor play view not checked.
- Maps that already had a `physis` block now run it (previously inert at runtime): different checksums/replays.

## Next steps

1. Push the branch and read `physis.yml` results (Windows + Web), fix what they report.
2. Data ignition (`physis.ignitions` or a generic command) + client visual QA of fire/smoke/sound.
3. Call `PhysisNatureRuntime` from the FPS host and the editor play view; expose grass renderer from WorldScene.
4. Register a `physis` engine module in `aiversebuild`.
