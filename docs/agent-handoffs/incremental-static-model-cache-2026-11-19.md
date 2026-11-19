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

---

## Stage 2 — revision-aware invalidation (2026-11-19)

**Branch:** `perf/incremental-static-model-cache`. **Code head before this handoff:** `e02709bb64fab7af7d63c62cb1097671a36a1c1a`.

**Code commits:** `67db0cc` adds `Model::geometryRevision` and the public `markGeometryChanged()` API; `f214ed6` increments the revision after `Model::finalize()`, `prepareModelUpload()` and `releaseModel()`; `994aef2` and `d44bfae` store and validate the revision in each per-entity CPU cache entry; `fd4a0b3` extends model batching unit assertions; `e02709b` updates the geometry performance contract.

**Changed files:** `engine/render/Model.h`, `engine/render/Model.cpp`, `engine/render/Renderer3D.h`, `engine/render/Renderer3DModels.cpp`, `tests/render/model_batching_policy.cpp`, `docs/geometry-performance.md`.

**Compatibility:** No serialized schema changes; model revision is runtime-only, starts at 1, never returns 0 on wrapping. FPS/RTS/client APIs do not need code changes unless they directly mutate live bind transforms or primitive geometry without calling standard engine mutation paths. Such callers must explicitly call `model.markGeometryChanged()` or invalidate the renderer cache. Existing `StaticModelCache` passed by a caller is unaffected and remains caller-owned. GPU render paths and platform defaults are unchanged. Versioning adds no per-frame heavy geometry hash and is not a lock for concurrent mutation; model edits must not race with rendering.

**Validation actually performed:** GitHub connector writes all code commits successfully. The branch PR #34 was open and GitHub reported `mergeable=true`, `mergeable_state=unstable` while checks were still pending. `GPU batching focused regression`, `CI`, `AIVERSE Studio validation` and `InputFlow` for `e02709b` were **queued** when inspected. Earlier `CI` runs for `d44bfae` and `fd4a0b3` were **cancelled**, not passing. **Native build NOT RUN; CTest NOT RUN; editor launch NOT RUN; GPU screenshots NOT RUN; macOS A/B NOT RUN** because this container cannot resolve github.com for cloning and has no AIVERSE checkout.

**Risks:** Native plugins that mutate `Model::bindWorld` or model geometry in place without calling a marking API can still invalidate correctness. The old authored model might be destroyed and recreated with the same pointer before renderer invalidation; revision checks reduce that risk for normal mutation paths, and `Editor::reloadRules()` explicitly clears the cache, but allocator/address reuse across entirely different Model objects still warrants asset lifetime testing. Cache will not eliminate CPU scene traversal or GPU draws.

**Next specific action:** Run `cmake --build build-metal -j8` and `ctest --test-dir build-metal -R '^model_batching_policy$' --output-on-failure` on owner's Mac at the latest PR #34 head; inspect the PR checks to completion; run same-camera Scene A/B/A with and without `AIVERSE_PERSISTENT_CPU_MODEL_CACHE=1`. Use screenshots and check that hits > 0 for suitable static rigid assets, that changing a model's geometry revision forces a rebuild, and that deleting/reloading models does not reuse stale transforms. If all pass, investigate targeted persistent submission packets and revision-aware dirty events instead of enabling Metal GPU ICB by default.

---

## Stage 3 — revisioned persistent rigid CPU render packets (2026-11-19)

**Branch:** `perf/incremental-static-model-cache`. **Implementation head before this handoff:** `a8d9d98e343525bd90ebc5701b6df774dd7bbb46` based on `main` at `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`. Continue on this same branch/PR #34; do not create a competing render branch. Merge latest main only after checking concurrent agents and resolving conflicts.

