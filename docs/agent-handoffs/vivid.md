# VIVID agent handoff — chronological log

This is the **canonical living handoff** for AIVERSE VIVID. Read `docs/vivid.md`, `AGENTS.md`, the active branch HEAD and this log before touching code. After each implementation or design stage, commit the implementation first and append its handoff in a **separate** commit on `feature/vivid-digital-humans`. Never claim unexecuted tests passed.

## Stage 0 — 2026-11-19 — architecture contract

- **Branch**: `feature/vivid-digital-humans`, created from main `d71c6d47e532b25fb816a8bb1f96c41e5275a165`.
- **Implementation/doc commit**: `4ae116ecd3b80ecc99104c0734139b5045798898`.
- **Files changed**: `docs/vivid.md` (new).
- **What works**: stable product scope, layer boundaries, ownership, requirements, staged validation contract. No executable VIVID implementation at this stage.
- **API/persistence**: no API introduced, no engine ABI or save format change.
- **Tests actually executed**: NOT RUN — design-only stage.
- **CI**: NOT RUN / no CI result.
- **Risks / dependencies**: existing AIVERSE 8 GPU morph-target limit, 80 palette joints per draw, four skin influences, FBX blend shapes ignored, no verified groom pipeline. Must avoid touching `engine/render/` ownership without coordination. OpenRigLogic/MetaHuman asset licensing not audited.
- **Next exact step**: create `engine/character/vivid/` pure C++20 semantic facial core and its native unit tests; link through a separate, opt-in CMake target. Keep its dependency graph clean; commit implementation and then append Stage 1 handoff with actual tests.

## Stage 1 — 2026-11-19 — semantic face runtime / CMake / tests authored

- **Branch**: `feature/vivid-digital-humans`.
- **Implementation and documentation commits**:
  - `986a52ec64057964a07e582af493df95b0162f4c` — typed facial API (`Face.h`).
  - `fa5ef617aed009c05caf09a1b79771530b9e16f5` — 52 channel names, layering, presets, temporal smoothing (`Face.cpp`).
  - `a29913e15e08f4f22dc94983d38e0c5b2cc09226` — `tests/vivid/face.cpp` (release-safe exception-based checks).
  - `12aea6665b1866ca829fe4c04b878ce450c9de76` — separate `AIVERSE::vivid` target and `vivid_face` CTest in local CMake.
  - `675ee74e73442da8d12ae1470639531ef2a7c893` + `a66c9fd61eff8bed45baaa4f4e2a0af4f4ecbe00` — root CMake add_subdirectory and corrective newline fix.
  - `8f6c8bfef09ca76e30f15c88464ce04f47470a6f` — subsystem `AGENTS.md`.
  - `44f5bf558a4b1340d542e0d31730b1139f8ad1e3` — API and limits in `docs/vivid.md`.
- **Public API**: `aiverse::vivid::Channel`, `FaceFrame`, `FaceController`, `Layer`, `Emotion`, `Viseme`; source uses 52 fixed channel indexes in canonical order. No persistence, ECS or network shape changed.
- **Actual verification**: used GitHub fetch to verify the two code files have exactly 52 enum members, exactly 52 unique strings, 1:1 enum/name ordering and a correct root CMake registration line. **Native compile, CTest and rendering NOT RUN**: local container cannot resolve github.com to fetch a complete repository; no AIVERSE checkout/build host is connected. No GPU/Metal claim.
- **Known limitations**: no glTF/FBX facial binding, no actual mesh deformation or rendering, only 6 authored viseme presets and 6 non-neutral emotion presets; no MetaHuman DNA parsing, no audio analysis, no eye/skin/groom shaders; responseSeconds retained by reset intentionally. Do not label as complete character system.
- **Build/test commands for a checkout (NOT RUN here)**:
  ```sh
  git fetch origin feature/vivid-digital-humans
  git switch feature/vivid-digital-humans
  cmake -S . -B build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build build/vivid --target aiverse_vivid_face_tests -j 4
  ctest --test-dir build/vivid --output-on-failure -R '^vivid_face$'
  ```
- **Next exact step**: implement `VividMorphBinding` bridge to `render::Model` / `render::Pose` with typed per-node 0..7 morph-index routes, validation and weight policies; author headless pose tests and document that native tests still require the AIVERSE build host. Keep 52 semantic channels separate from 8 legacy GPU morph slots.

## Stage 2 — 2026-11-19 — native-to-render::Pose adapter

- **Branch**: `feature/vivid-digital-humans`.
- **Implementation/doc commits**:
  - `ae4838657d6c2a08529db47728f72234b3e6ee9d` — public `MorphBinding.h` contract.
  - `a90d8c3b4348d305e12a7561abdf6fc0f33ca723` — validated adapter implementation.
  - `9a0b2f1c8faa6e84771efe20d301b6fbc3b65313` — headless `tests/vivid/morph_binding.cpp`.
  - `4b5a761fde6e57f02b3646863fd2ecfd9a602d3a` — `AIVERSE::vivid_render` CMake target, new `vivid_morph_binding` CTest.
  - `daddf30df5fadafe0dbaadc468bad9d48aebc8ff` — stage 2 usage/status in `docs/vivid.md`.
  - `5bea692d31bf83ed48269e619471afdde4de5571` — root `AGENTS.md` VIVID pointer.
- **Public API**: `MorphRoute{Channel, node, target, scale}`, immutable `MorphBinding::compile(Model, routes)`, `MorphBinding::apply(FaceFrame, Pose, influence, MorphApply)`. No game save/network ABI changes; `Model` and `Pose` layouts untouched. Adapter is opt-in; callers must invoke it **after** AnimationGraph evaluation.
- **Tests actually executed**: no native compile/CTest/GPU (NOT RUN; remote source was modified through GitHub connection, no checkout/build host available). Static GitHub fetch verified both sources and CMake definitions on this branch. CTest source includes invalid route, missing target, duplicate route, invalid source and pose, override/additive/preserve-other-values cases; these are **authored, not passing tests**.
- **CI**: NOT RUN / not checked.
- **Known limitations**: 8-slot GPU morph cap remains, bridge currently requires explicit routes with node/target indices; no user-facing asset config parser; not wired automatically into renderer/editor; no FBX blend-shape import, MetaHuman DNA/rig logic or real character appearance.
- **Build/test commands for checkout (NOT RUN here)**:
  ```sh
  git fetch origin feature/vivid-digital-humans
  git switch feature/vivid-digital-humans
  cmake -S . -B build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build build/vivid --target aiverse_vivid_face_tests aiverse_vivid_morph_tests -j 4
  ctest --test-dir build/vivid --output-on-failure -R '^(vivid_face|vivid_morph_binding)$'
  ```
- **Next exact step**: Stage 3 investigation/implementation of sparse >8-target morph buffer layout with backends and animation channel ABI compatibility, or first add validated asset-authored per-character route mapping and a CPU modelview preview. Test Stage 1+2 in the real checkout before changing shared renderer/animation sources. Follow root `AGENTS.md` build protocol and record measured numerical/visual results.

## Native validation checkpoint — 2026-11-19 — user-provided macOS run

- **Branch tested by user**: checkout at `/Users/aleksandrfeer/Projects/AIVERSE-vivid/build/vivid`, created from `origin/feature/vivid-digital-humans`. Exact tested SHA not included in terminal output; the test results should NOT be attributed to a later commit.
- **User-reported build**: `[100%] Built target aiverse_vivid_morph_tests`.
- **User-reported CTest**: `vivid_face` PASS (0.69 s), `vivid_morph_binding` PASS (0.38 s), **2/2 tests passed**, total real 1.08 s. These are the first actual native test results reported for Stages 1-2. Not independently rerun by this agent.
- **Unrelated shell errors**: after successful CTest, zsh attempted to execute pasted diagnostic/prompt text, generating `no matches found` and `command not found`; not a build/test failure.
- **GPU/visual**: NOT RUN. No rendering acceptance or MetaHuman import demonstrated.
- **Next**: stage 3 implementing an extensible CPU-reference facial morph representation and GPU-ready offline sparse buffers, without changing existing `Model` ABI or claiming that the engine's GPU render backend accepts >8 morphs.

## Stage 3A — 2026-11-19 — scalable sparse morph CPU/reference format

- **Branch**: `feature/vivid-digital-humans` (based on main `d71c6d47e532b25fb816a8bb1f96c41e5275a165`).
- **Implementation/document commits**:
  - `3c14d358b6c29682cbfa704af40829f3b2b95f22` — `SparseMorph.h` public geometry, immutable packed layout and CPU evaluation contract.
  - `4c57498612f982d9dcd8b7e76eefb7b81b386a71` — `SparseMorph.cpp` name/index validation, canonical sorting, zero-delta compaction and CPU deformation.
  - `a3eb0c30480a73ffa2028b94992a8b9c0a3cdfd9` — `tests/vivid/sparse_morph.cpp`.
  - `2a60fcbfaf61d7aa64400a2b86abd81b3d645757` — add `SparseMorph.cpp` to `aiverse_vivid`, register `vivid_sparse_morph`.
  - `ea7c3a5899cf18281d470467998bef0b92c872c6` — documentation design/capabilities/status.
