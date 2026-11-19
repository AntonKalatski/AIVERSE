# AIVERSE VIVID — universal digital-human framework

Status: DESIGN / staged implementation; created 2026-11-19.
Owner / branch: VIVID agent, `feature/vivid-digital-humans`.
Handoff log: [docs/agent-handoffs/vivid.md](agent-handoffs/vivid.md).
Code ownership: `engine/character/vivid/`, `tests/vivid/`, `docs/vivid.md`, `skills/visuals/aiverse-vivid/`.
Cross-cutting render, animation and build changes must be small and coordinated with their owners; avoid editing their in-progress features.

## Purpose and non-goals

VIVID provides believable, customisable digital humans for any AIVERSE game genre: cinematic characters, gameplay avatars, NPCs, crowds, first-person hands, dialogue portraits and user-created avatars. The core MUST NOT depend on RTS, FPS, specific game rules or proprietary Unreal runtime code.

Three independent levels of support:
1. **Interchange character:** import licensed generic glTF/FBX characters, skeletons, animations, textures and morphs; provide standard AIVERSE materials and scalable quality.
2. **MetaHuman-compatible runtime:** handle permitted character exports and optional MetaHuman DNA/rig data through a separately audited adapter (e.g. OpenRigLogic). No dependency on Unreal Engine at game runtime.
3. **VIVID Creator:** editor-side procedural character editing, presets, morphable body/face, skin, eyes, hair, accessories, outfit fitting and non-destructive saved recipes. This is a much later product milestone, NOT an implemented feature.

Never assume that a MetaHuman package can simply be copied or distributed with the engine; inspect Epic asset/export restrictions and third-party dependency licenses per release. The example assets must be own-authored or openly licensed, with provenance.

## Architectural separation

```
external character assets (glTF/FBX/DNA/groom/audio/tracking)
             | offline/authoring converters, validation, provenance
             v
 VIVID CharacterAsset + Skeleton + MorphLibrary + Material profiles
             | shared immutable asset data, per-character ECS references
             v
 VIVID input sources (gameplay, facial tracking, audio, cutscene, AI)
             | semantic facial channels, visemes, expressions, gaze
             v
 VIVID facial solver + rig mapper + corrective deformation
             | pure presentation values, body AnimationGraph/IK layers
             v
 dynamic deformation (GPU compute/vertex; CPU fallback for tooling)
             | bounded LOD/culling/streaming policies
             v
 AIVERSE renderer/RHI (skin/eyes/teeth/hair shaders, hair geometry)
```

- **Presentation vs simulation:** VIVID facial state, animation and hair motion are visual presentation state, not authoritative fixed-point gameplay or net checksum. Gameplay/UI/ECS actors may provide commands or compact semantic replicas; network/replay wire contracts must be explicit and versioned if added later.
- **One skeleton/graph:** reuse `engine/render/AnimationGraph*`, `AnimationRig*`, `Animation.cpp` for body animation. Compose the face as a post-animation modifier / deformation pass. Do not introduce another generic body animation controller.
- **One renderer:** integrate with `engine/render/Model*`, common RHI, materials, Prisma/lighting; do not add an Unreal renderer or bypass RHI for Metal-only features.
- **Data-oriented instances:** immutable shared `CharacterAsset` and mapping tables, compact per-instance `FacialState`, LOD policy and material overrides. ECS components contain handles/state, not per-frame duplicated mesh buffers.
- **Portable core:** C++20, platform-independent CPU reference; use optional backend adapters for GPU compute. macOS Metal and GL/Vulkan paths are verified independently.

## Capability requirements

### Geometry, rig and import
- glTF 2.0/FBX skeletal meshes, body and facial meshes; named shapes, corrective shapes, clip names, sockets, eyes, oral cavity, teeth and tongue; attachment hierarchy and blend-shape tangents if present.
- Support >8 facial morph targets per mesh and >4 skin influences where required **without** raising unrelated uniform/vertex ABI limits silently. Define sparse/corrective GPU morph buffers, 8-influence optional vertex streams and palette strategies; preserve legacy formats and performance.
- MetaHuman DNA LOD mapping, GUI controls and rig logic via optional runtime adapter only after license/API verification. Support multiple rig generations, failure reporting and source-version metadata. FBX blend shapes currently ignored by AIVERSE — implement their conversion before claiming end-to-end FBX facial import.
- Retarget from different skeleton layouts, scale/axes validation and lossless reference pose; offline mesh/skin LOD preserving morph correspondence, face/head attachment remaps.
- Assets carry schemaVersion, import warnings, material slots, texture colour space, dependencies, provenance and character identity-independent asset IDs.

### Facial animation
- Standard 52 ARKit-style semantic channels, extensible custom channels and named blend shapes.
- Blend live face tracking, keyed cinematic animation, authored emotions, visemes/audio-driven lip-sync, procedural micro-motion, blinks and gaze; explicit priority/masks/weights, deterministic input sampling per presentation frame and graceful missing-channel fallback.
- Symmetric/asymmetric controls and correctives, jaw and eye joint drivers, tongue/teeth coupling, head/body layering, stable blending through transitions. AnimationGraph bridge should not overwrite unrelated animated values.
- Precompute/control the rig at character-LOD-specific frequency; support sleeping and update budgets for offscreen/distant actors.

### Rendering and material appearance
- Skin: multiple scattering approximations, subsurface profiles, transmittance, pore micro-normals, sweat/wetness, wrinkle maps, anisotropic detail and makeup variation; all quality-scalable.
- Eyes: cornea/iris/sclera layers, depth/refraction approximation, tearline and wetline; physically plausible eye gaze and catchlights.
- Teeth, gum, tongue, nails and eyelashes with appropriate shading/collision restrictions.
- Hair: hair-card path first, then groom strands with root binding to skinned scalp, tangents, anisotropic shading, shadows, density LOD and collision-aware secondary motion; eyebrows, eyelashes, facial hair and peach fuzz. Fallback to hair cards for mobile.
- Correct in motion, shadows, GI, Prisma postfx, varying exposure and camera distance; no brute-force path-tracing requirement.

### Creator, tools and workflows
- Import inspector with per-asset report, editable facial mapping, expressions/phonemes preview, GPU/CPU deform diff and retargeting diagnostics.
- Preview editor: neutral/brow/smile/frown/jaw/gaze/blink, animation timeline, LOD and skin/hair quality selectors; side-by-side Metal/GL/Vulkan screenshots and GPU/CPU timing.
- Eventually a VIVID Creator: presets, blendable parameter ranges, face/body morphs, rig-compatible clothing, hairstyle/colour, age-appropriate variations, material presets, undo/redo, reproducible JSON recipes and user-generated content validation.
- Optional external DCC workflow (Blender/Maya) and offline import packages; engine runtime must not require them.

## Phased delivery and acceptance gates

| Stage | Deliverable | Acceptance |
|---|---|---|
| 0 | Architecture and handoff contract | This document, owner-scope and durable status; no runtime claims |
| 1 | Facial semantic core | 52 channels, finite validation, tracking/expressions/viseme source mixing, time smoothing; isolated native unit tests |
| 2 | AIVERSE pose bridge | Explicit mapping to existing render::Pose morph slots (0..7), missing names/errors; no source ABI change |
| 3 | Extended GPU deform | Arbitrary named blend shapes, sparse/compute deformation, multiple skin weight streams; GL+Metal comparison incl shadows |
| 4 | Asset import | FBX blend shapes/glTF morph preservation, normal/tangent delta paths, metadata and export-validation CLI |
| 5 | DNA/OpenRigLogic | Optional guarded adapter and licensing audit; version coverage, controlled baseline asset |
| 6 | Appearance | Skin/eyes/mouth materials and quality tiers with reference renders |
| 7 | Hair | Cards, groom strands, scalp binding, physics, update/LOD budget and GPU captures |
| 8 | Performance/Creator | Character LOD, editor authoring, remote playback, mobile budgets and cross-platform regression |