**What changed (code commits):**
- `15ea343` and `33b3a76`: add `ModelDraw::staticPacketRevision` (zero = off), `Renderer3D` packet entry storage, `staticRenderPacketStats()` API and separate scene epoch. The render-item template keeps nonowning pointers and draw-identity/geometry generation state.
- `413565a`, `abddd13`: conservative `staticRenderPacketCandidate` and exact parameter comparison in `engine/render/StaticModelCachePolicy.h`. Must be static, stable `persistentId`, explicit nonzero packet revision, no per-draw material array or per-node override/variant, no runtime LOD, skin, morph, authored animation, mutable nodes, cluster geometry, baked GI, active vertex displacement or LOD inspector.
- `17300ef`, `3b78842`, `52cc135`: lifecycle cleanup and periodic eviction with independent retained byte accounting; `invalidateStaticModelCaches()` clears both static-transform and packet storage (also used on editor rule reload).
- `a819e47`: `Renderer3DModels.cpp::drawModel` reuses cached `ModelItem` templates for a warm eligible object, recomputes node camera/light culling and per-item depth per draw. Material/geometry primitive selection and static transforms are copied from a prior valid draw. LOD/skinned/baked dynamic paths still run original code. The regular rendering path records new templates at the end; `ModelItem::sourceNode` identifies which node to recull during replay. The renderer's per-pass grouping, instancing, CPU MDI and GPU ICB are unchanged.
- `a3a15e8`, `b735e3e`: limit individual packet to 128 items and retained template item storage to 64 MiB, and update accounting when replacing/evicting/invalidating.
- `7ecb94e`, `05028b4`, `a8d9d98`: editor Batching UI and read-only stats (hits/rebuilds/declines/evictions/live/retained bytes).
- `21936b3`: regression assertions added to `tests/render/model_batching_policy.cpp`.
- `1516a88`, `e7ef1aa`: API, limitations and rules in `docs/geometry-performance.md` and `AGENTS.md`.

**Changed paths in this stage:** `engine/render/Renderer3D.h`, `Renderer3D.cpp`, `Renderer3DModels.cpp`, `StaticModelCachePolicy.h`, `tests/render/model_batching_policy.cpp`, `apps/editor/windows/SceneWindow.cpp`, `docs/geometry-performance.md`, `AGENTS.md`, plus this handoff. No genre-specific simulation code, save formats or shader ABI modified in Stage 3.

**API / usage:** `AIVERSE_PERSISTENT_CPU_PACKETS=1` enables the experimental CPU packet store in the shared Renderer3D. Each eligible native consumer must set stable `ModelDraw::persistentId`, `kDrawStatic` and nonzero `ModelDraw::staticPacketRevision`; increment the revision for *any* caller-owned in-place appearance/material changes before the next draw. `Model::geometryRevision` also invalidates packets. No existing RTS or FPS host sets `staticPacketRevision` automatically yet, so merely enabling the environment flag does **not** make those scenes hit. They continue to render through the original path. `Renderer3D::invalidateStaticModelCaches()` clears both CPU caches; call on asset reload/destruction. Memory: 64 MiB max for stored `ModelItem` template payload, excluding unordered_map/vector overhead. Batching UI displays current counts. No GPU command reuse and no assumption of one draw call per material across distinct meshes.

**Validation actually performed / outcomes:** Used GitHub connector to fetch source, compare branch and write all Stage 3 source commits. GitHub returned PR #34 open, `mergeable=true`, still draft, at implementation head `a8d9d98`. Workflow statuses for this head at inspection: CI **queued**, GPU batching focused regression **queued**, Studio validation **queued**, InputFlow targeted checks **queued**. Old CI runs on superseded Stage 3 SHAs were **cancelled**, not passing. Attempted `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` in provided container; failed `Could not resolve host: github.com`. **Native full build NOT RUN; CTest NOT RUN; macOS Metal/Vulkan visual comparison NOT RUN; CPU/GPU benchmark NOT RUN.** No claims of successful compilation or measured performance are warranted.

**Required owner validation (on Apple Silicon, before merging or default enablement):**
```sh
cd ~/Projects/AIVERSE
git fetch origin
git switch perf/incremental-static-model-cache
git pull --ff-only origin perf/incremental-static-model-cache
cmake --build build-metal -j8
ctest --test-dir build-metal -R '^model_batching_policy$' --output-on-failure
```
With an explicitly revisioned immutable native `ModelDraw` fixture (set `staticPacketRevision=1`), run same-camera A/B/A with `AIVERSE_PERSISTENT_CPU_PACKETS=0` versus `=1`, then change camera and lighting/frustum/outline and compare pixels/shadows/depth order. Advance the revision after changing tint/material parameters, geometry revision after in-place bind edits, and verify counts/hit invalidation. Ensure editor reload clears entries; alternate Scene/Game viewports and inspect memory counter. Test a real model with multiple material/shadow batches and no runtime LOD. Do not use experimental Metal ICB in the first comparison. Also check build with all `-Wall/-Wextra` settings and full CI.