- **Public API**: `MorphVertex`, `MorphDelta`, `MorphTargetInput`, `GpuMorphDelta` (64B align16), `GpuMorphRange` (16B align16), `SparseMorphLibrary::compile/evaluate`. Versioned GPU binary serialization NOT yet implemented; current ABI only defined in C++.
- **Compatibility**: no shared `render::Model`/RHI or ECS mutation; existing morph and animation pipeline unchanged. Core independent of render/genre.
- **Tests actually executed by this agent**: NOT RUN, no local AIVERSE checkout. Static source review only. Earlier user Mac run of Stage 1+2 reports 2/2 PASS, and says nothing about new test `vivid_sparse_morph`.
- **CI/GPU**: NOT RUN. GPU layout *specified*, not uploaded or dispatched.
- **Remaining limitations and risks**: sparse accumulation by target is a CPU oracle and not a usable GPU implementation because naive scatter would need atomics. Per-vertex gather adjacency must be compiled before backend implementation. Each delta uses 64 bytes, capacity fixed to 16M deltas/4096 targets. Names are case-sensitive. No MetaHuman import or actual model asset path. CPU evaluator assumes base/output are either identical spans or nonoverlapping buffers; partial overlap must not be provided.
- **Next exact step**: implement compiler to map a semantic `FaceFrame` to named sparse targets (one channel can affect multiple targets; no duplicate target ownership), and CPU tests. Then define and test GPU gather representation, choose how to integrate with existing `render::Model` and RHI without affecting other agents' renderer work.
- **Build/test on user's Mac (NOT RUN in this stage)**:
  ```bash
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_face_tests aiverse_vivid_morph_tests aiverse_vivid_sparse_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_(face|morph_binding|sparse_morph)$'
  ```

## Stage 3B — 2026-11-19 — typed semantic-to-sparse target routing

- **Branch**: `feature/vivid-digital-humans`.
- **Implementation SHA / paths**: `0c4e55901df73b06265336a81cbe07132a46574c` (`FaceShapeBinding.h`), `6f0ebd68d9d91797081d3fc81b9ded6eb74629b0` (`FaceShapeBinding.cpp`), `96ab1360bcb260a61f66708281e30eb22abd3e83` (`tests/vivid/shape_binding.cpp`), `c524927b58cf144bd0d65c38979fc95bd54b5fd3` (local CMake adds compilation/test), `feb841d717f51c6fea5663ce5d5550527c1e7fc7` (`docs/vivid.md`).
- **API**: `ShapeRoute`, `FaceShapeBinding::compile(SparseMorphLibrary, routes)`, `evaluate(FaceFrame, weights, influence)`. Case-sensitive names, one target destination per route; one channel may drive many targets. Explicit output length = library target count.
- **Compatibility**: pure portable C++20 in `AIVERSE::vivid`, no engine RHI/Model changes and no saved game/network schema changes.
- **Tests actually executed**: NOT RUN on this new source revision. New test `vivid_shape_binding` authored with 97 routes and end-to-end CPU reference. Earlier 2/2 tests user-reported PASS refer only to Stage 1+2.
- **CI/GPU**: NOT RUN. No actual render or GPU compute.
- **Next exact step**: add deterministic CSR per-vertex GPU gather adjacency derived from `SparseMorphLibrary` so shader can evaluate every vertex without float atomics. Do not rely on the current 8-morph `render::Pose` for 52+ channels. Then test GPU compute on Metal and GL/Vulkan.
- **Mac native test commands (NOT RUN yet)**:
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_face_tests aiverse_vivid_morph_tests aiverse_vivid_sparse_tests aiverse_vivid_shape_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_'
  ```

## Stage 3C — 2026-11-19 — immutable CSR adjacency for race-free GPU morph gather

- **Branch**: `feature/vivid-digital-humans`.
- **Implementation SHA / paths**: `f647646dbd136a84915ccb37d2bfe3ced744908a` (`SparseMorph.h`, public `GpuMorphLink` and CSR accessors); `1328583597b4605c2f0aab1333a656bf5c6c868e` (`SparseMorph.cpp`, CSR compiler and `evaluateGather()`); `90429d7c8a88c71a1900fa640c5cecdaca23c114` (`tests/vivid/sparse_morph.cpp`, CSR ordering/CPU parity fixtures); `e8de015b93d165aadc34ecbb1530a96d8edc6338` (`docs/vivid.md`, GPU handoff contract).
- **API/ABI**: Added `GpuMorphLink` (8 bytes), `SparseMorphLibrary::gpuVertexOffsets()`, `gpuVertexLinks()`, `evaluateGather()`; `bufferBytes()` now counts CSR allocations. No existing `render::Model`/Pose or save/network ABI changed. `SparseMorphLibrary` binary layout is still internal and not serialized into external assets; provide a schema version before persistence.
- **Implementation**: CSR gather is computed once at library compilation. Offsets count = vertices+1; links count = nonzero delta count and are ordered by target index per vertex. CPU gather mirrors intended one-thread-per-vertex shader, not requiring float atomics. CPU `evaluate()` remains a separate reference implementation.
- **Tests actually run**: NOT RUN on this revision, no build host attached to the coding session. Test fixture source is authored with 160-target parity, invalid input and expected offsets. Prior user-confirmed Stage 1+2 tests 2/2 PASS do not validate this new stage.
- **CI, Metal, Vulkan, GL**: NOT RUN. GPU kernel, dispatch/barrier integration, GPU buffers and character preview are not implemented. Do not claim performance or visual fidelity.
- **Risks**: CSR duplicates the target-major lookup (additional ~8 bytes/delta plus 4 bytes/vertex). Both formats remain until GPU backend chooses an optimal representation. Model vertex interleaving, skinning order and resource upload lifetime are unresolved and must be coordinated with renderer/texture RHI agents. Data is bounded to 16M vertices/deltas to avoid untrusted content overflows.
- **Next exact step**: native compile and full `^vivid_` CTest on user's Mac. If that passes, implement a backend-independent VIVID `GpuMorphDispatch` buffer/command descriptor and start a Metal compute backend behind an opt-in feature flag, comparing GPU readback vs `evaluateGather()`. Do not modify common RHI contracts without coordinating changes with texture/render agents.
- **Mac checkout and test commands (NOT RUN by this agent)**:
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_face_tests aiverse_vivid_morph_tests aiverse_vivid_sparse_tests aiverse_vivid_shape_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_'
  ```

## Stage 3D — 2026-11-19 — fix Xcode/Clang C++20 span compilation in shape test