Stages may split into smaller bounded implementation units; after **every** unit commit a separate handoff commit, recording exact SHA, tests actually executed, limitations and next step.

## First-stage public contract

Prefer public headers under `engine/character/vivid/`, namespace `aiverse::vivid`; C++ usable from any native project module. Keep the pure facial core free of render/OS/engine imports. Expose a constant semantic channel order, safe named lookups, an instance-owned input mixer and an explicit `advance(dt)` method. Never silently truncate unknown channels to eight; only the legacy render adapter is temporarily eight-target-limited.

## Required test matrix (do not claim passed until run)
- Native semantic channel unique-name/lookup, all 52, NaN/Inf/negative input rejection, clamping and saturation.
- Source priority/layer interaction, independent controller instances, first-frame setup, dt=0 and pause/resume.
- Pose adapter remapping, duplicate targets, invalid model/node/morph index and animation-layer preservation.
- Rig binary/asset corruption tests, malformed or absent metadata; valid sample renders and facial pose screen captures.
- CPU/GPU skinning and morph numerical parity, sun/reflection/shadow consistency, render-thread safety and repeatability.
- 1/10/100 character CPU/GPU costs with LOD, crowd performance, memory peak and mobile/desktop fallback.

## Status truth (2026-11-19)
At design creation no VIVID runtime, editor, DNA, skin or groom features were implemented/validated. Existing AIVERSE supports up to 8 GPU morphs per primitive and up to 80 joints per primitive palette, and FBX import ignores blend shapes; these are **blockers for full MetaHuman fidelity**, not bugs to remove blindly. This file is the design contract, not completion evidence.

## Implemented stage 1 — portable facial channels (2026-11-19)

Available now (source implemented; full native build **NOT RUN**):
- `engine/character/vivid/Face.h/.cpp`: typed 52-channel ARKit-compatible semantic API, strict names and lookup, instance-owned track/emotion/speech source layering, optional per-layer crossfade, source clearing, safety clamp, exponential smoothing, instant snap and reset.
- `setEmotion(Neutral/Happy/Sad/Angry/Surprise/Disgust/Fear)` and `setViseme(Silence/Aa/Ee/Oh/Fv/Mbp)` are simple *authoring presets*, **not** speech recognition, lip-sync extraction or MetaHuman DNA solving.
- `AIVERSE::vivid` CMake alias, `vivid_face` CTest unit target; no renderer, importer or gameplay coupling.

Usage from native code:

```cpp
#include "engine/character/vivid/Face.h"
aiverse::vivid::FaceController face(0.08f);
face.set(aiverse::vivid::Layer::Tracking, "eyeBlinkLeft", 1.0f);
face.setEmotion(aiverse::vivid::Emotion::Happy, 0.6f);
face.setViseme(aiverse::vivid::Viseme::Aa);
face.advance(dt);
const float jaw = face.frame().value(aiverse::vivid::Channel::JawOpen);
```

The `face.frame()` result is a semantic vector only. **Nothing applies it to meshes or draws a face yet.** No save format, ECS registry or network schema was added. Stage 2 will bridge this data to existing `render::Pose` morph slots while leaving their current 8-target limit explicit.

## Implemented stage 2 — legacy pose bridge (2026-11-19)

Source available, **native build/test not yet executed**.

- `engine/character/vivid/MorphBinding.h/.cpp` adds `aiverse::vivid::MorphRoute` and `MorphBinding::compile(model, routes)`. Compile-time validation checks the face channel, owning model node, target slot and actual mesh primitive morph count, finite scale and duplicate destinations.
- `MorphBinding::apply(frame, pose, influence, MorphApply::Override | Additive)` composes a facial frame **after** `render::Animator::evaluate` or `AnimationGraphInstance::evaluate`, maintaining unrelated bone transforms and morph slots. Invalid input/pose length returns false **without partial modifications**.
- Target `AIVERSE::vivid_render` links the pure semantic core and existing `aiverse_models`; `vivid_morph_binding` CTest source covers rejected routes, interpolation, additive clamping, preservation and invalid inputs.
- Only the **already supported 0..7 pose morph slots** are writable; binding 52 semantic controls to different slots is possible in data, but it does not give a mesh 52 simultaneously usable morph targets. This adapter is *not* the Stage 3 extended GPU morph deformer.

Native host example:

```cpp
#include "engine/character/vivid/MorphBinding.h"
std::vector<aiverse::vivid::MorphRoute> routes {
    {aiverse::vivid::Channel::JawOpen, faceNodeIndex, jawMorphSlot, 1.0f}
};
auto binding = aiverse::vivid::MorphBinding::compile(model, routes);
aiverse::render::Pose pose;
animGraphInstance.evaluate(pose);
face.advance(dt);
binding.apply(face.frame(), pose); // before model renderer consumes pose
```

Next: Stage 3 must add a separately versioned, scalable blend-shape storage/deformation path; do not raise `kMaxMorphTargets` globally or bypass RHI without profiling.

## Implemented Stage 3A — sparse morph reference + GPU buffer contract (2026-11-19)

**Source authored; new native tests NOT RUN yet.** The two earlier `vivid_face` and `vivid_morph_binding` tests were reported PASS by the user (2/2), prior to this stage.

- `engine/character/vivid/SparseMorph.h/.cpp`: immutable `SparseMorphLibrary`, `MorphTargetInput` and `MorphDelta`, named targets with per-vertex position, normal and tangent deltas. Canonical sorting, duplicate/bad numeric validation, zero-delta compaction, finite input validation, persistent source-independent packed layout and sparse CPU reference evaluator.
- No hard eight-slot limit: practical per-asset budget **4096 targets and 16,000,000 sparse entries**. Reference test exercises 160 independent facial targets. These limits are intentional resource-safety budgets, not UE/MetaHuman quotas.
- `GpuMorphDelta`: explicit **64-byte** / 16-byte-aligned representation containing vertex index plus float4 position, normal, tangent deltas. `GpuMorphRange`: 16 bytes per named target, containing index offset/count. Layout validated at compile time and designed to be portable to Metal/GLSL storage buffers. **Not yet GPU executed**.
- `SparseMorphLibrary::evaluate(base, weights, output, normalizeDirections)` is a CPU oracle to measure GPU parity and catch import bugs. The caller supplies fixed-sized vertex and weight spans; no per-call heap allocation; malformed arguments return false without output mutation; same-span in-place evaluation is supported.
- New pure-core test `vivid_sparse_morph`; sources are under `tests/vivid/sparse_morph.cpp`. **CTest not run** on this revision.

This stage does **not** replace the legacy `render::Model` eight-target path, import MetaHuman DNA, bind arbitrary names to semantic channels or produce rendered character pixels. Next stages must generate per-vertex *gather* adjacency to avoid race-prone GPU scattering, wire the new deform buffer into the RHI skinning path, and keep legacy loads identical unless explicit VIVID assets are present.