**Known limitations / next exact step:**
1. Opt-in only; RTS passes per-instance `show->materials` overrides and modern imported models often have generated LOD chains, so they are deliberately ineligible. Plan a **LOD-aware packet template** and caller-owned material generation before wiring genre-agnostic static ECS presentation adapters. Keep view-dependent LOD/culling live; do not bake visibility or GPU draw output across cameras.
2. The packet revision is an explicit caller contract, not an auto-detected material mutation mechanism. Native side mutations that forget to bump it could reuse stale parts/material batching; default remains OFF. Consider a central model/material revision plus change notifications before relaxing eligibility.
3. Packet entry lifetime uses nonowning source pointers. All asset reload/unload paths outside `Editor::reloadRules()` require review/invalidation; check model pointer reuse and GPU handle reuse. Do not hold packets after model destruction.
4. CPU packet copies may be slower than regeneration for small primitive models; only benchmark can justify enabling default. Remaining RTS/editor CPU scene build and object traversal is not eliminated.
5. Start next stage by reviewing latest CI result, fixing any compile/test failures, then add a dedicated real renderer packet parity fixture that demonstrates a genuine warm hit and identical image. Do not claim full GPU-driven renderer or skip the remaining tests.

---

## Stage 4 — automatic material invalidation, rigid LOD and generic ECS adapter (2026-11-19)

**Branch:** `perf/incremental-static-model-cache`, PR #34 (Draft). **Code HEAD before this handoff:** `4b67a1661ad6baab5b4a56b50bcafeefcb0d9994`. **Base:** `main` at `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`. The branch still had no behind commits when Stage 4 began; re-fetch/compare before any merge. Do not move experimental rendering flags into production defaults without hardware validation.

### Implementation changes and commit trace
- `3fcddeb`: `engine/render/StaticModelCachePolicy.h` adds `staticPacketMaterialHashes` and `staticPacketMaterialStateMatches`. Captures hashed resolved GPU material state (shader, factors, texture handles) and exact `equivalentGpuMaterial` comparison to guard hash collisions. Supports `ModelDraw::materials` arrays and pointer-stable changes without relying on raw material pointer equality. **Not** a texture-content fingerprint; writes behind an unchanged GPU handle still need native asset revision / sync.
- `2987e8d`: `engine/render/Renderer3D.h` adds saved material snapshots/signatures, per-node selected LOD mesh indices, and material index in every stored `ModelItem`. Cached external pointers must not be dereferenced after source vectors are reallocated.
- `f0061bd`: `Renderer3DModels.cpp` performs per-hit exact source/appearance/material validation, reselects the rigid LOD from the **current main + sun shadow** projected error, and replays only if each selected mesh matches. LOD crossfade samples bypass cache. Material pointer and per-vertex palette references are re-bound to the live material vector before replay, with original per-node frustum checks and transparent depth each draw. Model animation, skin, morph, baked GI, node material overrides/variants, geometry clusters, GPU ICB compute LOD all use conventional paths. Cold/mismatched items rebuild from the ordinary `drawModel` implementation and save the new LOD selection/signatures.
- `f737938`, `41dcde6`: fixed retained memory accounting (materials/LOD snapshots included) and validated every cached item before replay rather than risking skipped draws.
- `f054ef4`, `93d466`: extended `tests/render/model_batching_policy.cpp` for in-place material mutation, vector size/reallocation, compatible material copies, LOD eligibility, LOD selection threshold and crossfade rejection.
- `fd53ae5`: new shared public header `engine/render/EcsStaticModelRender.h`, with `EcsStaticModelRender` integer-only presentation component, `visitEcsStaticModels` testable view system and `drawEcsStaticModels` native project wrapper. Uses `World::eachExistingConst` + existing `WorldTransform` / `hierarchyMatrix` after hierarchy barrier. Resolves model by caller-supplied asset key; nonzero namespaced persistent draw ID supplied by host; no Model pointers or floats stored in authoritative ECS. It is presentation-only; no deterministic simulation mutations or gameplay serialization changes.
- `ce5565c`: CPU-only ECS adapter assertions for identity, transforms, missing components, hidden/unresolved entities, revision, render flags and recycled Entity generation.
- `8e2bf7e`: small opt-in RTS presentation host adapter in `modules/rts/view/RtsViewDraw.cpp` sets `staticPacketRevision=1` only on existing finished stationary `kDrawStatic` candidates. The renderer still validates all real material/transform/LOD state, and the environment flag remains necessary; no RTS feature logic was moved into render core.
- `ee2e4c9`, `4b67a16`: `docs/geometry-performance.md` and `AGENTS.md` updated with API/eligibility, ECS architecture, limits, opt-in and ownership.

### Public API and contract
`AIVERSE_PERSISTENT_CPU_PACKETS=1` enables experimental cache globally; `AIVERSE_PERSISTENT_CPU_MODEL_CACHE=1` independently enables per-entity transform reuse. Source material state is now **automatically checked on each packet hit** even when `staticPacketRevision` is unchanged. The revision remains required to opt in, and for native non-renderer state not covered by the GPU material fingerprint / standard `ModelDraw` fields. The full `Model::geometryRevision` + `markGeometryChanged()` contract applies; `invalidateStaticModelCaches()` clears both stores before model reload/unload.

