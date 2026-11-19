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


---

## Stage 10 — static node-material overrides and KHR variant packet support (2026-11-19)

**User priority:** features first; defer tests to a later stage. **Branch:** `perf/incremental-static-model-cache`; **PR:** #34 Draft. **Implementation HEAD before this handoff:** `44fc9b44b7e0fa1291c15b43765793a7653bd2ef`. Main ancestry checked at stage start: `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, feature ahead 61, behind 0. All changes are in this feature branch. Do not move code to main before compilation/hardware validation.

### Implemented code
1. `22c24619c1efffc03ee0d39cde7291bed0602090` — `engine/render/StaticModelCachePolicy.h` no longer rejects static packet candidates purely because they supply `ModelDraw::nodeMaterial` or select `ModelDraw::variant`. Other safety checks for runtime mutability, active displacement, pose, skinning, morph, clustering, baked GI and LOD inspector remain.
2. `4c4b56fc56dbf769ee8a7730b89f1dc79d59fba1` — `engine/render/Renderer3D.h` adds `StaticPacketEntry::hasNodeMaterialOverride` and `nodeMaterialSnapshot` (owned vector). The packet payload budget includes this saved mapping. Never persist the user-owned `ModelDraw::nodeMaterial` pointer.
3. `93e83a8e1e97b654530ee119658fcf38bb8785b3` — `engine/render/Renderer3DModels.cpp` compares presence + exact contents of the live node override vector to the snapshot on a warm hit, before replaying cached `ModelItem` material indices. On a cold path it saves the new mapping; a change triggers ordinary regeneration. The per-draw material variant index was already included in `equivalentStaticRenderPacketDraw` and is likewise rebuilt on a change. The LOD replay gate now matches the existing baseline: when `d.variant >= 0` or `d.nodeMaterial != nullptr`, dynamic CPU LOD selection is disabled. Otherwise normal per-view LOD re-selection is preserved, including crossfade exclusion. No material/shadow batch policy was loosened: `ModelMerge` remains authoritative.
4. `b8333eddca923ce8ef1446b4d27d399573273c21`, `44fc9b44b7e0fa1291c15b43765793a7653bd2ef` — documentation in `docs/geometry-performance.md` and `AGENTS.md` now states that static per-node overrides and selected KHR material variants are eligible, and explains the copy/compare/lifetime semantics.

**Public API/compatibility:** No new ECS authoritative components, save data, shader ABI, GPU buffer format, or gameplay rule branches. Renderer-level opt-in still requires `AIVERSE_PERSISTENT_CPU_PACKETS=1`, nonzero stable `persistentId`, nonzero `staticPacketRevision`, and `kDrawStatic`. `nodeMaterial` is nonowning in the immediate draw only; stored packet holds a value snapshot. No new genre-specific renderer dependency. Changing the selected variant is already caught by draw signature. Changing underlying Model variant tables or meshes in place requires `Model::markGeometryChanged()`.

**Validation ACTUALLY performed:** GitHub connector read main/branch/PR metadata and applicable renderer methods, updated the three code files and two docs successfully. No local checkout available; the GitHub Actions queue is congested. **Build: NOT RUN; CTest: NOT RUN; GPU parity: NOT RUN; CPU/GPU benchmark: NOT RUN.** Tests intentionally deferred by user's latest instruction. Existing regression assertions still target the old conservative eligibility predicate and **must be updated before the test phase**; do NOT represent this stage as passing CI. PR remains Draft, flags OFF by default.

**Known risks:** Revision-less in-place mutation to variant-related source geometry requires explicit `markGeometryChanged()`; model material GPU-state fingerprint covers per-draw material factor changes, not texture bytes edited behind a retained handle. Per-node override array is compared by value on every cache-hit candidate (no allocation on hit), which costs proportional to mapping size. The dynamic LOD-path restriction is intentional for explicit node overrides and variants to remain pixel-equivalent with baseline. Run screenshot parity across these modes before production enablement.

**Next feature (before tests):** extend static cache eligibility to rigid bind-pose instances of models containing authored animation clips *when the actual `ModelDraw` supplies no animated pose/baked sample, none of its nodes is marked runtime mutable, and the model has no skins*. Keep `kDrawStatic` as the explicit caller promise; do not cache actual animated nodes. Then document this as Stage 11. Test debt: update `tests/render/model_batching_policy.cpp` expectations for override/variant eligibility, add invalidation and GPU parity cases for node override index change and variant switching, run focused CI once features are complete.


---

## Stage 11 — static bind-pose cache for clip-bearing rigid assets (2026-11-19)

**Priority:** user explicitly requested **features before tests**. **Branch:** `perf/incremental-static-model-cache`, PR #34 Draft. **Implementation HEAD before handoff:** `3ee89c3679511d12beffd08df85a1ae27b7de08b`. Current main was `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` and branch was ahead/behind `61/0` at start of Stage 10; re-check before merging. Continue exclusively in this PR branch.

### Feature changes
- `e9bb6fb865ea5a1162ec5677ba5fe078d1c1e934`: `engine/render/StaticModelCachePolicy.h` no longer rejects an otherwise rigid `kDrawStatic` instance simply because the imported `Model` contains authored `clips`. The **instance**, not the asset's animation metadata, controls cache eligibility. Any draw supplying a live `nodeWorld`, `pose`, or `baked` sample still declines. All skinned assets, any runtime-mutable node, and geometry clusters remain excluded. Additionally, `m->bindWorld.size() == m->nodes.size()` is required, so an un-finalized model cannot create an unsafe transform cache. `staticModelCacheGeometryCompatible()` follows the same requirement.
- `b527e489ea1e8a32f648af4a9fc3fc3bf3d7c44a`: `docs/geometry-performance.md` clarifies static bind pose versus active animation.
- `3ee89c3679511d12beffd08df85a1ae27b7de08b`: `AGENTS.md` updates cross-agent eligibility and notes about animation metadata versus live pose.

**Compatibility:** no changes to serialized data, ECS world state, shaders, renderer flags, public types, GPU pipeline, or platform code. The cached transform has exactly the same bind-pose value as the pre-existing original renderer. An actively animated instance is still uncached; `kDrawStatic` is an explicit caller declaration. Model bind edits must call `markGeometryChanged()`, and unload/reload must invalidate renderer cache.

**Actual validation:** GitHub connector read/updated source + docs and returned successful commits. **Native CMake compilation NOT RUN, CTest NOT RUN, Metal screenshot parity NOT RUN, A/B benchmark NOT RUN**, per user priority (and no local repo). Existing policy tests set up synthetic models with nodes but no `bindWorld`, so their expectations will need updating to finalize or populate `bindWorld` during the later dedicated test phase. The old test expecting `clips` to decline also must be updated to assert that a static no-pose draw with clip metadata is eligible, while active animation poses remain ineligible. Test debt from Stage 10 (node material/variant eligibility) likewise remains. Do not call this validated.

**Next feature before tests:** improve packet lifetime correctness and predictable memory pressure for large scenes, or extend per-pass packet reuse in a bounded way without caching GPU submissions. Keep strict dirty generation and no unsafe pointer reuse. Once feature work is complete, update `tests/render/model_batching_policy.cpp` and the real Metal `static_packets` fixture, let CI run to completion, and benchmark a real urban scene.


---

## Stage 12 — targeted cache invalidation for entity and asset lifetime (2026-11-19)

