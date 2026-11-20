# PRISMA Stage 66 — native validation run initiated (results pending)

Date: 2026-11-19
Repo: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
PR base: feature/universal-entity-inspector-fps
Previous: docs/handoffs/prisma-stage-65.md
Validation workflow: .github/workflows/prisma-final-validation.yml
First accepted run (obsolete, cancelled): https://github.com/chromealex/AIVERSE/actions/runs/44595169973
Current run: https://github.com/chromealex/AIVERSE/actions/runs/44595377866
Current tested SHA: 96da88d098307f10d7745af6b2f880e35d918f9e
Latest docs/skill commit before handoff: add7583c773f9ae7affa7b5eb48202f1fa30d034
main untouched. No merges or force-pushes.

## 1. What was actually implemented in Stage 66

1. `bf54aae08fd1f3c1c8404999ab872029b076e021` —
   **new** `.github/workflows/prisma-final-validation.yml`,
   GitHub Actions workflow triggered by PRISMA source/workflow
   pushes to `feature/prisma-postfx`, or workflow_dispatch.
   Least-privilege contents:read. Two independent jobs:
   `ubuntu-24.04` (X11/EGL/GLES/Vulkan/Mesa/Xvfb installed)
   and `macos-15`. Each checks out source+submodules,
   configures desktop editor/tests/HDR scene Release,
   builds ALL default native targets once, then runs ONE
   full CTest invocation and attempts the available
   GL/HDR fixture with a 240-second timeout.
   The runner always uploads configure/build/CTest/GPU logs,
   possible HDR PPM image and CTest LastTest log.
   Existing source, project simulation and test APIs
   were not modified to create this workflow.
2. `96da88d098307f10d7745af6b2f880e35d918f9e` —
   `tests/render/prisma_volume.cpp`, extend the existing
   GPU-free Prisma volume/settings test to check
   nonnumeric RGB data, strict parser throwing and
   non-strict default preservation, NaN/Inf native
   authored values and truncated vector recovery,
   huge finite native value clamping, extra parameter
   truncation and native bool 0/1 normalization.
   Reuses existing CMake/CTest `prisma_volume`
   registration; no new target or runtime owner.
3. `c9123d9ef65452e13263c2694aaaf6f2dd723595` —
   `docs/prisma.md`, Stage-66 workflow invocation,
   test details, queued status and honest limitations.
4. `add7583c773f9ae7affa7b5eb48202f1fa30d034` —
   `skills/visuals/aiverse-prisma/SKILL.md`, pending
   CI/runner log investigation contract, accurate
   reporting and next validation responsibilities.

This is the handoff-only commit following source/test,
architecture and skill updates. Read latest feature
branch SHA before starting a new stage.

## 2. Actual CI invocation and observed status

- A GitHub Actions `push` event created run
  `44595169973` for workflow commit bf54aae.
- Adding the actual GPU-free numeric tests caused
  `concurrency.cancel-in-progress` to cancel that
  old run; it is NOT an executed test pass.
- GitHub Actions accepted current run
  `44595377866` for commit `96da88d09830`.
  The two jobs are:
  `135812125320` PRISMA full CMake + CTest (ubuntu-24.04)
  `135812125653` PRISMA full CMake + CTest (macos-15).
- **Observed status:** both jobs `queued`, no
  `started_at` or `conclusion` yet at the
  last API check. The run is not PASS/FAIL.
- Attempted native `git ls-remote` in the assistant
  Linux sandbox, but its network cannot resolve
  github.com (DNS failure). No local AIVERSE
  checkout or physical Mac Studio GPU is mounted
  in the assistant's container. Therefore no
  direct native build was attempted successfully.

**NOT RUN / NOT YET OBSERVED:** CMake configure/build,
clang/Obj-C++/MSL/GLSL compilation, CTest, GPU HDR
fixture, pixel capture/parity, resource lifetime,
perf and GPU fence checks. A queued job is ONLY
evidence that GitHub accepted the work request.

## 3. Next agent must read the REAL workflow results

1. Fetch `https://github.com/chromealex/AIVERSE/actions/runs/44595377866`
   / GitHub REST
   `GET /repos/chromealex/AIVERSE/actions/runs/44595377866`.
2. List jobs of run ID `44595377866`. If `completed`,
   fetch its jobs/steps and precise job logs and artifacts.
   Distinguish `success`, `failure`, `cancelled`,
   `skipped` and `queued` without guessing.
3. Report actual CMake command/exit status, compile
   diagnostics, total CTest passed/failed, HDR fixture
   invocation/return code and whether an HDR PPM
   artifact was captured. macOS GitHub OpenGL is
   NOT physical Apple Silicon Metal validation.