Runtime rigid LOD **is** supported when stable on the selected level. GPU LOD compute is not reused; crossfade frames take the full dynamic path. Per-node main view/shadow visibility, transparency sorting, GPU shader/pipeline grouping and actual GPU draws remain per frame. The packet store caps each object at 128 items and approximate retained item/material/LOD payload at 64 MiB. No claim of GPU-driven scene selection.

For genre-independent ECS use:
```cpp
#include "engine/render/EcsStaticModelRender.h"
// At ECS setup/creation: world.add<render::EcsStaticModelRender>(
//     entity, {assetKey, appearanceRevision, flags, true});
// After SceneHierarchy transform update, in presentation stage:
render::drawEcsStaticModels(world, renderer,
    [&](uint32_t assetKey) -> const render::Model* { return resolveModel(assetKey); },
    [&](aiverse::Entity entity) -> uint64_t {
        return namespacedNonzeroStableDrawId(entity);
    },
    renderOrigin);
```
The host ID must be stable, unique across all concurrently rendered Worlds and **all draws** in one renderer. A zero key/revision, unresolved asset, hidden entity or missing `WorldTransform` is skipped. The helper uses read-only ECS iteration. Cross-genre access is via public engine headers, not `RtsModule` or FPS modules.

### Files changed this stage
`engine/render/StaticModelCachePolicy.h`, `engine/render/Renderer3D.h`, `engine/render/Renderer3D.cpp`, `engine/render/Renderer3DModels.cpp`, `engine/render/EcsStaticModelRender.h` (new), `modules/rts/view/RtsViewDraw.cpp`, `tests/render/model_batching_policy.cpp`, `docs/geometry-performance.md`, `AGENTS.md`, this handoff.

### Actually performed validation / outcomes
- GitHub connector: fetched PR #34 head and base, main compare, source files and existing contract; wrote each code/doc commit to the existing feature branch successfully. The PR remains open Draft and was reported mergeable by GitHub at prior inspection.
- Local checkout attempt: `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` **FAILED**, with `Could not resolve host: github.com` in the provided runtime. This prevents local CMake/CTest and real GPU capture.
- **CMake build: NOT RUN. CTest: NOT RUN. Native ECS consumer compile: NOT RUN. macOS Metal A/B and screenshots: NOT RUN. CPU/GPU timings: NOT MEASURED.**
- GitHub Actions status for `4b67a16` at inspection: `CI` queued, `GPU batching focused regression` queued, `AIVERSE Studio validation` queued, `InputFlow targeted checks` queued. Workflows for superseded intermediate commits were frequently cancelled (not passed).
- Assertions added to `tests/render/model_batching_policy.cpp` are source-level checks only until compiled and executed. Do not report a PASS.

### Known risks and next exact actions
1. **Immediate gate:** build the current branch with `cmake --build build-metal -j8`, then `ctest --test-dir build-metal -R '^model_batching_policy$' --output-on-failure`; review GitHub focused GPU batching workflow and address compiler/test output. Run with both cache flags off to establish unchanged baseline visuals.
2. Use a real fixed-camera RTS city with `AIVERSE_PERSISTENT_CPU_PACKETS=1` and compare Scene → Batching hits/rebuilds and Scene CPU without logging overhead; then move camera through LOD thresholds and shadow angle changes, inspect shadow/material/mask/decal parity against OFF. Use identical camera/settings and separate-process A/B/A. A hit counter is **not** proof of reduced CPU/GPU frame time.
3. Native ECS fixture must submit an immutable `EcsStaticModelRender` through `drawEcsStaticModels` with a live resolver and stable namespaced key, and demonstrate warm hits, component revision changes, missing component and recycled entity handling.
4. A critical unresolved safety contract is GPU texture content mutation behind the same texture handle (not detected by GPU state hash), plus arbitrary native modifications to model bind hierarchy that bypass `markGeometryChanged()`. Require explicit native asset revision/invalidation; keep mode OFF by default until lifecycles are audited.
5. The material fingerprint traverses GPU material state and may be expensive when thousands of instances share a material. Profile first; consider an optional **frame-local** memo keyed only by a verified stable material-owner revision, not raw Material* identity (temporary vectors can reuse addresses). Never degrade GPU material equality merely to inflate cache hit counts.
6. Current cached packet reuses primitive geometry/material selection only; per-object submission loop and CPU frame setup remain. A further phase should add safe persistent scene command graph only after testing this step. No changes to PHYSIS, Terra, water, Prisma, SpectraFX or other owners' modules.