## Implemented Stage 3B — facial semantics to named morph targets (2026-11-19)

**Source authored, native build/CTest NOT RUN in this stage.**
- `FaceShapeBinding.h/.cpp`: immutable `ShapeRoute{Channel,targetName,gain}` compiled against a `SparseMorphLibrary`. Many distinct morph targets may be controlled by one semantic channel; unknown names and duplicate destination names rejected, with explicit finite/clamped scales. This bypasses the *semantic* 8-target bottleneck without modifying the existing renderer.
- `FaceShapeBinding::evaluate(face, weights, influence)`: per-character weight buffer must be sized to all packed targets; all mapped weights updated in one pass, unused targets zero, invalid input leaves output unmodified. No heap allocations during evaluation.
- `tests/vivid/shape_binding.cpp` test exercises 97 routes (96 jaw corrections + one smile), 97 named shapes in one library, full `FaceController -> FaceShapeBinding -> SparseMorphLibrary::evaluate` chain, parameter validation and instance isolation.
- `aiverse_vivid_shape_tests` and CTest `vivid_shape_binding` registered. **These tests are authored, not yet run**.

Example:

```cpp
std::vector<aiverse::vivid::ShapeRoute> routes{
    {aiverse::vivid::Channel::JawOpen, "jawOpen_primary", 1.0f},
    {aiverse::vivid::Channel::JawOpen, "jawOpen_corrective", 0.4f}
};
auto shapeBinding = aiverse::vivid::FaceShapeBinding::compile(library, routes);
std::vector<float> shapeWeights(library.targets());
shapeBinding.evaluate(face.frame(), shapeWeights);
library.evaluate(baseMesh, shapeWeights, deformedMesh); // CPU reference only
```

Performance: current code evaluates targets on CPU, not on the GPU; do not claim production facial rendering. Next: Stage 3C produce a deterministic per-vertex gather table for a compute/vertex GPU pass without unordered float atomics.

## Implemented Stage 3C — deterministic GPU gather topology (2026-11-19)

**Source authored; full native compile, CTest and Metal/GL/Vulkan execution of this stage NOT RUN.**

A target-major sparse morph delta list cannot safely be scattered to GPU vertices without floating point atomics. VIVID now also compiles a **compressed sparse row (CSR)** per-vertex table:

- `gpuVertexOffsets()`: `uint32[vertexCount+1]`, a prefix sum of how many nonzero shape deltas affect every vertex.
- `gpuVertexLinks()`: `GpuMorphLink{targetIndex,deltaIndex}`, exactly **8 bytes** each, ordered by ascending target index within every vertex.
- GPU kernel contract (not yet implemented): one thread per output vertex; loop `links[offsets[vertex]..offsets[vertex+1])`, multiply corresponding morph delta by that target weight and accumulate position/normal/tangent locally, with no cross-thread scatter or float atomic writes. Feed resulting vertex buffer into ordinary skinning/rendering. This can support much more than eight channels without changing legacy `render::Pose` arrays.
- `SparseMorphLibrary::evaluateGather()`: portable, no-alloc CPU implementation of the GPU gather algorithm, for parity against existing `evaluate()` and future Metal/GL/Vulkan tests.
- `GpuMorphDelta` 64B, `GpuMorphRange` 16B, `GpuMorphLink` 8B and offsets `uint32` form the proposed GPU buffer contract; exact backend alignment/barrier/ownership rules must be confirmed before shipping GPU dispatch.
- Conservative asset budgets now explicitly include **16,000,000 vertices**, **4096 targets**, and **16,000,000 sparse deltas**. Values above the vertex budget fail before large allocations.
- `tests/vivid/sparse_morph.cpp` now checks exact CSR offsets/references and a 160-target gather/scatter CPU parity case. Updated test not run in this stage.

End-to-end renderer changes remain pending. The existing 8-target `render::Model` morph pipeline is unchanged. Shader compute dispatch, buffer ownership, dirty weights, skinning order, shadow and reflection path consistency, async compute barriers, mobile fallback and live MetaHuman import are **not** implemented.

## Native verification checkpoint — 2026-11-19

**User-confirmed macOS validation at commit `31d04c986ada3ff4602d303d28fa4cdc08686de8`.** Configured successfully on macOS, built targets `aiverse_vivid_face_tests`, `aiverse_vivid_morph_tests`, `aiverse_vivid_sparse_tests`, and `aiverse_vivid_shape_tests`. User-shared CTest output reports **4/4 passed, 0 failed** in **0.71 s**: `vivid_face` (0.03 s), `vivid_sparse_morph` (0.03 s), `vivid_shape_binding` (0.61 s), `vivid_morph_binding` (0.03 s). No warning/error diagnostics appear in the shared post-fix build output. These results validate the current CPU facial core, sparse morph/gather reference and routing tests, **not** a GPU morph deformer, Metal/Vulkan/OpenGL integration, DNA import, or visual output. Tests were executed by the user on Mac, not independently by this agent. Full suite and other configurations remain unverified.

## Stage 4A — opt-in GPU morph compute dispatcher (2026-11-19)

**Source committed, native build/GPU execution NOT RUN.** This is an *opt-in* RHI adapter, not an integrated renderer/character pipeline.

- `engine/character/vivid/GpuMorph.h/.cpp` adds `GpuMorphDeformer::initialize(gfx::Device&, SparseMorphLibrary, baseVertices)`, `dispatch(weights)`, `outputBuffer()`, `download()` (diagnostic-only between frames), `release()`. Six RHI storage buffers: 48B GPU-packed base vertices, 64B deltas, uint CSR offsets, 8B links, float weights and a 48B packed output stream. A 32B aligned uniform block supplies counts and normalization mode.
- `VividComputeShaders.h` supplies paired Metal/GLSL compute shaders. Each GPU invocation gathers every sparse morph contribution for one vertex, using the CSR table with no floating-point atomics. Position, normal and tangent are accumulated; normal/tangent normalization is optional. It runs over the existing RHI compute interface; Metal has a concrete compute implementation. GL/Vulkan support depends on actual backend capability and has **not** been proven by simply supplying GLSL source.
- Independent link target `AIVERSE::vivid_gpu`, linked to `aiverse_vivid` + `aiverse_engine`. `vivid_gpu_contract` CTest uses a fake gfx device to verify buffer allocations, uniforms, dispatch/bindings and failure cleanup. **The fake device does not execute GPU code or validate MSL compilation.**
- `GpuMorphDeformer` memory allocation is capped to 512 MiB and rejects nonfinite vertices/weights, invalid counts and unsupported compute backends. GPU resources own their device lifetime; do not destroy device before VIVID deformer. `initialize` and `release` are device-thread-only, `dispatch` must run inside a GPU frame but outside render passes.
- **Missing before production**: actual Apple Metal GPU dispatch/readback numerical parity, GPU-to-Model skinning integration, output vertex format use in existing render passes, buffer synchronization across frame slots, tested GLES/GL/Vulkan fallback, complex-body/facial corrective graph and LOD.

### MotionWeave integration policy (2026-11-19)