4. Fix actual failures on `feature/prisma-postfx`
   without changing the original base/main and
   preserve separate agent handoff after each
   bounded fix. Source file pushes retrigger the
   single workflow automatically; avoid CI runs
   caused only by documentation changes.
5. If GitHub runners remain queued, do not claim
   testing succeeded. Possible causes include
   insufficient runner capacity, GitHub Actions
   settings or billing; investigate from the
   GitHub UI rather than inventing a conclusion.
6. As soon as CI has real output, UPDATE this
   Stage-66 handoff with actual PASS/FAIL and
   any fixed blockers. Keep all backend-specific
   visual gaps marked unverified until tested.

## 4. Consolidated commands already in the workflow

```bash
cmake -S . -B build/prisma-final \
  -DCMAKE_BUILD_TYPE=Release \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_HDR_TESTS=ON \
  -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/prisma-final --parallel 4
ctest --test-dir build/prisma-final --output-on-failure -j 4
```

Mac/Linux additionally attempt their supported
`aiverse_hdr_gl` / `aiverse_hdr_scene` test
executables after the full CTest. These native
fixture invocations only execute when compiled
and actually present; a missing fixture is
logged `NOT RUN`, not `PASS`.

The `docs/prisma-feature-closure-stage-65.md` matrix
remains the full inventory of 19 GPU-supported
effect pathways requiring source/visual checks.
The most important smoke gaps: Shader Graph
4-texture TEX9 vs graphless TEX8, AlphaCutout
reactive skin/morph/baked, Actor Reveal current
pose, HDR DoF and exposure/Camera Cut, per-view
GPU fenced target retirement.

## 5. Local Mac worktree fallback (only if CI cannot run)

```bash
cd ~/Projects/AIVERSE
git fetch origin
git worktree add ../AIVERSE-prisma-validation origin/feature/prisma-postfx
cd ../AIVERSE-prisma-validation
cmake -S . -B build/prisma-final \
  -DCMAKE_BUILD_TYPE=Release \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_HDR_TESTS=ON \
  -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/prisma-final --parallel 4
ctest --test-dir build/prisma-final --output-on-failure -j 4
```

Only the host Mac can verify real Apple Silicon Metal
image/readback/fences under its actual GPU, and platform
GLES/Vulkan/WebGL must be tested when runners/devices
are available. Never claim these happened based on
a successful Linux or hosted macOS CTest.

## Stage 66 continuation — compiler-backed source regression tests

**Additional source commit:** `aa262d6b44546985429c0cda0abf0338f270c693`,
`tests/render/prisma_graph.cpp`.

Added two functions to the existing registered `prisma_graph`
CTest executable (already declared in CMake; no new build target):

- `reactiveShaderVariantContract()` invokes
  `reactive_shaders::animatedVertexGlsl` and
  `animatedMsl` with the actual unchanged
  `motion_shaders::kDeformed* / kBaked*` source
  (skinned/morph + baked × GLSL + MSL), checking
  all FOUR generated variants, current-deformed
  coordinates, UV, vertex-alpha, preserved prior
  motion output, and fail-closed behavior when
  template source anchors do not match. It also
  checks animated reactive alpha-cutout, dissolve,
  and Actor Reveal fragment source presence.
- `compositeTextureLayoutContract()` exercises
  the real C++ `PrismaCompositeTextureLayout::plan`
  for 0–4 Shader Graph inputs both with and
  without GPU Auto Exposure, including the
  TEX8/TEX9 transition, 9/10 bound-slot count
  and rejection of 5 input textures.

