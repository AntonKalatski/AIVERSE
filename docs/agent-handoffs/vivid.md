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
