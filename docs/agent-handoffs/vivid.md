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

## Stage 5D — 2026-11-19 — real Renderer3D visual preview authored

- **Branch:** `feature/vivid-digital-humans`; previous user's verified Stage 5C test selection 3/3 Passed (0 failed, 1.75s): `vivid_metal_gpu` 0.96s, `vivid_metal_rhi` 0.46s, `vivid_vertex_override` 0.31s. User log did not display an explicit SHA; corresponding source from prior suggested commands: `f21777a6b79f8c3087d1a9260b499eebc56f3f0f`. Recording commit: `96d5c6d6aecee4a663966da818b0e491c6880cfe`.
- **Implementation and docs commits**:
  - `431d3abb5d7a639c578ead79d14eaddae727715a`: new `tests/vivid/renderer_preview.cpp`, platform-driven *actual Renderer3D* Metal colour+shadow and 3 GUI frame captures. Uses procedural head/floor, VIVID 52-channel FaceController + named shape mapping into sparse morph targets, real `GpuMorphDeformer`, `GpuMorphMeshStream`, and **single MotionWeave presentation World/clip**, no copied IK or animation scheduling.
  - `6aadceb365866af6eb80aa2ee360611a8310c05e`: root `CMakeLists.txt`, opt-in standalone macOS GUI target `aiverse_vivid_renderer_preview` with `aiverse_runtime`, `AIVERSE::vivid_mesh`, `AIVERSE::MotionWeave` and existing zstd dependency. No automatic CTest because a real GUI drawable/capture loop is required.
  - `a32ad84e6f721d08ac761235238b6ad841d7f6e1`: `docs/vivid.md`, workflow, capture files, status and limitations.