**Actually performed for this continuation:** GitHub
readback of the test source and **11/11 static
source-contract checks** of the committed test.
These are NOT the CTest results. GitHub accepted
a new push workflow run
[44597082684](https://github.com/chromealex/AIVERSE/actions/runs/44597082684)
for `aa262d6b44546985429c0cda0abf0338f270c693`.
The prior run `44595377866` was **cancelled**
automatically when source tests were added.
The newest run was **queued** at the last observed
check. No native C++ compile or GPU run can be
claimed PASS at this point.

The next agent MUST check the **latest** run ID,
rather than relying on the originally queued one
in section 2 above. The Linux/macOS runners have
not begun running at the time recorded. Do not
automatically retry or cancel these jobs merely
to refresh documentation; no further source pushes
are necessary until actual compiler feedback arrives.


## Source-only closure / latest docs reconciliation

Subsequent documentation-only commits after the additional
`prisma_graph` test:
- `be9fb1b272efcc64089670de44feff462a6cd8c0` —
  this Stage-66 handoff updated to reference latest queued
  CI run and regression tests.
- `19b11febe3af0e06fafd62499bb6cc2a38914d91` —
  `skills/visuals/aiverse-prisma/SKILL.md` updated the
  authoritative latest run and shader-contract guidance.
- `2bb1582110ea8b187be7cc62ee3e0693371dd992` —
  `docs/prisma.md` no longer incorrectly labels the
  earlier cancelled workflow as the current run.

GitHub returned the current source-test workflow
`44597082684` as QUEUED, with Linux job
`135818178734` and macOS job `135818179217`
both QUEUED and no native test output. No further source
writes were made after the test commit, so this
documentation-only handoff update should not supersede
the queued source-tested SHA. Do not interpret tests
in source or queued runners as a native PASS. Next
action remains: inspect exact run job statuses,
logs and artifact outputs, then fix actual blockers.


## 2026-11-20 — lighting integration Stage 21

2026-11-20, integration/lighting-main-stage1-20261120.
Code 966963f676658ba3dc75c3ef0760e6c2eb7741a2.

Added Luma screen direct/denoise/GGX runtime, bounded quality policy, and optional
Renderer3D material/normal receiver hooks. New header APIs are additive; no save
format/default changes. Capture passes suppress profiling/ICB/debug output, use
PBR/graph material evaluation and independent depth. Legacy direct ownership
conflicts prevent composition. Fences cover compute and graphics texture readers.
Validated BVH before upload and checked actual dispatch encoding. HDR composition
uses premultiplied RGB/zero alpha, because generic Additive uses source alpha.

Build aiverse_hdr_scene/aiverse_hdr_gl on KINGSTON: PASS. Initial compilation
failed missing GiGpuBvhMetalShader, nested default initialization and assigning
C arrays; fixed. First actual pixel run found black output from alpha-zero
Additive: fixed and retested. Stage21 logs under KINGSTON build logs directory.
run-tests.sh --no-build --build-dir /Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120/build --filter '^lighting_(luma|graph_gi|dynamic_gi|terrain_gi|compute_gi)':
11/11 PASS, report 20261120-150547-51961. Luma nine analytic RGB cases passed:
128 sources/cluster overflow, off, deletion, metallic diffuse suppression, legacy
conflict, dielectric GGX, actual BVH shadow, geometry restoration, metallic GGX.
Existing Metal/GL GI model/terrain/graph acceptance remains PASS. diff --check PASS.
CI/push N/A. Full quick suite/editor/client, arm64 Release and mobile/Vulkan Luma
NOT RUN at this stage. No performance claim. Texture sources and normal maps
reuse the shader path but broad authored scenes need further visual acceptance.

Read docs/luma-native-lighting.md for host contract. Busy changed views can miss
Luma contribution; native host must address this before continuous lighting claims.
Luma host/editor adoption, animation GI, ICB GI, Release and platform acceptance
remain. Next: native Luma host/source ownership, followed by the remaining user
scope. Do not stop after this intermediate stage. All builds on KINGSTON.


## 2026-11-20 native Luma host

2026-11-20, integration/lighting-main-stage1-20261120; code 6839eafb7fa0589a0336ab4004b234e8985ff05f.
Added apps/renderbench/LumaHost.h, Bench integration, recipe docs and real-glTF
acceptance. Additive optional luma JSON key: backend auto/gpu, quality
balanced/high/ultra; legacy settings unchanged. One direct owner, explicit
fallback for unsupported backend/capacity, rejection of duplicate static/baked
ownership. GI independent. Source colors converted back to linear. Static
geometry captured before upload. Native host synchronizes previously committed
GPU work so moving cameras never silently omit Luma. Each view requires actual
composition or reports failure. Synchronization cost is included, not a speedup.

Built aiverse_renderbench/aiverse_renderbench_gl on KINGSTON, PASS.
run-tests.sh --no-build --build-dir /Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120/build --filter '^lighting_(luma_host|native_host)':
4/4 PASS, report 20261120-150855-58200. Real glTF, source on/off, moving camera,
legacy conflict rejection, Metal GPU and desktop GL fallback; existing GI host
regression PASS. git diff --check PASS. No CI/push.

Release arm64 configured successfully in KINGSTON root/release-arm64; first
renderbench/hdr_scene build RUNNING, not yet validated. Native session72016,
logs/release-arm64-build.log. Preserve this build; do not spawn a duplicate.
No editor/client/mobile/Vulkan hardware or production performance PASS implied.
Current static native host excludes animated casters; next stage: generic
animated pose geometry snapshots and automatic host publication. Then editor
settings/UI, ICB and remaining platform/performance gates. User asked finish ALL,
not just this stage; continue. All build/temp/artifacts on KINGSTON.