- **Branch**: `feature/vivid-digital-humans`. **Fix commit**: `9b7e5b1e8e2cd6a25a842a25da0437de7756c354`.
- **Affected file**: `tests/vivid/shape_binding.cpp`. No runtime source, library API, ECS, network or persistence changes.
- **User's Mac evidence (before fix)**: Xcode 26.2 SDK libc++ rejected six `FaceShapeBinding::compile(library, {{...}})` calls because C++20 `std::span<const ShapeRoute>` cannot be built implicitly from a braced initializer list. The log contains 6 primary compiler errors and 60 secondary `note:` diagnostics; no `warning:` entries. The test executable failed to build, so CTest reported `vivid_shape_binding (Not Run)`. The other three tests — `vivid_face`, `vivid_sparse_morph`, `vivid_morph_binding` — **passed** in the user's pre-fix run (3/4).
- **Fix**: tests now use `rejectRoutes(library, std::initializer_list<ShapeRoute>)`, materialize an owned `std::vector<ShapeRoute>`, and pass it as a valid view to `compile`. All six invalid-input fixtures remain intact, including duplicate destinations; production API continues to accept `std::span<const ShapeRoute>` without acquiring ownership.
- **Tests actually executed by this agent**: local isolated C++20 Clang `-fsyntax-only` example of the exact owned-vector-to-span helper passed with `-Wall -Wextra -Wpedantic -Werror` (no warnings). Static inspection confirmed six replacements and zero remaining braced-list direct calls in `shape_binding.cpp`. **Full AIVERSE Mac build, CTest and GPU tests NOT RUN** in this session; require user's follow-up run before marking 4/4.
- **CI**: not run / not verified.
- **Next exact step**: pull latest branch in existing `~/Projects/AIVERSE-vivid` worktree, configure/build all four VIVID tests, run `ctest --output-on-failure -R '^vivid_'`. If warnings or errors remain, capture the full unformatted build log and repair the first primary compiler diagnostic; do not mask compiler warnings.
- **Mac commands**:
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_face_tests aiverse_vivid_morph_tests aiverse_vivid_sparse_tests aiverse_vivid_shape_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_'
  ```

## Validation checkpoint — 2026-11-19 — macOS VIVID 4/4

- **Verified source commit**: `31d04c986ada3ff4602d303d28fa4cdc08686de8` (VIVID Xcode `std::span` fix + Stage 3C), in `feature/vivid-digital-humans`. User's local worktree was detached at exactly this commit per the provided `git switch --detach origin/feature/vivid-digital-humans` output.
- **Documentation status commit**: `b50851f11b0068a9c20417a5a2545c2c8bcc04aa` (`docs/vivid.md`), following this user's validation report.
- **Actual test evidence**: user reported successful CMake configure/generate (4.1 s / 0.9 s) and completed builds of `aiverse_vivid`, `aiverse_vivid_face_tests`, `aiverse_vivid_morph_tests`, `aiverse_vivid_sparse_tests`, `aiverse_vivid_shape_tests` on macOS. CTest at `/Users/aleksandrfeer/Projects/AIVERSE-vivid/build/vivid`:
  - `vivid_face`: Passed (0.03 s).
  - `vivid_sparse_morph`: Passed (0.03 s).
  - `vivid_shape_binding`: Passed (0.61 s).
  - `vivid_morph_binding`: Passed (0.03 s).
  - **100% tests passed, 0 failed / 4, total real 0.71 s.**
- **Diagnostics**: The supplied post-fix output contains no `error:`, `warning:`, or `note:` compiler diagnostics; this is limited to the displayed incremental build, not a warning-as-error clean full rebuild.
- **Status interpretation**: Stages 1, 2, 3A, 3B, 3C now have user-confirmed local native unit tests on macOS for this exact revision. Testing was performed by the user; this agent recorded evidence but did not independently rerun it. GitHub CI, comprehensive engine suite, GPU shaders, render captures, cross-platform tests and MetaHuman DNA import remain **NOT RUN / NOT IMPLEMENTED as appropriate**.
- **API/save compatibility**: this checkpoint changes no executable API, engine ABI or game persistence, only documentation. Existing `render::Model` eight-morph GPU limit is still in effect.
- **Next actionable step**: begin Stage 4 as opt-in GPU morph execution, first inspect the existing RHI buffer/compute and model skinning contracts on current main and feature branches. Implement Metal compute dispatch and readback parity against `SparseMorphLibrary::evaluateGather`, with a backend-neutral command/resource interface, while keeping legacy rendering unchanged. Add real GPU tests and a separate implementation + handoff commit before claiming working GPU rendering.

## Stage 4A — 2026-11-19 — opt-in RHI GPU morph dispatch

- **Branch:** `feature/vivid-digital-humans`, previous checkpoint `2dfa495471e055adf69179db384db754934365e9`. **MotionWeave source inspected** at `feature/universal-ecs-animation-pipeline` HEAD `2ae54740331d7ed6ba8d8e01e6c39e753391852f`; **not merged**, because Stage 4A does not consume it and 913 unrelated commits/245 changed files would introduce large simultaneous integration risks.
- **Exact implementation SHAs/paths**:
  - `2eb2e3308f852b5cf424b5b7cd02b7b0f9d32318` — NEW `engine/character/vivid/VividComputeShaders.h` (matching Metal/GLSL CSR compute kernels).
  - `823c690802853aeeb84a64e6b78de9ebd67b1b84` — NEW `engine/character/vivid/GpuMorph.h` (48B GPU vertex, 32B uniform, backend-neutral API, resource ownership).
  - `fb32ea3b0027d90f87778bdd425d86728ec1bf10` — NEW `engine/character/vivid/GpuMorph.cpp` (six storage buffers, memory caps, weight validation, dispatch/readback/cleanup).
  - `9d641cd433df367127fc42eb8c832beb3a8f9ca0` — NEW `tests/vivid/gpu_contract.cpp` (recording RHI and error-path fixture).
  - `23370138ff37e5672bf9e427aabf16d10eb9a3cc` — `GpuMorph.cpp` correct buffer allocation size to avoid reading more bytes than 1–3 small weight values at initialization.
  - `6342d505f37610894c4b8e2ac60bc59ab8fe6f4d` — remove unused partial GPU fixture.
  - `9ed748eaa639f3d3e1484d25b0c696cc78bee4b3` — `engine/character/vivid/CMakeLists.txt` adds `AIVERSE::vivid_gpu` and `vivid_gpu_contract`.
  - `51d1b4accdbb50eeeff24cf9d060aff23fda52e0` — recording test correctly handles weight upload before an out-of-frame `beginCompute` rejection.
  - `cb4fc06813c897a62e4bb9f695ce97b082ca8d24` — `docs/vivid.md` feature documentation and MotionWeave compatibility plan.
- **What works at source level**: VIVID now supplies RHI-neutral `GpuMorphDeformer` for the existing `gfx::Device` compute contract, paired MSL/GLSL kernels, explicit GPU buffer packing, opt-in CMake target and headless fake-RHI fixture. CPU `SparseMorphLibrary::evaluateGather` remains a reference; no change to 8-morph legacy `render::Model` runtime.
- **Compatibility and public API**: `AIVERSE::vivid_gpu` adds `GpuMorphDeformer::initialize/dispatch/download/release/outputBuffer`, `GpuMorphVertex` and `GpuMorphUniforms`. No changes to `engine/gfx/Gfx.h`, Metal/GL/Vulkan backend implementation, MotionWeave, simulation, game saves or network.
- **Tests actually executed by this agent**: **NOT RUN** (the development environment does not have a full local AIVERSE checkout and cannot resolve github.com for clone). Static connector review of gfx::Device, Metal compute, source and CMake contracts performed. **New CTest `vivid_gpu_contract` has NOT RUN**; its fake backend validates command binding but **does not run a GPU shader**. User-confirmed earlier 4/4 native CPU CTest pass applies only to `31d04c986` and earlier.
- **CI, GPU execution, real device readback, screenshots, warnings**: NOT RUN/NOT CHECKED. No GPU-vs-CPU numerical comparison has actually been executed on Mac yet. Do not claim Metal correctness from the fake-backend test.
- **Known issues and risks**: RHI has no `inFrame()` query. `dispatch` may upload weights before `beginCompute` rejects an out-of-frame call; no GPU dispatch occurs, but it can increment upload metrics. Real GPU buffer synchronization/usage, Metal shader acceptance, render->skinning pipeline integration, GL/Vulkan compute support and resource lifetime must be tested. The output format is `GpuMorphVertex` (48B), not legacy `ModelVertex` (60B); never feed it straight into model draws. No 52+ GPU shapes render until the renderer explicitly uses this deformed stream.
- **MotionWeave handoff**: use presentation ECS `MotionWeave::update(presentationWorld,dt)` or persistent scheduler overload from `<aiverselib/motionweave/MotionWeave.h>` once that branch stabilizes. Final rig/retarget pose must remain MotionWeave-owned; VIVID applies optional facial offsets afterwards and deforms only the character's render stream. MotionWeave Stage 125 user deferred tests; do not call it production verified. Never copy/squash 913 animation commits into VIVID just for this GPU step.
- **Next exact step**: build and run all five VIVID CTests on user's Mac. Fix any compiler warnings/errors and append new handoff with exact test SHA. Then create a REAL Metal graphics-device test that launches compute, reads output between frames, compares to `SparseMorphLibrary::evaluateGather()` across 160 targets and checks normal/tangent parity. Add opt-in integration to the render-stream ownership only after GPU parity and resource lifetime tests pass. Coordinate with MotionWeave agent when entering final-pose integration.
- **Mac commands (NOT RUN by this agent)**:
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_face_tests aiverse_vivid_sparse_tests aiverse_vivid_shape_tests aiverse_vivid_morph_tests aiverse_vivid_gpu_contract_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_'
  ```

## Native validation — 2026-11-19 — user confirms Stage 4A 5/5