- **What it is intended to exercise:** platform-owned Metal gfx::Device frame → VIVID GPU CSR morph → VIVID 60B `ModelVertex` packing → `ModelDraw.vertexOverride` → original `Renderer3D` indexed direct shadow+colour passes. The final capture also advances `MotionWeave::update` with an animation clip, using its output matrices as the sole skeletal/node-pose source. Baseline and active-morph image captures share a fixed MotionWeave pose so that pixel differences originate from facial deformation; a later capture validates visible graph-driven pose motion. Textures/materials are drawn by the original model renderer.
- **How it validates:** `Renderer3D::profile().draws[0]` and `draws[1]` must be nonzero after warmup, `Renderer3D::modelError()` empty, 3 actual 960×640 RGBA captures must complete, and two staged pixel differences must exceed conservative thresholds. Writes PPM files to `/tmp/aiverse-vivid-{baseline,morph,motionweave}.ppm` unless `AIVERSE_VIVID_PREVIEW_PREFIX` overrides path. Diagnostic screenshot differences are **not** shader visual-equivalence golden images or proof of MetaHuman quality.
- **Deliberately disabled in the fixture:** geometry pooling, model LOD, model instancing and MDI; `ModelDraw` override remains default-off elsewhere and works only for dedicated dynamic Metal models. Any shader/renderer compile failure or headless screenshot issue must be fixed without relaxing production safety rules.
- **Actual tests performed for this new stage:** **NOT RUN**. The GitHub source and root CMake entries were authored/inspected only. The authoring environment cannot compile the full AIVERSE platform renderer or launch macOS GUI Metal. Prior 3/3 native tests cannot certify this preview program or real colour/shadow output. CI NOT RUN. No actual screenshots were produced.
- **ABI/save / dependencies:** no changes to production `gfx::Device`, `Renderer3D`, `MotionWeave`, `render::Model` ABI or saved/network schemas in this stage. Only a desktop diagnostic tool. Rendering uses the already integrated Stage 5C `ModelDraw` vertex override and inherited MotionWeave APIs.
- **Next exact step:** owner builds `aiverse_vivid_renderer_preview` on Mac and runs it in a graphical session. If C++ compilation fails, fix first diagnostic and commit another handoff. On a live GUI, inspect the printed shadow/colour counts and the three PPMs, ensure file captures differ in the intended face/head region and shadow silhouette, and record concrete screen/log evidence. After the preview passes, design proper in-flight buffer slotting and device-generation ownership for many dynamic character instances, then test model shadow/LOD and render performance before enabling default VIVID visual playback.
- **Mac commands (NOT RUN here):**
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_renderer_preview -j 4
  AIVERSE_VIVID_PREVIEW_PREFIX=/tmp/aiverse-vivid \
    ../AIVERSE-vivid/build/vivid/aiverse_vivid_renderer_preview
  ```

### Stage 5D follow-up — explicit preview index bounds

- Code commit `d6898b98d747fece18416995353b8eae1b84ec6a` updates `tests/vivid/renderer_preview.cpp`: guard `model.vertices.size()` against the GPU uint32 index limit, iterate vertices with `std::size_t` and convert to `uint32_t` only after the bounds check. This keeps synthetic morph target indices compatible with `SparseMorphLibrary` on both 32- and 64-bit builds, without narrowing warnings or changing engine code.
- Tests after this fix: **NOT RUN**. Still need the user's real macOS GUI compile and colour/shadow/MotionWeave PPM captures before calling Stage 5D visually verified.

## Stage 5E — 2026-11-19 — fix procedural preview vertex ranges and asynchronous shader warmup

- **Feature branch:** `feature/vivid-digital-humans`. Previous published preview implementation HEAD `8b81646ca68d415650028682cf4abe885bc789f3`. `main` and `feature/universal-ecs-animation-pipeline` not modified.
- **Native Mac evidence actually reported by user BEFORE this fix:** `aiverse_vivid_renderer_preview` compiled/linked successfully, started with `VIVID render preview ready: 925 vertices, 2 GPU shapes`, and saved only `/tmp/aiverse-vivid-baseline.ppm`. Preview **FAILED** with `VIVID preview missing shadow/colour model submissions: 0/0` and `model vertex override: rejected invalid/pooled 60B stream`. Metal assets summary: `sourceMisses=11 pipelines=15 pipelineFailures=0`; it does not prove colour/shadow correctness. This is not a CTest failure: this is a GUI preview failure. No successful morphed or MotionWeave captures were reported.
- **Root cause confirmed by source:** `engine/render/Model.cpp::modelFromParts` supplied `Primitive.firstIndex/indexCount` but did not supply `Primitive.firstVertex/vertexCount`. The preview's 925 vertices were valid, but `modelVertexOverrideStatus` computed no ranges because every primitive had zero vertex count, correctly rejecting the override. Initial zero draw counters can also occur while Metal model shader/pipeline jobs warm asynchronously; the fixed-frame screenshot script treated that as fatal prematurely. The log alone does not prove whether shader warmup was the sole cause of the initial `0/0`.
- **Code commits and exact paths:**
  - `ce21f978f59cd9a2a7bf050f3746e5e68f11b5b5` — `engine/render/Model.cpp`: set `p.firstVertex=base` and `p.vertexCount=sv.size()` for each procedural part in `modelFromParts`, preserving model-relative indexing. This repairs asset metadata rather than bypassing renderer validation.
  - `ca38810949f44c71c97b58617410305ce807a68a` — `tests/vivid/render_vertex_override.cpp`: regression for two procedural parts with expected 0..2 and 3..5 ranges; requires `ModelVertexOverrideStatus::Ready` before/after releasing the CPU vertices. Old invalid/pooled policy checks remain.
  - `b9579965c8af1256eb51254464c77f911c9c1ef0` — `tests/vivid/renderer_preview.cpp`: explicit `modelVertexOverrideStatus` preflight and range log after upload; explicit shadow bounds; replace fixed frame 6/17/39 captures and instant `0/0` failure with a 3-stage capture state machine. It requires at least 4 consecutive frames of nonzero shadow AND colour model submissions, retains each stage until asynchronous `captureNextFrame` callback completes, waits for MotionWeave animated translation, and times out with diagnostics after 400 frames.
  - `a52acfae9dbce6e89d0f3f68b99ad38fbd17a42f` — `docs/vivid.md`: source cause and preview-specific constraints.
- **API/compatibility:** no new public CMake targets, `ModelDraw`, `gfx::Device`, MotionWeave animation API, saved/network schemas, GPU compute shader layout, renderer queue/batching or simulation changes. Procedural `Model` instances now have correct `Primitive.firstVertex/vertexCount`. This changes previously invalid metadata as intended; downstream model batching/LOD code should be built and tested on Mac.
- **Actual tests executed AFTER this stage:** **NOT RUN**. GitHub code/file checks only; no macOS CMake build, GPU test rerun, actual preview screenshots or full renderer test suite on these source changes. Previous user 3/3 Metal GPU/override CTests were on the pre-preview revision.
- **Known limitations / risks:** GUI GPU shader first-use may still need substantial warmup; waiting for 4 consecutive draw frames prevents blank screenshots but does not prove shadow pixels match morph silhouette. A stage can fail at the new 400-frame timeout, in which case report `stage`, draw counts, pending pipeline jobs and GPU errors. Large motions or artistically subtle morphs may require adjusting fixture parameters based on real captured images. Some backends may generate additional LOD/material batches; retain the post-upload range preflight. Buffer/device lifetime and cross-frame re-use remain explicit host responsibilities. MotionWeave remains the single owner of body pose/retargeting.
- **Next actionable step:** on macOS GUI host fetch latest VIVID, configure and build `aiverse_vivid_vertex_override_tests`, `aiverse_vivid_metal_rhi_tests`, `aiverse_vivid_renderer_preview`. Run the two CTests first; run the GUI preview; ensure three captures and meaningful pixel deltas, visually inspect shadow edge and deformation. Record exact results in new handoff before moving on to multi-character in-flight buffering or batching/LOD.
- **Commands to reproduce, NOT RUN by this agent:**
  ```sh
  cd ~/Projects/AIVERSE && git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_vertex_override_tests aiverse_vivid_metal_rhi_tests aiverse_vivid_renderer_preview -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_(vertex_override|metal_rhi)$'
  AIVERSE_VIVID_PREVIEW_PREFIX=/tmp/aiverse-vivid ../AIVERSE-vivid/build/vivid/aiverse_vivid_renderer_preview
  ```

## Stage 5E native macOS visual validation — 2026-11-19 — PASS

- **Source revision confirmed by user's fetch/checkout:** `53e5f0cb069dff8d4c173001947ffce3b4791a98` (`feature/vivid-digital-humans`), macOS Apple Silicon worktree at `/Users/aleksandrfeer/Projects/AIVERSE-vivid`. This run began in a different `AIVERSE-physis-terra-integration` terminal, but the commands explicitly fetched and built the *VIVID* detached worktree; its unrelated TerraWeave status/conflict listing does **not** establish any merge into VIVID.
- **Native configure and compilation executed by user:** macOS CMake configure 6.2 s and generation 1.1 s. Targets `aiverse_vivid_vertex_override_tests`, `aiverse_vivid_metal_rhi_tests`, `aiverse_runtime`, and full `aiverse_vivid_renderer_preview` built and linked without warning or error lines in supplied output. `Model.cpp` / `renderer_preview.cpp` newly compiled.
- **CTest actually run:** `vivid_metal_rhi` **Passed** 0.45 s and `vivid_vertex_override` **Passed** 0.93 s; **2/2 Passed, 0 Failed**, elapsed **1.39 s**. No other CTest is claimed for this exact run.
- **Real GUI Metal / Renderer3D visual preview actually run by user:** procedural head **925 vertices**, **2 GPU shape targets**, confirmed `Primitive.firstVertex=0, vertexCount=925`. Stage 0 baseline captured at frame 3 (`shadow=1, colour=1`); stage 1 VIVID GPU morph captured at frame 10 (`shadow=1, colour=1`); stage 2 MotionWeave-driven pose captured at frame 31 (`shadow=1, colour=1`). Three PPM files reported: `/tmp/aiverse-vivid-baseline.ppm`, `/tmp/aiverse-vivid-morph.ppm`, `/tmp/aiverse-vivid-motionweave.ppm`.
- **Image change metrics from test:** baseline-to-GPU-morph **81,988 changed pixels**; GPU-morph-to-MotionWeave **66,972 changed pixels**, test status `pass=1`. Output summary `[metal-assets] libraries=0 sourceMisses=11 pipelines=15 pipelineFailures=0 archive=0 strict=0`. `sourceMisses` reflects cache misses, **not** compilation failures. No output excerpt shows `modelError`, missing draw submissions, GPU parity errors, or failed screenshot comparisons.
- **What is verified:** actual Metal RHI compute morph to 60B ModelVertex, renderer's direct indexed shadow + colour draw calls, user-executed screenshot capture and measurable pixel changes, and use of MotionWeave animation state. This is an integration milestone, **not** proof of anatomically realistic human, production-quality skin/groom, exact visual shadow silhouette (PPM images are on user's Mac, not inspected here), multi-character scheduling/LOD/ICB throughput, or long-running resource lifetime.
- **Code changes for this checkpoint:** documentation only. `main`, MotionWeave and TerraWeave/PHYSIS source branches unchanged.
- **Next exact milestone:** design per-character lifecycle and safe in-flight compute-output buffers using gfx::Device's resource-retirement status and no unconditional per-frame GPU idle. Keep VIVID morph/renderer default-off outside opt-in paths and MotionWeave authoritative for skeleton animation. Add regression tests and handoff on every feature step; benchmark with many characters before enabling batching or LOD.

## Stage 5F — 2026-11-19 — per-character Metal GPU stream ownership

- **Destination branch:** `feature/vivid-digital-humans`. Scope: VIVID only; `main`, TerraWeave/PHYSIS and MotionWeave source branches were not mutated by this stage. MotionWeave remains the only skeleton pose/graph/IK/retargeting scheduler. Stage 5E prior source HEAD `53e5f0cb069dff8d4c173001947ffce3b4791a98`, user GUI verification documentation SHA `4dea755d192b70abe2501a63eba553d4230ffd06`.
- **Evidence actually reported for preceding Stage 5E:** native `vivid_metal_rhi` and `vivid_vertex_override` CTests **2/2 Passed** (1.39s total). Real Renderer3D GUI produced three PPMs for baseline, VIVID facial deformation and MotionWeave pose; colour/shadow draw submissions `1/1` at all captures; pixel change counts `81,988` and `66,972`, `pass=1`, zero shader pipeline compilation failures. These were user-executed on Mac, not the authoring agent; images have not been visually inspected here.
- **Stage 5F implementation / source commits**:
  - `d86864698732080e549c0c0e29d7eaae2da93425` — NEW `engine/character/vivid/GpuActorStream.h`: one owner per character; lifecycle/mesh/facial binding API, last-error diagnostics, strict one-dispatch-per-frame ownership contract and transactional ModelDraw attachment.
  - `118ea1d36f392707825ed9c25d8bd8df15477e80` — NEW `engine/character/vivid/GpuActorStream.cpp`: builds MorphVertex from original 60B model input, validates dedicated uploaded model stream, holds separate `GpuMorphDeformer`, `GpuMorphMeshStream` and weights for each actor, performs ordered GPU morph + packed vertex compute, blocks duplicate dispatch in a frame, rejects stale/wrong-model attaches, explicit idle-teardown via `waitForResourceIdle()`.
  - `b4dd888e0e9687a1d1a63cbe99c388491616a1b3` — `engine/character/vivid/CMakeLists.txt`: NEW `aiverse_vivid_actor` / `AIVERSE::vivid_actor` library; the existing native `aiverse_vivid_metal_rhi_tests` target now links it.
  - `0ff7370d11c7107dcd28ec91aa9e24c31ca405e4` — `tests/vivid/metal_rhi.mm`: two independent actors with one shared immutable Model/library fixture, distinct compute and model vertex buffers, three frames of different facial weights, CPU gather parity for both, immutable attribute integrity, stale/duplicate submission guards and lifecycle shutdown.
  - `39d46a12a0fb7bb00aaa46d50839e836bba33f47` — test correction: stale-frame attachment guard runs **after** the Metal `beginFrame()` advances the frame serial, not before.
  - `1fb8aaa4a7de8f9eb25475efd4f803051b90ef22` — `docs/vivid.md`: public API, ownership, safety gates, limitations and example use.
- **Architecture/API:** `GpuActorStream::initialize(gfx::Device&,const render::Model&,const SparseMorphLibrary&,span<const ModelVertex>)`, `dispatch(const FaceFrame&,const FaceShapeBinding&,influence,normalizeDirections)`, `attach(render::ModelDraw&)`, `vertexBuffer()`, `download(span<ModelVertex>)`, `shutdownAfterIdle()`. A single actor's writable output and weight data are independent of every other actor. `dispatch` requires Metal and a nonzero current device frame serial; duplicate same-frame submissions are deliberately rejected (no accidental partial overwrites). `attach` is transactional, references the exact uploaded source Model, validates the 60B stream and requires last successful compute to belong to this frame. Material and bone animation still run in the original `Renderer3D` and MotionWeave.
- **Resource/lifecycle:** both deformer and repacker own persistent per-actor GPU allocations. No per-frame allocation or forced GPU idle in `dispatch`. `shutdownAfterIdle` may block at rare actor removal: call AFTER the host has committed the GPU frame, never during an active frame; a failed idle barrier keeps the actor resources alive so the host can retry. Hosts must not destroy the actor or gfx device before safe retirement, because the low-level buffer destructors are ordinary nonblocking destructors and cannot infer current uncommitted work. Frequent dynamic actor spawn/despawn still needs an opt-in shared-asset cache + async retirement queue.
- **Public ABI/schema:** new C++ library/API; no modifications to `render::ModelVertex` layout, `Renderer3D` rendering path, `gfx::Device`, MotionWeave, authoritative ECS, save files or network serialization. `main` unaffected.
- **Tests actually executed for Stage 5F code:** **NOT RUN** by this agent (no native Mac GPU/GUI runner). The extended `vivid_metal_rhi` CTest is authored and source-inspected, but this new multi-actor code has not compiled or run on macOS. No CI or full suite has been checked. Do not claim Stage 5F passed based on prior 2/2 or GUI screenshots.
- **Risks / limitations:** each actor duplicates the immutable base and CSR library GPU allocations and compiles two compute pipelines, so memory/CPU setup scales poorly with large character counts (address via shared immutable asset/program cache in a future measured stage). The first release is **Metal-only** and an opt-in path. `frameSerial()` gates duplicate submission but is not a device-generation proof and cannot detect a host trying to attach a stream after `endFrame()` but before the next `beginFrame()`; callers must obey frame boundaries. No GPU-backed crowd benchmark yet; no async per-actor resource retirement. `GpuActorStream` destructor cannot replace an orderly explicit `shutdownAfterIdle()` after GPU submission.
- **Exact next action:** Mac owner fetch current VIVID branch and build/run `aiverse_vivid_metal_rhi_tests`, `aiverse_vivid_vertex_override_tests`, `aiverse_vivid_metal_gpu_tests`; record first compiler diagnostic, real 2-actor 3-frame parity and actual CTest results. If green, extend the GUI preview to use per-actor stream and test many independent characters, then design asset-shared immutable CSR/pipeline residency and nonblocking GPU retirement. Continue writing one handoff per stage.
- **Owner Mac commands (NOT RUN by this agent):**
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_metal_rhi_tests aiverse_vivid_vertex_override_tests aiverse_vivid_metal_gpu_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_(metal_gpu|metal_rhi|vertex_override)$'
  ```