**User direction:** prioritize remaining features, defer test execution and new test-writing to the later test phase. **Branch:** `perf/incremental-static-model-cache` (PR #34 Draft), based on main `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` at the most recent ancestry check. **Code HEAD before this handoff:** `fa435954a9f84ca45d002de6a8eb26e7d5d6a7e0`.

### Feature commits / paths
- `b1c025c8b8f23159a0b60cd5e3b1e10f7fd2499d` — `engine/render/Renderer3D.h` publishes two explicit targeted invalidation APIs:
  `void invalidateStaticModelCache(uint64_t persistentId)` and
  `void invalidateModelCachesFor(const Model* model)`.
- `cb88bd3c66f60ae413d1d0c98763de3ad550f613` — `engine/render/Renderer3D.cpp` implements exact-ID removal for a single stable draw identity, and model identity removal for all transform/packet cache entries using the given `Model*`. Both update the retained packet byte counter and eviction diagnostic; no GPU resources are owned or released by either cache. Code never dereferences the potentially soon-to-be-unloaded model.
- `c60022be34f65894b348bee1be1bd5d1048d90e8` — `engine/render/EcsStaticModelRender.h` adds `invalidateEcsStaticModel(renderer, entity, stableKey)` as a public native ECS presentation helper, for an entity despawn or asset replacement using the same stable namespaced ID callback as `drawEcsStaticModels`.
- `a3726479aab974fba76e61ca6230a0abf3ad3a02` and `fa435954a9f84ca45d002de6a8eb26e7d5d6a7e0` — usage contract, memory and lifetime notes in `docs/geometry-performance.md` and `AGENTS.md`.

### API / lifecycle contract
Call either targeted invalidation on the **renderer submission thread, between drawModel calls**, before the corresponding model's memory or GPU buffers are released or an ID is reused. Already enqueued `ModelItem` data for the current frame is not retroactively removed; ensure ownership extends through any frame containing queued draws. Full `invalidateStaticModelCaches()` remains appropriate for whole-scene reload. Per-ID invalidation does not invalidate unrelated models sharing one GPU geometry pool page; per-model invalidation does not dereference model contents. The adapter never mutates authoritative simulation, and the ECS component remains data-only with integer asset/revision identifiers.

Example native integration:
```cpp
// Before removing the visual component/entity, while its stable key is known:
render::invalidateEcsStaticModel(renderer, entity, stableKey);
world.destroyLater(entity);

// Before a streaming cache releases a Model shared by multiple entities:
renderer.invalidateModelCachesFor(model);
render::releaseModel(device, *model);
// Keep source Model alive through any draws already queued in the active frame.
```

**Compatibility:** No shader/gfx changes, ECS authoritative or serialized state modifications, genre checks or newly enabled defaults. Only new render APIs/helper and docs. No change to Metal ICB.

### Actual validation (do not overstate)
- GitHub connector fetched current renderer headers/implementation and ECS adapter, then wrote all feature/documentation commits successfully. **CMake/compile NOT RUN; CTest NOT RUN; Metal raster NOT RUN; A/B FPS NOT MEASURED.** Test-writing and execution deliberately deferred by user; earlier lack of local AIVERSE checkout still applies. New APIs have not been exercised in a native runtime.
- Known risk: existing tests from prior stages still assert the old conservative cache policy, and a full real GPU parity fixture is pending. Validate targeted removal, bytes accounting, model reuse, current-scene draw lifetimes, clip-bearing rigid models, node overrides and variants when tests resume.

**Next feature-first step:** further reduce per-view CPU preparation, ideally with safe generation-aware per-model eligibility memoization (without raw pointer-only caching) and camera-independent static draw subpacket representation. Keep material GPU equality exact, support concurrent render viewports without reentrancy assumptions, and do not enable default flags before actual Metal validation. In the later test phase, repair/update `tests/render/model_batching_policy.cpp` and run comprehensive focused CI and real city A/B/A.

---

## Stage 13 — geometry-revision-memoized static asset eligibility (2026-11-19)

**Priority:** features first, tests explicitly deferred. **Repo:** `chromealex/AIVERSE`. **Branch:** `perf/incremental-static-model-cache`, open PR #34 Draft. **Code HEAD before this handoff:** `09a4744023d57cb3b864ab8a2947d4c28618eb6d`. The branch was ahead 85, behind 0 vs current `main` `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` at this checkpoint; no main merge needed.

### Why this feature
The old `staticModelCacheCandidate()` scanned model node runtime-mutable flags and `staticRenderPacketCandidate()` rescanned all nodes, meshes, morph target/joint-palette data on **every instance submission**, including tens of thousands of repeated buildings. These properties are asset-level and should change only when its geometry/hierarchy revision changes, not when each entity's camera, transform or material state changes. This stage shares a memo between transform reuse and persistent packet replay, while preserving live draw-level validation.

### Commits and changed files
- `07738756c6c15416ed871223991a5b7e5f9eda27`: **new** `engine/render/ModelStaticEligibility.h`, CPU-only/genre-neutral `ModelStaticEligibility {transforms, packets}` and `classifyModelStaticEligibility(const Model&)`. Checks finalization, skin/geometry safety, runtime-mutable nodes, invalid mesh references, mesh deformability, morph targets and primitive joint palettes. Models can have clips while a static bind-pose instance remains eligible. No GPU backend dependency.
- `1c10ecb8fabd9730f5594cdafc94a9a9efa1aef5`: `engine/render/StaticModelCachePolicy.h` splits model-invariant classification from **current draw** policies: `staticModelCacheDrawCandidate(d, activeDisplacement, traits)` and `staticRenderPacketDrawCandidate(d, activeDisplacement, bakedLighting, lodDiagnostics, traits)`. Public pure `staticModelCacheCandidate` / `staticRenderPacketCandidate` remain available and compute the same classification directly for standalone callers.
- `daa3fb3914ca9ee9cf19fa09c86ee09153a1f87d`: `engine/render/Renderer3D.h` adds per-renderer `StaticEligibilityEntry` map, `cachedStaticModelEligibility()` declaration, scene epoch, and public `staticEligibilityStats()` (hits/misses/evicted/live). No authoritative ECS or serialized state changes.
- `a6c4edadb306399331797305ece948d655c85fef`: `engine/render/Renderer3DModels.cpp` implements the memo and routes both existing hot paths through it. Each hit verifies `Model*` lookup plus `Model::geometryRevision`, vertex/index GPU buffer handles, node/mesh/bind-matrix/skin/material cardinalities and geometry attachment pointer. A mismatch triggers reclassification. Existing per-instance `kDrawStatic`, nonzero stable ID, live pose, baked sample, displacement, LOD, GPU material-state equality and shadow/camera state are **still checked for every draw**. The memo has an 8192-model hard cap; at saturation newly seen models are classified directly rather than growing without bound.
- `8b873bdcc893e0eca5821e9aec7bcf18a7dd88f6`: `engine/render/Renderer3D.cpp` clears the memo at renderer shutdown, removes its entries on `invalidateModelCachesFor(model)`, and retires entries unseen for 128 scene epochs at 64-epoch sweeps. The existing full cache reset clears the memo too. Per-scene memo stats are reset at `beginScene()`; the memo activates only when either CPU transform cache or CPU packet cache is enabled.
- `e19340be65316e0e0178514e1160ed9b7baf7d16`: `apps/editor/windows/SceneWindow.cpp` adds model eligibility memo hits/misses/live/evictions to Scene → Batching diagnostics.
- `1632df942e445a0b2bd3cf6ddd9bba7396b27035` and `09a4744023d57cb3b864ab8a2947d4c28618eb6d`: documentation in `docs/geometry-performance.md` and `AGENTS.md`.

### API/architecture and behavioral constraints
- The pure classifier returns two **independent** immutable model properties; the renderer memo is a presentation cache scoped to one `Renderer3D`, never gameplay/ECS state, never shared across render threads.
- Eligibility is keyed by `Model*` **only for lookup**, not as proof of unchanged content: geometryRevision, resource handles, cardinals and geometry attachment are also checked. Source asset code editing hierarchy/primitive flags in-place without changing array sizes MUST call `Model::markGeometryChanged()` and may also call `invalidateModelCachesFor(model)`. Release must invalidate before destroying the model; allocator reuse with identical revision/handles is not detectable without explicit owner invalidation. Do not rely on the memo as asset lifetime ownership.
- Default behavior and flags remain identical: `AIVERSE_PERSISTENT_CPU_MODEL_CACHE=1` and `AIVERSE_PERSISTENT_CPU_PACKETS=1` are independent experimental opt-ins, both OFF by default. If both OFF the memo is not queried/updated. Existing direct `ModelDraw::staticCache` still takes precedence. Nothing changes shadow/LOD sorting, GPU buffer format, shader variants, MDI or Metal ICB. Original material/GPU state safety checks are untouched.
- Bounded memory: 8192 memo entries plus small per-renderer high-water scratch; no GPU allocations. Periodic aging tolerates alternating editor viewports, and explicit model invalidation removes entries immediately.

### Validation ACTUALLY performed
- GitHub connector fetched latest main/ref and target feature sources, successfully wrote code + documentation commits. Read back every changed header and implementation; source-level consistency checks showed classifier defined, both per-draw gates present, `Renderer3D::cachedStaticModelEligibility` called on the real hot path, old per-instance full classifier calls removed from it, memo cap and asset invalidation/expiry present, and Scene diagnostics wired. **These checks inspected source strings, not executable code.**
- GitHub Actions at code HEAD `09a4744`: `GPU batching focused regression` pending; `CI` pending; Studio and InputFlow queued. Superseded prior head CI/GPU jobs cancelled by workflow concurrency. None reported successful completion. PR #34 was mergeable Draft, ahead 85/behind 0.
- **CMake compilation NOT RUN; CTest NOT RUN; Metal raster parity NOT RUN; CPU/GPU benchmark NOT RUN.** Tests are deliberately deferred by user. Historical regression fixtures from Stages 10–11 have stale expected eligibility and need updating before the test phase; do not claim that this branch currently passes CI.

### Next actionable feature and test debt
- **Next feature-first task:** avoid repeating expensive GPU material fingerprints across thousands of static instances when they intentionally share an immutable material set, but **only** with an explicit material-owner revision/dirty API and safe lifetime key. Do not memoize by `Material*` or `std::vector<Material>::data()` alone (temporary copies and stable-pointer material mutations are common). Validate GPU texture handle identity and shader-graph revisions before allowing shortcuts.
- Later test phase: update `tests/render/model_batching_policy.cpp` for Stage 10 node override/variant eligibility, Stage 11 clip-bearing rigid bind-pose eligibility and proper finalized bindWorld; add targeted eligibility revision invalidation and stable IDs; run model-policy focused jobs and the actual Metal `static_packets` A/B fixture; then real fixed-camera urban scene A/B/A CPU/GPU measurements. Keep Draft/off-by-default pending these results.

---

## Stage 14 — material source revision and explicit shared owner contract (2026-11-19)

**Branch:** `perf/incremental-static-model-cache` PR #34 Draft, originally from `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee`. At start PR head `71caa3fe15ba6d00f889b8df8d9a0eb3ffd52950`; GitHub compare showed 86 ahead / **0 behind**, so main already included. **Stage 14 code head before this handoff:** `a1ce55fb44c27c5fd4de04ea0d7c77df3c6d4e55`. User explicitly wants **features first, tests later**.

### Implementation and files
- `1f3b006dedccf91986dea15901f368056ba73477`: `engine/render/Model.h` introduces runtime-only `Model::materialRevision = 1` and `markMaterialChanged()` monotonic increment (never zero), independent of `geometryRevision`. This captures in-place changes to material factors, shader graphs, texture bindings and texture content where handle IDs remain unchanged, *when called by the owner*.
- `b5b75cacb07e64043859aaa7afec27a50ef61864`: `engine/render/Model.cpp` bumps material revision on `finalize()`, changes in texture handle bindings during `uploadModel()`, `addImage()`, and `releaseModel()`. Native caller mutations through public vectors outside these methods must explicitly call `markMaterialChanged()`; no write observer was added.
- `e1010dd2fd80e6750028151c495d24b407fa0ca3`: `engine/render/Renderer3D.h` extends `ModelDraw` with optional `materialOwnerKey` and `materialOwnerRevision`, both zero by default. A nonzero stable owner key is scoped within a Model and must represent the same material-set source. A nonzero revision must change on in-place overrides, shader or texture changes, including content updates preserving GPU handles. Supplying only one field is **not** a valid opt-in.
- `8d9d6d5dabb08642e47e9221cb131c28cfef3250`, `a1ce55fb44c27c5fd4de04ea0d7c77df3c6d4e55`: documented contract in `docs/geometry-performance.md` and `AGENTS.md`.

### Compatibility, safety, what is not yet implemented
This stage introduces the versioned **API only** and source revision tracking. It does **not yet** change the existing packet material matching algorithm or introduce a shared material signature memo. All `ModelDraw` owner identity fields default to zero, so current RTS/FPS/native behavior is unchanged. There are no save schema, ECS authoritative state, network or GPU shader ABI changes. Existing `AIVERSE_PERSISTENT_CPU_PACKETS=1` remains opt-in and default OFF. The next stage must actually consume owner identity/revisions in a bounded renderer-side material signature cache, with strict fallback to current exact `staticPacketMaterialStateMatches()` for unversioned draws.

A raw Material pointer or vector allocation address is not a correct identity: memory may be recycled and values can change in place. Model `materialRevision` must be incorporated into the memo key/validation; `geometryRevision` still protects retained geometry/primitive packets. A texture upload that changes underlying pixels behind a stable handle requires a revision, even if the GPU texture index did not change.

### Validation performed
GitHub connector fetched main/ref/PR, read affected files, wrote five code/docs commits successfully. No compile or unit test executed. **CMake: NOT RUN. CTest: NOT RUN. GPU parity: NOT RUN. Mac city performance: NOT MEASURED.** Existing policy regressions already need updates from earlier stages; do not claim they pass. No merged main, PR still draft.

### Next exact step
Implement `Renderer3D`'s **shared versioned material signature memo** with a resource cap, owner key and revision, model material/geometry revisions, and explicit invalidation on model unload/full reset. Reuse one fingerprint for every identical owner across thousands of static instances rather than rehashing them per draw, while unversioned paths continue full exact comparison. Never silently promote legacy RTS `show->materials` unless its owner guarantees revisions. Then write Stage 15 handoff separately; tests remain deferred by user.

---

## Stage 15 — bounded shared material-state memo and packet reuse (2026-11-19)

**Branch / PR:** `perf/incremental-static-model-cache`, PR #34 Draft. **Stage 15 code/doc HEAD before this handoff:** `6500e1b0c64abebae06d2b54e9a6f65d06765975`. Stage 14 added explicit material-owner API/revisions. This stage connects that opt-in contract to the actual renderer. Keep in feature branch; never enable default flags without tests.

### Implementation commits and changed paths
- `676f8b3c27d937b93e0b6c3911547f8525861540`: `engine/render/Renderer3D.h` adds a bounded `SharedMaterialEntry` memo keyed by `(const Model*, materialOwnerKey)`, with owner revision, `Model::materialRevision`, geometry revision, material count, monotonic internal signature serial, last seen epoch, cached resolved-GPU-state hashes, and a min quality threshold for vertex displacement. Each static packet now stores `materialRevision` plus `sharedMaterialSerial`. Adds `sharedMaterialStateStats()` and full-cache reset of owner signatures.
- `540afc342f07b628d500bdee6e4a2fc152d9f6f8`: `engine/render/Renderer3DModels.cpp` implements `cachedSharedMaterialState()`. On new owner revision it computes `gpuMaterialStateHash()` for each material exactly once and memoizes the minimum vertex-displacement graph quality; on same explicit owner/model/geometry revisions and material count it returns the shared serial without recomputing hashes. Caps: 4096 owners, at most 512 materials/owner and 8 MiB of retained fingerprint payload. New owners exceeding limits gracefully use the conservative old path.
- `823177825d8ed0f5433b4659d7964759543ee4ba`: real `drawModel()` integration. When `AIVERSE_PERSISTENT_CPU_PACKETS=1` and stable nonzero owner key, owner revision, packet revision and object ID are supplied, eligible draws reuse the signature serial for packet validation and cached displacement threshold. Packet builds under this contract omit **per-instance** material snapshots and hash vectors, with live material pointers rebound by material index. All unversioned draws still run `staticPacketMaterialStateMatches()` with both actual GPU hashes and exact equality. The packet records source `Model::materialRevision`, so a changed model material revision invalidates even an unversioned packet.
- `27e86a7fa1e04f778831cf775d361187143ec1dc`: `engine/render/Renderer3D.cpp` expires idle owners, clears them on shutdown/whole-scene invalidate, removes a Model's owners via `invalidateModelCachesFor(model)`, and maintains material fingerprint byte accounting.
- `f64fcd9bb6dd8124cba3034fa6c57afab35a8958`: `engine/render/StaticModelCachePolicy.h` extends exact draw signature to material owner key/revision, preventing an old packet from being reused when owner identity switches.
- `2b52034deb41e951bb4510e307e7c23360ff458a`: Scene → Batching shows hits, refreshes, retained owners/bytes and evictions.
- `cc91065ef24e9f970528379ed4af8879e88a9d7e` and `414235b230798fedb088dab2bcdd56b556b3c3c3`: targeted `Renderer3D::invalidateSharedMaterialState(const Model*, uint64_t ownerKey)` API. It removes one owner signature and its retained byte count. Previously built packets miss/rebuild after this owner is reintroduced with a new serial.
- `74cb5478b295c6cd6c0f6d6e976a0c02409ca5aa`: on migration from a per-packet unversioned material snapshot to a shared owner, actually frees prior snapshot vector allocations instead of retaining capacity outside the reported packet budget.
- `75496564ea6763836e52b7141f33824bd1f3ac4a`, `5837d738a9d90c1155f94ceb3b84791052a3509f`, `6500e1b0c64abebae06d2b54e9a6f65d06765975`: docs/geometry-performance and AGENTS contract updated.

### Public API, safety and limitations
- User/native caller opts in via `ModelDraw::materialOwnerKey != 0`, `materialOwnerRevision != 0`, `staticPacketRevision != 0`, stable `persistentId`, and `kDrawStatic`, plus global `AIVERSE_PERSISTENT_CPU_PACKETS=1`. One owner key means the **same immutable GPU material state** across every corresponding draw until its revision increments, independent of pointer or vector allocation location. If two different material states reuse the same owner/revision, that is a caller bug: the explicit contract is not a dynamic material write barrier. There is no safe optimization when the caller cannot promise this; the fallback uses exact GPU hash/equality.
- `Model::materialRevision` must advance on in-place source material, graph, texture bindings or GPU texture content edits; `Model::markMaterialChanged()` is public. Engine upload/finalize/release paths added in Stage 14 bump on known mutations. `Model::geometryRevision` still protects mesh/topology packet lifetime. Material quality is evaluated against the memoized minimum vertex displacement quality for each draw; no shader, GI, LOD, visibility, sorting or GPU submission behavior changed.
- Shared signature payload limited to 8 MiB; material packet payload remains capped at 64 MiB; not GPU memory allocation. Keyed by Model pointer only as a **scoped lookup**, and model removal must call `invalidateModelCachesFor(model)` before Model destruction. Targeted owner removal uses `invalidateSharedMaterialState(model, ownerKey)`. Existing per-object removal uses `invalidateStaticModelCache(persistentId)`. Renderer submission is nonreentrant.
- **Model-owned vs per-instance state:** the fast shared path only activates with explicit owner key/revision. We deliberately did **not** auto-infer ownership from `Material*`, `vector.data()` or model pointer, since mutable native objects and temporary material copies can reuse addresses. Thus old RTS/FPS code without owner revision does not silently acquire a potentially unsafe shortcut.
- Unversioned packet material fingerprint comparison is preserved (not replaced with pointer equality). Model/editor asset reload must continue to invalidate caches.

### Actual checks and test status
- GitHub connector fetched latest PR #34 and source files, wrote all above commits. **Native compilation NOT RUN; CTest NOT RUN; Metal pixel parity NOT RUN; real city CPU/GPU A/B NOT RUN**. User requested **features before tests**. No binary/perf pass is claimed. Existing `tests/render/model_batching_policy.cpp` still contains stale expectations for stages 10/11 and must be reconciled during test phase; new opt-in shared owner path likewise needs explicit revision/mismatch/eviction/multi-instance regressions.
- No changes were merged to main. CI may queue/cancel superseded runs on each feature push. Current cache defaults remain OFF.

### Next actionable feature
Propagate the new owner key/revision through the universal `EcsStaticModelRender` adapter (append fields after old aggregate members to preserve source compatibility), allow generic native projects to share named material sets. Optionally add an explicit ECS owner invalidation helper if the native asset source is hot-reloaded. Do **not** automatically promote existing RTS material copies without a reliable revision source. Append a distinct Stage 16 handoff before any further feature. Once features are complete, update policy regressions and run focused CI and real Metal raster A/B.

---

## Stage 16 — shared material revisions through universal ECS presentation (2026-11-19)

**Branch/PR:** `perf/incremental-static-model-cache`, PR #34 (Draft). **Code/doc HEAD before handoff:** `801c2f63db27cefd1577f7860f68a6ef927ef927`. `main` was `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` and had 0 missing commits at start of these Material State Cache stages. Re-fetch/compare before integration. **Instruction:** features before tests.

### Feature files and commits
- `22b7639393ffb58a9b8bed659c19c3cde161a8e6`: `engine/render/EcsStaticModelRender.h` now appends `uint64_t materialOwnerKey=0` and `uint64_t materialOwnerRevision=0` to the existing four-field, integer-only view component. They are appended **after** `assetKey`, `appearanceRevision`, `flags`, `visible`, so old aggregate initializers remain source-compatible. `visitEcsStaticModels` copies them to `ModelDraw`, enabling identical material-set owners to share the new memo even from generic ECS native games. Existing renderer and model resolution callbacks are unchanged.
- `18ee40f0c9b7630a45e27286dfa52b74600bda8f`: `docs/geometry-performance.md` documents the ECS material-owner bridge and a native example, including how to resolve external material override vectors only in the presentation callback instead of placing C++ pointers in ECS.
- `ac40494482f8f4c89ce07b14753a033cf7688f36`: `AGENTS.md` documents the shared owner revision contract for ECS/genre-independent native projects.
- `801c2f63db27cefd1577f7860f68a6ef927ef927`: `engine/render/Renderer3DModels.cpp` adds the direct `<limits>` include required by `std::numeric_limits<int>::max()` in the Stage 15 memo (header dependency hygiene).

### Usage / compatibility
Native ECS code can add:
```cpp
render::EcsStaticModelRender view{
    assetKey, appearanceRevision, flags, true,
    materialOwnerKey, materialOwnerRevision
};
world.add<render::EcsStaticModelRender>(entity, view);
render::drawEcsStaticModels(world, renderer, resolveModel, stableDrawId);
```
The component remains data-only; `WorldTransform` is converted to a float matrix only at the rendering boundary. For custom material overrides use `visitEcsStaticModels(..., submit)` and set the live `ModelDraw::materials` pointer inside `submit` before calling `Renderer3D::drawModel`. The same owner key/revision must denote identical material state for every draw using it, and the owner must bump revision after native changes (including texture content behind unchanged GPU handles). `Model::markMaterialChanged()` is likewise required for model-owned state edits. **No RTS/FPS genre branches, new authoritative simulation dependencies, or shader/GPU ABI changes.** No automatic enabling: still requires nonzero stable ID, `kDrawStatic`, `staticPacketRevision` and env `AIVERSE_PERSISTENT_CPU_PACKETS=1`. Old four-field initializers and unversioned material hash/equality path remain intact.

### Actual validation and risks
- GitHub connector successfully applied the ECS, doc, AGENTS and include commits to the same PR branch. Source re-fetch inspections confirmed owner fields, versioned memo declaration/use, serial check, conservative fallback and new editor diagnostics are present. **These were static source inspections only.**
- **Native CMake compilation: NOT RUN. CTest: NOT RUN. Metal raster parity: NOT RUN. City A/B CPU/GPU benchmark: NOT RUN.** User explicitly deferred test work. Earlier test fixtures still have stale expectations for static bind-pose clip metadata and node material/variant eligibility. New signature memo has no executed regression coverage yet. Do not claim build or GPU performance success.
- Correctness depends on explicit owner revision propagation, no unsafe use-after-model-unload, and in-place `Model::markMaterialChanged()` where applicable. Keying by Model pointer is only a scoped identity inside the renderer; `invalidateModelCachesFor(model)` must occur before asset destruction. `invalidateSharedMaterialState(model, ownerKey)` is provided for individual material-owner replacement. Never use owner version fast-path for mutable sources without reliable generation tracking.

### Exact next feature/test handoff
Feature-first next work: investigate broader static material batching across distinct rigid models only where shader, resolved resource bindings and primitive geometry are genuinely compatible; do not force glTF nodes/mesh identity together or break culling/LOD. Alternatively implement owner-driven dirty events to retire changed static packet templates rather than relying only on sampling on draw. Once feature scope is declared complete, update tests and run focused CTest/Metal A/B + large city A/B/A with explicit CPU frame time and memory measurements. Leave PR Draft and cache flags OFF by default until confirmed.

---

## Stage 17 — opt-in shared GPU-page batching across distinct static Models (2026-11-19)

**User direction:** Implement features first; tests later. **Branch:** `perf/incremental-static-model-cache`, PR #34 (Draft); continue here, not directly on `main`. **Code/doc HEAD before this handoff:** `74469bdb0c2836cd384d1bf7a626378e381da58f`. At start, GitHub reported current `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee` and feature branch ahead 110 / behind 0. A new main merge was not necessary; reconfirm before integration.

### Why / relationship to existing work
The renderer already had optional cross-model CPU indexed MDI when different models happen to share the GPU buffer and identical resolved material state; `AIVERSE_MDI_GEOMETRY_SORT=1` could encourage such grouping but sorted dynamic and static model items together. This stage adds a **narrow, independently gated static-only GPU geometry-page grouping policy** and concrete submitted-mixed-model statistics. It does NOT merge CPU mesh assets, modify GPU shaders or claim to create a new indirect backend. Existing MDI, instancing and direct-draw fallback remain authoritative.

### Feature changes (commits and paths)
- `4ab00f24659dabf8035c0a4fee25653fc3c5851c`: new `engine/render/StaticCrossModelBatchPolicy.h`, pure `staticCrossModelPageCompatible(const Model&, const Model&)` requiring **different Model identities**, one exact shared `GeometryPoolPage`, valid/equal GPU vertex and index handles, and no cluster geometry. It does not infer geometric compatibility from similar textures/materials.
- `ef428c9eadb6712477945a71264c5dbd65881727`: `engine/render/Renderer3D.h` adds `ModelRenderStats::staticCrossModelSubmissions`, `staticCrossModelCommands`, `staticCrossModelItems`, which count successful CPU MDI groups containing at least two Models, not mere eligible items.
- `99c9ad6110fd3085bf59f77f03a294d9e68ff9fd`: `engine/render/Renderer3DModels.cpp` adds an opt-in static shared-page sort in `renderModels(phase)`, limited to opaque/main-view and sun-shadow phases, with CPU MDI supported and enabled, not auxiliary/reflection, render-safe, draw-call debug or GPU-authored path. Only rigid `kDrawStatic`, opaque, no shader graph or GI, no skin/morph/baked animation, no material-batch role, no GPU-authored LOD and no active LOD crossfade are eligible. The normal pass selector already independently performs camera and sun-shadow visibility, occlusion, reflection and LOD selection **before** sorting. Sorted pooled candidates cluster by exact GPU material grouping, VB/IB handles, draw flags and original primitive; other ModelItems use their original ordering. CPU MDI group preflight checks same shared GPU page for any distinct Models, plus all legacy shader/material/pipeline/buffer/flags checks; insufficient unique indexed commands or incompatible items fall back to original instancing/drawIndexed path.
- `f59da68389284b968c5d99c8e940e7b8d14ec2c2`, `4a1cb035d89b5c7e141f667f9d8c5be0890adb33`: `Renderer3D.h` and `Renderer3D.cpp` add independent opt-in `AIVERSE_STATIC_CROSS_MODEL_BATCHES=1`, read at scene initialization. Default is OFF, independent of `AIVERSE_PERSISTENT_CPU_PACKETS`; does not enable GPU ICB. Requires normal MDI support and respects `AIVERSE_MULTI_DRAW_INDIRECT=off` and `AIVERSE_RENDER_SAFE=1`.
- `beda2ec69df08de14d2fe10f12fda651a0bdd350`: `Renderer3DModels.cpp::drawModelIndirectBatch` increments the new stats **only after a successful indexed-indirect GPU submission**, when the group contains genuinely distinct Model pointers that share an identical geometry page and meet the strict rigid/static prerequisites. Commands count uses final indirect command vector size.
- `bc9b509cf34ccb8fd140c7813ec52c43a7a93b7a`: `apps/editor/windows/SceneWindow.cpp` shows mixed-model CPU MDI submissions, commands and items under Scene → Batching → Instancing and indirect details when the opt-in flag is set.
- `0ef91cf25c817ef362e24b8f6b548093f7a33499` and `74469bdb0c2836cd384d1bf7a626378e381da58f`: feature policy and safety contract in `docs/geometry-performance.md` and `AGENTS.md`.

### API/ownership and limitations
- Public pure policy: `staticCrossModelPageCompatible(modelA, modelB)` in shared `engine/render` headers, with no FPS/RTS dependency. Render usage: launch editor/native game with `AIVERSE_STATIC_CROSS_MODEL_BATCHES=1`, while keeping `modelMultiDrawEnabled` true and an indirect-capable backend. Cross-model batching can group static ModelItems even if the optional persistent CPU *packet* cache is OFF. All original model draw interfaces, submitted GPU VB/IB/firstIndex/baseVertex/firstInstance offsets and per-object transforms remain unchanged.
- The existing GPU geometry pool is mandatory for distinct Model grouping; unrelated standalone meshes do not magically share one index buffer. This avoids false grouping on matching Material assets. Pipeline/material equality remains exact, with optional existing `AIVERSE_MDI_INSTANCE_PALETTE` behavior for PBR factors when already enabled.
- Only opaque/shadow phases reorder; blended sorting stays back-to-front, and reflected/auxiliary passes keep their original path. Every source performs independent current-camera/shadow culling and LOD decision before joining a group; LOD crossfade/fading dynamic objects are excluded.
- Vulkan indexed-indirect with nonzero pooled base vertices is explicitly ineligible for cross-model sorting and continues through the existing direct path. Metal's existing `AIVERSE_METAL_MDI_MIN_COMMANDS` threshold (default 16 commands) still applies: no unconditional MDI for tiny batches. GPU ICB is unchanged and excluded from this policy. Cross-model stats measure actual MDI submissions, not guaranteed draw-call or frame-time savings.

### Actual validation / test debt
- GitHub connector fetched current PR/main/branch, inspected real render-pass sorting, material grouping, existing MDI command encoding and stats, then wrote source/docs commits. Read back each changed file. Static source checks confirmed pure shared-page policy is included by the renderer; flag is wired to `beginScene`; sorting and shared-page compatibility gating are present; actual MDI success updates counters; the editor and docs reference the same feature. **These are source inspections only**.
- **CMake build: NOT RUN. CTest: NOT RUN. Metal raster parity: NOT RUN. GPU CPU A/B: NOT RUN.** No measured frame-time or draw-call improvement has been demonstrated. User requested tests later; do not claim code passes CI. Existing regression policy assertions remain stale for earlier Stage 10/11 eligibility changes and must be reconciled in the future test phase.
- The existing code already had optional mixed-model MDI. The newly added value is the conservative static-only resorting and guaranteed pooled-page compatibility, not a new engine-level geometry merge or default GPU-driven renderer. Keep PR Draft, flags OFF by default.

### Next exact action — features before tests
- Assess whether static cross-model groups can be planned more efficiently for very large cities: use inexpensive per-page/per-material candidate counts to avoid sorting/gathering when fewer than the indirect command threshold exists, without mixing geometry/alpha/GI states. Alternatively implement explicit per-source dirty notifications or a persistent *CPU grouping signature* per shared geometry page, always retaining independent camera and LOD selection.
- Do not introduce genre-specific batching rules; document each next phase in this same handoff. Once the remaining features are complete, update `tests/render/model_batching_policy.cpp`, add a real Metal shared-page mixed-model raster fixture with multiple models/Lods/shadows, and run macOS CTest + GPU screenshot parity and a fixed-scene A/B/A benchmark.

---

## Stage 18 — page-aware preflight before cross-model static CPU MDI sorting (2026-11-19)

**Priority:** implement features first, tests later (explicit user instruction). **Repo:** `chromealex/AIVERSE`. **Branch:** `perf/incremental-static-model-cache`; open PR #34 remains Draft. **Code/documentation head before this handoff:** `9936740f1be47b1350c17daeda5e5b493f17e540`. At the beginning of this stage, GitHub main was `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` and the feature branch had `ahead=120, behind=0`. Main was already fully included; no merge or main mutation was needed. Check fresh ancestry before subsequent work.

### Why this stage

Stage 17's `AIVERSE_STATIC_CROSS_MODEL_BATCHES=1` promoted every conservative rigid static pooled candidate to a special sort, even when the selected main/shadow view had only one model or too few unique indexed primitives to meet the backend's MDI command threshold. Reordering and cross-model preflight then wasted CPU and could not create a mixed-model indirect submission. Stage 18 adds a bounded **necessary-condition preflight** after actual per-pass visibility/material-state classification and before this extra sort. It does **not** change GPU command encoding, mesh data, material equality, LOD/culling or render graph passes.

### Implementation commits and changed files

- `a6db6ef70647b4bb080277fa8365ecb77d023fb6` and `e1b24f80c44ee35fea7b7f50d87e598f6651658e`: `engine/render/StaticCrossModelBatchPolicy.h` adds `StaticCrossModelPreflightKey`, a small hash, and `StaticCrossModelPreflightGroup` to gather distinct Model identities, submitted item count and unique primitive candidates. `opportunity(threshold, instanceLimit)` requires at least two Model pointers, enough distinct primitives/visible items, and enough instance slots to meet the same backend minimum. This is conservative, never an automatic draw submission.
- `d39170fa956f23ae54776c392067917eae063c38`: `engine/render/Renderer3D.h` publishes `ModelRenderStats::staticCrossModelPreflightGroups`, `staticCrossModelPreflightReady`, `staticCrossModelPreflightRejected` and `staticCrossModelPreflightItems` separate from successful `staticCrossModelSubmissions/Commands/Items`.
- `67b2eaf3575c75ae0b79e43275f4f310a141e4c9`: `engine/render/Renderer3DModels.cpp` preflights `order_` only after it contains actually selected model items for a particular pass and `materialGroup` IDs have been resolved via exact GPU state comparisons. Unique candidate key includes shared geometry page, exact material-group ID and flags. On Metal, uses `indirectCommandThreshold()` (default 16, clamp 2–32); other MDI backends use the existing four-command threshold. Only preflight-ready indices go into a per-index `staticCrossCandidates` bitmap. If no ready groups, additional cross-model sorting is skipped and original material/primitive order is used. Ready static items sort by exact GPU geometry page and existing shader/material flags. The existing MDI group builder **still** performs strict GPU handle/material/pipeline/primitive-run checks; opaque/shadow visibility and LOD were decided independently before this stage.
- `301bb4d9fea00d556710c869de957ccacbb317b5`: fixes a subtle per-pass error: readiness is decided by **local** `readyGroupsThisPass`, not accumulated renderer statistics that include earlier shadow/main passes.
- `8c3ed5fe8372d37252cd58abfdb57c107d638a65` and `bcc15096691462e0637383f3bd685a001afb9318`: the key additionally incorporates exact GPU vertex/index buffer IDs. This avoids unnecessary preflight promotion for Models with distinct buffers even if a page pointer or material state coincides.
- `616b8dd0bffe43cf5acc527a1e2623ee3d929953`: replaces each group's heap-allocated unique-primitive vector with a fixed `std::array<const Primitive*, 32>` and distinct count. At most the 32 unique primitive identities needed for current MDI thresholds are sampled; repeating one primitive across thousands of instances still counts as only one command candidate.
- `516bf6dd7bfbd0f6a14c637e0d5ca6e48fd4655f`: cap the temporary preflight map to **2048 groups per pass**; on highly fragmented scenes exceeding the budget, abort the additional sort and use ordinary rendering rather than allocating an unbounded map.
- `6e63349be4379d350d183ccc300d70cfcf675fdc`: `apps/editor/windows/SceneWindow.cpp` reports ready/rejected/total preflight groups and selected visible items in Scene → Batching. These are *potential* groups; successful submitted GPU MDI remains separately counted.
- `d6f4105f83aae0d1ff49cafef1dd62a56ecddf9e`, `9936740f1be47b1350c17daeda5e5b493f17e540`: `docs/geometry-performance.md` and `AGENTS.md` updated with ownership, API/flag, memory constraints and fallback.

### Eligibility, behavior and native API

No new public call is required: enable existing `AIVERSE_STATIC_CROSS_MODEL_BATCHES=1` opt-in, plus standard CPU MDI support. The special sort runs only on the selected main opaque/sun-shadow view, not transparent, reflection, auxiliary, safe or GPU-authored paths. The separate instance material palette experiment keeps its own sorter; Stage 18 deliberately disables special cross-model preflight while `paletteSort` is active. Each group is keyed by `GeometryPoolPage*`, material-group ID, flags, and GPU VB/IB IDs; a material group is already derived from `gpuMaterialStateHash` with `equivalentGpuMaterial` checks, never material pointer equality alone.

No global scene visibility/LOD packet is created. Per-object camera and shadow visibility, fade, LOD, occlusion and sort depth still determine current `ModelItem` eligibility. A `ready` group is not evidence the final MDI builder accepted a submission: after sorting, a batch may still have too few contiguous distinct commands, incompatible pipeline conditions or fail the backend draw. The regular renderer fallback and existing MDI routes remain unchanged. Model source asset identity and GPU page lifetime still follow Stage 12 invalidation requirements. This code is genre-independent and only manipulates renderer presentation data.

### Validation ACTUALLY performed

- GitHub connector read PR #34/current main/feature compare and existing MDI sorting/command submission code; wrote all Stage 18 feature and documentation commits successfully.
- Re-fetched policy, renderer, public stats and editor source after changes. Read-only static checks confirmed the fixed 32-primitive array, key including GPU page/material/flags/VB/IB, threshold computation, 2048-group cap, per-pass local readiness, eligible-item bitmap, page-aware sorting, existing exact MDI preflight and editor diagnostics. **These are code/source inspection checks, not executable tests**.
- **CMake compile: NOT RUN. CTest: NOT RUN. GPU/Metal raster parity: NOT RUN. CPU/GPU A/B: NOT RUN.** Tests remain deferred by explicit user request, and there is no active local AIVERSE checkout here. GitHub Actions queued/cancelled earlier heads due ongoing feature commits, so do not claim any CI PASS.
- Main branch was not modified. Existing packet/transform caches, shared material owner revisions and CPU MDI/Metal ICB defaults are unchanged (experimental options OFF).

### Next exact step

Prioritize remaining features over tests per user. Consider an adaptive CPU MDI preflight/dispatch policy based on backend command count, per-pass build cost and actual prior-frame submission statistics; do not cache visibility, LOD or results across different camera views. Keep per-page/material safety and the original exact comparison. Alternatively add a bounded per-page *current-scene* grouping plan, with invalidation on streaming, material or GPU buffer revisions, only if profiling demonstrates a repeatable CPU benefit. At feature completion, reconcile stale model batching test eligibility assertions from stages 10–11, add real mixed-model/shared-page GPU fixture (including shadows, LOD and page lifetime), run focused CI and matched large-city A/B/A. All future stages require separate implementation and handoff commits.

---

## Stage 19 — adaptive CPU MDI vs ordinary instancing for static pooled groups (2026-11-19)

**User priority:** features before tests. **Repository:** `chromealex/AIVERSE`. **Branch:** `perf/incremental-static-model-cache`. **PR:** #34, still Draft and **not merged**. **Code/docs head before this handoff:** `c53f09811090b126d52fc9a40f0ca5c61172a39b`. At stage start current `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, branch ahead 133, behind 0. No main merge necessary. Recompare before any subsequent change because other agents work concurrently.

### Goal

After Stages 17–18, static rigid models sharing one GPU geometry page could be sorted into cross-model CPU MDI groups, with a page/material preflight requiring multiple distinct primitives. But command count alone cannot decide whether **CPU command scanning, packing, state binding and backend API submission** costs less than regular instancing/direct draws. Stage 19 adds a bounded experimental *feedback* selector that can turn off the special static pooled sort when the observed CPU cost is high; it never bypasses the original strict material, geometry, view culling, GPU support, LOD and indirect submission checks. It is not a GPU-time model or a proven speedup.

### Implementation commits and paths

- `b5a3ece2377eef4d929d36c409539d1d5fc710a8` — **new** genre-neutral `engine/render/CpuMdiAdaptivePolicy.h`: `CpuMdiAdaptiveFeedback` stores CPU cost EMA per distinct indexed primitive command, warmup/sample counters, successful submission count and qualifying pass opportunities. Discards the first two attempts as pipeline warmup. After eight usable samples, if the observed CPU API cost exceeds the user-configurable threshold, it skips extra pooled MDI grouping, except once every 32 qualified passes to allow recovery (probes). Signal is a bounded 15% exponential moving average, not a GPU timestamp.
- `a2d22cf55997377445c64f9dfe4b6320255066ea` — `engine/render/Renderer3D.h` adds per-renderer `std::array<CpuMdiAdaptiveFeedback,2>` for sun-shadow pass 0 and opaque main pass 1, runtime opt-in flag and budget, readonly getters, plus `ModelRenderStats` counters for skipped/probe passes and measured attempts.
- `14b5631d4be786a9eb2b36d6de3cf7de464581cc` — `engine/render/Renderer3D.cpp` reads `AIVERSE_CPU_MDI_ADAPTIVE=1` and `AIVERSE_CPU_MDI_MAX_US_PER_COMMAND` (default 25.0, valid 1.0–1000.0), activates feedback only when `AIVERSE_STATIC_CROSS_MODEL_BATCHES=1` is also set, resets feedback on renderer shutdown. The experimental budget is not benchmark-derived.
- `62f1f0f718d3a86953da0f5a648dfc83c9b3a1d7` — `engine/render/Renderer3DModels.cpp` checks feedback **after** page/material/view preflight reports qualifying groups for that pass and **before** promoting candidates to special sorting. If too costly, the pass retains its original ordering. Each qualifying pooled MDI attempt then measures CPU wall-clock from compatible-run scan through `drawModelIndirectBatch` (including command packing, uniform/pipeline binding and backend call), normalizes by actual unique primitive runs and updates the per-pass EMA. It distinguishes real MDI submissions using the pre/post `modelStats_.multiDrawSubmissions` value instead of treating a fallback draw as a successful GPU submission. No additional clock reads are added when adaptive is off, aside from the renderer's pre-existing MDI scan timing.
- `045a01c453c42703c1c45969bd4a0dcabf4ad7ff` — safe fallback refinement in `Renderer3DModels.cpp`: when adaptive policy declines a ready static pooled group, **only eligible static pooled items** skip the CPU MDI attempt and proceed to the existing ordinary instancing/direct batching logic. Unrelated nonstatic/nonpooled MDI candidates retain their original path even if an adaptive static pass falls back.
- `12b5f7bac6316535181dc820c02b953df6c48c60` — `apps/editor/windows/SceneWindow.cpp`: Scene → Batching → Instancing and indirect details now displays shadow and opaque average CPU microseconds per unique command/sample count, configured budget and skipped/probe passes/measured attempts. Actual submitted mixed-model MDI statistics remain distinct.
- `8022c91432bdd77635e5c4e634030c3ece03b6f2` and `c53f09811090b126d52fc9a40f0ca5c61172a39b` — `docs/geometry-performance.md` and `AGENTS.md` document the opt-in contract, heuristics, instrumentation scope, fallback/probing, and integration rules.

### API, safety, expected behavior

No new ECS/gameplay component, serialized format, GPU shader code, or platform-specific API. All batching decisions remain inside the common renderer. The feature only activates when BOTH `AIVERSE_STATIC_CROSS_MODEL_BATCHES=1` and `AIVERSE_CPU_MDI_ADAPTIVE=1` are set, and only where CPU indexed-indirect is already supported and allowed by the existing `AIVERSE_RENDER_SAFE` / `AIVERSE_MULTI_DRAW_INDIRECT` flags. The optional budget `AIVERSE_CPU_MDI_MAX_US_PER_COMMAND` defaults to 25 microseconds per unique CPU MDI command and rejects nonfinite/out-of-range input (1–1000). These values are experimental; user must tune with actual measurements.

After ordinary per-object camera/shadow culling, occlusion, LOD selection, material equivalence and strict page-aware preflight, adaptive feedback decides whether static pooled items enter *extra* cross-model grouping or use ordinary instancing. It warms up on two attempted MDI submissions, collects eight normalized observations before enforcing the budget, then probes one full qualified pass per 32 when over budget. Per renderer and per view-pass state is retained across frames to avoid mixing shadow/opaque CPU costs; it is reset on renderer shutdown. GPU ICB and other CPU MDI paths are untouched, as is Stage 18's 2048-group/32-primitives preflight bound.

**Cost semantics:** The signal measures time spent in the renderer's CPU MDI scan/command submission path, including backend call overhead, NOT GPU execution time, effective frame time, power usage, or a true direct-vs-MDI A/B. An over-budget candidate is a heuristic reason to skip expensive pooled grouping, not an authoritative proof that instancing is faster. A new workload, shader warmup or backend can change the result; periodic probes prevent permanent starvation. Even an under-budget candidate still must satisfy all original primitive, material, renderer state and backend constraints and may fall back later.

### Actual validation

- GitHub connector fetched PR/main/feature comparison, source of the existing CPU MDI pipeline and previous page preflight, then successfully committed code and docs. Re-fetched all changed source files and performed read-only consistency checks: pure feedback helper, two independent pass states, explicit double-opt-in and bounded numeric config, per-pass preflight before feedback, static-only ordinary-instancing fallback, actual submission delta and editor cost diagnostics are present. This is **static source inspection only**.
- **CMake compilation: NOT RUN. CTest: NOT RUN. Actual Metal raster parity: NOT RUN. CPU/GPU FPS A/B: NOT RUN.** User explicitly requested all tests later. The project is not checked out locally in this environment. No measured throughput improvement or successful CI result is claimed. The prior policy test fixtures have known stale assumptions from Stages 10–11.
- Experimental flags remain OFF by default and PR stays Draft. The shared GPU page/Material* lifetimes and revision invalidation contracts from Stages 12–18 remain mandatory. No changes merged to main.

### Next feature-first step / eventual test debt

For continued features, consider refining per-page cost feedback so heterogeneous pooled city scenes do not contaminate one pass-level EMA. This must be capped and revision-aware (no unbounded map keyed only by stale GPU handles). Or further reduce per-instance CPU grouping work in the shadow and opaque pass with a bounded current-frame workspace. Do not introduce genre-specific code, runtime GPU shader changes or default-on experiment without parity evidence.

When the user authorizes the later test phase: update `tests/render/model_batching_policy.cpp` eligibility expectations from Stages 10/11, add coverage for versioned material owner and Stage 18 page-aware thresholds, separate adaptive CPU MDI unit-policy tests and real Metal mixed-model off/on screenshot comparisons, measure same-camera CPU and GPU frame-time OFF/ON/OFF, and confirm shadow/opaque cross-pass independence. Preserve the detailed handoff after every stage.

---

## Stage 20 — per-geometry-page adaptive CPU MDI feedback (2026-11-19)

**User priority:** features before tests. **Repository:** `chromealex/AIVERSE`. **Branch:** `perf/incremental-static-model-cache`; PR #34 stays Draft. **Implementation/docs HEAD before this handoff:** `32bdaf3c6256fe5bea8c3e466aaa38374747ceaf`. At the start of Stage 20, `main` was `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, feature HEAD was `e3daa12f32a2a58fdab8435ea338a70e8b3664ee`, ahead 142, behind 0, no merge needed. Refresh before later integration; other agents may update main.

### Why

Stage 19 retained one adaptive CPU MDI EMA per render pass (shadow/opaque). A single expensive pooled geometry page could set the shadow/opaque average over its budget, globally turning off all extra pooled cross-model sorting in that pass — including other pages whose indirect commands were inexpensive. Stage 20 makes the admission decision per **live shared geometry page and render pass**. The existing global EMA is retained for diagnostics only, never gating.

### Code and documentation commits / changed paths

- `2b9c43cd0f711a37b2ab7fe10c67981acd23f457` — `engine/render/CpuMdiAdaptivePolicy.h`: adds `CpuMdiPageKey` and hasher, using `GeometryPoolPage*` plus GPU vertex/index buffer IDs. This is a **lookup key**, never sufficient alone to prove page lifetime.
- `532c2a1126357e2db4f6aa2f309ca43ba1c5bbe3` and `608d7d9e79ce16ae0eff56b5a2eda0e7b787d268` — `engine/render/Renderer3D.h`: adds `CpuMdiPageEntry` containing a **weak_ptr to the actual GPU page**, two independent feedback states (`byPass[0]` shadow, `byPass[1]` opaque), scene epoch, cached per-pass decisions, and a bounded `unordered_map`. Adds `CpuMdiPageStats` public diagnostic getter (live, reused, created, evicted, untracked, over-budget shadow/opaque). Adds skipped/probed group counters. Whole-scene static cache invalidation clears page feedback and pass aggregates.
- `b73a250b3801c381805942aca9ca661c2748e013` — `engine/render/Renderer3DModels.cpp`: implements `cpuMdiPageEntry(const Model&)`. A hit checks `weak_ptr::lock() == model.geometryPoolPage`; a new allocator page at the same address and recycled VB/IB handles cannot inherit old feedback. Cache capped at **256 tracked GPU pages**; on saturation, newly seen pages are *untracked* and retain ordinary CPU MDI (never a speculative global instancing fallback). No GPU resources are retained by feedback.
- `205ead97e26214ea26a9488fc0f3b8c006f421ae` — `engine/render/Renderer3D.cpp`: resets page state on shutdown, increments the page epoch on `beginScene` only while the adaptive path is enabled, and prunes expired/idle feedback entries every 64 scene epochs (after 128 unseen epochs). Render-only; ECS simulation and save data unchanged.
- `f900c03dfd1397d6012a4d90f80375117309adb7` — `engine/render/StaticCrossModelBatchPolicy.h`: adds a `permitted` flag for preflight-ready material/page groups, separate from the original `ready` threshold decision.
- `7ad887986bca606dd2897654720c6acf6d148269` — `engine/render/Renderer3DModels.cpp`: replaces the Stage 19 **pass-wide** `adaptivePooledFallback` switch with a bounded per-visible-item decision bitmap: 0 = ordinary, 1 = page permitted for cross-model MDI, 2 = page over-budget and assigned to ordinary instancing/direct drawing. Each preflight-ready material/page group consults its source page's own feedback, **at most one `permit()` call per geometry page, scene epoch and render pass**. Hence page A can fall back while page B remains in MDI during the same pass. Only permitted ready groups are promoted to special pooled sorting; only declined ready groups skip their CPU MDI attempts. Unrelated MDI and GPU ICB paths are unchanged. If every page declines, the extra sorter is disabled but each declined item's fallback remains active.
- `58f2929c8ca5419e7f5d36ef3d1589e5c381e0c8` — `Renderer3DModels.cpp`: measures CPU scan/command packing/submission attempts on the originating page and updates **that page's** shadow/opaque EMA. The existing pass-wide EMA is also updated, **for display only**. A failed backend submission is not counted as a successful MDI; the existing indirect fallback remains authoritative.
- `2661cbbeea594eb00e2ef9f197fa323e7d6e12b9` — `apps/editor/windows/SceneWindow.cpp`: shows page cache occupancy and churn, shadow/opaque over-budget page counts, selectively skipped/probed groups, plus diagnostic-only aggregate CPU costs in Scene → Batching.
- `cf54352a70a0d3521cdfae0e416e4cbad348e3df`, `32bdaf3c6256fe5bea8c3e466aaa38374747ceaf`: `docs/geometry-performance.md` and `AGENTS.md` updated with Stage 20 policy, lifetime contract and debugging guidance.

### API, scope and invariants

No new public ECS components, scene formats, GPU shaders, GPU buffer structs or backend features. The same two experimental flags enable adaptive pooled CPU MDI: `AIVERSE_STATIC_CROSS_MODEL_BATCHES=1` and `AIVERSE_CPU_MDI_ADAPTIVE=1`; budget remains `AIVERSE_CPU_MDI_MAX_US_PER_COMMAND` (default 25.0 us per unique command, 1–1000). **All OFF by default.**

A page's identity is the combination of its actual shared `GeometryPoolPage` ownership and current GPU VB/IB allocations; `weak_ptr` prevents unsafe feedback reuse across streaming page lifetime boundaries. The map never takes ownership of a page. Material state still derives from exact resolved GPU comparison, and the preflight still requires two distinct source Models, adequate unique indexed command counts, rigid/opaque compatibility and independent per-view culling/LOD. A material group on a page that fails preflight is unaffected; only **ready** groups are subject to page feedback. A page applies one decision per scene/pass regardless of how many material groups it contains, and after 8 useful local samples can decline until its next one-in-32 page probe. Untracked pages continue existing MDI; they cannot suppress known-good tracked pages. Global pass feedback is purely diagnostic; users must not read it as a model selecting one mode for the whole scene.

`Renderer3D::invalidateStaticModelCaches()` clears the page map, and renderer shutdown fully resets it; normal model unload relies on weak ownership expiry plus periodic pruning. If multiple models still share a page, one model's unload should **not** erase the page's useful cost history. If page GPU handles change, a different key and fresh feedback are used. Renderer submission remains nonreentrant and presentation-thread-only.

### Verification ACTUALLY performed, risks

- GitHub connector fetched current PR #34, main and branch ancestry and existing Stage 19 policy; wrote the feature and documentation commits. Read back all changed source files. Source consistency checks confirmed key/weak lifetime, 256 entry cap, per-pass feedback states, once-per-scene/pass permit, explicit per-item mode 1/2, MDI allowed only for permitted groups, declined-item fallback only for that page, page-local observation, and UI counters. **Source inspections only**; not compiled or executed.
- **CMake: NOT RUN. CTest: NOT RUN. Metal pixel parity: NOT RUN. CPU/GPU FPS or latency A/B: NOT RUN.** User explicitly deferred tests. Do not claim performance improvement or validated raster correctness. Other feature stages introduced stale model batching test assertions; future test agent must reconcile them. Current PR remains Draft; all experimental flags remain off by default.
- Known limitations: CPU EMA is a heuristic for command preparation, not GPU time or direct-vs-indirect A/B. A page's different material groups share feedback intentionally; a large heterogeneous page may still warrant finer grouping if actual measurements justify it. At cap, untracked pages use original MDI and remain untracked until entries age out; this avoids unbounded memory or false suppression. No persistent scene visibility/LOD snapshot is stored. Weak page identity does not replace source asset invalidation contracts for model/primitive packet pointers.

### Next precise work (features first)

Possible next work: make per-page candidate/feedback workspace reuse capacity across passes, or selectively defer the costly second preflight pass on known-good page/material state while still regenerating current visibility and LOD. Do not add hidden persistent GPU state or compromise source lifetime. When user authorizes testing, repair stale model-policy expectations (stages 10/11), add CPU-only per-page independence/lifetime/probe tests and real Metal mixed-page raster parity, and benchmark same-camera dense scenes OFF/ON/OFF with separate CPU/GPU timings.

---

## Stage 21 — reusable per-pass cross-model preflight workspace (2026-11-19)

**User direction:** features before tests. **Repo:** `chromealex/AIVERSE`. **Branch:** `perf/incremental-static-model-cache`; **PR:** #34 remains Draft, **not merged**. **Code + AGENTS/doc head prior to this handoff:** `56ffe42bd45f5e579ecb779a9dd95a53ddc0b8f1`. **Main reconciliation:** At start of Stage 21, `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, feature head `a1fdf77fd6582370a11cf390563750f58fe870b2`, GitHub compared `ahead_by=154, behind_by=0`. So latest main was already included, no merge needed. Recompare before another agent writes.

### Problem and architecture

Stage 18/20's page-aware preflight allocated a **new `std::unordered_map<PreflightKey,PreflightGroup>` and dynamic per-item mode vector each shadow/opaque render-pass**. Clearing/deleting the map after the pass invalidated all bucket/node allocation work, and promoting preflight-ready items scanned visible objects a second time, recomputing eligible-static predicates, geometry page/material hash keys and map lookups. On large cities with multiple material groups this could consume CPU even when actual MDI was subsequently rejected.

Stage 21 replaces that temporary map with a renderer-owned, genre-independent `StaticCrossModelPreflightWorkspace`, with an exact-key, open-addressed directory and per-item group-index scratch. Storage is **reused across passes** but the `beginPass()` generation and reset guarantee each pass still evaluates its own camera/LOD/shadow/material visibility. It is **not a persistent visibility or grouping-result cache**, so no stale render decisions are retained. Existing group thresholds, GPU command encoding and per-page adaptive cost feedback are unchanged.

### Commits and modified files

- `d3fab308395283c8632eec4dd39c5986afe14a2c` — `engine/render/StaticCrossModelBatchPolicy.h` adds reusable `StaticCrossModelPreflightWorkspace`: lazily allocated **4096 hash slots** with epoch stamps and exact `StaticCrossModelPreflightKey` comparisons, **2048 bounded group entries** with 32 fixed primitive pointers each, `uint16_t` per-item group indices, and `uint8_t` per-item mode array. `findOrCreate(key)` returns index rather than a pointer, so vector growth cannot dangle references. On epoch wrap slot stamps reset. It has `retainedBytes()` for diagnostic accounting and `release()` to free high-water capacity on a full scene reload.
- `ee8bf0d33aa35f4ea3335cd365af1e5fa0fec854` — `engine/render/Renderer3D.h` includes the shared policy, owns a workspace member, exposes `ModelRenderStats::staticCrossModelPreflightWorkspaceBytes` and releases workspace from `invalidateStaticModelCaches()`. No ECS, shader, or game-only dependency.
- `636481aa8ba90d2715a621712a1299c2ebdb9fed` — `engine/render/Renderer3D.cpp` releases workspace memory at renderer shutdown.
- `2d27062ebdb885ab61aa7a5b598a4438b507760c` — `engine/render/Renderer3DModels.cpp` switches Stage 20's local hash map and local mode vector to the reusable workspace. First pass scans only the actual selected `ModelItem` indices and stores an item→group index; the promotion pass uses that index **without a second page/material hash lookup or repeated `eligibleStaticPage`**. All original group material/flags/page/vertex/index signatures, 2048-group cap, per-geometry-page admission, MDI/direct/instancing mode assignments and strict final MDI submission checks remain unchanged. `staticCrossModelPreflightWorkspaceBytes` reports retained capacity in the current frame.
- `396d95bf5bc0e88c9a6bea06269a015720f70b27` and `1f4a8b11a5f96b354b550c7f8408d6c377b2933e` — page-aware `modes` are reset to empty on every pass and only `activateModes(modelItems_.size())` if the current view has ready preflight groups. Thus small/fragmented scenes do not zero an unused full-size mode map.
- `306bb9bb29f3c153ca6f9fd528cbe1dddb97d522` — `apps/editor/windows/SceneWindow.cpp` shows `Preflight workspace retained: ... KiB` under Scene → Batching → Instancing and indirect details, alongside ready and actually submitted cross-model MDI stats.
- `0f37e459e6550071e92b669f275c424628526367` — shared policy uses the typed `std::numeric_limits<uint16_t>::max()` invalid group sentinel.
- `ac89975f02c091d55d6ff1d5a8d0cdb5eac61464`, `56ffe42bd45f5e579ecb779a9dd95a53ddc0b8f1` — user-facing `docs/geometry-performance.md` and `AGENTS.md` ownership, lifetime, cap and fallback contract.

### API, memory and safety contract

Public header-only helper: `StaticCrossModelPreflightWorkspace` in `engine/render/StaticCrossModelBatchPolicy.h`. `beginPass(itemCount)` creates a new logical epoch with fresh group state, `findOrCreate(key)` returns a bounded 16-bit index, `rememberItem(itemIndex, groupIndex)` and `groupForItem(itemIndex)` link current render items to the group, `activateModes(itemCount)` lazily creates the 8-bit mode map only when needed, and `release()` frees high-water capacity. Consumers must **not** cache `Entry*` or per-item group indices across a `beginPass()`. No dynamic allocations occur per group after the workspace has grown to the working-set high watermark; initial growth is still possible when scenes become larger. Reusing capacity retains CPU memory until explicit scene invalidation or renderer shutdown.

Memory bound is fixed **4096 slot directory + at most 2048 group structs** (each with a 32-primitive fixed pointer array) plus current-scene `2 * modelItemCount` bytes of group index and, only when ready groups exist, `1 * modelItemCount` bytes of mode decisions. No GPU buffer/texture ownership. Item scratch capacity follows the largest `ModelItem` count observed until scene reload; it is not globally capped by this helper because actual renderer scene item count is dynamic. Renderer remains nonreentrant and on its presentation thread.

Per-pass `materialGroup` IDs are still derived from exact GPU-state comparison; camera/shadow culling, LOD, occlusion and sorting occur before preflight, and the existing real MDI compatibility pipeline verifies material, buffer, indexed command offsets and minimum unique command threshold again at submission. The Stage 20 per-live-GPU-page feedback model is unchanged; the pass-wide EMA remains diagnostic only. Any preflight map saturation results in **ordinary fallback**. All new and prior experimental flags remain OFF by default. No genre, authoritative ECS, serialization, shader or GPU ABI changes.

### Actual validation and limitations

- GitHub connector fetched latest PR, current main and branch compare, inspected the original Stage 20 preflight and policy. Created/wrote all Stage 21 code/docs commits successfully, re-fetched modified source and ran **static source consistency checks** verifying 4096 slots, 2048 groups, typed sentinel, exact key comparisons, epoch handling, per-item group recording/replay, absence of the former local `unordered_map`, conditional mode initialization, release on full invalidate/shutdown and UI memory metric. These are read-only source checks, **not executable tests**.
- **CMake compile: NOT RUN. CTest: NOT RUN. Metal GPU pixel parity: NOT RUN. CPU/GPU frame-time A/B: NOT RUN**, per user-requested feature-first sequence; no local repository checkout available in this environment. Do **not** claim measured allocation reductions, draw-call improvement or visual correctness. Existing tests for Stage 10/11 policies remain outdated and are part of the later dedicated test stage.
- PR #34 stays Draft and unmerged; main untouched. CI is likely queued/cancelled due frequent feature commits; refresh GitHub Actions after the handoff but do not count queued/cancelled as passing.

### Next actionable feature and later tests

Possible further feature-first optimization: avoid zeroing the complete per-item index array for views where most items are not static (e.g. epoch-marked item-index slots), or bound adaptive page/material group preflight to the current scene's distinct visible pages to cut work in dense worlds. Do not add stale cross-pass visibility caches or compromise exact GPU state/LOD checks. Once feature work ends, update `tests/render/model_batching_policy.cpp` for stages 10/11/13/15 etc, add CPU policy tests for workspace epoch/wrap/saturation/reuse, run focused CI, Metal raster parity and a real large-city A/B/A CPU/GPU benchmark.

---

## Stage 22 — sparse epoch stamps for preflight item indices and MDI modes (2026-11-19)

**User direction:** prioritize features; compilation and tests later. **Repository:** `chromealex/AIVERSE`, **branch:** `perf/incremental-static-model-cache`, **PR:** #34 Draft (unmerged). **Implementation + agent-doc head before this handoff:** `0b59ed99e9f884f75a5194a0d8eeec94df78dcd9`. At start, current `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, branch head `4e440b7e6a17668b55979e4fff7bafae9ac7df21`; compare `ahead=165, behind=0`, main already fully included. Check again before any later write.

### Problem and design

Stage 21 removed per-pass hash-table allocations but still performed **linear full-array writes**:
`groupForItem_.assign(modelItems_.size(), invalid)` whenever preflight began, and
`modes_.assign(modelItems_.size(), 0)` whenever any preflight group qualified. In a city with many dynamic and nonbatchable objects but only a few eligible static ones, this zeroed far more indices than were used, twice per render scene (shadow and opaque). Stage 22 uses **item-generation stamps**: index and mode are written only for actually selected items, and older records are interpreted as invalid without clearing their value arrays. This never caches culling, LOD or visibility decisions between views.

### Implementation, commits and changed files

- `7d5d20ff09ee3c653ec325b07d5614fb66f1d847`: `engine/render/StaticCrossModelBatchPolicy.h` extends `StaticCrossModelPreflightWorkspace` with `groupStamp_`, `modeStamp_` (two reusable `std::vector<uint32_t>` arrays), 32-bit `itemEpoch_`, `activeItemCount_` and `modesActive_`. `beginPass(itemCount)` increments both directory generation and item generation, clears only the bounded per-pass group vector, and **resizes only on high-water growth**, not `assign()` each pass. `rememberItem(index,group)` stamps the written group; `groupForItem(index)` returns `kInvalidGroup` unless the element is current. `activateModes(itemCount)` lazily grows mode+stamp capacity, while `rememberMode(index,mode)` marks actual selected items and `modeOf(index)` returns 0 for unstamped/disabled elements. `clearModes()` is a constant-time logical disable. Rare 32-bit generation wrap resets **both stamp arrays** once before advancing again, avoiding false reuse. `retainedBytes()` includes both stamp vector capacities; `release()` frees them and resets generations/active state.
- `586fe7a44521c6cd9cc3110731536139b40997d9`: `engine/render/Renderer3DModels.cpp` routes all cross-model sorting, per-page adaptive MDI gating, candidate compatibility and fallback through `staticCrossPreflight_.modeOf(index)` instead of direct access to an eagerly initialized full mode array. On the promotion pass it now uses `workspace.rememberMode(itemIndex, mode)`; group lookup already uses `workspace.groupForItem(itemIndex)` and its new stamp check. No original direct mode array references remain. The preflight group table and exact material/GPU page equality checks from Stage 21 are unchanged.
- `32057d897935a2f4258dad76246b2a79f2d19006`: `docs/geometry-performance.md` explains sparse generation behavior, overflow, visibility invariants and memory/write tradeoff.
- `0b59ed99e9f884f75a5194a0d8eeec94df78dcd9`: `AGENTS.md` updates agent responsibilities and warns against reading unstamped item data across passes.

### API, lifetime, memory and safety

Shared reusable workspace remains in the core renderer, genre-independent. The new methods `rememberMode(index, uint8_t)` and `modeOf(index)` replace direct mutable access to the mode vector in render code. `groupForItem`/mode lookups validate a generation and (for mode) `modesActive_`. `beginPass` is still called only when the special opt-in preflight will run; unrelated passes call `clearModes()`. The existing 4096 hash slots, 2048 preflight-group cap, exact page/material/VB/IB keys, MDI command thresholds, per-GPU-page cost decisions, and original fallback remain. No geometry, shader, GPU resource, ECS or saved-game changes. Both `AIVERSE_STATIC_CROSS_MODEL_BATCHES` and `AIVERSE_CPU_MDI_ADAPTIVE` remain OFF by default.

The memory tradeoff is **+8 bytes per retained item** for two 32-bit generation stamps (earlier 3-byte/item group+mode content, now 11 bytes/item excluding vector capacity overhead). Scratch still grows only with the highest number of ModelItems seen on this renderer until full scene reload, where `invalidateStaticModelCaches()` frees all vectors; shutdown likewise releases them. This is intentional to reduce repeated O(total scene items) writes in sparse static scenes; dense static scenes may not improve. At the rare 32-bit item generation wrap, two stamp arrays receive a full clear once. Hash directory generation remains independently 64-bit. All pointers stored in a preflight group are valid only for that same draw pass and are not used in a subsequent one.

### Actual verification (do not overstate)

GitHub connector successfully read PR/main ancestry and current files, wrote the two code commits plus `docs/geometry-performance.md` and `AGENTS.md`. Re-fetched `StaticCrossModelBatchPolicy.h` and `Renderer3DModels.cpp`; read-only static source checks verified:
1. Old per-pass full group-index/mode `assign` calls are gone; high-water `resize` remains.
2. Both group and mode writes set `itemEpoch_`; both reads require the current generation.
3. `modesActive_` protects passes that bypass preflight, and wrap/reset logic clears both stamps once.
4. Direct renderer mode-array access is removed. Sorting, candidate comparison and adaptive instancing fallback all read via `modeOf`; promotion writes via `rememberMode`.

**Native CMake compilation: NOT RUN. CTest: NOT RUN. GPU/Metal raster parity: NOT RUN. Real city CPU/GPU frame-time A/B: NOT RUN.** These remain deferred by the user's explicit feature-first instruction, and no local AIVERSE checkout exists in this session. No measured CPU improvement or visual correctness claim is made. The experimental code is still Draft/opt-in, with existing stale prior-stage test expectations to be reconciled later.

### Next exact work

Possible feature-first follow-up: reduce per-pass `order_` sorting on already-qualified pages using a bounded stable bucketization strategy or preflight-only sorted segments, while retaining exact material/pipeline compatibility, transparency order, and per-camera LOD. Avoid changing other renderers' ownership or GPU ICB without independent design. At the later test phase, add standalone CPU-only generation/wrap/sparse and stale-state cases for `StaticCrossModelPreflightWorkspace`, plus live Metal shadow/opaque off/on raster comparisons and fixed-city CPU/GPU time A/B/A. Update legacy `model_batching_policy.cpp` expectations from Stages 10–11, then run full CI. Append a new handoff after every stage.

---

## Stage 23 — pre-ranked static cross-model sort groups (2026-11-19)

**Branch and PR:** `chromealex/AIVERSE`, `perf/incremental-static-model-cache`, Draft PR #34. **User priority:** feature implementation first, tests later. **Implementation and general docs HEAD before this handoff:** `f122c6983f790c0766d0b81a676e0285d5aeb3a0`. At start of the stage, the current `main` SHA was `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, PR HEAD `4d2aa3bbd660bfcdd2259eba6a495b3293b3658a`; `main...feature` reported `ahead=170, behind=0` and GitHub PR was Draft/mergeable. Main is unchanged and already included; always refresh before writing to any other branch.

### Architecture and purpose
Stage 21 and Stage 22 removed per-pass hash-map allocation and full ModelItem zero-fills, but the **`std::sort(order_)` comparator still repeatedly fetched `model->geometryPoolPage`, GPU VB/IB handles, flags and material-group identity** for pairs of eligible static pooled items. That information is invariant inside each of the already-exact `StaticCrossModelPreflightWorkspace::Entry` groups for the current render pass. This stage computes a compact per-pass order for those *groups*, not all items, then compares numeric ranks for permitted static candidates. This is an opt-in CPU-comparison optimization, **not** a new renderer, mesh merge, GPU draw path or reduction of the overall `std::sort` algorithmic complexity.

### Code commits / files
- `df39bd90967f57427bced81fd02d4b216ba7a538`: `engine/render/StaticCrossModelBatchPolicy.h` adds `Entry::sortRank` (16-bit invalid by default), a reused `std::vector<uint16_t> sortedGroups_`, and `prepareSortRanks()`. It retains only groups with `group.ready && group.permitted`, and sorts their **exact preflight keys** by `materialGroup`, geometry page (with `std::less<const GeometryPoolPage*>`), GPU vertex/index buffer IDs and draw flags. It assigns each accepted group a bounded numeric rank. The vector is part of `retainedBytes()` and freed by `release()`. Maximum group count remains **2048** and ranks are refreshed only after current-view preflight and the Stage 20 page-adaptive allow/deny decision.
- `be232028ebec79c715ef1c186b078184df5d3fa3`: `engine/render/Renderer3DModels.cpp` calls `prepareSortRanks()` only when the current pass has permitted preflight-ready static groups. In the `std::sort` comparator, **only if both items are accepted cross-model candidates** does it compare the per-group numeric sort rank. If ranks match, it keeps the previously used `lmPage`, primitive-pointer and original ModelItem-index tie order. All other objects fall through the pre-existing sorting comparator, including transparencies, nonpooled/nonstatic meshes, shader/material-role/GI items, GPU-authored draws and adaptive pages that fell back to instancing. Per-object view/shadow culling/LOD and final MDI submission checks are untouched.
- `84e0617ed0af91ff7cb4caeecff09882c314e7bd`: `engine/render/Renderer3D.h` adds `ModelRenderStats::staticCrossModelRankedGroups` per scene, an actual number of permitted groups given a rank, **not** submitted GPU draw calls.
- `13d423f9c3454ac6c2d491c5ee5bf5c5ea4ec7d5`: `apps/editor/windows/SceneWindow.cpp` displays `Pre-ranked pooled state groups` under Scene → Batching.
- `b42b8ab8af788307ebefbb6f0ab9cee9793e714e` and `f122c6983f790c0766d0b81a676e0285d5aeb3a0`: rename the lookup to `sortRankForPermittedItem()`. The caller has already checked current-pass `modeOf(item)==1`, so the rank getter uses the separately epoch-guarded `groupForItem` without repeating the mode lookup on each `std::sort` comparison.
- `1128b9f3976f440a0250310b3a0b652bd0141eb1` and `fef56d2b73326c44ab852493a6e4def86a46d45c`: `docs/geometry-performance.md` and `AGENTS.md` updated with sorting order, lifetime and ownership contract.

### Safety, API and performance contract
The new public helper `StaticCrossModelPreflightWorkspace::prepareSortRanks()` must run **after** preflight `ready` and page-adaptive `permitted` are finalized, and before the final sort uses any `sortRankForPermittedItem`. `sortRankForPermittedItem` is for a caller that has already checked `modeOf(item)==1`; it returns the invalid sentinel when the group index is not current. The workspace remains renderer-owned, bounded by the existing 4096 directory slots / 2048 exact groups, and generation-cleared each render-pass. Its sorted group vector is reused across passes without caching an old camera or material decision. No pointer to any vector element is retained after resizing.

The numeric rank matches the *prior* comparator's order for eligible cross-model items: material state, shared GPU page, VB, IB, flags, then lightmap page, primitive identity and stable input item index within an exact group. All eligible cross-model items already satisfy the same rigid/no-baked/no-GI/no-deformation invariants. Transparent blended pass sorting stays back-to-front and is not affected; the separate `AIVERSE_MDI_INSTANCE_PALETTE` sorter is unchanged. Every source continues current per-object view/shadow culling, occlusion, LOD and exact GPU material compatibility; real CPU indexed MDI still validates actual material/buffer/pipeline/indirect command conditions and may use original instancing/direct fallback. This stage **does not** reduce the number of `std::sort` item comparisons asymptotically, only their cost for the relevant static groups; extra `O(G log G)` ranking is worthwhile only when `ModelItem` comparisons dominate. Benchmark required before claiming gain.

No new ECS authoritative, save/network, shader, GPU ABI, GPU ICB or genre-specific changes. `AIVERSE_STATIC_CROSS_MODEL_BATCHES=1` remains the independent, default-OFF opt-in; stage 20 adaptive page feedback, persistent CPU packet cache and ordinary CPU MDI behavior are unchanged. If no groups are permitted, no group ranks are computed and normal sorting proceeds.

### Verification actually performed
- Connected GitHub fetch confirmed main ancestry and PR status, existing sort comparator, workspace and source layout.
- Wrote the above source/UI/docs commits to `perf/incremental-static-model-cache`. Re-fetched `StaticCrossModelBatchPolicy.h`, `Renderer3DModels.cpp`, `Renderer3D.h`, `SceneWindow.cpp` and checked source structure: only permitted-ready groups receive ranks; exact key order includes material/page/VB/IB/flags; numeric comparison only applies when both mode-stamped cross candidates are ready; original lightmap/primitive/index order remains; other items retain old comparator; sorted-vector capacity is included in diagnostics and freed on `release()`. **These are source text checks, not binary tests**.
- **CMake compile: NOT RUN. CTest: NOT RUN. Metal CPU/GPU pixel parity: NOT RUN. Fixed-camera urban scene A/B: NOT RUN.** User continues to prioritize features before tests and a local AIVERSE checkout is not available here; do not claim code compiles or runs faster. The older policy test fixtures (Stages 10/11) still require updates. PR remains Draft and all experimental flags default OFF.

### Exact next feature and later tests
Consider further reducing `std::sort` overhead by partitioning preflight-ready static items into ranked buckets and sorting only within each group, while preserving the original global order for ordinary items, transparency/alpha and indirect fallback. Do **not** introduce an unstable sort comparator or regroup different GPU pages simply because their material or texture pointer matches. Any new feature stage must update this handoff. Later, reconcile stale policy tests, add CPU-only ranked-key / strict weak-order equivalence cases, real Metal mixed-page raster parity and a fixed-camera dense-city CPU/GPU A/B/A; validate memory churn and page lifetime before merging.

---

## Stage 24 — per-pass ranked bucketization of admitted static page groups (2026-11-19)

**Repository/branch:** `chromealex/AIVERSE` / `perf/incremental-static-model-cache`, Draft PR #34 (unmerged). **Source HEAD before this stage:** `6c6751114e15f310bf1cf16822199dfa06d4ff47`, `main` was `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` and compare confirmed `behind=0`; no main merge required. **User instruction:** features first, tests later.

### Implemented feature, ownership and API

- `engine/render/StaticCrossModelBatchPolicy.h`: new reusable `bucketizePermittedPrefix(order, candidateCount, withinGroupLess)` uses the exact preflight Stage 23 ranks to count and prefix-assign accepted items by page/material/flags/VB/IB group. Every candidate's group rank is checked before mutating draw order; a missing/stale group returns false. `bucketOffsets_`, `bucketWrites_` and `bucketItems_` grow to high-water capacity, are tracked by `retainedBytes()` and freed by `release()`. `rankedGroupCount()` exposes the current-pass count.
- `engine/render/Renderer3DModels.cpp`: for shadow/opaque passes under the existing cross-model opt-in, uses `std::partition` to separate accepted mode-1 candidates from all ordinary/adaptive mode-2 items, exactly as the old candidate-first comparator did. For 64+ accepted items performs an `O(C+G)` bucket distribution, then sorts **within** exact ranked groups by lightmap page, primitive and stable item index. The regular suffix keeps the **same** legacy sort comparator. Fewer than 64 candidates or rank validation failure retain the Stage 23 rank comparator for the candidate prefix. Phase-2 transparency retains original back-to-front sorting.
- `engine/render/Renderer3D.h` and `apps/editor/windows/SceneWindow.cpp`: new `staticCrossModelBucketedItems` / `staticCrossModelBucketedGroups` CPU-ordering diagnostics in Scene → Batching, distinct from actual successful MDI submissions. Retained workspace accounting now happens *after* sorting, so scratch allocations are counted immediately.
- `docs/geometry-performance.md`, `AGENTS.md` and this handoff record contracts, affected files and next work.

Existing `AIVERSE_STATIC_CROSS_MODEL_BATCHES=1` opt-in, per-GPU-page adaptive admission and strict final GPU submission checks are preserved. **No new GPU path**, shader change, ECS or genre dependency. Transparent/material-palette ordering is intentionally untouched. The old ordinary-item comparator is kept byte-for-byte except removal of the candidate-only block, which cannot be reached for the ordinary suffix.

### Validation performed / not performed

Connected GitHub source read and write integration used with a branch-head lease; API/layout and exact source replacements checked before commit. **CMake: NOT RUN. CTest: NOT RUN. Metal raster: NOT RUN. Dense urban A/B/A CPU/GPU: NOT RUN.** No compilation, visual correctness or performance speedup is claimed. Stage 10/11 policy fixtures remain to be updated when switching to tests. Keep PR Draft and experimental flags OFF by default.

### Next action

Feature-first follow-up: inspect the CPU indirect-command preparation for per-group transient allocations / redundant exact-state fetches, and optimize only if lifetimes/compatibility remain unchanged. Then update stale policy test expectations, add CPU strict-weak-order/bucket equivalence tests, Metal OFF/ON shadow+opaque raster parity and dense-city A/B/A before enabling the feature.

---

## Stage 25 — zero-copy cached CPU MDI layout replay and fixed palette slots (2026-11-19)

**Repository/branch:** `chromealex/AIVERSE` / `perf/incremental-static-model-cache`, Draft PR #34 (unmerged).
**At start:** `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, branch `de5665f40df45fa4abbe20fabb49f25c1dae2738` and `main...branch behind=0`; `main` was already an ancestor. User continues feature-first work; tests deferred.

### Implemented code/API/ownership

- `engine/render/Renderer3DModels.cpp`: `drawModelIndirectBatch` **no longer copies the whole cached indirect-command vector into `indirectCommandScratch_` on each exact layout-cache hit**. It continues validating each primitive pointer, index count/offset and GPU base/index offsets before reuse. Hits read `cacheEntry->commands` as a const reference for the synchronous backend call; misses build in the existing renderer-owned scratch, snapshot into their positional `IndirectLayoutEntry`, and upload from scratch. The cache cursor and pass-specific invalidation/lifetime contracts are unchanged. Exact command count threshold and pipeline/material/transform validation remain. GPU indirect-command buffer upload still occurs on every accepted draw. The zero-copy design does not elide GPU transfers.
- In the same source, the `instancePalette` preflight and `drawModelBatch` U3 slot assignment now each use `std::array<const Material*, kMaxMaterialPalette>` and an explicit active length instead of allocating a `std::vector` per call. Both paths retain unique material-pointer checks, first-seen slot ordering, 64-material limit and the preexisting indirect fallback semantics.
- `engine/render/Renderer3D.h`: `ModelRenderStats::mdiLayoutZeroCopySubmissions` and `mdiLayoutCopyAvoidedBytes`. These count *successful* GPU indirect submissions that used a validated cached command vector, and the number of CPU command bytes previously copied into scratch; they are NOT GPU bytes saved or frame-time gains.
- `apps/editor/windows/SceneWindow.cpp`: new MDI exact-layout zero-copy counters under Scene → Batching.
- `docs/geometry-performance.md`, `AGENTS.md` and this per-stage handoff: new architecture/ownership rules and pending work.

### Safety and fallbacks

- The cached command source is borrowed only during synchronous `drawModelIndirectBatch`; persistent cache entries remain owned by `Renderer3D`, and a cache-miss rebuild never modifies them on a hit.
- `AIVERSE_MDI_LAYOUT_CACHE=off` follows the existing uncached scratch path. Material palette mode retains its distinct opt-in. Original backend indirect fallback draws and `drawModelBatch` pipeline-warm behavior are unchanged.
- No ECS, FPS/RTS, GPU ABI, shader, geometry-page or LOD change. No experimental flag is turned ON globally; PR remains Draft.

### Actual verification and status

Connected GitHub fetched existing code, confirmed `main` ancestry, and staged exact, unique source replacements. Read-back/source checks must confirm the cached hit no longer calls `commands.assign`, every miss uses scratch, palette fixed arrays are bounded and backend submissions still upload GPU command bytes. **Native CMake: NOT RUN. CTest: NOT RUN. Metal raster parity: NOT RUN. Fixed-camera A/B/A: NOT RUN.** No measured speedup or compilation success is claimed; older Stages 10/11/13 policy tests still need reconciliation.

### Next work

Continue feature-first work by auditing excessive per-pass `indirectLayoutCache_` high-water growth and explicit retirement/invalidation on scene unload, or move to focused policy tests when requested. Test exact-layout hit/miss GPU command parity, zero-copy lifetime across renderer shutdown/cache invalidation, palette 64-limit and backend fallbacks, and record real A/B/A before merge.

---

## Stage 26 — bounded CPU MDI layout cache, idle ageing and model unload retirement (2026-11-19)

**Branch:** `chromealex/AIVERSE:perf/incremental-static-model-cache`, Draft PR #34. **At start:** `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee` and feature HEAD `99702417144f1e09bc71a6196268328fe14f89fe`, with `main...feature behind=0`. Main was not modified. User directive remains **features first; tests later**.

### Feature and architecture

- `engine/render/Renderer3D.h`: positional layout cache bounded to **96 entries per phase** and **256 items per entry** (five phases), plus idle retirement after 120 `beginScene` epochs. `IndirectLayoutItem` now stores owner `Model*`, `geometryRevision`, GPU VB/IB IDs, primitive pointer and existing exact command geometry fields. `IndirectLayoutEntry::lastUsedScene` enables aging; `IndirectLayoutCacheStats` exposes live entries, retained CPU capacity in bytes, cumulative retired entries; `ModelRenderStats::mdiLayoutCacheBypassed` counts current-scene attempts bypassing the cache due to limits. Renderer-private clear/targeted-retire/maintenance methods own the layout vectors; no GPU resources are owned by them.
- `engine/render/Renderer3DModels.cpp`: cache entry lookup refuses to create entries past bounds, falls through to the unchanged indirect-command scratch/real GPU upload and MDI submission. Warm-hit signature validation checks every model/revision/VB/IB/primitive/index field before borrowing cached commands. `clearIndirectLayoutCaches` frees all slots; `retireIndirectLayoutsFor(model)` removes **every** cached group with a matching source Model pointer (including mixed-model batches) without dereferencing it; `maintainIndirectLayoutCaches` ages out unused records and reclaims high-water vector capacity when notably underused; `shutdownModels` clears both cache and scratch. All invalidation calls belong **between rendering passes** to protect Stage 25 zero-copy borrows until their synchronous backend call returns.
- `engine/render/Renderer3D.cpp`: `beginScene` ages caches regardless of scene visibility (even when no `renderModels` runs), while existing `invalidateModelCachesFor` now retires dependent layouts before an asset is unloaded.
- `engine/render/Renderer3D.h`: existing inline `invalidateStaticModelCaches()` now frees the layout cache too on full scene reload.
- `apps/editor/windows/SceneWindow.cpp`: display CPU cache retained entries/bytes, cumulative retired layouts and current-scene capacity bypass count.
- `docs/geometry-performance.md`, `AGENTS.md`, this handoff: documented ownership, API, actual checks and follow-up.

### Invariants, tradeoffs, API contracts

The new cap bounds the number of retained source signature/command items, **not** submitted primitives: uncached batches still use the same `drawModelIndirectBatch` shader/pipeline checks and `dev_->drawIndexedIndirect` upload/fallback. The cache's historical positional lookup behavior remains, and no cross-frame cached visibility, LOD, textures or GPU command buffers are introduced. Invalid/stale model data is rejected by exact signature checks. Targeted model retirement removes complete mixed-model records; other records may shift slot position and safely cold-rebuild. Age 120 counts renderer `beginScene` invocations (multiple viewports may advance it faster than game frames), not wall-clock seconds. Retained storage includes vector capacity; the ordinary uncached scratch is excluded from the displayed cache budget.

**No default flags changed.** `AIVERSE_STATIC_CROSS_MODEL_BATCHES`, `AIVERSE_PERSISTENT_CPU_PACKETS` and experimental GPU ICB remain OFF by default. `AIVERSE_MDI_LAYOUT_CACHE=off` preserves uncached CPU MDI.

### Actual verification

GitHub source inspection verified pre-stage cursor, model unload and shutdown paths; edits were applied via exact single-match replacements with a leased branch HEAD and then read-back inspection. **Native CMake/CTest NOT RUN; Metal image parity NOT RUN; dense-city CPU/GPU A/B/A NOT RUN.** Therefore compilation, correctness under a real backend and performance improvements are unverified. PR remains Draft.

### Next action

Feature-first follow-up: audit the GPU indirect command buffer capacity/resizing behavior and its GPU upload costs, preserving backend snapshot lifetime, or advance to tests once feature work is complete. Future tests must exercise entry cap fallback, big batches, model unload with mixed-model layouts, age/cursor rollover and zero-copy warm replay; reconcile older Stages 10/11/13 policy assertions, then Metal OFF/ON raster parity and fixed-scene A/B/A before merging.

---

## Stage 27 — GPU-safe indirect command snapshot growth and prefix overwrite (2026-11-19)

**Repo / branch:** `chromealex/AIVERSE` / `perf/incremental-static-model-cache`, Draft PR #34. **At start:** `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, feature HEAD `43bf1f6355096884a888e9e0c8c74b552169093f`, compare `behind=0`; main was already an ancestor, not modified. User continues to prioritize feature implementation over tests.

### Architecture / implemented files / API

- `engine/gfx/Gfx.h` adds the opt-in `BufferDesc::discardOnUpdate` contract (default false): an offset-zero dynamic update replaces the entire **readable** prefix, with no need to retain old tail bytes. This does not weaken `snapshotUpdates` or allow writing to in-flight GPU allocations. Other buffer types and call sites retain unchanged semantics.
- `engine/render/Renderer3DModels.cpp` opts only its existing dynamic `indirectCommands_` buffer into `discardOnUpdate=true`. Every submission updates exactly the current contiguous indexed command prefix, and `drawIndexedIndirect` consumes `commands.size()` commands. Exact material, geometry, LOD, visibility and GPU pipeline/backend compatibility remain. Cache-hit zero-copy CPU layouts and ordinary fallback remain intact.
- `engine/gfx/metal/GfxMetal.mm` preserves the existing per-frame-slot/per-submission indirect snapshot pool (three fenced frames). Opted-in snapshots grow geometrically to reduce MTLBuffer reallocations, and no longer copy an old command tail before each prefix replacement. The preexisting **optional** `AIVERSE_METAL_ICB_DIRTY_UPDATES=1` mode now compares 128-byte chunks on safe *completed* slots and copies only changed chunks. Non-opted-in partial updates retain their previous copy-before-write semantics. `AIVERSE_ICB_PROFILE=1` logs copied/avoided CPU bytes and snapshot allocations per 600 updates.
- `engine/gfx/vulkan/VkDevice.h` / `VkResources.cpp`: dynamic `VBuffer` carries `discardOnUpdate`. Offset-zero replacement skips old-tail shadow copies when selecting a new GPU-safe version and after allocation growth. Existing Vulkan serial completion/version allocation and actual current command bytes remain unchanged.
- `engine/render/Renderer3D.h` and `apps/editor/windows/SceneWindow.cpp`: new diagnostics `mdiIndirectCommandBytes` (successful RHI command payload size) and `mdiIndirectPrefixOverwrites` (existing command-buffer updates); these are **not** backend transferred bytes or GPU time. `docs/geometry-performance.md`, `AGENTS.md`, this handoff document the contract.

### Hard safety contracts

- `discardOnUpdate` does **not** make command buffers immutable, persistent across independent scene states, or safe to overwrite in-flight snapshots. Metal maintains one allocation per draw submission until fenced completion; Vulkan continues serial-tracked versioning. Never replace those with one writable MTLBuffer/VkBuffer without GPU fencing.
- The optional Metal byte-diff optimization only writes/reads a previously completed recycled snapshot; it must never overwrite one used by an earlier draw in the *same* GPU frame. The 20-byte indexed command ABI and full per-command validation are preserved.
- Do not advertise GPU-side dirty-only uploads; Metal uses shared-memory snapshot writes, Vulkan maintains actual command prefix updates. `AIVERSE_METAL_ICB_DIRTY_UPDATES` remains opt-in; CPU memcmp overhead may exceed saved copies.
- No new ECS/genre or GPU-driven culling path, no change to transparent/LOD/material batching. Metal CPU ICB remains opt-in and all experimental multi-model features remain OFF by default.

### Actual verification / pending

Source/API and exact replacement consistency was checked against GitHub branch and main ancestry. **Native CMake: NOT RUN. CTest: NOT RUN. Metal raster: NOT RUN. Vulkan real device: NOT RUN. Xcode GPU validation: NOT RUN. Dense-city A/B/A: NOT RUN.** No rendering correctness or speedup claim. Keep PR Draft.

### Next work

Feature-first option: add backend-owned byte-copy statistics exposed via a read-only RHI diagnostic API, so the editor can distinguish 20-byte command payload submissions from actual snapshot CPU copies. Later tests should cover Metal multiple submissions in one GPU frame, frame-slot fence reuse, mixed exact/partial updates, dirty mode, snapshot capacity growth, Vulkan version reuse and growth, unload/reload and LOD/shadow parity. Reconcile old Stage 10/11/13 policy fixtures, run Metal visual A/B/A and fixed-scene CPU/GPU benchmarks before enabling or merging.

**Stage 27 source follow-up:** corrected the opt-in Metal snapshot profiling format string to emit one actual newline; no change to GPU commands or buffer lifetime. Native compilation and runtime tests remain NOT RUN.

---

## Stage 28 — actual CPU upload diagnostics from Metal/Vulkan per indirect buffer (2026-11-19)

**Repo:** `chromealex/AIVERSE`; **branch** `perf/incremental-static-model-cache`; Draft PR #34. At start `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, branch `4483e8fa77db41114733798d485e16d385d85e78`, `main...branch behind=0`, so no merge necessary. User continues features first, tests later.

### Architecture, API, changed files

- `engine/gfx/Gfx.h`: `gfx::BufferUpdateStats` and optional virtual `Device::bufferUpdateStats(BufferHandle)`. Default returns `available=false`. Per-buffer counters are cumulative over the current handle lifetime and do not block the GPU: successful dynamic update calls, requested logical bytes, CPU mapped-memory memcpy bytes, separate CPU-shadow writes, memcmp input span bytes, verified copy-elision bytes, old contents preserved by memcpy, live snapshot/version allocation count and reuse count. Retained backend buffer bytes and CPU-shadow capacity are current estimates, not lifetime sums. **Initial buffer versions and creation-time mapped writes are included** where applicable.
- `engine/gfx/metal/GfxMetal.mm`: store `BufferUpdateStats` in `MTBuffer`. The existing per-submission fenced indirect-snapshot update path accumulates copied bytes (including old-content preservation), compared bytes, skipped equal dirty chunks, new MTLBuffer snapshots and recycled slots after successful updates. The read-only getter excludes double-counting `bufs[slot]` when it aliases an `indirectSnapshots[slot]` entry. No GPU readback and no change to resource lifetime.
- `engine/gfx/vulkan/VkDevice.h` and `VkResources.cpp`: store `BufferUpdateStats` in `VBuffer`. Count initial allocation/mapped copies, subsequent host-visible updates, CPU shadow writes, version-preservation writes, version allocations/reuses and retained Vulkan suballocation spans. The existing GPU serial/fence checks are unchanged.
- `engine/render/Renderer3D.h`: `indirectBufferUpdateStats()` forwards stats for **only** its `indirectCommands_` handle and returns unavailable for an unsupported backend / before creation.
- `apps/editor/windows/SceneWindow.cpp`: diagnostics for mapped/shadow copies, compared/skipped/preserved data, allocations/reuse/update counts, retained logical capacities. They are explicitly cumulative since indirect GPU command buffer creation.
- `docs/geometry-performance.md`, `AGENTS.md`, and this handoff describe contracts and pending verification.

### Safety, ownership and interpretation

The API is read-only and cannot change GPU submission, synchronization, draw order, shader/material state, or ECS/genre integration. Existing per-submission Metal snapshots and Vulkan version fences remain. The returned numbers count CPU memory traffic **inside the instrumented backend**; they are NOT external memory-bus/GPU bandwidth counters, GPU frame time, or proof that Stage 27 accelerates any scene. A Metal resource referenced as both the active slot and one pooled snapshot contributes to retained capacity once. Vulkan Allocation::size is a logical memory suballocation span, not actual separately resident physical VRAM. Initial and later transfers are distinguished by `updateCalls` (which excludes create). In particular the Scene panel reports cumulative counters, not a single render-pass delta. No opt-in flags were enabled globally.

### Actual verification (features first)

Read source from the GitHub connector, confirmed head/main ancestry and applied exact unique replacements. Backend status and code consistency should be re-fetched after the commit. **Native compilation: NOT RUN. CTest: NOT RUN. Metal image and Xcode GPU validation: NOT RUN. Vulkan device tests: NOT RUN. Real scene CPU/GPU A/B/A: NOT RUN.** Keep PR #34 Draft.

### Exact next action

Feature-first option: add per-view **delta snapshots** of these cumulative counters for easier comparison between shadow and opaque phases, but only by sampling at explicit pass boundaries and never pretending per-buffer lifetime allocations measure instantaneous VRAM. Then reconcile stale Stage 10/11/13 test fixtures, add CPU policy tests for upload stats, run real backend correctness and fixed-camera A/B/A before merge.

---

## Stage 29 — scene view shadow/opaque CPU indirect transfer attribution (2026-11-19)

**Repository/branch:** `chromealex/AIVERSE` / `perf/incremental-static-model-cache`, Draft PR #34, unmerged. At beginning `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, feature `67b08ec594c6ce7a9a5d18d80a35ee62adb613b7`, and `main...feature behind=0`. `main` already belongs to the branch history; no merge needed. **User priority:** continue features, tests later.

### API, architecture, files changed

- `engine/gfx/Gfx.h`: two read-only helpers `gfx::bufferUpdateCounterDelta(before,after)` and `gfx::accumulateBufferUpdateCounters(total,delta)`, working on the existing `BufferUpdateStats` Stage 28 data. Compute only additive monotonic lifetime counters (updates, logical requested bytes, mapped/shadow memcpy, memcmp bytes, skipped/preserved copies, new allocations and safe reuse counts). A missing/incompatible/reset snapshot returns `available=false`. Retained allocation capacities remain zero in deltas and are never summed. No new virtual RHI calls or backend behavior.
- `engine/render/Renderer3D.h`: `ModelRenderStats::mdiBackendPassTransfers` (array of two additive `BufferUpdateStats` deltas) and `mdiBackendPassSamples` (number of sampled invocations) are reset with normal `modelStats_={}` in `beginScene`. Index 0 is sun-shadow `renderModels(0)`, index 1 is the main-view opaque `renderModels(1)`.
- `engine/render/Renderer3DModels.cpp`: samples `indirectBufferUpdateStats()` directly at model-pass entry and exit. Handles absent at entry use zero baseline (first-pass buffer allocation and initial upload are attributed to that pass); a prior valid handle must match the exit handle, otherwise no delta is emitted. Only normal shadow or main-view opaque passes are sampled; reflection/auxiliary and other phases are excluded. Repeated qualifying renderModels invocations in the same beginScene accumulate in the corresponding phase total. This adds no GPU readback or resource mutation.
- `apps/editor/windows/SceneWindow.cpp`: nested `Backend transfers by Scene model pass` panel inside Scene → Batching → Instancing and indirect details. For sun shadow and main opaque it shows per-scene-backend update calls, requested bytes, actual mapped/shadow CPU memcpy bytes, memcmp/skipped/preserved copy bytes, new snapshot/version allocations and safe reuses. Stage 28 lifetime counters remain separately visible.
- `docs/geometry-performance.md`, `AGENTS.md`, and this stage handoff document scope, safety and pending work.

### Lifetime, correctness and definitions

These are deltas for **one `Renderer3D::beginScene` view submission**, not necessarily one hardware GPU frame. Multiple Scene/Game viewports or rerenders may share a GPU frame, and a single scene may render more than one phase call. The helper guards against lifetime counters decreasing and does not treat point-in-time retained resource capacity as additive. The initial dynamic indirect buffer creation may initialize multiple frame-slot buffers, so its mapped bytes and allocation count appear only with the *first* sampled model pass. We report only backend-instrumented **CPU** operations on `indirectCommands_`, not GPU execution time, GPU-driver VRAM residency, non-model renderers or actual PCIe bandwidth. No shader, ECS, per-material grouping, command encoding, GPU fence, RHI resource lifetime or genre behavior changes.

### Verification actually performed

Connected GitHub inspection confirmed single `renderModels` outer function, `beginScene` stats reset, phase-0 sun shadow and phase-1 main opaque calls, reflection main-view flag and Stage 28 per-handle backend instrumentation. Replacements were exact one-match source updates. **Native CMake: NOT RUN. CTest: NOT RUN. Metal GPU raster and Xcode validation: NOT RUN. Vulkan runtime: NOT RUN. Fixed-camera CPU/GPU A/B/A: NOT RUN.** No compilation correctness or performance gain is claimed. Leave PR Draft and all experimental modes off by default.

### Next agent action

Feature-first: optionally add per-phase CPU profiling for command selection/packing versus backend mapped copies to pinpoint the remaining bottleneck, keeping cheap counters and preserving semantics. When feature work ends, update old Stage 10/11/13 policy fixtures, add CPU-only monotonic delta/unsupported/reset/first-allocation tests, run Metal and Vulkan shadow/opaque raster comparisons, frame-slot reuse/stress tests, and fixed-city CPU/GPU A/B/A before enabling/merging.

---

## Stage 30 — CPU MDI selection / sort / packing / upload / backend timings (2026-11-19)

**Repository:** `chromealex/AIVERSE`; **branch:** `perf/incremental-static-model-cache`; **PR:** #34 Draft, unmerged. **Before this stage:** `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, feature HEAD `d1e112ded960f2119e4e92d833bafb62c9f1ce02`, `behind=0`; no main merge was needed. User preference remains **features first, tests later**.

### Implementation, APIs, and changed files

- `engine/render/Renderer3D.h`: new `ModelRenderStats::CpuMdiStageProfile` and `mdiCpuStages[2]` accumulators, index 0 for sun shadow, index 1 for main-camera opaque. Fields: qualifying pass count, compatible candidate scans, attempted command packs, successful backend indirect submissions, too-short encoded batches, pipeline warmups, backend fallbacks, and durations in milliseconds for overall model pass, visibility selection, material grouping/sort, candidate scan, exact command packing/cache lookup, pipeline+uniform setup, CPU-to-RHI command buffer create/update and synchronous backend `drawIndexedIndirect` call. Values are reset by existing `modelStats_={}` in `beginScene`, with no new retained pointers.
- `engine/render/Renderer3DModels.cpp`:
  - `renderModels` samples selection and combined grouping/sort at existing view-boundary points and measures the whole model pass. For each eligible CPU MDI candidate group it counts scan cost through compatible-item and distinct-primitive threshold selection, stopping the scan timer before entering `drawModelIndirectBatch`. This prevents including expensive indirect packing/upload/backend calls in `candidateScanMs`; successful early `continue` paths are handled explicitly.
  - `drawModelIndirectBatch` measures exact geometry/layout cache check + packing, the pipeline setup/binding call, GPU-command-buffer create/update CPU duration, and the backend indirect submission call. It counts actual successful calls separately from pipeline warming and backend failures that fall back to direct draws. Timing around a rejected command threshold is attributed to command packing but no GPU submission is claimed.
  - The gate is `profiling_ && !auxiliaryModelPass_ && (phase==0 || (phase==1 && geometryMainView_))`, so **profiling is OFF by default** and timed views match Stage 29 transfer metrics. No measurement of reflection/auxiliary/pass 2–4 or non-model draw code. Older `AIVERSE_MODEL_BATCH_PROFILE` and `AIVERSE_ICB_PROFILE` logs retain their behavior.
- `apps/editor/windows/SceneWindow.cpp`: nested **Scene → Batching → Instancing and indirect details → CPU MDI stage timings**, shown when the existing `Profile Scene renderer` checkbox is enabled. It presents both pass breakdowns, total CPU model time, attempts/actual submissions/warmups/fallbacks and explicitly states CPU-only scope.
- `docs/geometry-performance.md`, `AGENTS.md` and this handoff record scope, exact files, constraints and pending tests.

### Performance and safety interpretation

`selectionMs` covers visible item screening; `groupingSortMs` also includes material GPU-state classification and page-aware preflight, not only `std::sort`. `candidateScanMs` covers contiguous compatibility/run/threshold work before the indirect call. `commandPackingMs` covers signature checks/layout construction and on-miss cached CPU copies; `pipelineSetupMs` includes `drawModelBatch`'s pipeline and uniform/texture binding, `bufferUploadMs` measures the RHI CPU method duration, and `backendSubmitMs` measures only the synchronous API call. GPU execution is asynchronous and is not included. Full model-pass `totalMs` includes normal instancing, any fallback draws and instrument overhead, and should not be reconstructed by adding the listed stages. Per-pass values are summed across repeat qualifying invocations **within the same `beginScene`**, not necessarily one physical GPU frame. No shader/CPU MDI ordering/material compatibility, ECS, GPU fence or indirect draw count rules are modified.

### Verification actually performed

Read current feature source, confirmed `main` is already an ancestor and that scene profiling toggle is wired to `setProfiling`. Used exact source transformations and rechecked critical code paths via GitHub. **Native CMake: NOT RUN. CTest: NOT RUN. Metal screenshot/AGX stability: NOT RUN. Vulkan runtime: NOT RUN. Dense-city CPU/GPU A/B/A: NOT RUN.** No compilation or performance speedup claim; PR remains Draft and experimental flags OFF by default.

### Next step

Feature-first option: persist a small, read-only rolling distribution of per-stage CPU timing (e.g. last 120 eligible scene passes) to compare spikes/P95 without confusing CPU vs GPU and without allocating per frame. When switching to validation, reconcile stale Stage 10/11/13 policy tests and add timed-path coverage for no-items/no-MDI, pipeline warming, backend fallback, mixed geometry pages, order parity and instrumentation OFF. Run Metal/Vulkan visual parity and balanced same-camera A/B/A before considering merge.

**Stage 30 opt-in overhead follow-up:** revised the short-command-count rejection and failed indirect submission branches to call the new Stage 30 timing clock only when `profiling_` is active. Original successful path retains its pre-existing timestamps for the independent `AIVERSE_ICB_PROFILE` trace; fallback drawing, backend conditions and counters are unchanged. Re-fetched source only, native compile/CTest/Metal tests NOT RUN.

---

## Stage 31 — fixed 120-invocation CPU stage mean/P95 history (2026-11-19)

**Repository / branch / PR:** `chromealex/AIVERSE`, `perf/incremental-static-model-cache`, PR #34 **Draft**, not merged. Before this work `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, feature head `871de354c3656e5a19acdba707d9bce11fdbbcff`; compare `behind=0` so no main merge needed. **User direction:** features before tests.

### Feature, ownership and API

- `engine/render/Renderer3D.h`: new `kCpuMdiRollingWindow=120`, `CpuMdiRollingSummary {samples,meanMs[8],p95Ms[8]}`, public read-only `cpuMdiRollingSummary(phase)`, public `clearCpuMdiRollingHistory()`, and renderer-owned `cpuMdiRolling_[2]`. The latter contains **two fixed 120 x 8 double arrays**, one shadow and one main-opaque, with ring cursor and sample count, not heap-allocated vectors. Ring storage remains alive across ordinary `beginScene` calls even though `modelStats_` resets. Its samples contain eight Stage 30 CPU-time durations (total, selection, material grouping/sort, candidate scan, command packing, pipeline setup, buffer upload, backend submit).
- `engine/render/Renderer3DModels.cpp`: `recordCpuMdiStageSample(phase,before)` snapshots stage-time differences from the Stage 30 current-`beginScene` cumulative profile and stores one fixed-length record per eligible renderModels invocation. Stage 31 records on both the visible-order-empty early return and the normal end; the empty modelItems early return does not count as an eligible model pass. The existing Stage 30 profiling predicate (`profiling_ && !auxiliaryModelPass_ && (phase==0 || (phase==1 && geometryMainView_))`) continues to gate all history writes. All other render phases, reflections and auxiliary views are excluded.
- `Renderer3D::cpuMdiRollingSummary(phase)` computes an independent arithmetic mean and **nearest-rank P95** for each of eight metrics over the currently filled ring (up to 120 observations). It uses `std::array<double, 120>` scratch on the stack, sorts only when the UI/API requests a summary, and returns zero samples for invalid phases/empty rings. No sorting or allocation is performed on a per-frame render path.
- `Renderer3D::invalidateStaticModelCaches()` and `shutdownModels()` clear this history across a full scene reload or renderer shutdown; turning profiling off *pauses* collection rather than deleting the existing history.
- `apps/editor/windows/SceneWindow.cpp` displays rolling sample count and mean/P95 for all eight CPU metrics under the already existing **Scene → Batching → Instancing and indirect details → CPU MDI stage timings**, retaining Stage 30 live per-view totals and warnings.
- `docs/geometry-performance.md`, `AGENTS.md` and this Stage 31 handoff explain exact files, behavior and restrictions.

### Safety, interpretation, limitations

This does not change render order, LOD, material checks, indirect command ABI, ECS modules, graphics RHI, Metal in-flight snapshots, Vulkan GPU-fenced versions or GPU memory use. The rolling sample is **one eligible model-pass invocation**, not a hardware GPU frame. A single overall scene frame can contain a sun shadow and an opaque sample, and several Scene/Game `beginScene`s can happen in one hardware frame. Asynchronous GPU time is not measured. P95 is computed via `ceil(0.95*N)-1` (nearest rank), so its granularity depends on the number of collected samples; Stage 30 cumulative per-view numbers and Stage 29 backend memcpy deltas retain their separate scope.

**Current state:** feature-first GitHub code changes/source inspection; **native CMake: NOT RUN, CTest: NOT RUN, Metal/Vulkan raster parity: NOT RUN, fixed camera CPU/GPU benchmark: NOT RUN**. No measured acceleration or compilation success is claimed. PR remains Draft, experimental modes default OFF.

### Next exact action

Feature-first option: add spike/threshold highlighting using the rolling P95 and count when profiling is explicitly on, without conflating model-pass CPU times with GPU times or treating a threshold as a benchmark. When changing to test phase, reconcile stale Stage 10/11/13 test expectations, add 120-sample wrap/eviction/empty/inactive/scene-clear and P95 nearest-rank cases, verify backend parity and run the dense-city fixed-camera A/B/A before merging.

---

## Stage 32 — automatic P95 CPU MDI spike candidate highlighting (2026-11-19)

**Repo/branch:** `chromealex/AIVERSE` / `perf/incremental-static-model-cache`; Draft PR #34, not merged. Before work `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, feature HEAD `311e84c879d2a604a53419de0d99feac851ca952`, `main...feature behind=0`; no main merge required. User continues **features first, tests later**.

### Changes, architecture and API

- `engine/render/Renderer3D.h`: extends the Stage 31 `CpuMdiRollingSummary` with `spike[8]`, `highlightedStages`, `dominantStage` (1–7 or -1), `dominantExcessMs` and `hasBaseline`. Adds four compile-time diagnostics thresholds: `kCpuMdiSpikeMinSamples=30`, `kCpuMdiSpikeMinP95Ms=0.25`, `kCpuMdiSpikeMinExcessMs=0.15`, `kCpuMdiSpikeP95ToMeanRatio=1.8`. No change to ring storage or renderer hotpath.
- `engine/render/Renderer3DModels.cpp`: during the existing read-only `Renderer3D::cpuMdiRollingSummary(phase)` call, *after* Stage 31 mean/P95 calculations, flags a CPU stage only when enough samples exist and **all three** P95 magnitude / excess / ratio conditions hold. Detects total model-pass wall-time variation independently; index 0 is excluded from `highlightedStages` and `dominantStage` to prevent falsely blaming MDI for ordinary draw-call work. Chooses the flagged sub-stage with largest absolute excess, retaining natural stage-index tie order.
- `apps/editor/windows/SceneWindow.cpp`: under Scene → Batching → Instancing and indirect details → CPU MDI stage timings, shows sample collection progress until 30 samples, then either no spike warning, per-stage warm-colored P95 variability warnings with top candidate, or a total-only warning without incorrectly attributing it to indirect stages. Displays all original mean/P95 values and the diagnostic thresholds for shadow and main opaque. The existing opt-in `Profile Scene renderer` still controls measurement and history writes.
- `docs/geometry-performance.md`, `AGENTS.md` and this handoff document exact changes, restrictions, verification and next step.

### Interpretation, safety and non-goals

This is a **heuristic CPU P95 variability indicator**, not objective evidence of a regression, slow GPU, bad frame time, sustained expensive work or required optimization. A stage with consistently high CPU time but no P95 excursion will not flag; workloads with camera movements/editor actions may legitimately vary. The initial >=30-sample guard reduces first-frame noise but does not guarantee stationarity. No new runtime timestamps, allocations or GPU operations are performed by renderModels; classification runs only on the same bounded read-only UI summary query. History and full scene invalidation remain exactly as Stage 31. ECS/genre independence, render order, GPU resource ownership and indirect command compatibility unchanged. Experimental flags are OFF by default.

### Verification actually performed

GitHub connector re-fetched main ancestry, Stage 31 summary code and Studio UI, applied exact unique modifications, and checked source invariants prior to commit. **Native CMake: NOT RUN. CTest: NOT RUN. Metal/Vulkan raster parity: NOT RUN. GPU device validation: NOT RUN. Fixed-camera CPU/GPU A/B/A: NOT RUN.** No claim of compilation or measured speedup; PR #34 remains Draft.

### Next action

When switching to tests, validate threshold boundaries (29 vs 30 samples; exactly 0.25 ms P95 / 0.15 ms excess / 1.8x ratio), stable-high stages, total-only spikes, dominant-stage tie behavior, wrap at 120 observations, disabled profiling, view separation, and scene-invalidation reset. Then reconcile older Stages 10/11/13 policy fixtures and perform Metal/Vulkan visual parity and balanced CPU/GPU performance A/B/A before enabling or merging. If more features are desired first, the next useful addition is identifying sustained CPU bottlenecks separately from P95 spikes, without using those classifications for automatic batch control.

---

## Stage 33 — stable expensive CPU stage detection, independent of P95 spikes (2026-11-19)

**Repo:** `chromealex/AIVERSE`; **branch:** `perf/incremental-static-model-cache`; Draft PR #34. Start: `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, feature HEAD `de62b1d196015f1b87f0ef795725d6d037402d29`, compare `behind=0`. Main was already an ancestor; no merge needed. **User direction:** features first, tests later.

### Code, API and documentation

- `engine/render/Renderer3D.h`: new `kCpuMdiSustainedMinMedianMs=0.35` and `kCpuMdiSustainedMinMeanShare=0.15` diagnostics thresholds. Extends `CpuMdiRollingSummary` with `p50Ms[8]` (nearest-rank median), `sustained[8]`, `sustainedStages`, `dominantSustainedStage` (-1 or 1–7), `dominantSustainedP50Ms` and `dominantSustainedShare`. Existing `hasBaseline` min 30 eligible samples is reused. The whole model-pass total (metric index 0) is never flagged as a specific sustained MDI stage.
- `engine/render/Renderer3DModels.cpp`: `cpuMdiRollingSummary` now calculates `P50` using the **same sorted 120-double scratch array** used for P95, then after Stage 32 spike classification independently flags stages whose median is >=0.35ms AND mean is >=15% of the overall model-pass mean. The metric with maximum P50 becomes the dominant sustained stage, preserving stage index on ties. A stage may also have a spike; these classifications deliberately do not suppress each other. Zero/invalid total mean never divides.
- `apps/editor/windows/SceneWindow.cpp`: Stage 30/31 CPU timing UI adds mean/P50/P95 numbers, a distinct blue sustained-cost warning with largest median and mean-share, keeps the orange P95 warning, and combines labels on rows meeting both conditions. Shows independent thresholds, 30-sample baseline progress and non-GPU limitations.
- `docs/geometry-performance.md`, `AGENTS.md` and this handoff document API, architecture, changed files, validations, guardrails and next action.

### Interpretation / safety

This addresses the deficiency of Stage 32: a stable CPU stage can be continuously expensive even without a P95 spike. Requiring P50 >=0.35ms means at least approximately half of the last 30–120 model-pass invocations are this expensive, while the mean-share >=15% gate makes the candidate materially relevant to its pass. It is a **CPU diagnostic heuristic**, not proof of causality, regression, A/B improvement or GPU time. Stage averages can include overlapping work, so their shares cannot be summed to a percentage of the whole pass. P50/P95 are nearest-rank order statistics over **model-pass invocations**, not entire hardware GPU frames. It is read-only and is evaluated only on summary/UI read: no new per-frame allocation, sort, timestamp, dynamic memory, GPU buffer, ECS/gameplay module or backend work. The existing opt-in `profiling_` gate and history reset on full scene invalidation/shutdown remain unchanged. Cross-genre compatibility and experimental default-OFF status are preserved.

### Verification actually performed

Inspected live branch `Renderer3D.h`, `Renderer3DModels.cpp`, Studio UI, agent docs and latest main ancestry via GitHub. Applied exact one-occurrence code replacement and rechecked structural invariants before commit. **Native CMake/CTest: NOT RUN. Metal/Vulkan GPU raster/driver validation: NOT RUN. Fixed-camera A/B/A CPU/GPU: NOT RUN.** No compilation or measured speedup claim. PR #34 stays Draft.

### Next exact action

Prefer moving to validation soon: reconcile stale policy tests from Stages 10/11/13, add CPU-only summaries for P50 (N=1, 29, 30, 120), stable-high/no-spike, rare spike-only/no-sustained, both classifications, dominant ties, share=0.15 boundary, empty total, ring wrap/scene reset and no-profile operation. Run full CMake/CTest and owner Metal/Vulkan fixed-view raster comparisons, then measured CPU/GPU balanced A/B/A before enabling/merging. If features continue first, consider configurable (non-mutating) diagnostic thresholds or an exportable per-view CPU report, but do not add more render-path work without measurements.

---

## Validation & safety stage — Vulkan in-flight allocation failure and CPU policy regression coverage (2026-11-19)

**Repo:** `chromealex/AIVERSE`, branch `perf/incremental-static-model-cache`, PR #34 Draft. Before change `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, feature HEAD `9f4e85f449c4d9e4760cac29a09dea4813d71599`, `behind=0`. User explicitly approved transitioning from feature implementation to validation/fixes.

### Concrete problem found

In `engine/gfx/vulkan/VkResources.cpp`, `writableVersion(VBuffer&,keepContents)` returned a reference to the previous **in-flight** version if `createBufferObject` failed, so `updateBuffer` could overwrite an earlier frame's indirect command data on OOM. The CPU shadow was also modified **before** the safe version selection; on failure its contents would contradict the last GPU upload. The immutable→dynamic and dynamic growth paths retired previous versions before securing a replacement. These are correctness/robustness hazards independent of any claimed performance benefit.

### Changes, API, files

- `engine/gfx/vulkan/VkDevice.h` and `VkResources.cpp`: `writableVersion` now returns `VBuffer::Version*` or `nullptr`, creating a new version only when no completed version is reusable. On allocation failure, the caller skips the entire CPU-shadow and mapped update rather than overwriting an in-flight allocation. CPU shadow is updated only *after* a safe version has been selected, while `keepContents` still copies the prior shadow before the new write. Immutable→dynamic growth and dynamic growth allocate the new buffer before retiring old versions, so failed allocations leave the old buffer valid. Existing GPU serial/fence tracking, ABI, RHI virtual methods, and command ordering remain.
- `tests/render/model_batching_policy.cpp`: adds `verifyIncrementalBatchingWorkspaceAndRhiCounters` CPU regression cases for Stage 21–24 exact group keys, mixed-model opportunity, permitted rank ordering, deterministic bucketization and its no-mutation-on-failure contract, stale group/mode generation invalidation, workspace release, Stage 27 default-OFF `discardOnUpdate`, and Stage 28/29 monotonic delta/accumulation, shadow/mapped/preserved/compared/skip bytes and never-added retained-capacity gauges. Uses the already configured `model_batching_policy` CTest target; no CMake target changes needed.
- `docs/geometry-performance.md`, `AGENTS.md`, this handoff document the failure and requirements.

### Actual verification / limits

GitHub connector read working source, inspected the CI workflow and found GitHub Actions for PR #34 **queued/pending**, not completed. Local environment has CMake/Clang/GCC but no AIVERSE checkout and DNS cannot reach GitHub; therefore a full native build/CTest/Metal run cannot be executed locally. Source replacements are exact and should be re-read after commit. **Full native CMake: NOT RUN. Focused test binary: NOT RUN at time of commit. Metal/Vulkan device raster parity: NOT RUN.** Never represent these tests as passed merely because they were added. The remote CI may subsequently execute them when the runner becomes available.

### Next

Inspect the latest PR #34 CI `model-policy` + Linux/macOS editor jobs as they actually complete. Fix any compiler diagnostics and test failures on the branch with an additional handoff entry and re-run validation. Later add a fault-injection test for Vulkan failed buffer allocation, Metal in-flight snapshot reuse and an actual renderer-backed 120-sample mean/P50/P95/spike classification test; then owner Metal/Vulkan OFF/ON raster parity and same-camera GPU/CPU benchmark. Leave PR Draft until proven.

---

## Feature continuation — Scene-only pooled static CPU MDI control (2026-11-19)

**Repository:** `chromealex/AIVERSE`; **branch:** `perf/incremental-static-model-cache`, Draft PR #34. **At start:** `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, branch `91ac7c23f0e2a80804c36b01f3cb1b8d489ccb49`, `behind=0`. The user explicitly clarified **finish the batching features before any testing**. Do not add/execute additional tests in a feature stage; existing queued CI may execute independently.

### Feature and API

- `engine/render/Renderer3D.h`: `StaticCrossModelBatchMode : uint8_t` (`Environment`, `Enable`, `Forbid`), public setter/getter and read-only effective-state getter. Default is `Environment` so previous opt-in OFF-by-default and `AIVERSE_STATIC_CROSS_MODEL_BATCHES=1` process configuration are unchanged. Stored per renderer, not in gameplay components or GPU resources.
- `engine/render/Renderer3D.cpp`: `beginScene` resolves the effective pooled cross-model CPU MDI mode from the explicit override or process environment for that view. Independent `AIVERSE_CPU_MDI_ADAPTIVE=1` only enables per-page feedback when the **effective** cross-model mode is enabled. Existing regular CPU MDI, compatible pooled geometry checks, shader/texture/material signatures, per-item LOD and view selection, shadow/opaque-only condition and backend/fallback safety remain mandatory.
- `apps/editor/windows/SceneWindow.cpp`: Batching popup's three-way Scene-only control; applies before any Scene renderer calls, restores `Environment` after Scene draw and before the Game window's use of the same renderer. Both the on-screen preflight diagnostics and opt-in controls work without setting a global env flag. Benchmark mode does not override safety flags or force shared geometry pooling. The opt-in is intentionally not serialized to project/game data.
- `AGENTS.md` and `docs/geometry-performance.md`: new ownership/use instructions. This handoff records actual verification, changed files and next action.

### Verification and limits

GitHub branch/source inspection and exact source transformation only. **Native compilation NOT RUN, CTest NOT RUN, Metal/Vulkan raster parity NOT RUN and CPU/GPU timing A/B/A NOT RUN.** No visual correctness or performance speedup claimed. PR #34 remains Draft. Do not accidentally use the editor's UI mode to enable GPU-encoded ICB, or let the Scene choice leak to non-Scene views.

### Next step

Feature-first: audit the remaining non-editor integrations and ensure all relevant model batching policies are accessible through renderer-level, genre-neutral APIs without needing environment changes or requiring per-game-specific code. Once all functionality is complete, run a comprehensive compile/test/parity cycle and fix failures. Keep each stage handoff current and avoid launching tests until instructed.


---

## Sync only current main into batching branch — 2026-11-19

Before sync: `main=d71c6d47e532b25fb816a8bb1f96c41e5275a165`, batching feature head `2e9661a300122b610d9da7394ce999ec9fdfefa6` and common ancestor `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, behind by 409 main commits. Only `main` was merged, not individual competing feature branches. Github tree comparison found 114 changed main paths and 23 independent batching paths, with exactly three overlapping files: `AGENTS.md`, `engine/render/Renderer3D.cpp`, `engine/render/Renderer3D.h`. The two renderer files were merged by nonoverlapping base-relative changes; `AGENTS.md` appended NEREID and batching guidance at the same EOF coordinate, preserved both sections with main content before batching content. Every main-added or modified path was overlaid by its exact Git blob SHA. The resulting merge commit has two parents (batching feature and main). No unrelated feature branches were manually merged.

Architectural effect: integrated NEREID renderer water/reflection fields, engine/world scene integration, water systems/tests, SpectraFX/VFX public interfaces from main while preserving the renderer-local static-cross-model MDI opt-in and all prior batching APIs, shader/LOD checks, cache owners, pipeline decisions, and Scene UI overrides. Actual verification: GitHub tree/readback/merge-parent/invariant inspection only; native CMake, CTest, Metal visual/GPU checks and benchmarking NOT RUN at user request (features first). Keep PR #34 Draft and experimental modes OFF. Next: expose remaining CPU MDI policies (adaptive decision, layout cache) as renderer-level per-view controls without global env changes, with corresponding Scene UI. Tests only after all features are complete.