- **Source revision**: `b256a1ec35da33cc8f2a484551cc02e7b50ab822` (`feature/vivid-digital-humans` as last reported to user). Exact local `HEAD` not reprinted in this log, so the revision is inferred from the supplied pull/build instructions, not independently obtained from the Mac.
- **User-provided macOS build evidence**: `[100%] Built target aiverse_vivid_gpu`, compiled and linked `aiverse_vivid_gpu_contract_tests`; five CTest results PASS: `vivid_face` (0.15 s), `vivid_sparse_morph` (0.16 s), `vivid_shape_binding` (0.16 s), `vivid_gpu_contract` (0.53 s), `vivid_morph_binding` (0.16 s); **5/5 passed; total real time 1.18 s**. No warning/error lines in supplied excerpt.
- **Scope**: this validates CPU/reference and a **fake gfx::Device** recording contract. It does not validate shader compilation on Apple Metal hardware, GPU numerical parity, graphical face deformation, MotionWeave integration, cross-platform output or the full engine suite.
- **Actor of verification**: tests executed by user on Mac; this agent recorded the result, did not rerun CTest locally.
- **No runtime changes** for this checkpoint. Next: implement a macOS-only real-device Metal compute CTest, compare output readback after `endFrame` to `SparseMorphLibrary::evaluateGather`, preserve previous five tests.

## MotionWeave merge checkpoint — 2026-11-19

- **VIVID destination branch:** `feature/vivid-digital-humans`, source `feature/universal-ecs-animation-pipeline`.
- **Exact source snapshots:** VIVID parent `dfd4f3fab67cd573da4322ddad2f108a27f7cb38`; MotionWeave parent `548f75f0ce824db6f468f6577da95387d388ea0b`. Complete two-parent **merge commit `f27c076410a5cb0aa7d12cc689d83c7eff302928`**, source tree `c0f7a96eeb711bf5f895787212df479a3451203a`. MotionWeave snapshot tree `a72c12db735584caabb808ac5340ddd0effa49af`.
- **Resolution method:** PR #37 reported conflicts. Reconstructed normal Git merged tree using full MotionWeave tree as base, restored intact VIVID-added directories (`engine/character/vivid`, `tests/vivid`), docs (`docs/vivid.md`, `docs/agent-handoffs/vivid.md`) from the VIVID parent, and combined both branch deltas in the only shared edited files `AGENTS.md` and `CMakeLists.txt`. Created a true merge commit with both parents and advanced only VIVID with a fast-forward expected-head lease. MotionWeave branch and `main` did not move.
- **Verified using GitHub comparison and file fetch:** merged head `f27c0764` is ahead of MotionWeave snapshot by 52 commits and 0 behind; tree contains public `libraries/include/aiverselib/motionweave/MotionWeave.h`, `engine/character/vivid/GpuMorph.cpp`, VIVID's CMake registration, MotionWeave target `AIVERSE::MotionWeave` and respective docs/agents ownership.
- **Public API/save format:** no symbols renamed or removed deliberately; the merged branch now exposes MotionWeave's presentation ECS, scheduler, AnimationPose, retargeting, existing VIVID face controls and opt-in morph GPU. No game save/network migration. Before relying on APIs, compile on Mac; the merge may reveal latent incompatible changes or code-generation dependencies.
- **Documentation status commit:** `de8612040d52e49259ff96f04421e372da9d1cb2` (`docs/vivid.md`).
- **Tests actually run on this merged revision:** **NOT RUN**. Prior VIVID 5/5 Mac tests were on the pre-merge code. MotionWeave Stage 126 handoffs state deferred build/tests; do not claim any inherited CI pass or platform parity. No merged Metal GPU numerical readback has been run.
- **Risks:** MotionWeave includes extensive shared Renderer3D, Gfx, Model, render texture and LOD lifetime work. Test cross-module link order, FPS and RTS asset/presentation regressions, experimental GPU resource ownership, especially under hot-reload. If current MotionWeave source continues advancing, integrate new deltas deliberately after validation; do not copy worker or graph implementations into VIVID.
- **Next exact implementation step:** add a small bridge consuming MotionWeave `AnimationPoseComponent` **after** MotionWeave::update; produce a derived render::Pose with VIVID facial morph weights, never modify MotionWeave's cached pose or create a duplicate AnimationGraph. Tests must cover repeat-frame composition without weight accumulation and invalid pose. Then build/run all VIVID + MotionWeave tests on Mac.

## Stage 4B — 2026-11-19 — consume MotionWeave pose without extra animation pipeline

- **Branch:** `feature/vivid-digital-humans` (MotionWeave source `548f75f0ce824db6f468f6577da95387d388ea0b` merged previously as `f27c076410a5cb0aa7d12cc689d83c7eff302928`). PR #37 was opened during conflict diagnosis, then closed after the equivalent two-parent merge was completed manually; it was not an additional PR merge.
- **Implementation SHAs and files**:
  - `ddc93cd481eca7e108d3e6494090b0d18c2cf7c5` — `engine/character/vivid/MotionWeaveBridge.h` public post-pose composition API.
  - `a883e2f6ba3c3ff5be892db9d7626ce3fdb4e11e` — `MotionWeaveBridge.cpp`, caller-owned output/scratch pose double-buffer, copy final MotionWeave source and apply only VIVID morph weights, swap on success.
  - `e2fa835f689cf974bf2f32b5d6a574b4ec26bbb1` — `tests/vivid/motionweave_bridge.cpp`, MotionWeave ECS World + AnimationGraph bind fixture and repeated additive/no-drift checks.
  - `944b9a115c051518feb53bf4a16c83ad4551b5e3` — local VIVID CMake adds `AIVERSE::vivid_motionweave`, target and CTest `vivid_motionweave_bridge`.
  - `3096f54f337ef332b95fd6a191148c6e733c6244` — root CMake moves VIVID add_subdirectory AFTER declaring the `AIVERSE::MotionWeave` alias/interface to support linking correctly.
  - `62bf6edd715d99642f6474e0578d0f4671718801` — add headless `aiverse::platform::readContentFile` fixture hook, matching MotionWeave's existing standalone native test.
  - `82e0b760e9bd5f519e9914ffef1d1c151d4021cf` — `docs/vivid.md` usage and status.