MotionWeave is on `feature/universal-ecs-animation-pipeline` (inspected at **`2ae54740331d7ed6ba8d8e01e6c39e753391852f`**), with `AIVERSE::MotionWeave`, `<aiverselib/motionweave/MotionWeave.h>` and presentation ECS World. Its branch is **913 commits ahead of main**, substantially divergent from the current VIVID branch (913 ahead, 46 behind on GitHub compare). Its ongoing animation feature scope includes `render::Pose`, resource/lifetime/retargeting and shared RHI files. **No MotionWeave commits have been merged into VIVID during Stage 4A**: a wholesale merge would introduce unrelated evolving renderer changes before the GPU morph adapter needs those interfaces.

Future adapter must consume MotionWeave's *final presentation pose* (after animation-graph IK, retargeting and authoring adjustments), blend VIVID face semantics as a clearly ordered post-graph stage and feed a separate *extended morph GPU stream*. Keep MotionWeave as authoritative producer of skeleton pose and prevent competing pose writers, preserve presentation-only ECS isolation and transfer only safe GPU buffers to the render thread. Once MotionWeave is stable/merged into main, integrate via public `AIVERSE::MotionWeave` rather than copy its animation algorithms. Recheck HEAD before cherry-picks; tests in MotionWeave documentation are deferred and are not evidence of production readiness.

## MotionWeave integration — merged on 2026-11-19

The earlier Stage 4A sentence saying MotionWeave is "not merged" is superseded by this section. MotionWeave branch snapshot `548f75f0ce824db6f468f6577da95387d388ea0b` was merged into VIVID using **real two-parent merge commit `f27c076410a5cb0aa7d12cc689d83c7eff302928`**, with VIVID parent `dfd4f3fab67cd573da4322ddad2f108a27f7cb38`. The merge tree starts from MotionWeave's full snapshot, adds the complete original VIVID source and tests, and deliberately combines the two overlapping files `AGENTS.md` and `CMakeLists.txt`; no features were intentionally dropped.

The resulting branch exposes `AIVERSE::MotionWeave`, `aiverse::motionweave::MotionWeave::update`, `AnimationPoseComponent`, the shared animation graph/retarget pipeline and the original VIVID `AIVERSE::vivid`, `AIVERSE::vivid_render`, `AIVERSE::vivid_gpu` targets. Source-branch MotionWeave and `main` remain untouched. This is a source-level integration, **not** a green combined native build or verified renderer path: macOS CMake, MotionWeave's tests and GPU readback tests remain outstanding on this merged revision.

VIVID shall **reuse** MotionWeave's presentation World, scheduler, finished `AnimationPoseComponent` and managed rig; it must not add a second skeleton graph, retargeting system, IK solver, animation worker scheduler or LOD manager. The order is: `MotionWeave::update` finishes (including worker joins) → VIVID samples inputs and computes facial weights → VIVID creates a derived render pose/morph stream **without modifying MotionWeave's cached pose** → the existing AIVERSE renderer consumes the result. Read and use `libraries/include/aiverselib/motionweave/MotionWeave.h` and `libraries/include/aiverselib/animation/EcsAnimationSystems.h`.

## Stage 4B — explicit VIVID / MotionWeave pose bridge (2026-11-19)

**Implemented in source; combined native build/CTest NOT RUN yet.** New `AIVERSE::vivid_motionweave` target depends on `AIVERSE::MotionWeave` and `AIVERSE::vivid_render`. Root CMake registers VIVID **after** the MotionWeave target is declared, avoiding unresolved CMake alias references.

Public function `composeFaceAfterMotionWeave(source, facial, binding, output, scratch, influence, mode)` in `engine/character/vivid/MotionWeaveBridge.h` consumes `aiverse::motionweave::AnimationPoseComponent`. Call only **after** `MotionWeave::update` and its worker-join barrier. It retains a single owner for AnimationGraph and the bone skeleton (MotionWeave). VIVID only applies the legacy-morph facial layer onto a **derived** render pose; neither the cached MotionWeave pose nor the matrix array is mutated. `output` and `scratch` are caller-owned reusable `render::Pose` buffers; the function uses scratch + swap to avoid a second copy and protect previously rendered output if an input is invalid. A repeated frame never accumulates additive face weights into a cached MotionWeave frame.

```cpp
#include <aiverselib/motionweave/MotionWeave.h>
#include "engine/character/vivid/MotionWeaveBridge.h"

// MotionWeave is the ONE animation scheduler.
aiverse::motionweave::MotionWeave::update(presentation, dt);
const auto& source = presentation.get<aiverse::motionweave::AnimationPoseComponent>(actor);
face.advance(dt);
aiverse::vivid::composeFaceAfterMotionWeave(
    source, face.frame(), morphBinding, renderPose, scratchPose);
```

The adapter covers only legacy `render::Pose` morph indices 0..7 because that is the format that MotionWeave/Renderer3D currently exposes. Extended 52+ shape weights are handled separately by VIVID's `FaceShapeBinding` / GPU CSR morph stream, pending real GPU numerical parity and renderer consumption. This is **not** a fork of MotionWeave, nor a replacement of the rig/retargeting pipeline.

New headless CTest source: `tests/vivid/motionweave_bridge.cpp`, registered as `vivid_motionweave_bridge` / target `aiverse_vivid_motionweave_tests`. The fixture uses real MotionWeave presentation ECS World and a bind-pose AnimationGraph and checks source isolation, repeat-frame additive stability, morph preservation and invalid-input rejection. Native compilation and execution **NOT RUN** in this stage.

## Stage 4D — real macOS Metal compute parity test (2026-11-19)

**Test source added; native Metal execution NOT RUN on this new commit.** This stage validates the GPU kernel directly, not yet the engine's complete model renderer.

- New macOS-only `tests/vivid/metal_gpu.mm`, target `aiverse_vivid_metal_gpu_tests`, CTest `vivid_metal_gpu`. It compiles the *actual* `kVividMorphMSL` kernel from `VividComputeShaders.h` at runtime using Apple's Metal framework and dispatches 64-thread workgroups, with an intentionally non-multiple-of-64 **137-vertex** mesh and **160 independent morph targets**.
- Test builds the same `SparseMorphLibrary`/CSR and `GpuMorphVertex`/uniform buffer layout that the VIVID GPU adapter uses. It uploads all six buffers (base, sparse deltas, vertex offsets, links, weights, output), dispatches and waits for completion, then compares all resulting vertex positions/normals/tangents against `SparseMorphLibrary::evaluateGather` within a maximum absolute difference of `0.0005`.
- Three real Metal passes: authored weights with normalized directions, authored weights without normalization, and all-zero weights. Non-finite outputs or a shader/pipeline/command failure cause a failing CTest. A machine without a Metal-capable device exits with skip code 77 and is reported **Skipped**, not Passed.
- CMake enables Objective-C++ only for macOS when tests are enabled and links Foundation + Metal. No changes to `engine/gfx` or MotionWeave source files, public C++ API, saved games or network formats.
- **What this test does not prove:** the `gfx::Device` Metal command path (`GpuMorphDeformer::dispatch()`), GPU resource lifetime across frames, drawing with VIVID-deformed mesh buffers, VIVID↔MotionWeave skinning order, other render passes or Vulkan/GL. A separate actual device RHI integration test and renderer hookup are required after the kernel passes.

The user's earlier **7/7 native tests Passed** on the pre-Stage-4D merged source. No real Metal GPU comparison has yet been executed, and do not transfer those passes to this new test.

## Stage 4F — real AIVERSE Metal RHI end-to-end compute validation (2026-11-19)

