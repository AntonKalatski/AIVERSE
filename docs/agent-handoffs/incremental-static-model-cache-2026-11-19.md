# Incremental static CPU model cache — Stage 1 handoff (2026-11-19)

## Branch / source of truth
- Repository: `chromealex/AIVERSE`.
- Work branch: `perf/incremental-static-model-cache`.
- Base: `main` at `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` (fetch again before merging).
- Implementation commits: `b04650f` (policy), `cee7820` (Renderer3D storage/API), `8031755` (lifetime), `48e7bdc` (drawModel integration), `fdd7fe6` (CPU policy tests), `9fe30fb` (Scene stats), `0457e48` (editor invalidation), `acbde87` (geometry docs), `f7318ef` (AGENTS discoverability).
- This handoff is a separate commit after the implementation. Check current branch HEAD and concurrent agent branches before further changes.

## What is implemented
1. `engine/render/StaticModelCachePolicy.h`: genre-independent, conservative CPU predicate for stable `kDrawStatic` + nonzero `ModelDraw::persistentId`. No animated/posed/baked/skinned/morph, runtime-mutable hierarchy, active vertex displacement or cluster geometry is admitted for automatic reuse.
2. `engine/render/Renderer3D.h`, `Renderer3D.cpp`, `Renderer3DModels.cpp`: opt-in `AIVERSE_PERSISTENT_CPU_MODEL_CACHE=1` stores `StaticModelCache` by persistent entity ID. It reuses world-space bind transforms / precomputed primitive offsets / bounds if same model pointer, world matrix, GPU vertex/index handles and hierarchy sizes are observed. Rebuilds otherwise; old entries are evicted on periodic scene epochs. Existing explicit `ModelDraw::staticCache` has priority when compatible. `Renderer3D::invalidateStaticModelCaches()` resets storage, and `staticModelCacheStats()` exposes hits/rebuilds/declines/evictions/live.
3. `apps/editor/Editor.cpp`: invalidates before `reloadRules()` destroys the old view/model cache.
4. `apps/editor/windows/SceneWindow.cpp`: Scene → Batching diagnostics with current opt-in state and counters.
5. `tests/render/model_batching_policy.cpp`: assertion coverage for base eligibility, material override eligibility, changed world transform, missing persistent ID/flag, node transform override, runtime-mutable node, authored animation and cluster geometry.
6. `docs/geometry-performance.md`, `AGENTS.md`: configuration, boundaries, integration/invalidation contract.

## Exact scope / public API
- No changes to ECS state, game serialization, network protocol, save files or shader ABIs.
- No default change in graphics quality or draw mode. Metal ICB remains OFF unless independently enabled; this stage does not change GPU indirect commands or MDI thresholds.
- Pure CPU rigid transform reuse only. Per-frame `RtsView` and `Renderer3D::drawModel` traversal, material selection, shadow and camera culling, runtime LOD, sorting, instancing and submission are retained.
- `ModelDraw::materials` / `nodeMaterial` no longer prevent an explicit compatible transform cache when current GPU material selection has no active vertex offset. This is a targeted relaxation of an older conservative restriction, not a material batching change.
- `persistentId` uniqueness is caller-owned; each independent draw/attachment must use a distinct ID or stay transient.
- `Model` can be edited in place without changing pointer / GPU handles; these edits require explicit `invalidateStaticModelCaches()`. Native projects and other asset-reload paths must add invalidation before opting in. If that cannot be guaranteed, leave the flag OFF.

## Validation actually performed
- GitHub connector fetched the current main and target files; new branch was created from `9ee6ff6`.
- API/logic changes were written successfully to GitHub and branch comparison confirmed 8 changed files before AGENTS update (no branch divergence at that point). No source files of other workstreams were overwritten.
- Native CMake build: **NOT RUN**. Container cannot clone the repository: `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` failed `Could not resolve host: github.com`.
- `ctest`: **NOT RUN**, no compiled target in this container.
- macOS Metal / Vulkan / WebGL screenshot parity: **NOT RUN**.
- CPU wall time, GPU time, FPS or memory gain: **NOT MEASURED**. Earlier numbers from `docs/metal-batching-agent-handoff.md` are not results for this branch.
- CI: not yet observed for this PR as of handoff writing. Verify new PR checks directly; do not report queued/in-progress as passed.

## How to validate on owner's Apple Silicon machine
```sh
cd ~/Projects/AIVERSE
git fetch origin
git switch perf/incremental-static-model-cache
git pull --ff-only origin perf/incremental-static-model-cache
cmake --build build-metal -j8
ctest --test-dir build-metal -R '^model_batching_policy$' --output-on-failure

# Same scene/camera and other flags; compare fresh processes.
AIVERSE_MULTI_DRAW_INDIRECT=off AIVERSE_FRAME_TRACE=1 \
  ./build-metal/AiverseEditor.app/Contents/MacOS/AiverseEditor \
  2>&1 | tee /tmp/static-cache-off.log
AIVERSE_PERSISTENT_CPU_MODEL_CACHE=1 AIVERSE_MULTI_DRAW_INDIRECT=off \
  AIVERSE_FRAME_TRACE=1 \
  ./build-metal/AiverseEditor.app/Contents/MacOS/AiverseEditor \
  2>&1 | tee /tmp/static-cache-on.log
```
Open Scene → Batching → Incremental static CPU model cache. Confirm warm stationary-scene hits are nonzero. Compare `[scene-stages]` build/view/renderer, whole-editor CPU/GPU times (same workload), and drawn pixels. Validate camera movement, selection, transparent/outlined and shadowed objects, moving units, animated structures, terrain sculpt, road edits, rule/model reload, switching Scene/Game and closing/reopening projects. Instrument a deliberately static non-animated native model with material overrides. Then test same scene with the flag OFF again for an A/B/A comparison. Profiling logs add overhead; run clean bench with tracing off before claiming any throughput gain.

## Risks / ownership / next actionable stage
- Do NOT enable the flag globally until the real Mac build, tests, screenshot parity and A/B/A performance are confirmed.
- Main correctness risk: same `Model*` + same GPU handles + same world matrix but in-place bind/material/geometry mutation without explicit invalidation. User-provided `staticCache` still requires its documented owner-side lifecycle.
- Candidate predicate scans the model's runtime-mutable node flags each draw. Measure this CPU cost; if high, cache model-level eligibility by an explicit content revision, not unsafe pointer identity.
- For a second stage, introduce engine-owned model content revision/dirty events and connect every streaming/unload/reimport path; only then consider switching the cache on by default for immutable scenery.
- Longer-term target (NOT implemented): persistent per-object render packet graph with dirty generations, asynchronous geometry/material streaming, GPU indirect grouping that respects culling/shadows/LOD, and stable per-pass render scheduling. Coordinate with PHYSIS, Terra, water, VFX and Prisma owners; do not migrate genre logic into renderer.