- **Public API**: `aiverse::vivid::composeFaceAfterMotionWeave(const aiverse::motionweave::AnimationPoseComponent&, const FaceFrame&, const MorphBinding&, render::Pose& output, render::Pose& scratch, float influence, MorphApply)`. Output/scratch must differ and must not alias MotionWeave's cached local pose. Invalid source/influence/binding must not change the previously completed output.
- **Ownership:** MotionWeave is the only writer of skeletal pose, AnimationGraph state, workers, IK and retargeting. VIVID derives a separate final render pose with legacy facial morph channels; no new animation scheduler/graph/ECS world. Only 0..7 `render::Pose` morph indices are used in this adapter; VIVID's separate sparse GPU stream remains 52+ capable but not yet consumed by renderer.
- **Tests actually run:** **NOT RUN** on this merged revision. Earlier user reported 5/5 VIVID native tests PASS before merge; these do not cover combined MotionWeave CMake or the new bridge. No Mac build, GitHub CI, GPU readback, visual capture or full suite has verified this integration.
- **Risks and limitations:** MotionWeave feature branch contains extensive yet-unverified resource and renderer changes; merged tree should be configured in a fresh build directory. The new adapter intentionally does not change bone matrices for facial jaw or eye joints, and it must execute after MotionWeave worker tasks are complete. A separate owned render pose is necessary to avoid accumulating facial values when MotionWeave animation LOD throttles pose evaluation.
- **Next exact step:** on Mac, fetch/reselect merged `feature/vivid-digital-humans` in existing worktree, configure with `AIVERSE_BUILD_TESTS=ON`, build six VIVID tests including `aiverse_vivid_motionweave_tests`, run `ctest -R '^vivid_'`. Build and run MotionWeave's standalone `aiverse_motionweave_api_tests` too; then consider full graphics/preview integration. Fix any first compiler warning/error and append a new handoff with actual results.
- **Mac verification commands (NOT RUN in this stage):**
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_face_tests aiverse_vivid_sparse_tests aiverse_vivid_shape_tests aiverse_vivid_morph_tests aiverse_vivid_gpu_contract_tests aiverse_vivid_motionweave_tests aiverse_motionweave_api_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^(vivid_|motionweave_api$)'
  ```

## Stage 4C — 2026-11-19 — MotionWeave ECS visibility compile fix

- **Branch:** `feature/vivid-digital-humans`. Merge source snapshot MotionWeave `548f75f0ce824db6f468f6577da95387d388ea0b`; original `feature/universal-ecs-animation-pipeline` was **not modified**. Previous VIVID integration HEAD `65ef792af7f7650a4701caa3fc5a0f433184d4d3`.
- **External build evidence from user:** In `~/Projects/AIVERSE-vivid/build/vivid`, Clang/Xcode compilation of `aiverse_vivid_motionweave` failed at `libraries/include/aiverselib/animation/MotionWeaveScheduler.h` lines 53, 54, 55, 62. Four primary errors: `World::poolIfExists<T>()` is private in `engine/core/ECS.h:548`; the many `note` messages only identify that declaration. No standalone `warning:` messages appear in the shared log. Other previously built tests still passed: `vivid_face`, `vivid_sparse_morph`, `vivid_shape_binding`, `vivid_gpu_contract`, and `vivid_morph_binding` — **5 passed**. `motionweave_api` and `vivid_motionweave_bridge` were **Not Run** because their executables did not exist after the failed build. CTest reported 5/7 effective passes, 2 Not Run. Do not describe these as runtime test failures.
- **Code fix commit `3b413656fafecc54c466baa5ab6b83681a43cb6d`:** replace the four private `read.poolIfExists<Component>()` accesses with public `read.tryGetConst<Component>(e)` inside the existing per-entity loop. Read-only, no implicit pool creation, no COW detachment; retains use of `prepareAnimationPresentation` for explicit writable pools and MotionWeave's existing worker/barrier logic. No `World` API exposure or ECS layout change.
- **Regression fixture commit `c1f76ed9bb0c21d0b3056225d0666ee29b81a938`:** extend `tests/animation/motionweave_api.cpp` to invoke the three-argument `MotionWeave::update` with a persistent 0-worker scheduler, assert exactly one eligible actor, check that missing optional Inputs/WorkHint/Budget components do not increase `World::poolCount()`, and verify a valid pose.
- **Public API/ABI and persistence:** unchanged. MotionWeave scheduler now uses the existing public const entity accessor; no genre modules, renderer, VIVID morph ABI, save or network schema touched.
- **Tests actually executed after fix:** **NOT RUN** by this agent. GitHub source diff/inspection verified replacement of the four private API uses and presence of the new test; native Xcode compilation and CI still require confirmation.
- **Risks/limitations:** Additional latent compilation failures in other deferred MotionWeave tests may emerge after this first error. Do not suppress compiler diagnostics globally or weaken `World` encapsulation. Original MotionWeave feature branch needs this fix separately upstream after its owner coordinates cherry-pick.
- **Next exact step:** on user's Mac, fetch VIVID HEAD and build both formerly missing targets **before** running CTest. If either build fails, capture the first compiler error and do not run CTest on stale executables. After success, run six VIVID tests plus `motionweave_api`, then full MotionWeave standalone tests as a separate compatibility gate.
- **Mac commands (NOT RUN in this stage):**
  ```bash
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_motionweave_tests aiverse_motionweave_api_tests -j 4 &&
    ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^(vivid_|motionweave_api$)'
  ```

## macOS native validation — 2026-11-19 — 7/7 integration tests

- **Verified branch state at request:** `feature/vivid-digital-humans` head `5013fe6d96796abaae4f0596d9129dac33f33729`. The user's excerpt does not repeat a local `git rev-parse HEAD`; this is the latest GitHub state given to the user before their run, and the exact tested local SHA should be checked when running subsequent commands.
- **User-executed build:** `aiverse_engine` and `aiverse_models` targets built; `aiverse_motionweave_api_tests` compiled and linked successfully. No warnings/errors shown in supplied output.
- **User-executed CTest**, `/Users/aleksandrfeer/Projects/AIVERSE-vivid/build/vivid`: `motionweave_api` PASS (1.03s), `vivid_face` PASS (0.14s), `vivid_sparse_morph` PASS (0.15s), `vivid_shape_binding` PASS (0.15s), `vivid_gpu_contract` PASS (0.16s), `vivid_motionweave_bridge` PASS (0.79s), `vivid_morph_binding` PASS (0.15s). **7/7 tests passed, 0 failed, 2.59 seconds**. These are the first reported passes of the integrated MotionWeave bridge, resolving Stage 4C's previous `World::poolIfExists` compilation failure.
- **Native/CI status:** Real macOS execution reported by the user. Not rerun by this agent and not a verified GitHub Actions CI run.
- **Scope:** all seven selected CPU and fake-RHI contract tests passed; this is **not** a native Metal shader compilation/readback result or proof of the full MotionWeave test suite, renderer/skin integration, Vulkan/GL support, or visual correctness.
- **Next action:** add an isolated macOS Metal shader parity CTest running the real `kVividMorphMSL` kernel against 160 named morph targets; then move to a separate gfx::Device integration test. Keep build status separate for each new commit.

## Stage 4D — 2026-11-19 — real Apple Metal shader/CSR CPU parity fixture

- **Branch:** `feature/vivid-digital-humans`. This stage starts after the user's successful 7/7 VIVID + MotionWeave native tests on the pre-test branch HEAD `5013fe6d96796abaae4f0596d9129dac33f33729`; validation record commit `b27728c6cd01029b7d10570eb947a9ce9a5fe14f`.
- **Implementation commits / changed paths:**
  - `ba6bfcdbbe5a95ab2da279adcfb83804d4b618f6` — new `tests/vivid/metal_gpu.mm` runtime MSL kernel compilation, GPU buffers and 3 parity passes with 160 morph targets / 137 vertices.
  - `ff7eaa56292b91d79333a2c30e31a38e82435a4e` — add required standard C++ includes to Objective-C++ fixture.
  - `a296aaf3b1d3992873d2bb49612ca0dc44ae84f5` — CMake macOS-only `aiverse_vivid_metal_gpu_tests` and `vivid_metal_gpu` registration, Metal/Foundation frameworks, Objective-C++ and ARC, SKIP_RETURN_CODE=77 when no Metal device.
  - `c35b6a576900b008d0f225d241aa164b7c9ea210` — `docs/vivid.md` stage 4D design/status.
- **Architecture / behavior:** The direct macOS Metal test compiles the unchanged `kVividMorphMSL` from `engine/character/vivid/VividComputeShaders.h`, constructs `SparseMorphLibrary` CSR with three sparse deltas per target, creates native Metal shared buffers, dispatches 64 threads per group and compares GPU bytes after `waitUntilCompleted` against the CPU `evaluateGather` oracle. 137 vertices forces tail-thread bounds checking. The three passes verify normalised directions, raw directions and zero weights. All vertex positions, normals and tangents require max absolute error ≤ 5e-4; nonfinite values/pipeline errors fail.
- **Public API, persistence, compatibility:** no changes to VIVID core ABI, MotionWeave, Renderer3D, gfx::Device, model asset layout, ECS components, save/network formats. This is an isolated opt-in test target on macOS; other platforms do not compile Objective-C++ or link Metal.
- **Tests actually executed in this stage:** **NOT RUN**. No macOS Metal device is attached to this connected-GitHub coding session. Previous 7/7 user-reported CTests are real but predate this stage; **do not** report `vivid_metal_gpu` as passing. Native GPU test binary and MSL compilation remain unverified.
- **CI:** NOT RUN / not checked for this new commit.
- **Known risks:** Direct MTLDevice dispatch tests the shader and packed ABI independently of the RHI; it does not exercise `GpuMorphDeformer` inside `gfx::Device` begin/endFrame or readBuffer. Host GPU availability/compilers vary; the test allows a skip on machines without a Metal device and fails for genuine shader/command errors. Any Xcode compile warning/error must be fixed at the first primary diagnostic; do not silence warnings globally.
- **Next exact step:** macOS owner should fetch this VIVID branch, configure, build `aiverse_vivid_metal_gpu_tests` and run `ctest -R '^(vivid_|motionweave_api$)'`. Expect **8 tests registered** (7 previous + native Metal parity). If the Metal GPU test passes, add a separate real `gfx::Device` Metal dispatch/readback fixture and then integrate output into model render/skinning, while reusing MotionWeave's pose ownership. Append a new handoff recording actual Mac results.
- **Exact commands (NOT RUN here):**
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_metal_gpu_tests aiverse_vivid_gpu_contract_tests aiverse_vivid_motionweave_tests aiverse_motionweave_api_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^(vivid_|motionweave_api$)'
  ```

### Stage 4D build compatibility addendum — Objective-C++20

- **Follow-up code commit:** `ecefa8a6cb285e8a3c32a08807d5e36939f7eaf6` (`engine/character/vivid/CMakeLists.txt`): explicitly sets `OBJCXX_STANDARD=20`, `OBJCXX_STANDARD_REQUIRED=ON`, `OBJCXX_EXTENSIONS=OFF` for `aiverse_vivid_metal_gpu_tests`, because the test's `.mm` file includes VIVID C++20 `std::span` headers and a CXX-only compile feature is not sufficient to guarantee Objective-C++ language mode. This does not change any other target.
- **Tests run on this follow-up:** NOT RUN. The real Metal GPU test still requires the user's Xcode/macOS host. Treat Stage 4D as unverified until that native build/CTest is reported.

## Stage 4E — 2026-11-19 — fix macOS Objective-C++ CMake generation