## Stage 5F user macOS verification — 2026-11-19 — 3/3 PASS

- **Source revision:** test run follows the Stage 5F pull/build commands and the then-current GitHub branch head `d206b815fe34df068ba2f3eb40f48e96fd9cca2d`. The supplied excerpt does **not** show local `git rev-parse HEAD`, so the specific SHA is inferred from the immediately preceding instructions rather than independently echoed by the Mac.
- **Mac compilation reported by user:** `aiverse_vivid_actor` (including new `GpuActorStream.cpp`) built and linked, `aiverse_vivid_metal_rhi_tests` Objective-C++ source rebuilt and linked, plus all prerequisite VIVID/Model GPU targets. No warning/error lines in supplied build output.
- **User-executed CTest:** `vivid_metal_gpu` PASS 0.89s, `vivid_metal_rhi` PASS 1.81s (includes Stage 5F's two-actor, three-frame Metal GPU parity/ownership assertions), `vivid_vertex_override` PASS 0.58s; **3/3 Passed, 0 Failed, total 3.29s**. These are test results on real Apple Metal hardware, **not** a rendering benchmark or proof of dozens of simultaneous actors.
- **Gaps:** existing 3-frame GUI Renderer3D/MotionWeave visual preview was previously verified at Stage 5E, not rerun in the supplied Stage 5F excerpt. No full `vivid_` / MotionWeave suite or long-term hot-reload/device loss soak test reported.
- **Next action:** replace per-actor blocking shutdown on frequent despawn with the existing genre-neutral `render::AsyncResourceRetirementQueue` and GPU retirement markers. Keep a single GPU completion barrier per batch, avoid reimplementing the retirement state machine. Test Pending/Complete/Failed/idle fallback with a recording backend and a real Metal lifecycle pass. No changes to the MotionWeave animation owner.

## Stage 5G — 2026-11-19 — batched token-based GPU actor retirement

- **Branch:** `feature/vivid-digital-humans`, preserving both merged MotionWeave source history and the VIVID presentation-owned GPU actor implementation; no changes to source `feature/universal-ecs-animation-pipeline`, `main`, TerraWeave/PHYSIS or authoritative simulation.
- **Previous Stage 5F user Mac evidence:** `aiverse_vivid_actor` compiled/linked; `vivid_metal_gpu` 0.89 s **PASS**, `vivid_metal_rhi` 1.81 s **PASS**, `vivid_vertex_override` 0.58 s **PASS**; **3/3 Passed, 0 failed, 3.29 s total**. The user's log does not repeat `git rev-parse HEAD` but follows the build instructions for the Stage 5F HEAD `d206b815fe34df068ba2f3eb40f48e96fd9cca2d`. Verification documentation commit `895b412ddd2a3fa0ff1ae99cc5d8f6d52b58e519`. These are real user-executed tests, not independently run by this author.
- **Code commits / exact file modifications:**
  - `7aadcb91484b8022934ff54ee694f28583005f41` — `engine/character/vivid/GpuActorStream.h`: friend-only completed GPU retirement function, deliberately inaccessible to ordinary game code.
  - `58561c66b71276744e2985aa508d14e65852df1f` — `GpuActorStream.cpp`: factor existing immediate-after-idle cleanup into private `releaseAfterGpuCompletion()`; pre-existing `shutdownAfterIdle` still checks idle barrier.
  - `476f66c6066e2f83268e812ca11e4cc6ef0d9bef` — NEW `engine/character/vivid/GpuActorRetirement.h`: device-thread-only `GpuActorRetirementQueue` using `render::AsyncResourceRetirementQueue<std::unique_ptr<GpuActorStream>>`.
  - `1a15bf35b0fd3419636601d7c5935a3fd3739326` — NEW `GpuActorRetirement.cpp`: batch token submission and complete/pending/failed handling via existing gfx::Device, idle fallback/drain, deferred actor memory release.
  - `d7e7e0e2b48ae361f5e250209d5f0aea43426e71` — add explicit required standard `<utility>` include in queue API.
  - `2c016196cc335d48dbe0dddebbfca1cb2b33c520` — NEW `tests/vivid/actor_retirement.cpp`: recording backend verifies two actors sharing a GPU completion token, pending retention, successful release, failed-token + failed idle fallback, later successful idle recovery, unsupported token fallback, device identity rejection.
  - `4dd9462e7fc1f274758cdb71c910198a0df23871` — `engine/character/vivid/CMakeLists.txt`: build retirement implementation into `AIVERSE::vivid_actor`; register `aiverse_vivid_actor_retirement_tests` / CTest `vivid_actor_retirement`.
  - `2bb30669f8e183274b920964e075e1da7bcef8cf` — `tests/vivid/metal_rhi.mm`: add actual Apple Metal actor resource retirement token validation after three frames of two-actor GPU parity. Bounded token polling must complete without fallback.
  - `a2e40729951f3e0c45ec6c438b0fedcf0bfdf754` — explicit `<utility>` in portable fake-RHI test.
  - `85e3f795c6a285cf2b272bf229646c1fbf7379eb` — `docs/vivid.md`: Stage 5G API, GPU lifetime, usage, risks and test scope.
- **API:** `GpuActorRetirementQueue::retire(std::unique_ptr<GpuActorStream>&)` takes actor ownership only after same-device/ready validation; `collect()` polls/reports per-batch GPU tokens, `drain()` requires explicit successful GPU idle barrier for exceptional shutdown/fence failure. `pending()`, `stats()`, `saturated()`, `setDiagnosticObserver()` reuse the renderer's genre-neutral retirement diagnostics. `GpuActorStream::releaseAfterGpuCompletion()` is private and may only be called by the retirement queue on a genuinely completed GPU marker. One token covers all newly pending actors; repeated polling is nonblocking when markers are supported.
- **Scheduling and safety:** all `retire/collect/drain` calls on the same `gfx::Device` owner thread **after `endFrame`** and before the next frame records resource uses. Metal's `beginResourceRetirement` token only follows previously COMMITTED work, not the uncommitted current command buffer. Never delete an actor, queue, Model or device while its GPU stream may be referenced. The queue MUST drain successfully before destruction. If token creation is unsupported, existing shared queue code performs one explicit idle recovery, never silent free. Token failure cannot be bypassed without full idle.
- **Compatibility:** no `gfx::Device` or renderer/Model ABI or GPU shader changes, no MotionWeave skeleton/graph duplication, no saves or network format changes. `aiverse_vivid_actor` grows a new callable API but existing code paths remain default-off.
- **Tests actually run for Stage 5G code:** **NOT RUN** by this agent; only GitHub source and design review. Neither the new portable CTest nor the extended real Metal RHI test has been compiled on Mac. Do not claim 4/4 Passed or hardware retirement throughput yet. Prior 3/3 report predates this stage.
- **Known risks:** the retirement queue requires explicit successful drain before gfx::Device destruction (matching existing generic engine owners), otherwise a caller could destroy GPU buffers prematurely. A token does not protect an actor still referenced by commands not yet committed; boundary discipline is mandatory. Shared immutable morph CSR/pipeline cache, GPU benchmark with many actors, device-loss handling and GUI multi-actor visibility have not been implemented. Real Metal marker completion may be delayed; its test has bounded polling and a failure branch requiring recovery.
- **Next exact step:** user Mac fetch Stage 5G, configure `AIVERSE_BUILD_TESTS=ON`, build `aiverse_vivid_actor_retirement_tests` and `aiverse_vivid_metal_rhi_tests`, run `ctest -R '^vivid_(actor_retirement|metal_rhi|metal_gpu|vertex_override)$'` **after building the named binaries**. Report the first compiler error/warning and test status. If green, integrate the actor queue in the real 3-capture preview for lifecycle events and benchmark dozens/hundreds of characters before enabling shared immutable assets.
- **Mac commands (NOT RUN here):**
  ```sh
  cd ~/Projects/AIVERSE
  git fetch origin feature/vivid-digital-humans
  git -C ../AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans
  cmake -S ../AIVERSE-vivid -B ../AIVERSE-vivid/build/vivid -DAIVERSE_BUILD_TESTS=ON
  cmake --build ../AIVERSE-vivid/build/vivid --target aiverse_vivid_actor_retirement_tests aiverse_vivid_metal_rhi_tests aiverse_vivid_metal_gpu_tests aiverse_vivid_vertex_override_tests -j 4
  ctest --test-dir ../AIVERSE-vivid/build/vivid --output-on-failure -R '^vivid_(actor_retirement|metal_rhi|metal_gpu|vertex_override)$'
  ```

## Stage 5H — 2026-11-19 — shared immutable morph GPU data and weak cache

- **Current work branch:** `feature/vivid-digital-humans`. Preserves the MotionWeave merge; `main` and MotionWeave source not changed. Prior actual Mac native 3/3 pass was Stage 5F, 3.29s; new Stage 5G async retirement and Stage 5H shared geometry **NOT RUN on Mac** in this coding turn.
- **Source commits / files:**
  - `d1962b5d543a3fc6d0c82f93397fd6b352c2cd4e` — NEW `engine/character/vivid/GpuMorphSharedAsset.h`, public immutable shared GPU asset contract.
  - `2596a8461e61a6aa059994d410197342693b8bdf` — NEW `GpuMorphSharedAsset.cpp`, uploads packed base48, CSR data, original ModelVertex60 and two compute pipelines; device budget 512 MiB; read-only shareable across actors.
  - `3032f014f5c23ae8bf5991dc72642d072b45f5b6` — `GpuActorStream.h` now has `initializeShared(device,model,sharedAsset)` and retains backward-compatible `initialize`; writable weights and 48B/60B outputs remain private, shared asset is retained by reference.
  - `d470488884dd158ded8625c84580dc3a64d1126d` — `GpuActorStream.cpp`, per-actor storage upload + two ordered compute dispatches reusing shared shaders/immutable CSR, same-frame/old-frame guard, post-fence resource reset.
  - `b0f63419d71bf807eb8c78e5356f48494c6e5e8a` — `engine/character/vivid/CMakeLists.txt` includes shared asset code.
  - `dec3134d9358fb5abd4250af68e955c551b530f7` — NEW `GpuMorphAssetCache.h`, device-local weak content-addressed GPU asset cache.
  - `e50454d48933e8af0d608e6dafcfde8ec93236c7` — NEW `GpuMorphAssetCache.cpp`, double 64-bit streaming digest of exact GPU-relevant `ModelVertex` and sparse CSR source bytes, weak-entry lookup and pruning.
  - `c88ca699d69d5900510d0d50a541fecc04f2741f` — CMake adds asset-cache source to `AIVERSE::vivid_actor`.
  - `7f1e2b9b80814a7008fdecec1c2586b0c14c9a99` and `ad3aa29b730a87b3029f1a8b9cd61a67aff5bbe4` — `tests/vivid/actor_retirement.cpp` checks shared GPU buffer/program usage, weak cache hit/miss/prune and last-reference release.
  - `1db0c020fdd10fe15f48da2ed55c32868affaff6` and `cf2cf653ee49fb309fa71925f6e4db1eaa96ec9b` — `tests/vivid/metal_rhi.mm` checks two actual Metal actors using one immutable cached GPU asset, 3 frame CPU reference parity and cache release.
  - `d9f459420771a9092a7ae60229aa5b3ef2546f83` — fake-Metal recording GPU stress: 128 independently deforming actors across four frames, one shared asset, one completion token for all 128.
  - `2aa40112a89c2f5d16a43dd202363d35787854f5` — preallocate actor CPU weight buffer before any GPU-owned resource publication to prevent partial GPU resource leaks on allocation exceptions.
  - `24f3783edef5949351aee52d41d3ea42aac77504` — `docs/vivid.md` Stage 5H API and test-status details.
- **Architecture and API:** `GpuMorphSharedAsset::create(device,library,vertices)` uploads five shared GPU buffers and two compute pipelines; `GpuMorphAssetCache::acquire` reuses an immutable asset for identical GPU-relevant source bytes. `GpuActorStream::initializeShared` retains a shared_ptr to this data and creates only three writable per-instance buffers (weights, 48B morph output, 60B final render output). MotionWeave continues to own pose/retargeting/IK; the existing `ModelDraw.vertexOverride` consumes final output. Asset GPU resources stay alive until the last actor/external reference is retired after confirmed GPU completion.
- **Memory scaling target:** `5+3*N` buffers and 2 compute programs for N instances sharing an asset, versus `8*N` buffers and `2*N` programs previously. Caller may opt into legacy independent init; no serialized format change. The 512 MiB limit applies per shared asset and per actor output set; broad scene-wide budgets/performance are not yet measured.
- **Test coverage authored but NOT RUN**: fake RHI `vivid_actor_retirement`: identical vs modified asset content, 128 actors/4 frames, distinct vertex handles, one GPU fence for all, Pending/Complete/Failed/idle fallback. Real Metal `vivid_metal_rhi`: shared asset initialization, per-actor weights and full 60B CPU parity. No Mac compiler/GPU tests, GUI visuals or CI results have been confirmed for these new Stage5G/5H commits.
- **Risks:** calling GPU compute or queue retirement outside device frame/boundary is prohibited. `GpuMorphSharedAsset` destructor destroys GPU resources immediately; it is safe only when ALL actor references have passed a successful completion token or idle barrier. The cache holds weak references to avoid premature lifetime extension; host must still retain original `render::Model` and device. GPU-asset content key is a noncryptographic 128-bit hash; no heterogeneous-backend cross-GPU sharing. CPU model revisions can require new cache entries and deferred old resource retirement. Direct GUI crowd/LOD and Vulkan shader tests remain open.
- **Exact next action:** one-pass macOS build + CTest for all VIVID/MotionWeave tests, GUI Renderer3D preview, and check the new shared-C SR/retirement cases, then record results. If red, fix primary compiler error and rerun ONE full command. New automated CI workflow exists but a runner's queue status is not equivalent to tests having passed.

## Stage 5I — 2026-11-19 — single-run Mac validation and focused CI

- **Branch:** `feature/vivid-digital-humans`; neither `main` nor MotionWeave/TerraWeave/PHYSIS feature branch mutated. Entire work remains portable C++20/Metal opt-in with MotionWeave owning skeleton animation.
- **Implementation commits and paths:**
  - `56191d9beedd5e7a32807bb8d4535e84544b3063` — new `tools/vivid-validate.sh` on Mac: safe non-dirty checkout, fetch current VIVID head, record SHA, configure dedicated build tree, compile all VIVID and MotionWeave API tests plus GUI preview, sequential CTest, real Renderer3D/MotionWeave screenshot verification. Logs/screenshots/summary retained in `build/vivid-onepass/vivid-validation`.
  - `c06533de9c2350547007cf4c37c6c80c00ba0eca` — `.github/workflows/vivid-validation.yml`, focused macOS-15 GitHub Actions build+CTest on VIVID branch pushes/dispatch. Hosted CI may have no Metal GPU; skip is not GPU parity. GUI preview intentionally omitted from CI.
  - `56c71b575432d00e5a165e90d0f8b9c7d0884f86` — `docs/vivid.md`, exact runner usage, validation scope and outstanding authoring/product features.
- **One single command for user Mac:** `bash ~/Projects/AIVERSE-vivid/tools/vivid-validate.sh` (run after already having a local worktree with this script; if not yet pulled, use `git -C ~/Projects/AIVERSE-vivid fetch origin feature/vivid-digital-humans && git -C ~/Projects/AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans` once to obtain the script). The runner handles future fetch and checkout. `--no-gui` is incomplete headless validation; `--full` optionally builds/runs the complete engine suite, potentially slower/affected by unrelated code.
- **Exact acceptance tests selected:** `vivid_face`, `vivid_sparse_morph`, `vivid_shape_binding`, `vivid_morph_binding`, `vivid_gpu_contract`, `vivid_metal_gpu`, `vivid_metal_rhi`, `vivid_vertex_override`, `vivid_actor_retirement`, `vivid_motionweave_bridge`, and `motionweave_api` (11 selected when configured on macOS). Followed by GUI `aiverse_vivid_renderer_preview` which must capture baseline, morph and MotionWeave images and print `pass=1`. Serial Metal CTest avoids running GPU jobs concurrently.
- **Native tests actually run by this assistant for new Stage 5G/5H/5I:** **NOT RUN**. User's previous Stage 5F macOS results were 3/3 Passed (0 failed, total 3.29s); those do not cover the new shared-cache refactor or async retirement tests. Existing GitHub Actions workflow run `44584352685` was **queued** when last checked, not a PASS. This is a launch attempt for one-shot CI, not proof of success.
- **Integrity/compatibility:** native GPU kernel and 60B `ModelVertex` ABI unchanged. Authoritative ECS, save/network schema and legacy renderer model path unchanged. `AIVERSE::vivid_actor` now consumes shared immutable asset API and stage 5G retirement. Both asynchronous and lifecycle tests only free GPU resources when exact completion token or successful idle barrier permits it; GPU resource owner must outlive model/device.
- **Remaining feature gates:** resolve any first compiler or GPU test failure in the ONE runner, then rerun the same command; do not use previous pass counts to imply new branch has passed. GPU shared asset/hardware character crowd benchmarking, Vulkan/GL parity, true hair/groom/skin/eyes/rig-DNA content and editor authoring remain unimplemented; VIVID is not a feature-complete MetaHuman replacement.

## Stage 5J — 2026-11-19 — real Renderer3D multi-character preview and one-command two-mode GUI acceptance

- **Branch and ownership:** `feature/vivid-digital-humans`, no merge to main, MotionWeave or TerraWeave. MotionWeave remains the only skeleton animation scheduler. VIVID only supplies per-instance face morph GPU vertex streams.
- **Code commits and exact files:**
  - `d887e31af195d676cfe8a560e7d23e567cd7618e` — `tests/vivid/renderer_preview.cpp` replaces single `GpuMorphDeformer` / `GpuMorphMeshStream` with cached immutable `GpuMorphAssetCache`, many independent `GpuActorStream`s and per-instance semantic face weights. Supports `AIVERSE_VIVID_PREVIEW_ACTORS` clamped to 1–32. Different actors draw the same original `Model` but independently deform their mesh outputs with distinct handles/expressions; all use the same MotionWeave graph/node pose. Retains baseline/morph/MotionWeave screenshots with shadow+colour draw count checks.
  - `bf346a88638142564bb9416a2b89c73f425d8a86` — `renderer_preview.cpp` removes now-unused CPU MorphVertex extraction helper and avoids a second unnecessary idle barrier: actors are retired/drained through `GpuActorRetirementQueue` once, after frame completion.
  - `8bc6d89ce1102ae7470114cbc37dbbe306d0452d` — `tools/vivid-validate.sh` runs one-actor and 12-actor real Metal preview in the same single command after CTest, captures/checks six PPMs, retains both logs.
  - `9eb03b780d7369ca29c6981db543619260de449f` — root `CMakeLists.txt` links `AIVERSE::vivid_actor` into the GUI preview target so GPU cache/actor symbols resolve.
  - `aa8ddc5cf0f78442a4179fa15d390ee05bbfd1b0` — `docs/vivid.md`, crowd preview and acceptance workflow.
- **Architecture / data flow:** host `platform::run` → gfx Metal frame → `MotionWeave::update` for shared body/node pose → one cached immutable CSR/pipelines asset → per-actor `GpuActorStream::dispatch` with unique facial weights → `ModelDraw.vertexOverride` per instance → one shared `Renderer3D` performing real direct colour and shadow passes. No duplicate skeleton ECS system or per-frame CPU readback. At shutdown `GpuActorRetirementQueue::drain` gates final actor GPU destruction and cached immutable asset release.
- **API / compatibility:** only diagnostic preview/CMake and single-run script modified. Production renderer and existing VIVID public C++ API unchanged in Stage 5J; no save/network formats. The 60B ModelVertex ABI and original model shadow shaders are unchanged. Crowd branch defaults to 1 actor when env var absent, user test explicitly uses 12.
- **Tests performed on this new source:** **NOT RUN** by this agent. Prior user macOS Stage 5E real scene (single actor) and Stage 5F 3/3 GPU CTests predates Stage 5H asset cache and this preview refactor. There is a GitHub Actions workflow on push, but it may still be queued; do not claim CI pass until completion. No screenshots for 12 actors have yet been reported by user.
- **Risks:** 12 procedural heads within the same camera may overlap/saturate screen; image difference thresholds remain conservative (>200, >100 changed pixels) but need real screenshots and render profile output. Limited to Metal dedicated nonpooled dynamic models, normal batching/ICB/LOD intentionally excluded from opt-in stream. Shared asset cache is per GPU device and must not outlive it; any failed queue drain must retain model/device resources and report a failure rather than silently freeing them.
- **Next exact action:** run `bash ~/Projects/AIVERSE-vivid/tools/vivid-validate.sh` after fetching latest branch (or bootstrap once with `git -C ~/Projects/AIVERSE-vivid fetch origin feature/vivid-digital-humans && git -C ~/Projects/AIVERSE-vivid switch --detach origin/feature/vivid-digital-humans`). This one run includes 11 CTests and both GUI stages. Capture build/CTest/preview logs and compiler diagnostics; if a step fails, fix the primary cause on this branch and rerun the entire script. Record the final Mac SHA and evidence here. Complete product scope (realistic skin/eye/groom/DNA, editor, Vulkan) remains unimplemented and cannot be called fully shipped.

## Stage 5J — macOS one-pass acceptance result — 2026-11-19 — PASS

- **Confirmed tested source SHA:** `fa5504a9f2e1f64087ef73139525e5bde31976b4`, from the user's actual `tools/vivid-validate.sh` `summary.txt` output. Branch at time of this run: `feature/vivid-digital-humans`.
- **Owner-run test command:** `bash tools/vivid-validate.sh` on macOS in `/Users/aleksandrfeer/Projects/AIVERSE-vivid`. It completed and printed `VIVID one-pass verification COMPLETED.`. The user supplied actual CTest and graphics capture logs, not merely a command.
- **Scoped CTest results: 11/11 PASS, 0 failed, 10.19s total.** `motionweave_api` 0.90s; `vivid_face` 0.61s; `vivid_sparse_morph` 0.33s; `vivid_shape_binding` 0.98s; `vivid_gpu_contract` 0.33s; `vivid_motionweave_bridge` 1.76s; `vivid_metal_gpu` 1.22s; `vivid_metal_rhi` 0.63s; `vivid_actor_retirement` 1.60s; `vivid_vertex_override` 1.16s; `vivid_morph_binding` 0.66s. This includes new shared-asset and 128-actor fake-RHI contract assertions, real Metal two-actor GPU execution and async resource lifetime tests.
- **Real macOS Renderer3D one-actor GPU/MotionWeave GUI preview: PASS.** Procedural model `925` vertices, `2` GPU shapes; immutable GPU model/CSR data `158324` bytes. Baseline, morph and MotionWeave captures each reported **1 shadow and 1 colour** model draw. Capture frames: 3, 10, 32. `81988` morph-changed pixels and `66972` MotionWeave-changed pixels; `pass=1`. Three nonempty PPM outputs under `build/vivid-onepass/vivid-validation/vivid-{baseline,morph,motionweave}.ppm`.
- **Real macOS Renderer3D 12-actor GPU/MotionWeave GUI preview: PASS.** Shared immutable data still `158324` bytes, `925` vertices and `2` GPU shapes; `12 shadow / 12 colour` model draws at each of frames 3, 11, 32. `179673` morph-changed pixels and `183275` MotionWeave-changed pixels; `pass=1`. Three nonempty PPM outputs under `build/vivid-onepass/vivid-validation/vivid-crowd-{baseline,morph,motionweave}.ppm`.
- **Real Metal shader diagnostics in both GUI runs:** `[metal-assets] libraries=0 sourceMisses=11 pipelines=15 pipelineFailures=0 archive=0 strict=0`. Source-misses are shader-asset cache lookups, not GPU compilation failures. No error/warning/note diagnostics appeared in the supplied excerpt.
- **Runner's persisted summary:** `GPU+MotionWeave CTest=PASS`, `Renderer3D colour/shadow/MotionWeave visual preview=PASS`, `Renderer3D shared GPU asset crowd preview=PASS`. Exact log directory: `/Users/aleksandrfeer/Projects/AIVERSE-vivid/build/vivid-onepass/vivid-validation`.
- **Verification provenance:** test and GUI work executed by the user on Mac. The agent has *not* opened the six local PPMs; claims are limited to actual render-pass counters, capture outputs and programmatic pixel-difference results. No full-engine CTest suite was run by this command (`--full` was not selected), nor production GPU frame timings/FPS, real photorealistic human assets/eyes/hair/DNA, Vulkan parity or a device-loss soak.
- **Source changes for this checkpoint:** documentation only; no reimplementation of MotionWeave, new simulation mutations, or branch merge.
- **Next step:** treat Stage 5J *focused Metal VIVID integration acceptance* as PASS. Follow-up milestones, if required for full product readiness, are accurate GPU timing and scaling at high actor counts, non-Metal backend parity, streaming/LOD/ICB safe activation, actual human asset/rig/groom/skin/eyes and editor authoring. Do not label those unfinished features as completed, and do not re-run focused tests without a source change or new QA target.

## Stage 6A — 2026-11-19 — HumanDNA schema and serialization

- Branch: `feature/vivid-digital-humans`; MotionWeave core and main unchanged.
- Commits: `f98f7e220dfb12ac76f0d4b17fbcae5efa406a56` `HumanDNA.h`; `accfd04657f95bbde794293dd5006c9098fdbad6` `HumanDNA.cpp`; `611f79c088e8dcc55b103398b7fc26a48e544885`, `ae3c8f38633a0433fc302015291d43d79fa5cbe3` corrections to public include/range parsing; `94b786f603dad567d16cc11b753db0f9f0dc80f0` `tests/vivid/human_dna.cpp`; `710d7b670fc198cf827615c05548b420fda0d1f4` `engine/character/vivid/CMakeLists.txt` new `AIVERSE::vivid_human` and `vivid_human_dna`.
- API: `HumanDNA::preset`, `HumanDNA::fromJson`, `HumanDNA::toJson`, `HumanDNA::validate`. Version 1 includes anatomy/skin/eyes/groom/rig and 52 facial rest channels. Strict finite/size guards and canonical serialization, no proprietary DNA parsing.
- Tests actually executed: **NOT RUN** for Stage 6A Mac compile/CTest; code authored and checked remotely. Do not infer passing tests from previous VIVID 11/11.
- Next: build material/shader integration, eye/groom procedural asset generation, editable authoring UI; then one Mac validation. Preserve genre-neutral MotionWeave ownership and write a stage handoff.

## Stage 6B — 2026-11-19 — rig, procedural appearance, groom and editor state

- Branch: `feature/vivid-digital-humans` only; MotionWeave and main remain unchanged. Previous focused VIVID Metal stage 5J passed all 11/11 tests and both GPU previews at tested SHA `fa5504a9f2e1f64087ef73139525e5bde31976b4`; this is NOT verification of newly added Stage 6 code.
- Implementation commits/files: `00f9fed74987534f0852d94ce03fd1c34cbe45e5` `HumanRig.h`, `703e85be1ebec862aed3721a3a59a18b2c403d0d` `HumanRig.cpp`, `87ecaf50285c486eb59a291bb66170e475257828` `tests/vivid/human_rig.cpp`; `bb65d6841ea9526219c6650487abce11b90a0b92` `HumanMaterials.h`, `b1c499951c6dedbf232453ae9df9bc7937915cb2` `HumanMaterials.cpp`; `0c8207209fe781929dca4b76362f44da576e942a` `HumanGroom.h`, `710b38561c07b461e3c8f9a6e280188a68cb8116` `HumanGroom.cpp`, `54bd71572b807102d24df25834336f6b83b7d160` post-finalize dynamic groom geometry pooling exclusion; `7b82369f97446295c269e042296ff3ecd311603f` `HumanPreview.h`, `710b91ae0d7506f6151c145d475ffffb22780421` `HumanPreview.cpp`; `f724bc3a2c1b6b16b6bfa4985ddf955a2bce2203` `HumanCreatorSession.h`, `4748f79e333fd2d2d82266f267505677086e2d86` `HumanCreatorSession.cpp`; `814722a86f0f9eb40c13a66cdfcabebdbab9f9c1` CMake wiring; `96eee91e92829aec0bda4cba41ebf2791a473ba8` `human_appearance.cpp`; `a36f05ec31f5849fb197744466c26c8e6e7a3ed8` `human_creator.cpp`.
- APIs and behavior: HumanRigBinding::compile/compose generates jaw and eye pose corrections using a copy of MotionWeave node matrices; DNA-named hierarchy validation, no authoritative ECS mutation. HumanMaterials::humanMaterial creates per-surface PBR ShaderGraphs with MSL/GLSL parity under existing renderer. HumanGroomRuntime::generate/step/makeCardModel/updateCardVertices supports 3 quality levels, bounded vertex/card budgets, deterministic roots and presentation physics. generateHumanPreview returns a rigged procedural `HumanPreviewAsset` with hair and five named correctives. HumanCreatorSession::edit/undo/redo/loadFile/saveFile validates transactions and avoids destructive invalid edits.
- Tests: 3 new portable CTest cases authored, **NOT RUN**. No new native Metal shader compilation, GUI screenshot or complete MetaHuman-quality comparison has been executed. Earlier VIVID 11/11 success is historical to Stage 5J.
- Limitations: procedural reference mesh rather than scanned head, approximate screen-space-independent skin scatter and hair-card highlight (no full physically measured multi-layer skin/strand shading), scalp collision proxy (no full body hair/cloth contacts), no GUI editor yet, glTF/FBX partial legacy morph import limitations, no Vulkan GPU parity for new materials. ShaderGraph generated code and model uploads require actual Mac build/pipeline warmup testing.
- Next exact step: complete interactive Human Creator GUI and asset import/export with presentational preview; extend single command to build new appearance targets and run CTest/GUI, record first actual compilation and visual result before declaring done.

## Stage 6C — 2026-11-19 — native VIVID Creator UI

- Branch: `feature/vivid-digital-humans`; owner `engine/character/vivid`, `tools/vivid-creator`, and new CMake target. MotionWeave unchanged and sole skeleton scheduler. No main merge.
- Implementation commits: `0cb91b095df7928d21401131335060f3080f33e3` added `tools/vivid-creator/main.cpp`; `3f2242835c929bfb68fbacda415c777d6aa4a417` added `aiverse_vivid_creator` root CMake target linking runtime/VIVID human/actor/MotionWeave; `2adeee4864bf48d680c42fa41a122e8d1722ae70` native `--smoke` GPU render/capture mode; `bf7c898c5da8e48cc186d2c2521023117e521a64` repaired stable ImGui groom sliders and engine font; `32c0463d09d9bb57794c6e8b98473c4e0ce2aa74` passes `AIVERSE_SOURCE_DIR`; `f61f8757703012873e37894584b5048ca25983bd` one-shot tester builds and smoke-runs Creator; `d9c52eb21f92b1e162b8cc4b993b22d2e1773800` focused macOS CI includes Creator compiler target.
- API/pipeline: UI owns `HumanCreatorSession` (transactional validated DNA + undo/redo + JSON save), `generateHumanPreview` model, `GpuMorphAssetCache` plus isolated `GpuActorStream`, `HumanRigBinding` overlay and a presentation MotionWeave World, not a second skeleton engine. The visual scene uses original `Renderer3D` with dedicated dynamic groom vertex storage. `--smoke` captures actual RGBA through `gfx::Device::captureNextFrame` after 4 stable shadow+colour frames; prints `pass=1` only on saved output and creates `vivid-creator.ppm`.
- Tests **NOT RUN** for new 6C C++/GUI. Prior Stage5J 11/11 and 2 GUI tests PASS at `fa5504a9f2e1f64087ef73139525e5bde31976b4`, but do not verify new Creator.
- Risks: failure of new model shader permutations, morph/rig model index mismatches, pending GPU framebuffer capture, dynamic buffer lifetime and GUI hot-reload; verify with user's native Mac one-shot command. Standalone Creator executable, not yet embedded as main AIVERSE editor pane. Imported model integration described separately in 6E.
- Next: add specialized BRDF skin/eye/hair and open import, run Mac full focused tests/GUI. No automatic merge.

## Stage 6D — 2026-11-19 — opt-in human Skin/Eye/Hair BRDF

- Branch: `feature/vivid-digital-humans`. Main and MotionWeave unchanged.
- Implementation commits/files: `49a00a68b48740de08a7c605532ee4d6ce3bccf0` `engine/render/Model.h` extends `ShadingModel` with `Skin/Eye/Hair` and adds `Material.humanProfile`; `10ac2caff3250e96ee52031d46a5ad71847ee6c4` `engine/render/GpuMaterialState.h` profile-aware equality/hash; `a0ac7df1c834d8f9f2dc5ec7025c4b031df22009` `engine/render/Renderer3DModels.cpp` passes profile to the existing material uniform U2, preserving the old zero/one PBR/Unlit values; `a0ec4e0dd8b20429461c8db6ba1b97c81c151095` `engine/render/ModelShaders.h` paired GLSL+Metal skin scatter, cornea/glint and tangent-aligned hair reflection; `065d1198b75683062a1439c6106cea802fae2d30` `engine/character/vivid/HumanMaterials.cpp` DNA-to-shading mode/param mapping; `1bb3688d1fbb8209b137385240f540a808655970` `tests/vivid/human_appearance.cpp` shader source/pipeline state/hash coverage.
- API/architecture: existing PBR renderer; no new RHI APIs, texture binding slots, material uniform size or vertex layout. New `ShadingModel` enum values appended after `Pbr=0,Unlit=1`, so old material output remains unchanged by default. Values `Skin=2,Eye=3,Hair=4` and two per-surface humanProfile floats control compact approximation. `equivalentGpuMaterial` and `gpuMaterialStateHash` now include humanProfile to prevent accidental shared U2 skin/eye/hair state. Avoids genre-specific dependency. Material shader compile still relies on paired `ShaderGraph`.
- Actual new tests: **NOT RUN**; hosted CI may queue, and remote source review does not prove compiling GLSL/MSL. Existing Stage 5J GPU/Renderer3D 11/11 from earlier SHA is historical.
- Scope limits: physically inspired wrapped diffuse/hemoglobin, wet corneal highlights, strand tangent lobe; **NOT** full diffusion-profile multi-layer SSS, ray-traced cornea/aqueous refraction, Marschner/dual scattering, measured scan textures or MetaHuman pixel parity. Batching, instancing, shadow and performance regression must be tested on Mac before production enablement.
- Next: import real glTF/FBX heads with open DNA/morph manifest, then run focused CTest plus actual Creator GUI shader compilation and full engine regressions. Record exact Mac diagnostics.

## Stage 6E — 2026-11-19 — open glTF/FBX DNA/morph package import

- Branch: `feature/vivid-digital-humans`. Open, non-proprietary import. No modifications to MotionWeave scheduler or simulation, `main` not merged.
- Source commits / paths: `5406fdcf31ab88fc3e89923a464fa77206884cc4` `engine/character/vivid/HumanImport.h` manifest + imported human contract; `2d5d85de2d0aa8bdb41889977a807f23fcd31753` `HumanImport.cpp` strict external manifest, DNA, sparse corrective morph JSON and render::loadModel through project FileReader; `32adc384558d5141536e52933c42a9721c46c393` `HumanImport.cpp` fills missing OBJ primitive model-relative firstVertex/count and guards original vertex positions after upload preprocessing; `35caba907465af5fa447712d3174f6cc26b0a7aa` `tests/vivid/human_import.cpp` manifest parse/roundtrip, security/range negatives, 3-vertex fake OBJ root/head import, two sparse morphs and morph-only rig tests; `2d1b73ed7ad815bf0f7641f2b01820a476a2cc06` CMake links new importer into `AIVERSE::vivid_human` and registers `vivid_human_import`; `823abfb980c89f2d49779881e2d613fc7be3437b` `tools/vivid-creator/main.cpp` adds `--asset /path/to/manifest.json` CLI, UI imported-model chooser, DNA sessions and imported material/groom updates while preserving original skinned mesh; `d144dbf5fea0d04bf1a40af564dbba494705bd8b` and `4df2d769d293bb227f91773e387d5898d8d5f8f8` single Mac script/CI include importer test.
- API: `HumanImportManifest::validate/fromJson/toJson`, `parseHumanMorphs(Json,vertexCount)`, `importHumanAsset(manifest,render::FileReader)`, `HumanImportedAsset`. JSON schema: `version=1`, model, optional DNA/morphs JSON, routes mapping named ARKit channels to sparse morph target names, materials mapping authored slots to `HumanSurface` types. Native render::loadModel loads glTF/GLB, FBX (when compiled), OBJ and other engine-supported formats. Asset paths must be project-relative and reject path traversal/absolute, numeric input finite/bounded, shape/CSR names validated. Original mesh vertex IDs are checked before/after prepare; OBJ omitted ranges reconstructed from actual indices. Invalid imports throw instead of publishing incorrect facial geometry.
- Creator import: manifests resolve assets relative to manifest directory, editor accepts user-provided manifest path, uses stored DNA to bind materials and generate groom, MotionWeave node pose continues to own rig. Imported meshes keep authored topology. Editing geometric anatomy parameters does not automatically resculpt a scanned imported mesh; edit materials/groom/expressions or reauthor external mesh.
- Tests actually run AFTER Stage 6E code: **NOT RUN** by this assistant or user. Previous Stage 5J 11/11 and two GUI passes are earlier commit only. No CI green or native import GLB/FBX test evidence. CTest source was written but Mac compilation and real imported 3D human visual preview not yet confirmed.
- Risks: glTF/FBX source must contain a root/head hierarchy matching the JSON DNA bone aliases; morph JSON must reference imported mesh CPU vertex indices after loader canonicalization, not Blender's original unordered mesh indices. Driver/file path and Model::prepareModelUpload topology safety must be checked on actual production assets. Asset importer currently does not parse proprietary Epic MetaHuman DNA binary; only open VIVID schema. Imported external rig/groom look still needs artist-provided model, real textures, animation clips and benchmark.
- Next exact step: do one macOS `bash tools/vivid-validate.sh` after pulling latest, now including 16 CTests, single/12-actor facial previews and real Creator skin/eye/hair smoke PPM. Then separately launch `aiverse_vivid_creator --asset /path/to/manifest.json` to visually validate a user-supplied actual rigged GLB/FBX. Fix first compile/runtime diagnostic; record handoff. Do not merge to main before full regression.

### Stage 6E follow-up — preserved scan textures and real imported-head GUI smoke

- **Implementation commits:** `68d001067d61af3f03795d8aa0565a46095bbccf` `HumanMaterials.h` adds `humanMaterialFromSource`; `c29b19278d61806db38a62762a05fe321b755bcf` implementation preserves imported base/MR/normal/AO/emissive/team texture IDs and UV transforms while replacing shader BRDF; `81f69e41bbf7ef68dfb663b6267fa098f0fb58d7` `HumanImport.cpp` uses it; `48e4f5001bcb050cdd45c580845ea94cabb689b7` Creator preserves imported texture maps during live DNA changes; `f3dbfc2ff485c79132da7cbf8cf196e048bb464d` adds PBR texture-ID regression.
- **Open asset GUI test fixture:** `3e8e149929bb4774d4770092249a7042f0d54def` `tests/vivid/assets/import-head.obj`, `32aba615cd2dca33340f1c3dd70984e996c98050` `tests/vivid/assets/import-face.json`, `d149d1d1fd8b0c6fc69c25d6d6eaefe2b2f2a07a` `tests/vivid/assets/import-head.manifest.json`; all are original tiny reference data, no proprietary MetaHuman meshes. `ac02aeb27cb203a474244e2739fae1591f505240` extends `tools/vivid-validate.sh` to launch GUI `aiverse_vivid_creator --smoke --asset <manifest>` after procedural Creator smoke; requires nonempty `vivid-imported-head.ppm` and `pass=1`.
- **Tests actually executed for follow-up source:** **NOT RUN**; no new Mac compiler/Metal result. Expected onepass now includes **16 CTests** (prior 11 plus DNA, rig, appearance, creator and import) and **four GUI acceptance runs** (1 actor, 12 actors, procedural human Creator, imported OBJ human Creator), with **eight PPM images**. Only earlier Stage5J 11 CTests + two GPU previews are verified as PASSED by user.
- **Next:** Mac owner run the same one-shot script, report first build/test/preview error. After green, full engine regression and realistic licensed scan assets are separate readiness gates. Do not merge source into main until the broader regression passes.


## Stage 6G — 2026-11-19 — DNA-proportional scalp collision

- Branch: `feature/vivid-digital-humans`. `main` and MotionWeave scheduler unchanged.
- Implementation commits: `66173dbe7fbff0313a02cb8e57b81e166b91ad3c` (`HumanGroom.h`), `bc6deb5cd3fe46d0420814623086b6f842dfbc27` (`HumanGroom.cpp`), `5ebb1fd0561aec9c7bc4673cf127bddcff589680` (`tests/vivid/human_appearance.cpp`), `a62d72b3153822775dac3c8e66fd6b7e2fe595bf` (final constraint pass).
- Issue: old grooming used a fixed 0.72-world-unit scalp sphere even though guide root positions use an ellipsoid whose x radius depends on `anatomy.headWidth`, with all axes depending on `headScale`. This permitted guide penetration on wide/tall heads and excessive separation on narrow heads.
- API: no public signature changes. `HumanGroomRuntime::generate` now retains three scalp radii matching the DNA-driven guide ellipsoid, and `step` projects penetrating points to the ellipsoid shell (including degenerate centre contacts). An additional final contact pass prevents a later segment constraint from moving earlier points back into the scalp.
- Regression authored: `tests/vivid/human_appearance.cpp` steps Low groom for 24 frames under wind at head width 0.65, 1.0, 1.35 and head scale 1.2, then rejects nonfinite points and world positions penetrating the corresponding ellipsoid.
- Actual tests executed for these new commits: **NOT RUN**. No native macOS/Metal test execution is available to this agent. The earlier 11/11 CTests + two Renderer3D previews were for Stage 5J SHA `fa5504a9f2e1f64087ef73139525e5bde31976b4`, not Stage 6G.
- Limits: scalp collision is an approximated ellipsoid, not mesh-based collision against arbitrary imported heads, neck, shoulders or cloth. Constraint projection can stretch strands and does not supply self-collision. Imported author-authored hair geometry is not simulated by this procedural card groom.
- Next exact step: on macOS, run `bash tools/vivid-validate.sh` from the VIVID worktree after fetching this branch. Address first compile/CTest/Creator Metal GUI failure, append the real logs/SHA here, then do `bash tools/vivid-validate.sh --full` for merge readiness. Longer-term 6H: scalp mesh collision and motion-stable hair simulation for imported heads.


## Stage 6H — 2026-11-19 — opt-in triangle collider and bounded grooming

- Branch: `feature/vivid-digital-humans`, without modifying `main` or MotionWeave.
- Implementation commits: `9b7d93b8d63e829d4f216600e0b7efc024fc58ef` header API, `86fac42582a985653ae9ac4cbe7524032b98654a` triangle collision implementation, `ad90d4b464516874f15a209b867912b827f2b6b9` regression fixture, `f2dc81e578d36fbc83ec5516dc333d084276eb50` bounded Verlet displacement and collision damping.
- New API: `HumanGroomRuntime::setCollisionMesh(vertices, triangles)`, `clearCollisionMesh()`, `hasCollisionMesh()`. It copies validated bounded local-space triangle data (up to 16,384 vertices / 32,768 triangles); failures keep last-good collider. When present, a closed triangle mesh replaces ellipsoid contact checks. Nearest-triangle projection plus ray intersection parity detects interior points. Groom step also caps violent single-step travel and removes velocity on corrected contacts.
- Tests authored: `vivid_human_appearance` verifies 4-frame collision against a 12-triangle closed box, invalid degenerate collider rejection without state loss, finiteness and reset; existing custom ellipsoid tests remain.
- **Tests actually run: NOT RUN** (remote GitHub source edit only). No compiler, GPU, exact-runtime or frame time claims. Use `bash tools/vivid-validate.sh` on native Mac, then `--full` after fixes.
- Limitations: mesh must be **closed**, in the same local coordinate system as the groom. This is a simple CPU O(strand-segments × constraint-passes × triangles) implementation with naive parity ray tests; ray-edge degeneracy, winding-independent odd-parity tests on non-manifold geometry, thin shells, moving/skinned colliders, backface contact, self-collision, neck/cloth integration and BVH are not production-ready. Root remains pinned, so if the imported closed mesh encloses roots, segment separation may be large; a scalp attachment/transfer rig is required. The Creator UI does **not** automatically pass imported model geometry into this opt-in API.
- Next bounded milestone 6I: BVH or signed-distance collision acceleration, coordinate-space/skin-pose adaptation, explicit collider mesh selection in Creator, tests for moving collision and deterministic dt subdivision. Following separate tracks: measured multilayer skin and corneal refraction with depth, strand hair BRDF, rig/morph/LOD authoring UI. Do not claim MetaHuman equivalence.