**Code committed; new native test NOT RUN on the Mac yet.** Stage 4D's direct Metal compute test `vivid_metal_gpu` was reported **Passed (1/1, 0.68 s total)** by the user at source SHA `424e8382144511c1b453ce9d3234cc813734356c`. Its CMake/Objective-C++ issue is resolved. This proves the kernel/CSR ABI numerically on real Apple hardware but not the AIVERSE GPU resource path.

New `tests/vivid/metal_rhi.mm` calls the engine's actual `aiverse::gfx::createMetalDevice` using a headless CAMetalLayer and drives `GpuMorphDeformer::initialize/dispatch/download/release`, using `gfx::Device::beginFrame/endFrame` between multiple submissions. It constructs **160 sparse morph targets / 79 vertices**, reads the actual output RHI storage buffer and compares all position/normal/tangent components to `SparseMorphLibrary::evaluateGather` with 0.0005 absolute tolerance. Four consecutive GPU frames vary morph weights and normalization and verify that deformation never accidentally accumulates across frames.

The macOS-only `aiverse_vivid_metal_rhi_tests` binary links `engine/gfx/metal/GfxMetal.mm` in the standalone test rather than linking all of `aiverse_runtime`; a minimal `platform::readContentFile` stub replaces the runtime's optional shader-archive content hook for this test only. CMake target uses Objective-C++20, ARC, Foundation, QuartzCore and Metal frameworks. CTest entry is `vivid_metal_rhi` with return code 77 treated as Skipped only if no physical Metal device exists; shader/RHI failures must fail.

This does **not** yet connect the GPU-computed 48B `GpuMorphVertex` stream to `ModelVertex` drawing/skin vertex buffers or the final MotionWeave skinned pose. Render integration will require careful frame ownership, barriers, LOD and shadow parity.

User Mac runner: `cmake --build build/vivid --target aiverse_vivid_metal_rhi_tests -j 4` then `ctest --test-dir build/vivid --output-on-failure -R '^vivid_metal_rhi$'`. Record exact compile errors, warnings and GPU parity numbers in the next stage handoff; until then **new RHI test is NOT RUN**.

## Stage 5A — render-compatible 60-byte GPU morph stream (2026-11-19)

**New implementation and Metal RHI test source committed; full native build / new GPU test NOT RUN on this revision.**

The renderer consumes a **60-byte** `render::ModelVertex` with float3 position/normal, float2 UV, float4 tangent with handedness in `.w`, 4B colour, 4B joints and 4B skin weights. VIVID's sparse morph kernel outputs a separate **48-byte** `GpuMorphVertex` (3 float4). Feeding 48-byte memory directly into Renderer3D's fixed 60-byte vertex pipeline would corrupt geometry and skin/UV data.

VIVID now has a second, **optional GPU compute stage**, `GpuMorphMeshStream` in `engine/character/vivid/GpuMorphMeshStream.h/.cpp` backed by matching Metal and GLSL compute kernels in `GpuMorphMeshShaders.h`. It consumes the original 60-byte vertex buffer + the morphed 48-byte stream and produces a packed 60-byte vertex stream. A GPU thread handles exactly one vertex and copies the original 15 uint32 words, replacing only the bit patterns for position (0–2), normal (3–5), and tangent.xyz (8–10); UV (6–7), tangent handedness (11), colour (12), joints (13) and weights (14) are preserved **bit-for-bit**. This means existing Renderer3D skinning semantics remain intact: morph deformation precedes skinning and no skeleton algorithms are duplicated.

Public API: `AIVERSE::vivid_mesh`, `GpuMorphMeshStream::initialize(device, span<ModelVertex>)`, `dispatch(deformedGpuBuffer, vertexCount)`, `vertexBuffer()`, `download(span<ModelVertex>)`, `release()`. A 512 MiB combined source/destination budget prevents unbounded GPU allocation. The full model stream (same indexed/global vertex space) is required, not a primitive-local partial stream.

The real Metal RHI test `vivid_metal_rhi` was **extended (source only)** to dispatch VIVID's sparse gather then stream conversion in one GPU frame across four weight profiles; compare all deformed position/normal/tangent components against `SparseMorphLibrary::evaluateGather` and require UV, tangent.w, colour, joint IDs and joint weights to match the original 60-byte vertices byte-for-byte. The previous `vivid_metal_rhi` passed on user Mac at `22b60c471`, **before this extension**; therefore extended vertex-stream test is NOT YET VERIFIED.

**Still NOT wired into normal draw calls.** `ModelDraw` does not yet accept an opt-in per-instance GPU vertex-buffer override. `GpuMorphMeshStream::vertexBuffer()` is now a storage buffer with model-compatible bytes, but the renderer must explicitly handle lifetime across colour, depth, shadows, instancing, geometry pooling/ICB and LOD before binding it for production. The 8-target compatibility path remains unchanged. MotionWeave still owns the finished pose, skin matrices and animation scheduling; VIVID only prepares input vertices for existing skin shaders.

## Stage 5B — Apple Metal MSL keyword / mesh compute diagnostics (2026-11-19)

**Source fix committed, Metal GPU test NOT RUN after fix.** The user successfully built the added 60B GPU stream target and the real Metal RHI test, but the `vivid_metal_rhi` CTest **FAILED** before submitting the new mesh conversion kernel, reporting only `VIVID RHI 60-byte mesh stream initialization failed`. The separate `vivid_metal_gpu` direct Metal kernel test remained **Passed**. The combined selection was **1/2 Passed**, not 2/2.

The new `kVividMeshStreamMSL` kernel incorrectly used `vertex` as a C++/Metal parameter name (`uint vertex [[thread_position_in_grid]]`), colliding with Metal's `vertex` stage qualifier keyword. It now uses `vertexIndex` consistently in the MSL source. The GLSL compute shader and all buffer layouts are unchanged.

The real `tests/vivid/metal_rhi.mm` error path now emits `GpuMorphMeshStream::lastError()` and `gfx::Device::lastError()` when `initialize` fails, making any remaining shader compile or allocation error actionable instead of hiding the cause behind a generic assertion. Further MSL/API compiler behavior must be verified by the next macOS run; **this is a root-cause hypothesis supported by source inspection, not yet proof of a passing shader compile**.

Run `aiverse_vivid_metal_rhi_tests` and `aiverse_vivid_metal_gpu_tests` on Mac again before changing `Renderer3D`. No changes were made to `MotionWeave`, `ModelVertex`, the general RHI or game save/network data.

## Stage 5C — opt-in Renderer3D dynamic vertex-stream override (2026-11-19)

**Source authored and committed; native build / renderer colour-shadow visual tests NOT RUN yet.** A production VIVID actor must not be enabled until compile and visual validation are green.

The AIVERSE renderer now accepts **per-`ModelDraw` opt-in fields**, `vertexOverride` (`gfx::BufferHandle`) and `vertexOverrideCount` (`uint32_t`). They default to an invalid handle/zero, preserving the legacy path. Pass `GpuMorphMeshStream::vertexBuffer()` and `vertexCount()` only after VIVID compute submissions in the **same GPU frame before render passes**. The buffer must belong to the exact gfx device and remain alive through all shadow/colour/reflection/depth draws, GPU command completion and any captures.

The header-only `engine/render/ModelVertexStreamOverride.h` policy validates that the stream is a full, model-relative **60-byte `ModelVertex`** array, long enough for all primitive vertex ranges, matches a retained CPU `model.vertices.size()` when that data exists, and is never used with pooled geometry, nonzero model base-vertex/index offsets or specialized geometry clusters. Invalid opt-ins are rejected; they never silently render the original unmorphed mesh.