---

## Stage 5 — real-backend cold/warm pixel parity fixture (2026-11-19)

**Branch:** `perf/incremental-static-model-cache`; **PR:** #34 (Draft); **implementation head before this handoff:** `d0dff792cbb25cd1b8ac79f40f538feabf193c77`.

**Main reconciliation requested by owner:** Queried current `main` and this branch on 2026-11-19. `main` was `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`; GitHub comparison `main...perf/incremental-static-model-cache` returned `ahead_by=50, behind_by=0`, merge-base exactly `9ee6ff6`. Therefore the working branch **already contained the complete current main**; no merge commit/cherry-pick/no-op ref move was needed. `main` remains untouched. Recompare before any subsequent write because other agents work concurrently.

### Implementation
- `fdeb288c5204e0a59ebc0d0c1dc14abfdd309e3f`: `tests/render/material_batches.cpp` gains `AIVERSE_PALETTE_TEST_CASE=static_packets`. It uses the embedded 16-independent-primitive, three-material scene and `ModelDraw::materials` copy. First image is uncached (`staticPacketRevision=0`), followed by warm frames with revision 1. Frame 23 demands an actual persistent packet hit; reference frame 8 was uncached. Main/shadow draw API counts must match; captured RGBA images must be byte-for-byte equal. This is a **real GPU fixture** when run against the selected graphics backend, not merely unit-checking the packet selector. The test intentionally keeps `materialBatches` fixed throughout; mutating mesh vectors without `Model::markGeometryChanged()` would violate the packet cache's asset-lifetime contract.
- `73e42ca324ac0d62526a9231392b95f42bd5c9d3`: `.github/workflows/gpu-batching.yml` adds a macOS Metal cold/warm packet parity step to the existing `metal-cpu-mdi` job, setting `AIVERSE_PERSISTENT_CPU_PACKETS=1` and `AIVERSE_MULTI_DRAW_INDIRECT=off`. Existing artifact collection uploads the `static-packets-before.ppm` and `static-packets-after.ppm` captures.
- `d0dff792cbb25cd1b8ac79f40f538feabf193c77`: `docs/geometry-performance.md` describes the isolated fixture and its local reproduction.

**Changed paths this stage:** `tests/render/material_batches.cpp`, `.github/workflows/gpu-batching.yml`, `docs/geometry-performance.md`, this handoff. No runtime renderer logic was changed during Stage 5; main branch unchanged.