- **Branch:** `feature/vivid-digital-humans`; prior HEAD `2114f5e7293fa78d287e5cf1a2e539ff50c32c6d`.
- **Mac repro supplied by user:** `git fetch` and detached checkout to `2114f5e72` succeeded; macOS CMake recognized AppleClang 17.0.0 Objective-C++ and completed configure, but failed *generate* repeatedly: `CMake Error: Error required internal CMake variable not set ... CMAKE_OBJCXX_COMPILE_OBJECT`. The failure is not a Metal shader compile error or a test failure. No fresh 8-test run was executed in this log.
- **Root cause:** `enable_language(OBJCXX)` was incorrectly called inside `engine/character/vivid/CMakeLists.txt`; CMake's language initialization must occur at the highest common directory of affected targets. The generated compile rule was not available throughout the root's target graph. `OBJCXX_STANDARD=20` alone cannot initialize the language.
- **Implementation commits:**
  - `6f9e6fbd6885e7fd39fa195b62cc1e87c3225c6b` — `CMakeLists.txt`: after platform detection and `AIVERSE_BUILD_TESTS` option, conditionally call `enable_language(OBJCXX)` for macOS test configurations, before declaring build targets or adding the VIVID subdirectory.
  - `09f389a293bc1d223824afaad1f63b116998f18d` — `engine/character/vivid/CMakeLists.txt`: remove the nested `enable_language(OBJCXX)` call. Retain the VIVID-only Metal GPU test target, `OBJCXX_STANDARD 20`, ARC and Metal/Foundation frameworks.
- **Verification actually performed:** remote GitHub file inspection confirmed **one** `enable_language(OBJCXX)` at root line 45, after the tests option at line 39 and before VIVID `add_subdirectory` at line 524; the VIVID subdirectory has no language-enabling call and retains its macOS-only test definition. **Native CMake generation, compilation, CTest and Metal readback NOT RUN after this fix** by the assistant; no Mac build runner is available through GitHub. User's previous 7/7 CPU and MotionWeave test result predates Stage 4D and is unaffected in source.
- **Scope/compatibility:** CMake-only change; no animation, renderer, shader, VIVID public API, save/network schema or RHI ABI modifications; macOS tests OFF and non-macOS targets should not enable Objective-C++ through VIVID.
- **Known risks:** an existing CMake build directory may retain inconsistent state from the failed generation; retry same directory first, and if the generator still fails, configure into a **fresh** directory instead of patching generated CMake variables. The real `metal_gpu.mm` test has never compiled/run and could expose a separate Clang/Metal diagnostic next.
- **Next exact step / owner Mac commands (NOT RUN by assistant):**
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON &&
    cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_metal_gpu_tests -j 4 &&
    ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_metal_gpu$'
  ```
  If stale generation persists: substitute a *new* `-B ../AIVERSE-vivid/build/vivid-metal-clean` for both configure/build/CTest paths. Run the 8-test `^(vivid_|motionweave_api$)` selection only after building the corresponding targets. Append exact results to this handoff.

## User macOS validation — 2026-11-19 — real Metal kernel PASS

- **Revision tested:** `424e8382144511c1b453ce9d3234cc813734356c` confirmed explicitly by the user's `git switch --detach` output from `origin/feature/vivid-digital-humans`.
- **Actual user-provided evidence:** CMake configured in 5.7 seconds and generated in 1.1 seconds; `aiverse_vivid_gpu` and Objective-C++ `aiverse_vivid_metal_gpu_tests` compiled and linked. `ctest -R '^vivid_metal_gpu$'`: `vivid_metal_gpu` **Passed** (0.66 s), 1/1 tests passed, 0 failed, total real time **0.68 s**. No compiler note/warning/error diagnostics appeared in the supplied output.
- **What this verifies:** native Apple Metal compiler and execution accepted `kVividMorphMSL`, with 160 sparse targets, 137 vertices, three dispatch modes and readback comparison to `SparseMorphLibrary::evaluateGather`. This was executed by the user on macOS, not independently by this agent.
- **Still NOT RUN / NOT VERIFIED:** real `gfx::Device` RHI-backed dispatch/readback; gfx vertex stream + MotionWeave skinning; visual render, shadows, memory/latency benchmark, Vulkan/GL; full merged 8-test suite at revision `424e838` (previous VIVID+MotionWeave 7/7 were separately confirmed on earlier commit).
- **API/persistence:** no production code changes from this validation checkpoint; `main` and MotionWeave source branch untouched.
- **Next exact step:** build isolated macOS real `gfx::Device` lifecycle/dispatch/readback parity test on new source commit, record exact results and warnings. Do not treat direct native Metal parity as automatically proving RHI integration.

## Stage 4F — 2026-11-19 — real gfx::Device Metal RHI parity

- **Branch:** `feature/vivid-digital-humans`. Previous source revision `424e8382144511c1b453ce9d3234cc813734356c`; confirmation handoff commit `e98b74ce948e25b182a0900cbf93a9a79aef8a3a`.
- **User's actual validation at previous stage:** `vivid_metal_gpu` **1/1 PASS**, total 0.68 s on a real Mac GPU; CMake configure/generate and OBJCXX compilation completed. This is the native kernel test from Stage 4D, **not** the RHI test added in this stage.
- **Implementation SHAs / changed paths**:
  - `88f035bfee3b504c7d61e4d11d7d89950b090824` — new `tests/vivid/metal_rhi.mm`, uses the real AIVERSE Metal `gfx::Device`, `GpuMorphDeformer`, four GPU frames, 160 targets, 79 vertices, CPU gather comparison.
  - `2991bd678ffac4a987a430bd9bfcb08f049a6166` — `engine/character/vivid/CMakeLists.txt` registers macOS-only `aiverse_vivid_metal_rhi_tests` / CTest `vivid_metal_rhi`, compiles the existing `engine/gfx/metal/GfxMetal.mm` into a standalone native test binary, links `AIVERSE::vivid_gpu`, ARC/Objective-C++20, Metal/QuartzCore/Foundation.
  - `ed4569d73d0e6ce23ebabf3fe33982c0d8559de7` — `docs/vivid.md` Stage 4F overview, test constraints and scope.
- **Runtime behavior tested by authored fixture (not yet executed):** create headless `CAMetalLayer` and `gfx::createMetalDevice`; initialize 6 VIVID storage buffers once; call `beginFrame` / `GpuMorphDeformer::dispatch` / `endFrame` / `download`; read back after each completed frame and compare all position/normal/tangent components to `SparseMorphLibrary::evaluateGather`. Frames vary weights/normalization to catch buffer updates, synchronization and stale output. Per-component absolute tolerance ≤ 5e-4. Release VIVID before gfx device teardown. Empty optional shader-archive platform hook is scoped to this executable, not game runtime.
- **API/save/network:** no production public ABI, `engine/gfx/Gfx.h`, MotionWeave, simulation or persistence changes. New test source/link definitions only.
- **Tests actually executed for this stage:** **NOT RUN**. The user has **not** compiled the new RHI test. Native Metal compiler, headless CAMetalLayer behavior, Metal backend linkage, output parity and resource cleanup remain unknown. GitHub structure verified; do not claim 9/9 or `vivid_metal_rhi` Passed until Mac reports it. No CI result.
- **Potential blockers:** headless `nextDrawable` behavior varies with OS, though compute command buffers should not depend on presentation. Linking `GfxMetal.mm` standalone may uncover additional platform dependencies. Check the first real build error instead of editing shared graphics backend unnecessarily; if the device cannot supply a drawable, consider an explicit graphics test harness or a small backend factory allowing an offscreen GPU frame instead of pretending the test passed.
- **Next exact action / Mac commands (NOT RUN by agent):**
  ```bash
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_metal_rhi_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_metal_rhi$'
  ```
- **After real RHI parity:** integrate opt-in extended facial morph stream with MotionWeave skeleton and common `Renderer3D` material/shadow passes using stable buffer ownership. The existing `render::Model` eight-target path must remain unchanged for non-VIVID assets until visual parity and runtime benchmarks prove the transition. Record numerical/screenshot/real GPU measurements in the handoff after each bounded stage.

## User-verified macOS Metal RHI — 2026-11-19 — Stage 4F accepted

- **Exact checked source SHA:** `22b60c4714c257e9714bcfd48943f4f62f1fb9d5`; user explicitly reported detached worktree HEAD `22b60c471`.
- **User-executed build:** `cmake` configured in 5.3 s, generated in 1.2 s; `aiverse_vivid_gpu` and real `aiverse_vivid_metal_rhi_tests` (including Objective-C++ `tests/vivid/metal_rhi.mm` and existing `GfxMetal.mm`) compiled and linked successfully.
- **User-executed test:** `ctest -R '^vivid_metal_rhi$'` → `vivid_metal_rhi` **Passed** (1.17 s), **1/1 passed, 0 failed**, total real 1.18 s. No errors, notes or warnings in supplied output.
- **Meaning:** on the tested Apple hardware, the existing AIVERSE Metal `gfx::Device` path successfully created GPU resources, encoded multiple VIVID CSR morph compute frames and read back results within the test's 5e-4 CPU `SparseMorphLibrary::evaluateGather` tolerance. This is stronger than the direct-Metal-kernel test (Stage 4D); both are now user-confirmed PASS at their respective SHA.
- **Verification provenance:** Mac test executed by user; this agent is recording the reported terminal output, not claiming an independent device run. No full 9-test CTest run on this SHA, graphics mesh render, shadows, MotionWeave skinning integration, performance benchmarks or Vulkan/GL verification yet.
- **Compatibility:** this checkpoint changes documentation only; no model ABI or genre-specific code was modified.
- **Next exact step:** inspect the actual `render::Vertex` / `Model` / `Renderer3D` layout and GPU skinning order. Build an opt-in VIVID render-stream integration adapter preserving the original model path, with explicit buffer ownership and a regression fixture. Only after native build + screenshots may it be enabled for characters.

## Stage 5A — 2026-11-19 — GPU ModelVertex stream repacking

- **Branch:** `feature/vivid-digital-humans`. Pre-stage confirmed user native Metal RHI source `22b60c4714c257e9714bcfd48943f4f62f1fb9d5` passed 1/1, 1.18s total. Validation-only commit `3ec4b5e5a757de9abb6390a5ed3cc9a9b2f443f8` records it. New Stage 5A commits are **not** covered by that pass.
- **Implementation commits and exact paths:**
  - `ccd59be309192a2cec734258bed62be714472aab` — NEW `engine/character/vivid/GpuMorphMeshStream.h`: model-compatible 60B GPU output, ABI assertions, public opt-in class, no direct Renderer3D ownership.
  - `b2c0bcda150d49107997223063528054f5ea933e` — NEW `engine/character/vivid/GpuMorphMeshShaders.h`: paired Metal/GLSL compute kernels that preserve 15 packed 32-bit words from `ModelVertex`, replace only morphed position/normal/tangent.xyz.
  - `f24365d7d4cd646498f4e60e3745cd517ba13c20` — NEW `engine/character/vivid/GpuMorphMeshStream.cpp`: opt-in separate compute pipeline with resource validation, 512MiB budget, dispatch/download/release.
  - `c6750f35c1e8c0633cdd37ecc36e9de377341b6a` — `engine/character/vivid/CMakeLists.txt`: `AIVERSE::vivid_mesh` static library and link into the real Metal RHI test target.
  - `bc2ab74663ff7898b97addd64c49e868e45a5d48` — `tests/vivid/metal_rhi.mm`: four-frame actual Metal RHI pass now includes mesh conversion and CPU parity, asserts bitwise preservation of UV/colour/tangent handedness/joints/weights.
  - `6895ba3a67f2651e37eaccd6b552d5c32e0217d9` — correct a malformed newline character literal in test diagnostics before compilation.
  - `a696eceef4e450555bd950fce5c2d02487b7f6a2` — `docs/vivid.md` technical API and compatibility limitations.
- **Functional path:** VIVID sparse 48-byte `GpuMorphVertex` → `GpuMorphMeshStream` GPU repack to full 60-byte `render::ModelVertex`. Same model vertex indices are preserved. Copy all original 15 × uint32 words per vertex, replace offsets 0–2 (pos), 3–5 (normal), 8–10 (tangent xyz), and preserve UV, tangent sign, colour and skeletal joint influences bitwise. Model shaders already deform before skin. No duplicate skeletal animation: MotionWeave still owns pose, retargeting, IK, worker scheduler and bone palettes.
- **Public API and compatibility:** new `GpuMorphMeshStream::initialize/dispatch/vertexBuffer/download/release` and `AIVERSE::vivid_mesh` only; no `ModelVertex`, `ModelDraw`, `Renderer3D`, `gfx::Device` or MotionWeave ABI changes. No saved game/network format changes. Vertex output is a `gfx::BufferType::Storage` allocation containing valid 60B words; renderer has not yet been changed to use it.
- **Tests actually run:** **NOT RUN** for new Stage 5A code. GitHub source fetch and layout review only. The modified `vivid_metal_rhi` fixture was written and statically inspected, but Mac compilation, actual Metal shader compilation, runtime parity, full CTest, and renderer screenshots remain outstanding. CI NOT RUN. Prior pre-stage user-confirmed Metal `vivid_metal_rhi` 1/1 PASS cannot be used as Stage 5A test evidence.
- **Risks:** GPU buffer type Storage-as-vertex use must be checked on each RHI, especially Vulkan/GL. Existing `render::ModelDraw` binds `model->vb`; this stage does NOT change normal draw behavior. Renderer colour/shadows/geometry pool/ICB/LOD must explicitly opt in and preserve full indexed stream offsets and device resource retirement. A fixed 512MiB maximum limits this bridge but does not solve all dynamic streaming or cross-frame draw hazards.
- **Next exact step:** user Mac: fetch current VIVID, configure, build `aiverse_vivid_metal_rhi_tests` and `aiverse_vivid_metal_gpu_tests`, run `vivid_metal_rhi` + `vivid_metal_gpu`, record compiler warnings and numerical parity. Then implement an opt-in per-ModelDraw vertex-stream override in Renderer3D (with direct draw/colour/shadow parity first, batching/ICB initially disabled and tested separately) rather than bypassing legacy UV/bone attributes. Append handoff for every implementation stage.
- **Commands for owner (not run by assistant):**
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_metal_rhi_tests aiverse_vivid_metal_gpu_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_metal_(rhi|gpu)$'
  ```