Initial acceptance is intentionally limited to **Metal**, dedicated **dynamic** `Model` buffers (no `kDrawStatic`, no `BakedSample`). Queued `ModelItem`s retain the custom handle. `drawModelBatch` binds the opt-in vertex stream in place of `model->vb` for ordinary draws. The same draw path handles shadow, opaque/transparent, reflection and other phases, preserving existing `ModelVertex` attribute layout, materials and MotionWeave-provided skin matrices.

**Safety gates**: overridden models disable the legacy eight-slot GPU morph shader (avoids double deformation), cached rigid lookup, regular/GPU LOD selection, primitive material/shadow merging, rigid bounds culling and primitive occlusion, regular instance batching, MDI, GPU-authored/encoded ICB and geometry-cluster optimizations. Switching an existing persistent-ID object to a stream explicitly retires its stale static scene record. These are conservative early-stage restrictions; richer culling/LOD/batching is deferred to a separate measured stage.

Example host-side integration (pseudo usage inside the same gfx frame):

```cpp
// MotionWeave remains sole owner of the animation pose and skeleton palette.
// VIVID data was prepared before the draw; 60B stream contains morph only.
deformer.dispatch(shapeWeights);
meshStream.dispatch(deformer.outputBuffer(), deformer.vertexCount());

render::ModelDraw draw;
draw.model = &model;
draw.nodeWorld = &motionWeavePose.matrices;
draw.vertexOverride = meshStream.vertexBuffer();
draw.vertexOverrideCount = static_cast<std::uint32_t>(meshStream.vertexCount());
// renderer.drawModel(draw) queues this item; renderer renders model passes later
// in the same GPU frame while both compute outputs remain alive.
```

New headless `vivid_vertex_override` test verifies public default-off behavior, valid/invalid stream sizes and shared-geometry offset exclusions. It does not render pixels or test real RHI state.

**Still pending:** actual native compilation of merged Renderer3D on Mac, baseline regression, real scene and colour/shadow/LOD/culling screenshots, GPU fences/in-flight safe reuse, material/normal-map parity, visual bounds correctness, and performance benchmarks; no claim that VIVID characters render successfully yet.

## Stage 5D — native Renderer3D colour/shadow/MotionWeave visual preview (2026-11-19)

**Preview executable authored; GUI build, pixel capture, colour/shadow visual parity and renderer link NOT RUN by this agent.** Prior 3/3 Mac test evidence was for Stage 5C pre-preview source, and must not be attributed to this new program.

The new macOS-only `aiverse_vivid_renderer_preview` is linked to the full platform-owned `aiverse_runtime`, `AIVERSE::vivid_mesh` and **existing `AIVERSE::MotionWeave`**, rather than creating an alternate animation system. It has its own test-only `aiverseMain` entry and runs a small procedural sphere/head and floor under real `Renderer3D` shadow + colour passes, using the actual Metal gfx device supplied by the platform.

It composes one reusable presentation World/AnimationGraph clip for gentle vertical node motion, and VIVID's semantic `FaceController`/`FaceShapeBinding` for two exaggerated facial target regions (jaw stretch and right-side displacement) to make visual differences measurable. Each frame, before calling `Renderer3D::drawModel`, it dispatches the sparse 48B CSR morph deformer and 60B mesh stream packer. The original model path renders the baseline, while the later frames explicitly assign `ModelDraw::vertexOverride` / `vertexOverrideCount`. Both paths use the same material and existing `nodeWorld` matrices. For this deliberately isolated fixture, global geometry pooling and the new model's LOD and instancing/MDI features are disabled; no production configuration is changed.

It requests three actual RGBA screen captures from `gfx::Device::captureNextFrame`, writing `/tmp/aiverse-vivid-baseline.ppm`, `/tmp/aiverse-vivid-morph.ppm`, and `/tmp/aiverse-vivid-motionweave.ppm` by default. Set `AIVERSE_VIVID_PREVIEW_PREFIX` to change the path prefix. The fixture checks that the render profile submitted both a shadow and a colour pass, model shaders report no error, the three captures are full-sized, a meaningful number of pixels differ between base and morphed render, and a later MotionWeave-driven node motion causes another visible change. This is a **diagnostic threshold test**, not a photographic/perceptual proof of realistic facial animation or exact shadow-map parity.

The program is intentionally **not registered as CTest**, because it opens a real macOS GUI Metal window and requires drawable/present/capture. It terminates with exit code 0 only after successful capture/comparison, otherwise 1. GPU resources are released only after a real idle barrier and after finishing all render passes. The default legacy renderer code path is unchanged.

Run on the user's Mac from any directory with a GUI session:
```sh
cmake --build ~/Projects/AIVERSE-vivid/build/vivid --target aiverse_vivid_renderer_preview -j 4
AIVERSE_VIVID_PREVIEW_PREFIX=/tmp/aiverse-vivid \
  ~/Projects/AIVERSE-vivid/build/vivid/aiverse_vivid_renderer_preview
```
The executable may be an app bundle on some CMake generators; inspect the generated target for its actual path rather than assuming. Keep all model, VIVID GPU stream and MotionWeave resources alive until rendering finishes. **NOT RUN** on the current coding host.

## Stage 5E — procedural vertex-range metadata and deferred Metal capture (2026-11-19)

**Source fixes committed; new native GUI run NOT RUN after these changes.**

The user's macOS `aiverse_vivid_renderer_preview` built and printed `ready: 925 vertices, 2 GPU shapes`. It delivered the initial baseline PPM but stopped with `missing shadow/colour model submissions: 0/0` and `model vertex override: rejected invalid/pooled 60B stream`. The `[metal-assets]` report had 11 source misses, 15 pipelines and zero compilation failures. This is not a passing visual preview.

Inspection found that `render::modelFromParts` had populated `Primitive.firstIndex`, `Primitive.indexCount`, and appended CPU vertices, but left both `Primitive.firstVertex` and `Primitive.vertexCount` at **zero**. The strict `modelVertexOverrideStatus` correctly rejected the procedurally generated head because no usable primitive vertex range could be established. `engine/render/Model.cpp` now populates both fields for each procedural part using the model-relative vertex base and count. A separate regression checks two distinct parts, checks the override before and after CPU vertex eviction and retains rejection of genuinely invalid / pooled geometry. We deliberately **did not weaken** `Renderer3D`'s safety checks or permit an unknown vertex stream length.

First-time Metal model shader compilation can also leave zero submitted draws for some frames without the shader itself being broken. The preview no longer fails immediately when the profile shows zero shadow and colour draws. Instead, it records successful draw submissions for four consecutive frames before requesting a screenshot. It proceeds in three stages (baseline, GPU facial morph, MotionWeave node animation), holding the current stage until the asynchronous screenshot callback reports a real saved 960×640 RGBA image. A later stage's capture is not requested until the previous capture completes. It emits progress diagnostics every 45 frames and fails clearly after 400 frames if warmup/capture cannot finish. It also checks procedural vertex ranges at initialization and uses explicit preview shadow bounds.

**Test status:** these modifications have not yet been compiled or visually run on the user's Mac. The earlier CTest 3/3 GPU/vertex-policy passes were on an earlier commit. The test-only preview requires a macOS GUI / Metal drawable and is not a CTest. The ordinary renderer path and MotionWeave ECS skeletal-pose ownership are unchanged.