### Actual validation and status
- **GitHub connector:** All three code/CI/doc writes succeeded. Current main/feature compare is `ahead=50`, `behind=0`; no integration conflicts identified by GitHub. PR #34 still open as Draft. No model or scene data files were modified.
- **GitHub Actions for `d0dff792`** when checked: GPU batching focused regression `queued` (https://github.com/chromealex/AIVERSE/actions/runs/44484106144), CI `queued` (https://github.com/chromealex/AIVERSE/actions/runs/44484105979), Studio validation `queued`, InputFlow `queued`. Older superseded workflow runs were often cancelled; **none of these is evidence of a pass**.
- **Real compilation: NOT RUN here. CTest: NOT RUN here. GPU/Metal parity: NOT RUN here. GPU/CPU benchmarks: NOT RUN here.** The connected GitHub data is available, but the local environment cannot clone the full AIVERSE repo over DNS and has no checked-out build.

### Next exact action
1. Wait for the GitHub **metal-cpu-mdi** job at the current head to run; fetch `fetch_workflow_run_jobs` then `fetch_workflow_job_logs`. If the new cold/warm fixture fails, inspect actual compile/runtime logs, commit a focused fix, and append a new handoff **before** further features.
2. Independently run `cmake --build build-metal -j8` and `ctest --test-dir build-metal -R '^model_batching_policy$' --output-on-failure` on the owner's Apple Silicon machine.
3. Validate original A/B images and count warm cache hits. If results pass, run the large, same-camera RTS city with cache off/on/off (separate process) and measure Scene CPU and GPU frame time without trace instrumentation.
4. Keep `AIVERSE_PERSISTENT_CPU_PACKETS`, `AIVERSE_PERSISTENT_CPU_MODEL_CACHE`, CPU MDI and GPU ICB OFF by default until correctness/stability and measured perf gain are established. Do not describe shader/texture/LOD correctness as proven until actual GPU runs complete.



---

## Stage 6 — CI status and false-positive-proof packet A/B fixture (2026-11-19)

**Branch:** `perf/incremental-static-model-cache` (PR #34, still Draft). **Code head before this handoff:** `f2c7f7b8db23d9114080fed454367e27ab336cd4`; `main` remained `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` when checked. Compare returned `behind_by=0`, so current main is already included.

**Actual work and changed files:** Commit `f2c7f7b8db23d9114080fed454367e27ab336cd4` tightens `tests/render/material_batches.cpp` for `AIVERSE_PALETTE_TEST_CASE=static_packets`. At frame 15 (the first revisioned frame), it explicitly requires **one cold packet build**, zero hits, one live entry and nonzero retained bytes. At frame 23, after warmup, it requires **exactly one replay hit**, zero rebuilds, the same live entry, nonzero bytes, unchanged shadow/opaque draw counts and the existing pixel-identical screenshot comparison. Previously, a mere nonzero hit with another rebuild in the same frame could be mistaken for successful stable reuse. No renderer, material or GPU implementation code changed in this stage.

**CI evidence queried:** GitHub Actions run for head `f2c7f7b8`: [GPU batching focused regression](https://github.com/chromealex/AIVERSE/actions/runs/44485160452) `queued`; [CI](https://github.com/chromealex/AIVERSE/actions/runs/44485160451) `queued`; Studio validation `queued`; InputFlow targeted checks `queued`. Earlier head `5682f1b` was superseded and its CI run was `cancelled`, not failed or passed. GitHub workflow-job inspection of that older CI found Linux arm64 running briefly, but no actionable compiler diagnostics or completed validation; the focused Ubuntu/macOS model-policy and macOS CPU-MDI jobs remained queued. Do not describe any test as passing.

**Actually performed:** GitHub connector read PR #34, compare, run status and specific job states; updated fixture source and confirmed GitHub commit accepted. Container `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` failed with `Could not resolve host: github.com` on this session, and there is no local checkout. Therefore CMake, CTest, actual macOS Metal capture, frame benchmark and visual parity were **NOT RUN** in this session. No measured CPU/GPU improvement.

**Important scope:** Current fixture checks GPU image parity for unchanged, non-LOD 16-part rigid geometry with copied materials and pre-existing shadow/material groups. It is not a comprehensive test of LOD transitions, vertex displacement, shader graph changes, real RTS scenes, asset unload or editor selection. The experimental packet cache and transform cache remain OFF by default; Metal ICB remains experimental and independent.

**Next concrete step:** Re-read PR #34 head/Actions (new commits supersede queued runs); fetch failing `metal-cpu-mdi` or model-policy job logs only after the jobs complete, fix the exact compiler/visual cause, then append the next handoff **in this same branch before more code changes**. For actual macOS runtime, run `AIVERSE_PERSISTENT_CPU_PACKETS=1 AIVERSE_PALETTE_TEST_CASE=static_packets AIVERSE_MULTI_DRAW_INDIRECT=off aiverse_material_batches unused.obj /tmp/static-packets` after building `aiverse_material_batches` with `AIVERSE_BUILD_HDR_TESTS=ON`. Then run A/B/A on the owner's large city scene with FPS/renderer CPU and GPU timings, not draw-submission totals alone.

---

## Stage 7 — remove packet-hit allocations; CI still queued (2026-11-19)

**Branch:** `perf/incremental-static-model-cache`, PR #34 Draft. **Implementation head before handoff:** `3f3a821eac45749f99b2701f80661e2d3c9b0ce0`. Based on `main` `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`; latest GitHub main/branch comparison prior to Stage 7 showed **behind=0** (no merge needed).

### Work completed
- `9025643efd12697b68b41d06140ebec9bbd29cb2` in `engine/render/Renderer3D.h`: introduces renderer-owned `StaticPacketNodeState` (current per-node visibility, shadow visibility, LOD mesh/level/transition) and a reusable `staticPacketNodeScratch_` vector. Renderer3D model submission is not reentrant.
- `3f3a821eac45749f99b2701f80661e2d3c9b0ce0` in `engine/render/Renderer3DModels.cpp`: removes `std::vector<PacketNodeState> nodeStates(m->nodes.size())` from every hot packet hit. Resizes/reuses the existing renderer-owned vector and updates every node-with-mesh entry before replay; main/sun-shadow LOD selection, crossfade rejection, culling, depth and material rebinding remain unchanged.
- No shader, GPU API, save, ECS component schema, genre logic or platform option changed. This stage fixes a probable per-entity CPU allocation hotspot, **not** measured performance yet.

### Actual checks and results
- GitHub connector read PR #34 head `0dd5d9b5ecd431778d45ed1dae1cab216a6a3673` at start; main still `9ee6ff6`, PR mergeable Draft, branch ahead by 54 and behind by 0. `GPU batching focused regression` at `0dd5d9b5` had four jobs queued (Ubuntu/macOS model-policy, metal-cpu-mdi, metal-icb-gpu), and CI/Studio jobs were also queued. Older CI attempts were cancelled after superseding pushes, not passing.
- Wrote both source commits successfully through GitHub connector. Read back both files and performed source consistency checks: new scratch declaration and resize present; prior per-hit `std::vector<PacketNodeState> nodeStates(...)` absent; `st.lod.` references absent; new mesh/level/transition assignments present. Previous strict cold/warm fixture assertions remain present. These are **textual/static checks only**, not compiler/CTest execution.
- **Compile: NOT RUN. CTest: NOT RUN. Metal pixel comparison: NOT RUN. CPU/GPU A/B: NOT RUN.** No local AIVERSE checkout is present in the available runtime and earlier attempt to clone over GitHub DNS failed. No CI job at head completed before this handoff; next agent must refresh statuses.
- Any new pushes create another CI head and may cancel older runs. After this handoff commit, prefer not to push unrelated changes until focused CI reports real results.

### Risks and exact next action
- Renderer-owned scratch assumes a non-reentrant `drawModel` submission loop. A future recursive draw callback or parallel renderer submission needs an explicit scratch context per call/thread before enabling this code in those environments.
- Scratch vector retains high-watermark node capacity for the renderer lifetime (small memory tradeoff); packet entry cache is independently limited to 64 MiB of tracked payload.
- When GitHub Actions focused GPU batching run for the **new handoff HEAD** reaches terminal state, fetch failing job logs and fix real compiler/runtime errors. Then run the large same-camera city A/B/A on Apple Silicon (cache OFF, ON, OFF), trace disabled for primary measurement; use matched camera/view settings and reported CPU/GPU frame times. **Do not merge or enable defaults based on static checks or submission count alone.**

---

## Stage 8 — cold-only LOD signature allocation; focused CI audit (2026-11-19)

**Branch:** `perf/incremental-static-model-cache`; **PR:** #34 Draft. **Code commit:** `fbcb81ff00caab757b54fc04450c1ca3d53912a0`. **Latest code HEAD before this handoff:** same SHA. **Base main:** `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`; the GitHub `main...feature` compare reported `behind=0` at stage start. Continue in this branch without overwriting other agents' work.

**Source modification:** In `engine/render/Renderer3DModels.cpp`, `packetSelectedMeshes.assign(m->nodes.size(), -1)` was previously executed at the start of every eligible packet draw—even a hot cache hit which returns before needing the signature. Commit `fbcb81ff` moves that allocation *after* the replay attempt and before the cold rebuild loop. The signature is now built only on cache miss / invalidation. Together with Stage 7's reused node scratch, this eliminates two unnecessary per-object allocations on warm packet replay while preserving the LOD and material correctness contract, cache eviction, memory bounds and rendering paths.

**Verification actually performed:**
- GitHub connection read `main`, PR #34, branch compare and GitHub workflow runs; the last Stage 7 head `ac510acc` showed the four focused GPU batching jobs (Ubuntu/macOS model-policy, Metal CPU MDI and GPU ICB) all **queued**, and the regular CI and Studio jobs queued. No job log with compiler diagnostics was available.
- Wrote `fbcb81ff` through the GitHub connector. Re-fetched `Renderer3DModels.cpp` from the branch. Textual source control-flow check confirmed the warm replay `return` precedes the **single** remaining `packetSelectedMeshes.assign` call and that this call is guarded by `if (packetEligible)`. This was a **source inspection**, not executable validation.
- GitHub workflow runs for implementation head `fbcb81ff` were still **queued/pending** at inspection: CI, GPU batching focused regression, Studio validation, InputFlow targeted checks. Previous superseded head CI runs were cancelled; none is a successful execution.
- Checked container filesystem: `/mnt/data` has no AIVERSE repository or build artifacts. C++/CMake executables exist but the necessary source tree does not. Prior session clone attempt failed due GitHub DNS. **CMake build: NOT RUN. CTest: NOT RUN. GPU fixture: NOT RUN. Visual parity: NOT RUN. CPU/GPU benchmark: NOT RUN.** No speedup claim is justified by the source change.

**Compatibility:** No save/network/ECS schema changes or default flag changes. `AIVERSE_PERSISTENT_CPU_PACKETS=1` and `AIVERSE_PERSISTENT_CPU_MODEL_CACHE=1` remain off by default. The frame's chosen rigid LOD, shadow visibility and transparent depth remain evaluated per model draw. Hot packet signatures are not rebuilt; cold packet saves still record one signature vector per candidate model as before.

**Next exact action:** Let the focused GPU batching runner finish at **the final handoff commit SHA**, then inspect **actual** failed `model-policy` and `metal-cpu-mdi` job logs via the GitHub workflow jobs/logs endpoints; fix any failure before merge and append Stage 9 handoff. On Apple Silicon also run the self-contained `static_packets` real-backend raster fixture and large urban A/B/A with flags OFF/ON/OFF. Don't keep pushing trivial documentation commits repeatedly once CI is queued, because superseded CI runs are often cancelled and useful test results are delayed.


---

## Stage 9 — prevent superseded GPU batching CI flood (2026-11-19)

**Branch:** `perf/incremental-static-model-cache`. **PR:** #34, Draft. **Code commit:** `c6b2ee65c744ab454abbbb02e0cc42109d849ae9`. **Main ancestry at stage start:** main `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, branch `ff9063945324533f89e70ccf1009636321f5f2fa`, `ahead=59`, `behind=0`. All changes remain in the PR branch; main is unchanged. This handoff is committed separately after code as required.

### Actual change
- `.github/workflows/gpu-batching.yml` adds top-level GitHub Actions `concurrency`:
  `group: gpu-batching-${{ github.event.pull_request.number || github.ref }}`
  and `cancel-in-progress: true`.
  This groups only the GPU batching workflow's runs for **the same PR** (or same ref on manual dispatch); later updates supersede older checks in that group, while unrelated PRs are independent. `CI` already had cancellation by ref; the batching workflow did not. This change does not affect the renderer, shader output or test assertions.
- The same workflow now explicitly watches `tests/render/material_batches.cpp` in its `pull_request.paths`, so changes to the real Metal pixel-parity fixture cannot silently skip the focused job.
- This is an **CI workflow orchestration stage**, not a feature or speedup. No other teams' workflows were edited.

### Evidence and checks actually performed
- Read PR #34 metadata and GitHub branch compare. PR open Draft, mergeable with current main and zero behind commits. All GPU batching jobs on latest previous head `ff906394` remained **queued**, so there was no failing compiler log to fix and no successful GPU regression.
- Queried repository GitHub Actions run statuses. The API reported `queued.total_count=2500` (possibly limited by the API; do not treat as an exhaustive exact count) and `in_progress.total_count=10` on the same date. Many different agent branches generated frequent new runs, while older `CI` runs were `cancelled` due `cancel-in-progress`. **This is strong evidence of a CI backlog, not proof of a GitHub outage or billing issue.**
- Wrote the workflow code commit and re-fetched the file from the branch. Confirmed literal GitHub Actions expression, correct YAML section, opt-in cancellation, and new fixture path filter in repository source. The workflow run for code commit `c6b2ee65` was **queued** immediately after push; older focused GPU runs were still queued when checked. A newly added concurrency key cannot be assumed to retroactively cancel every earlier run created without a group.
- **CMake/CTest: NOT RUN. Metal GPU raster parity: NOT RUN. CPU/GPU A/B: NOT RUN. YAML executed by GitHub runner: NOT YET.** No build/test success or frame-time gains can be claimed from this stage. The local execution environment has clang++/cmake but no AIVERSE checkout; git clone previously failed DNS.

### Next exact action / ownership
1. **Avoid further trivial pushes to PR #34** until one final-head `GPU batching focused regression` run reaches terminal status. Every push creates a new suite of CI jobs; old non-concurrent workflows (Studio, InputFlow) may still accumulate. This stage focuses only the GPU batching workflow.
2. Fetch `fetch_workflow_run_jobs(repo_full_name="chromealex/AIVERSE", run_id=<new head run>)`, and on failure `fetch_workflow_job_logs` for model-policy Ubuntu/macOS and metal-cpu-mdi jobs. Report exact failing command and line before any source change. The macOS real-backend `static_packets` fixture must show true cold creation, warm hit without rebuild and pixel-equal output.
3. If GitHub remains queued at the head, owner can review the Actions queue/runner availability and run model-policy + GPU fixture from his Apple Silicon checkout; do not infer run success from queue status.
4. After actual validation and A/B/A of an urban scene, evaluate performance effects and only then consider default-enable. `AIVERSE_PERSISTENT_CPU_PACKETS` and `AIVERSE_PERSISTENT_CPU_MODEL_CACHE` stay OFF by default; PR remains Draft.