## Stage 5B — 2026-11-19 — reserved MSL identifier and actionable failure diagnostics

- **Branch:** `feature/vivid-digital-humans`; previous Stage 5A HEAD reported in user verification: `208b4d37c06f49ba774c14dfc9a3a96fb57bab25`.
- **Actual user-run macOS evidence before fix:** `aiverse_vivid_mesh`, `aiverse_vivid_metal_rhi_tests` and `aiverse_vivid_metal_gpu_tests` compiled and linked. `vivid_metal_gpu` **Passed** (0.12 s); `vivid_metal_rhi` **Failed** (2.03 s) with `VIVID RHI 60-byte mesh stream initialization failed`, prior to mesh-conversion dispatch. CTest: **1/2 passed, 1 failed, 2.16 s**; output did not contain compiler warnings or notes. The old Metal deformer path was previously independently confirmed, but the new 60B packing path is **NOT** verified.
- **Root-cause analysis:** `GpuMorphMeshStream::initialize` can fail on no compute, invalid count/budget, `gfx::Device::createCompute`, or GPU storage allocation. Valid small test dimensions and a previously working Metal compute backend narrow this to new shader/pipeline compilation as the leading hypothesis. Source inspection shows MSL kernel parameter `uint vertex [[thread_position_in_grid]]`; `vertex` is the Metal shader-stage keyword. This inference has not yet been confirmed with the native Metal compiler diagnostic.
- **Implementation commits / files:**
  - `af74fb9c23139de9d20d8cfb506dbc83387754f3` — `engine/character/vivid/GpuMorphMeshShaders.h`: rename Metal compute parameter to `vertexIndex` and update every use, leaving GLSL source untouched.
  - `3488f98b5e444976786315e98e6b0eec4436b446` — `tests/vivid/metal_rhi.mm`: on failed stream initialization, include `meshStream.lastError()` and `gfx::Device::lastError()` in the thrown error. Future Metal compiler/allocator failures will be visible instead of generic.
  - `f434e01bdc349528a7337cdd09942c549a0b3a17` — `docs/vivid.md` Stage 5B source explanation and actual failed test status.
- **Public API / ABI / persistence compatibility:** neither `ModelVertex` 60B nor `GpuMorphVertex` 48B layout changed; no production `GpuMorphMeshStream` C++ signatures, render contracts, CMake targets, MotionWeave, simulation, save/network changes. Only the internal MSL variable naming and test diagnostics changed.
- **Tests actually executed AFTER fix:** **NOT RUN**. GitHub fetched modified source for verification; no remote macOS execution tool is available. Do not claim the user-visible `vivid_metal_rhi` is passing before rerun.
- **Known risks / next step:** if initialization still fails, the new error path should reveal the exact MSL compile error or allocation error; fix that primary diagnostic first. If it compiles, the test's four-frame output comparison will validate that 60B ModelVertex UV, colours, tangent handedness and joint data remain intact while positions, normals and tangents are morphed.
- **Mac commands (unrun by assistant):**
  ```bash
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON &&
    cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_metal_rhi_tests aiverse_vivid_metal_gpu_tests -j 4 &&
    ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_metal_(rhi|gpu)$'
  ```

## Stage 5B validation checkpoint — 2026-11-19 — 60-byte Metal vertex stream PASS