Run `aiverse_vivid_vertex_override_tests` and the GUI preview after pulling the new branch. Inspect three PPM files and the printed pixel differences; until they exist and both model pass counters become nonzero, do not declare this stage complete.

## Stage 5F — per-character GPU stream isolation and frame ownership (2026-11-19)

**Code committed; real Metal multi-actor test NOT RUN yet.** Stage 5E's real macOS visual preview **passed** on user-checked SHA `53e5f0cb069dff8d4c173001947ffce3b4791a98`: all three captured frames, real shadow and colour model submissions, 81,988 baseline-to-morph changed pixels, and 66,972 additional MotionWeave-pose changed pixels. User's selected CTests were 2/2 PASS. Handoff verification commit: `4dea755d192b70abe2501a63eba553d4230ffd06`.

Next, VIVID gains **`AIVERSE::vivid_actor`**, a per-character GPU presentation helper (`GpuActorStream.h/.cpp`). It owns one `GpuMorphDeformer`, one `GpuMorphMeshStream` and one instance-local semantic morph weight vector. Each actor has **independent mutable GPU weight and output buffers**. It may refer to the same immutable authored `SparseMorphLibrary` and uploaded `render::Model` metadata as other actors, but each actor owns its own RHI allocations in this initial implementation. Sharing pipelines and immutable CSR uploads without compromising per-instance output ownership is a later optimization.

Public workflow:

```cpp
vivid::GpuActorStream actor;
actor.initialize(dev, model, sparseLibrary, modelVertexSpan);

// Inside a platform-owned GPU frame, outside all render passes:
motionweave::MotionWeave::update(presentationWorld, dt); // sole skeleton owner
actor.dispatch(faceController.frame(), faceShapeBinding);

render::ModelDraw draw;
draw.model = &model;
draw.nodeWorld = &presentationWorld.get<
    motionweave::AnimationPoseComponent>(entity).matrices;
if (actor.attach(draw)) renderer.drawModel(draw);
```

The `dispatch` contract requires an active **Metal** frame with a nonzero `gfx::Device::frameSerial()`. An actor permits **only one two-stage compute submission per frame**; repeated calls are rejected rather than overwriting memory already queued for draw calls. `attach(draw)` is transactional and only succeeds for exactly the same `render::Model` and a valid stream produced in the **current device frame**. This prevents accidental reuse of an old character's vertex stream after frame rollover or attaching to a different model. The renderer still validates full 60B vertex metadata and does not enable pooled/static/ICB paths. No new animation graph or scheduler was implemented: MotionWeave remains authoritative for body pose, skin palettes, IK and retargeting.

At exceptional model unload or actor removal, the host must finish the GPU frame, then call `actor.shutdownAfterIdle()` **outside any active frame** before allowing the model and device to be destroyed. The method invokes `gfx::Device::waitForResourceIdle()` and returns false on failure without deliberately freeing GPU resources. This is **not a per-frame stall**; frame-to-frame updates simply reuse the actor's GPU allocations. `GpuMorphDeformer` and `GpuMorphMeshStream` still have ordinary destructors; a host must not destroy an actor or its device while GPU work is in flight. A scalable token-based nonblocking retirement queue for frequent dynamic despawns is future work.

The existing real macOS `vivid_metal_rhi` test has been **extended (not yet executed on the new revision)** with two actors referencing the same model and morph library. It checks separate output handles, three frames of different face weights, CPU-vs-GPU vertex parity for both actors, preservation of immutable UV/colour/tangent-sign/skin weights, rejection of a duplicate per-frame dispatch, rejection of stale-frame and wrong-model attachments, and explicit idle teardown. The ordinary VIVID and MotionWeave tests and the previously passing real visual preview are **not automatically certified** after this new source change.

## Stage 5G — batched asynchronous actor GPU resource retirement (2026-11-19)

**Implementation committed; Stage 5G CTests and native Mac GPU test NOT RUN yet.**

The user supplied a native macOS test log for Stage 5F: `aiverse_vivid_actor` compiled successfully; `vivid_metal_gpu`, `vivid_metal_rhi` and `vivid_vertex_override` all passed (3/3, 3.29 seconds). The real RHI test includes two independent actors over three frames. This result is recorded in the agent handoff and predates the new code below.

VIVID now adds `GpuActorRetirementQueue` (`engine/character/vivid/GpuActorRetirement.h/.cpp`) built inside `AIVERSE::vivid_actor`. The implementation deliberately reuses **`render::AsyncResourceRetirementQueue<std::unique_ptr<GpuActorStream>>`** from the renderer. There is no second retirement state machine or a new scheduler. A batch of actors queued after the GPU frame has been committed receives one backend `gfx::Device::beginResourceRetirement()` completion marker. `collect()` polls the ordered tokens and only releases actor GPU memory when `resourceRetirementComplete()` confirms success. A `Pending` or `Failed` token does not authorize release. Unsupported tokens require `waitForResourceIdle()` fallback; failed fences may only be bypassed by a successful full idle barrier on explicit `drain()`. Normal token-supported frame progression is **nonblocking** and never stalls per retired actor.

Actor GPU resource teardown is refactored through the private `GpuActorStream::releaseAfterGpuCompletion()`, accessible only to `GpuActorRetirementQueue` and invoked after token completion or a successful shutdown idle barrier. Normal `GpuActorStream::shutdownAfterIdle()` remains supported. Both paths release the two actor-owned compute pipelines and output/input/weight GPU allocations, preserving per-actor ownership and not touching shared `render::Model` resources. MotionWeave still owns body animation.

Usage at a graphics-device frame boundary:

```cpp
vivid::GpuActorRetirementQueue retirement(device);
std::unique_ptr<vivid::GpuActorStream> actor = /* initialized VIVID actor */;

// After ALL colour/shadow draws referencing this actor have been committed
// by gfx::Device::endFrame(), not during beginFrame/endFrame:
if (!retirement.retire(actor)) { /* ownership remains in actor */ }
retirement.collect(); // submits one token for the pending actor batch
// On later real frame boundaries:
retirement.collect(); // releases completed actors without blocking

// Before destroying the device or the retirement queue:
if (!retirement.drain()) { /* keep queue and device alive; retry */ }
```

The queue exposes pending counts, saturation, diagnostic observer and the shared engine's detailed stats. `retire` rejects actors owned by a different graphics device. The caller must keep the model, device and queue alive until GPU completion, must **successfully drain the queue** before destroying it, and must never start a retirement marker while commands referencing the actor are still being recorded in an uncommitted frame. A finished token is not a GPU-to-CPU readback and does not carry performance guarantees.

New portable CTest `vivid_actor_retirement` uses an instrumented mock Metal RHI and checks two actor objects retired under one marker, no GPU buffer destruction while `Pending`, resource release and statistics only on `Complete`, retention of a `Failed` marker, failed/successful idle recovery, unsupported-token fallback, and rejection of cross-device actors. It makes no claim of actual GPU work. The existing **real** Metal `vivid_metal_rhi` CTest is also extended with an actor GPU submission and a subsequently committed nonblocking resource retirement marker. The test allows a bounded polling window and fails if a real completion marker never completes.