- **Source revision under test:** user followed the latest `feature/vivid-digital-humans` fetch/checkout after Stage 5B (`60f95a95e32dda1886833cbce374f7238e0481be`). The supplied terminal excerpt does not independently repeat `git rev-parse HEAD`; attribute this to the latest user-instructed branch revision, not to future commits.
- **Mac build evidence supplied by user:** `aiverse_models`, `aiverse_vivid_mesh`, `aiverse_vivid_metal_rhi_tests`, and `aiverse_vivid_metal_gpu_tests` compiled/linked. No compiler `error:`, `warning:` or `note:` shown.
- **Native GPU test results (user executed):** `vivid_metal_gpu` PASS (0.94 s); `vivid_metal_rhi` PASS (0.75 s); **2/2 Passed, 0 Failed; total real 1.71 s.** The extended real RHI test checks the packed 60-byte `ModelVertex` stream for four GPU frames, CPU numeric parity and bitwise preservation of UV, colour, tangent sign, joint indices and skin weights.
- **Limitations:** no full 9-test suite executed after this shader fix, no Renderer3D draw/visual, no shadow and LOD parity, and no measured FPS or GPU time. Apple Metal only; Vulkan/GL and MotionWeave full suite remain separate gates.
- **Next exact step:** implement a strictly opt-in `ModelDraw` vertex-buffer override (same 60-byte ModelVertex format) with explicit capability, lifetime and fallback validation, routing both colour and shadow direct draws while bypassing unsafe shared geometry pool/MDI/ICB paths. Add a targeted regression test and test on Mac before enabling it for characters.

## Stage 5C — 2026-11-19 — Renderer3D opt-in 60B dynamic vertex stream (first source integration)

- **Branch:** `feature/vivid-digital-humans` based on merged MotionWeave snapshot. User Mac validation at previous Stage 5B: `vivid_metal_gpu` 0.94s **PASS** and extended `vivid_metal_rhi` 0.75s **PASS**, 2/2 pass, total real **1.71s**. Result handoff commit `8c25570c224b3a508f334191e2e51fc3cf0a7262`.
- **Code commits & changed files:**
  - `275f8f6952551ec8ccce01a0083593231a684fd5`: new `engine/render/ModelVertexStreamOverride.h`, model-relative 60-byte GPU vertex stream acceptance status policy and checks, no save changes.
  - `bd634b3d87c44780aac505dd5aaec03fa633b5b8`: `engine/render/Renderer3D.h`, opt-in `ModelDraw.vertexOverride` and `vertexOverrideCount` plus `ModelItem.vertexOverride`. Both default to off.
  - `02efa1f5120284093fc776c46fddb8e9095c8d4a`: `engine/render/Renderer3DModels.cpp`, opt-in MetaHuman-like stream submission using original 60B vertex layout for direct model draws, disabling legacy morph pass, cache/LOD/batching/MDI/ICB and geometry shortcuts for custom-stream items, bypassing stale bind-pose bounds.
  - `f2e0d87e9422da54b75a434e25182b4f1b0f4fa7`: new `tests/vivid/render_vertex_override.cpp`, regression fixture for model sizes, legacy default/off, rejected shared geometry/base offsets.
  - `84bb150a61456ddb29020334995eeb5897958c77`: `engine/character/vivid/CMakeLists.txt`, new `aiverse_vivid_vertex_override_tests` / CTest `vivid_vertex_override`.
  - `3c52771a46a09fab40e2e725969439f358b23dc3`: `Renderer3DModels.cpp` additionally rejects invalid override with diagnostic, retires an obsolete persistent GPU scene record on transition, conservatively disables reflection bounds culling for deformed stream.
  - `c7dea56172e72b2a99b578a7757f7e88a9b55cbb`: `docs/vivid.md`, API/ownership/integration restrictions and next steps.
- **Functional contract:** `GpuMorphDeformer` computes 48B position/normal/tangent → `GpuMorphMeshStream` packs full 60B model-relative vertices → `ModelDraw.vertexOverride` is passed by the host, with matching count, a matching device, and lifetime across the queued colour/shadow draws. `Renderer3D::drawModel` validates compatibility and requires Metal, dedicated (not pooled) dynamic model, no baked animation. Unsupported/invalid overrides reject instead of silently showing original mesh. `drawModelBatch` chooses override VB for all direct model phases, keeping existing material attributes and MotionWeave skin palettes. Normal ModelDraw path remains unchanged by default.
- **No new animation or simulation:** MotionWeave is the sole skeleton pose/IK/retarget/LOD animation policy owner; VIVID only supplies morph vertices and an opt-in presentation render stream. No game genre modules, ECS simulation checksum, saves or network ABI touched. `ModelDraw` source ABI adds two trailing fields; rebuilding all native C++ clients is required. Renderer3D internal item layout changes, no serialized format.
- **Acceptance gates:** invalid vertex counts, model shared geometry page, model GPU base vertex/first index, specialized geometry clusters, baked/static usage and non-Metal are blocked. Morphed streams use no old shader morph to prevent double application. CPU model batching, material/shadow composite batches, LOD fade, GPU model authorship, MDI, CPU instancing and GPU-encoded ICB are bypassed for streamed draws. Source model bounds cannot account for facial morph offsets, so conservative culling is disabled for these items pending correct dynamic bounds. Persistent geometry record is retired when switching from legacy source.
- **Tests actually executed for these new code commits:** **NOT RUN**. This agent inspected GitHub source and authored a native CTest, but there is no complete local AIVERSE checkout or Mac GPU execution. The user-confirmed previous 2/2 Metal RHI tests were performed BEFORE renderer code changes and do not demonstrate colour/shadow visual draw correctness. No CI run, full CTest or screenshot verified.
- **Risks/blockers:** RHI storage buffer binding as a vertex stream is known supported by inspected Metal `setVertexBuffer` code, but must be visually checked; no validation of handle-device ownership or in-flight resource lifetime is enforced by gfx::Device; the caller must keep the GPU stream alive to completion. Model vertex-range checks depend on original primitive firstVertex/vertexCount metadata (CPU vertices may be freed). The opt-in path deliberately trades performance for correctness and must not be enabled by default. The renderer's other shader permutations, outlines and render targets need real visual QA; no guarantee of consistent scene bounds/presentation without those tests. Ensure newly merged MotionWeave code still compiles; do not mutate its source branch.
- **Next exact step:** native macOS CMake configure + build `aiverse_vivid_vertex_override_tests` and `aiverse_vivid_metal_rhi_tests`, run `^vivid_(vertex_override|metal_rhi|metal_gpu)$`. Then a targeted minimal real Renderer3D drawable fixture with colour+shadow capture and skeletal palette, before any broader shader/batching enablement. Capture warnings/notes and errors and add another handoff. Re-run all `vivid_` plus `motionweave_api` and selected renderer/persistent scene tests before merge.
- **Mac commands (NOT RUN here):**
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_vertex_override_tests aiverse_vivid_metal_rhi_tests aiverse_vivid_metal_gpu_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_(vertex_override|metal_rhi|metal_gpu)$'
  ```

## Stage 5C macOS verification — 2026-11-19 — 3/3 Passed

- **Source revision expected:** `f21777a6b79f8c3087d1a9260b499eebc56f3f0f` (`feature/vivid-digital-humans`, previous advertised build command); the provided excerpt does not independently echo the detached HEAD SHA. Do not attribute this result to a later code commit.
- **User Mac build evidence:** `aiverse_engine`, `aiverse_vivid`, `aiverse_vivid_gpu`, `aiverse_models`, `aiverse_vivid_mesh` and `aiverse_vivid_metal_gpu_tests` built, `aiverse_vivid_metal_rhi_tests` linked. Three CTest fixtures ran: `vivid_metal_gpu` PASS (0.96 s), `vivid_metal_rhi` PASS (0.46 s), `vivid_vertex_override` PASS (0.31 s). **3/3 passed, 0 failed, 1.75 s real time**. The source excerpt contains no warnings or notes.
- **Evidence scope:** numeric GPU-vs-CPU and 60-byte mesh stream parity plus the headless renderer override policy are verified at this revision. **No actual Renderer3D colour/shadow draw, capture, image comparison, skinning-on-camera, FPS benchmark, or full test suite was run in the provided excerpt.** The `vivid_vertex_override` policy test does not compile/link the whole `Renderer3DModels.cpp` render path, so independent renderer compilation remains required.
- **Verifier:** native tests executed by user; this agent records user output, not an independent Mac run.
- **Next step:** construct a narrow real-Metal renderer test for a moving/deformed model and capture actual colour/depth/shadow results using existing Renderer3D and gfx device. Preserve default-off stream override, MotionWeave skeletal ownership and per-stage handoff. Run full renderer build and targeted CTest before claiming visual integration.