**Outstanding:** build and run the new headless and Metal tests on the user's macOS host. A safe shared immutable CSR asset/pipeline cache, renderer frame-budget benchmark, multi-character GUI soak, and device-recreation tests remain separate milestones. No change to authoritative simulation, save/network schemas, `render::ModelVertex` layout, `gfx::Device` ABI or MotionWeave APIs.

## Stage 5H — shared immutable GPU assets and content-addressed cache (2026-11-19)

**Source implementation committed; new shared-cache C++/Metal tests not yet verified on the user's Mac.**

VIVID actors can now share immutable GPU inputs and shader pipelines while retaining independent outputs. `GpuMorphSharedAsset` uploads a model's original 48B bind-pose morph vertices, CSR deltas/offsets/links, original 60B `ModelVertex` array, and both MSL/GLSL compute programs. Each `GpuActorStream` holds only a GPU-asset reference plus its *own* shape-weight buffer, 48B compute output and 60B render output. This reduces total buffers for N actors sharing the same asset from approximately `8*N` to `5+3*N`, and compute programs from `2*N` to `2`; GPU bytes also drop because bind geometry and CSR data are no longer replicated.

The previous `GpuActorStream::initialize(device,model,library,vertices)` remains source-compatible and creates an independent asset internally. To share intentionally, create a common `GpuMorphSharedAsset` or call the new `GpuMorphAssetCache::acquire(library,originalVertices)`, then call `GpuActorStream::initializeShared(device,model,sharedAsset)` for each actor. The weak device-local cache hashes **all GPU-relevant source bytes** from model vertices, CSR deltas, offsets and links, plus counts, into a two-word digest; identical authored geometry reuses the immutable GPU asset. A changed source geometry/C SR content gets a distinct entry. This is an internal cache key, not a cryptographic trust boundary or a serialized identity.

The cache keeps only **weak references** and can be pruned with `pruneExpired()`. The final strong reference to the asset must only disappear after ALL actors referring to it have completed their GPU work (the new generic token-based `GpuActorRetirementQueue` handles ordinary despawns). The cache is per-`gfx::Device` and must not outlive that device, across device recreation or GPU loss. It runs on the device owner thread, not a parallel loader thread; the CPU C++ `SparseMorphLibrary` may still be reused by many jobs elsewhere. GPU shaders are not duplicated and MotionWeave remains the sole scheduler for skeleton pose.

New coverage: the fake-RHI `vivid_actor_retirement` includes a 128-character 4-frame scenario verifying that immutable buffers/pipelines are shared, actor outputs remain separate, and a single ordered GPU token retires all 128 actors without blocking. It also verifies a cache hit for identical geometry, a separate asset on changed geometry, weak-entry pruning, and safe last-reference destruction. Real Metal `vivid_metal_rhi` now creates two independent GPU actors from a single cached GPU asset and tests their numerical parity over three frames, plus the existing async retirement test. The 3/3 user Mac tests from Stage 5F **predate** this source refactor; these extended tests are **NOT RUN** yet.

Explicit limitations: the cache does not currently deduplicate CPU-authoring metadata, textures, skin rigs, or MotionWeave pose buffers. The render override remains a Metal-only opt-in and still excludes unsafe MDI/ICB/LOD batching. The queue requires completion-validated final references and successful drain before gfx device shutdown. Automated performance measurements on large fully shaded crowds, per-asset budget controls, Vulkan GPU parity and photorealistic face/hair/eyes remain future work.

## Stage 5I — one-pass complete VIVID native validation (2026-11-19)

**Automation authored, not a substitute for actually successful tests.** `tools/vivid-validate.sh` is the supported macOS one-command acceptance runner. Call it from the existing VIVID source tree:

```sh
bash ~/Projects/AIVERSE-vivid/tools/vivid-validate.sh
```

It aborts rather than switching a dirty tracked worktree, fetches `origin/feature/vivid-digital-humans`, records the exact detached Git SHA, configures a separate `build/vivid-onepass` tree with tests, builds every VIVID test binary and the MotionWeave API test and real GUI preview, runs `ctest -R '^(vivid_|motionweave_api$)'` sequentially (avoiding concurrent Metal GPU resource contention), then opens the macOS Metal Renderer3D/MotionWeave 3-frame visual preview. It requires three nonempty screenshots and a logged `pass=1` report before declaring one-pass acceptance. Detailed configure/build/test/preview logs, screenshots, revision and a summary are retained under `build/vivid-onepass/vivid-validation`. The shell uses `set -Eeuo pipefail` so any failed compilation, test or visual capture halts the command.

`--no-gui` performs a **partial** headless validation; `--full` builds all engine targets and runs the full CTest suite before the same GUI preview. Neither optional mode is required for the normal VIVID acceptance.

The focused GitHub Actions workflow `.github/workflows/vivid-validation.yml` independently tries the same native macOS VIVID/MotionWeave compile+CTest selection on pushes to this branch; hosted runners may lack an accessible Metal device, in which case the Metal tests report **Skipped**, not proven GPU parity. GitHub Actions does not execute the GUI visual preview, which requires a real graphical Mac. A queued CI workflow is NOT a passing test. Do not cite CI as green until its jobs conclude successfully; user Mac remains authoritative for Metal/Renderer3D captures.

Current requirements for release-quality MetaHuman-like humans that remain outside this focused GPU facial-animation path include authored human DNA/rig assets, skin/eye shaders, groom and hair simulation, facial corrective shapes on actual head scans, mesh streaming/LOD benchmarks, editor authoring UI and cross-backend GPU parity. These are separate product features and must not be claimed finished just because the current VIVID graphics core passes.

## Stage 5J — multi-actor visual acceptance (2026-11-19)

The existing asset-free Metal GUI `aiverse_vivid_renderer_preview` now also supports a crowd mode through `AIVERSE_VIVID_PREVIEW_ACTORS=1..32`. The preview creates one `GpuMorphAssetCache` and obtains one shared immutable GPU CSR/compute program set for all actors. Each instance holds a separate `GpuActorStream` with its own writable weights and 48B/60B deformation buffers. Scene snapshots still use the original three-stage baseline → expression morph → MotionWeave node pose pipeline and verify real shadow/colour draws, screenshot presence and visible pixel changes. For multiple actors, expressions vary independently while the shared skeleton pose remains authored/sampled once by MotionWeave.

During preview teardown, all actors are moved into one `GpuActorRetirementQueue` and released after **one successful GPU idle barrier**. Only then are the final shared asset reference, source geometry and renderer freed. This is a shutdown-only barrier, not a stall within each frame. The original single-character preview remains the default if the environment variable is absent.

`tools/vivid-validate.sh` now automatically launches the preview **twice** after all VIVID and MotionWeave CTests: once with the original single-character default and once with **12 actors** (`AIVERSE_VIVID_CROWD_ACTORS` overrides the count). Each pass must save three nonempty PPMs and print `pass=1`; one failed capture or missing actor output fails the entire one-command acceptance. The script retains both log streams and all six screenshots under `build/vivid-onepass/vivid-validation`.

The new code and GUI paths have **NOT** been built or executed on the user's Mac after this change. The prior successful 3-frame GUI preview (81,988 morph-changed pixels / 66,972 MotionWeave-changed pixels) was for one actor on Stage 5E; it does **not** certify the 12-actor preview. Likewise a queued GitHub Actions job has not passed until the test runner reports successful completion. This stage is about visual consistency and lifetime at small crowd scale, not production performance, geometry LOD/ICB acceleration, authored realistic faces or hair.
