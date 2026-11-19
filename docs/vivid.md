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

## Stage 5J accepted — full focused macOS GPU + GUI validation PASS (2026-11-19)

User completed `tools/vivid-validate.sh` at exact detached SHA `fa5504a9f2e1f64087ef73139525e5bde31976b4`. All **11/11** selected `vivid_*` and `motionweave_api` CTests passed (10.19s, 0 failed). In particular, real `vivid_metal_gpu`, `vivid_metal_rhi`, actor retirement / shared GPU asset stress and render vertex override all passed.

Two real Metal Renderer3D GUI previews also completed with `pass=1` and three PPM captures each: baseline → morph → MotionWeave. **One actor:** shadow=1, colour=1, 81,988 morph pixel differences, 66,972 MotionWeave pixel differences. **Twelve actors:** shadow=12, colour=12, 179,673 morph pixel differences and 183,275 MotionWeave pixel differences. Both previews reported a shared immutable GPU morph asset of **158,324 bytes**, 925 vertices and two shapes. Metal graphics diagnostics reported `pipelineFailures=0`.

The Mac runner saved the full results under `build/vivid-onepass/vivid-validation`: exact SHA, separate CMake/CTest/GUI logs, six PPM images and `summary.txt`. The full result and provenance are appended to `docs/agent-handoffs/vivid.md`.

**Acceptance scope:** VIVID's focused C++20/Metal GPU facial deformation, independent multi-character weights/outputs, shared immutable morph GPU cache, safe token-based retirement, and Renderer3D/MotionWeave single-and-12-character diagnostic rendering are **verified by actual user Mac tests**. The agent has not personally inspected the PPM pixel appearance; programmatic image differences do not prove realistic character quality or exact shadow-pixel parity. Entire-engine tests, long-duration lifetime/performance, Vulkan/OpenGL GPU parity, scalable GPU batching/LOD, authoring editor and realistic DNA/rig/eyes/skin/hair/groom assets are distinct future stages and are **not** certified by this run.

## Stage 6A — editable versioned HumanDNA assets (2026-11-19)

Added `HumanDNA.h/.cpp` and `AIVERSE::vivid_human`. A single versioned JSON recipe describes facial/body proportions, skin/melanin/hemoglobin/scattering/pore parameters, iris/cornea/tear film, groom density/length/curl/wind, semantic rig naming and the 52 expression rest weights. Four built-in presets are available. Import is bounded, strict and rejects unknown fields, nonfinite floats, invalid resources/rig names and unsupported versions; export produces a canonical JSON document. This data is presentation-only, not authoritative simulation or network state. A `vivid_human_dna` headless regression tests canonical JSON roundtrip, presets, bounds, malformed inputs and missing optional sections. It does not yet import proprietary MetaHuman DNA.

**Source committed, new Mac tests NOT RUN**. Other creator/shading/groom work is in progress; see next stage handoffs for precise tested status.

## Stage 6B — human appearance, facial joints and groom cards (2026-11-19)

New `HumanRig` binds the DNA-named root/head/jaw/eye joints to any existing `render::Model` hierarchy and produces a **separate** node-matrix overlay on top of MotionWeave's pose. It rejects invalid/nonfinite facial inputs without altering MotionWeave state. Missing jaw/eye bones are accepted for morph-only imports; required head/root must exist.

`HumanMaterials` compiles material ShaderGraphs using AIVERSE's shared GLSL/MSL generator. Skin includes pigment/hemoglobin and inexpensive view-dependent scattering with pore noise, roughness and wetness response; iris includes pupil/limbal ring and coloured fibres; sclera, cornea (separate alpha-blended surface), hair cards, lashes and teeth use the same PBR renderer. This is a real shader-graph integration but **not** diffusion-profile physically-correct SSS, ray-traced eyes or an offline hair scattering solution.

`HumanGroomRuntime` creates deterministic hair/scalp guides, generates multi-LOD indexed PBR card meshes, simulates bounded Verlet secondary motion with simple scalp exclusion and populates a full `render::ModelVertex` stream. `generateHumanPreview` creates an asset-free procedural reference head with layered eyes, eyelids, brows, jaw, cheeks, mouth, teeth, groom and five named corrective morphs. All are ordinary `render::Model` assets, and the main model/morph path remains unchanged unless a caller opts in. An imported scanned glTF/FBX human may reuse the DNA and shader/rig APIs, but the procedural face does not reproduce MetaHuman scan quality.

`HumanCreatorSession` provides immutable validated DNA transactions, undo/redo, preset changes and safe JSON load/save for a UI. New `vivid_human_rig`, `vivid_human_appearance` and `vivid_human_creator` unit tests cover hierarchy, shader graph compilation, groom LOD/finite geometry, facial corrective mapping and editing history. **These new tests are NOT YET RUN on macOS.**

## Stage 6C — standalone VIVID Creator UI (2026-11-19)

The project now has a standalone, interactive C++20/ImGui **`aiverse_vivid_creator`** built against the existing AIVERSE `Renderer3D`, `gfx::Device`, `render::Model` and MotionWeave libraries, instead of introducing a parallel engine. Its DNA panels cover head/jaw/nose/cheek/eye/lip proportions, skin pigment and scattering, iris/limbus/cornea/tear film, hair colour/guide count/segments/length/curl/clumping/stiffness/damping/gravity/wind, facial controls (jaw/blink/smile), model/groom LOD, camera and animation. A `HumanCreatorSession` owns validated transactional changes with undo/redo and versioned recipe JSON save/load. GUI font uses the engine's native asset path. The program is not embedded as a tab in the main editor; it is a standalone character-creation tool with ordinary public native C++ APIs.

The Creator renders a real editable head via VIVID GPU compute streams with MotionWeave's skeletal/node pose, layer-separated cornea/iris/sclera/PBR skin and a deformable hair-card mesh. During preview, card vertices can be updated through `gfx::Usage::Dynamic`. `--smoke` creates a real GPU capture from a GUI Metal session and checks both shadow and colour submissions and nonempty PPM output. The script `tools/vivid-validate.sh` builds the creator and runs `aiverse_vivid_creator --smoke`, saving `vivid-creator.ppm` and diagnostics. It also keeps the previous single/12-actor facial preview tests.

**Stage 6C native tests/UI compile/GPU screenshot NOT RUN yet.** This tool is a functional source implementation, not yet a certified production character artist interface or MetaHuman-level renderer.

## Stage 6D — opt-in human BRDF skin/eye/hair shading (2026-11-19)

The existing `render::Material::shading` enum now adds explicit `Skin`, `Eye` and `Hair` modes while leaving legacy `Pbr` and `Unlit` values intact. `humanProfile` provides two surface-specific float parameters; `GpuMaterialState.h` includes them in exact batching equivalence/hashes, so different skin/eye/hair shaders cannot be merged accidentally. `Renderer3DModels.cpp` passes the opt-in shading mode/profile through the existing U2 material layout without changing shader ABI size. Paired MSL and GLSL `ModelShaders.h` fragment branches add approximate skin wrapped/transmitted diffuse, tear-film/cornea highlights and tangent-directed hair glints. The shared `HumanMaterials` layer creates the matching graphs and maps DNA fields into these PBR profiles.

This is an **efficient physically inspired approximation**, not measured multi-layer skin diffusion, a spectral eye refraction model or physically accurate Marschner strand scattering. It runs through ordinary AIVERSE colour/shadow/material pipelines and does not change legacy PBR unless `HumanSurface` opts in. The renderer, new BRDF shader permutations and eye/hair screenshots **are not yet compiled or GPU-tested** on Mac after these changes.

## Stage 6E — open glTF/FBX/OBJ human interchange (2026-11-19)

An open authoring manifest (version 1) can reference a native `render::loadModel`-supported model, a JSON `HumanDNA` profile and optional sparse morph JSON with named target arrays. `HumanImport.h/.cpp` validates project-relative asset paths, required root/head rig hierarchy, material names/types, 52 facial expression channel routes, sparse morph vertex indices/finite deltas and import size limits. Model vertex ranges missing from OBJ imports are reconstructed conservatively from actual indices, and post-prepare original vertex positions are checked to reject silent topology reorder before publishing sparse corrections. `human_import` CTest covers manifest JSON roundtrip, invalid paths/channels/sizes, OBJ root/head import with two shapes and morph-free variants. `HumanCreator` now supports selecting a `--asset /path/to/manifest.json` on the command line or using the **Import glTF/FBX/OBJ + DNA + morphs** button, and can switch back to the procedural reference model. Imported geometry remains authored (anatomy sliders do not resculpt an imported scan), but DNA materials, groom and facial expressions can be edited and saved; MotionWeave still owns the resulting animation pose.

This deliberately does not decode Epic proprietary MetaHuman DNA binary assets. Imported glTF/FBX must have the required rig joint names (or a matching DNA rig map) and morph JSON must refer to imported model-relative vertex IDs. Haircard groom is currently regenerated from VIVID DNA rather than converted from an imported external strand cache. Source is committed but **native compiler / importer CTest / GPU Creator import screenshots are NOT RUN** yet.

## Stage 6F — DNA-driven appearance refinements and end-to-end creator acceptance (2026-11-19)

This stage closes several source-level correctness gaps in the new human authoring feature:

- Renderer3D's paired GLSL/MSL corneal glint now uses the authored **cornea IOR** (Schlick-normal-incidence approximation), not just tear-film wetness. The IOR slider produces a real shader effect when the `Eye` shading mode is active.
- Skin ShaderGraph uses DNA `wrinkleStrength` for extra micro-crease variation in addition to pores. Hair ShaderGraph now uses groom melanin for pigmentation, while groom geometry generation responds to `density` and `clump` settings instead of merely storing them.
- The procedural head reference now includes independent neck, shoulder and upper-torso meshes, driven by `heightMeters`, `shoulderWidth` and `bodyBuild`. These body meshes are parented to the root so they do not inherit every head nod. This is a **reference bust**, not a full scanned/rigged body.
- Creator exposes configurable root/head/jaw/eye bone aliases, more anatomy controls and support for imported authored hair geometry without adding overlapping procedural groom cards. Imported alpha-masked hair cutouts, UV transforms and albedo/normal/MR/AO maps are preserved.
- `HumanCreatorSession::saveFile` no longer deletes the existing last-good DNA recipe after a failed atomic replacement. It reports the failure and keeps the staging file available.
- A small original manifest/OBJ/CSR package under `tests/vivid/assets` allows the Creator GUI to smoke-test actual **external asset import** without bundling proprietary scans.
- The one-shot `tools/vivid-validate.sh` now builds and executes **16 VIVID/MotionWeave CTests**, then GUI-renders original 1-actor, 12-actor, procedural human Creator, warm-preset human Creator and imported external head Creator. It checks all required PPM captures and the screenshot test's render-pass results; `tools/vivid-creator/compare_ppm.py` compares neutral/warm materials **only outside the GUI inspector** to validate actual 3D viewport changes. All logs/screenshots remain under `build/vivid-onepass/vivid-validation`.

**Important:** Stage 6A–6F have been implemented in GitHub but the new native C++ test targets, shader programs, real Creator GUI and imported-head captures have **NOT RUN on the user's Mac yet**. The previous Stage 5J 11/11 passes on SHA `fa5504a9f2e1f64087ef73139525e5bde31976b4` predate all of these changes. We do not claim physically measured MetaHuman skin/groom/eye parity or release readiness; real authored high-resolution human scans, proprietary asset licensing, complete body rig/groom/LOD streaming, GPU time measurements, Vulkan parity and full AIVERSE regression remain independent gates.


## Stage 6G — groom collision respects head DNA (2026-11-19)

`HumanGroomRuntime` now retains scalp ellipsoid radii from the same `HumanDNA` parameters used to generate hair roots. Secondary-motion constraint projection excludes the DNA-proportional head shape rather than a hard-coded world-space sphere. A final contact projection closes the gap left by sequential strand-length correction. The `vivid_human_appearance` test now includes wind-driven scalp nonpenetration for multiple head widths and scales.

**Source and regression authored, native build/CTest and GPU Creator preview NOT RUN yet.** This is a procedural ellipsoid collider and does not yet support exact collision with imported scalp meshes. See `docs/agent-handoffs/vivid.md` for implementation SHAs and validation instructions.


## Stage 6H–6J — mesh collisions, bounded groom, eye DNA, Creator expression authoring (2026-11-19)

The optional `HumanGroomRuntime::setCollisionMesh` accepts a bounded closed triangle mesh in groom-local coordinates and uses nearest-surface projection/odd ray parity to exclude interior hair points. `clearCollisionMesh` restores the DNA-scaled ellipsoid fallback. Verlet travel is capped per segment and collision velocity is damped to reduce extreme-frame penetration. This CPU reference path remains **O(strand points × triangle count)** and needs spatial acceleration, stable handling of non-manifold geometry, animated mesh support and validated attachment roots before production-scale use. The current Creator does not automatically install an imported scalp collision mesh.

`HumanMaterials` now routes `scleraRedness` into actual sclera PBR pigment. The Creator exposes all 52 `Face.h` facial defaults for authoring and undoable DNA persistence; interactive jaw, blink and smile preview controls override their corresponding defaults. Existing Hair High/Med/Low is a groom presentation quality selector rather than mesh-LOD authoring.

**Source authored; native macOS build, the newly extended `vivid_human_appearance` test, actual imported character GPU preview, and all-engine regressions NOT RUN** for these commits. The next validation is `bash tools/vivid-validate.sh --full` on an appropriate Mac, with logs appended to `docs/agent-handoffs/vivid.md`. True mesh sculpting, skinned scalp collision/BVH, production hair simulation, physically correct eye refraction and multilayer SSS are still outstanding.


## Stage 6K–6O — collision acceleration and imported-human authoring (2026-11-19)

The procedural hair runtime now builds a bounded median-split **CPU BVH** from an optional closed scalp proxy. Mesh validation rejects open/non-manifold edges; nearest-surface projection and ray-parity interior checks use the hierarchy rather than scanning all triangles each simulation iteration. Guide updates run bounded internal steps up to 120 Hz, with collision inelastic response.

Open v1 import manifests may specify `"scalpColliderMesh": "closed_head_proxy"`. Creator extracts that named mesh's triangles into a compact collider, rejects explicitly skinned and open meshes, and projects guide roots onto the collision proxy, translating rest and previous positions consistently. This is **static bind-pose local-space authoring**. Dynamic skinned refits, arbitrary node transforms and full self-/cloth-/body collision are not yet supported.

Creator now also exposes imported facial ARKit channel-to-morph route gain sliders (0–4) and can atomically save edited manifests. The existing 52-channel DNA authoring and procedural groom LOD selectors remain available.

**No new C++ build, CTest, Metal GUI acceptance or performance benchmarks have been run for these changes** (implementation-first request). Source includes newly authored regression assertions covering open-surface rejection, last-good preservation, scalp root relocation, manifest roundtrip and 30/120 Hz substep equivalence. These are not passing-test claims. Full photoreal MetaHuman-level character creation still requires additional artist-authored mesh sculpt/rig/LOD tools, realistic scanned assets, accurate skin/eye/hair optics and runtime GPU performance validation. See `docs/agent-handoffs/vivid.md` for implementation SHAs and the final validation gate.


## Stage 6Q — animated groom root attachments (2026-11-19)

An imported closed scalp collision mesh now supports stable per-guide source triangle/barycentric anchors. During a successful `refitCollisionMesh`, guide roots, their rest positions, live positions and previous positions follow the deformed local-space scalp triangles. Failed geometry/refit/attachment staging retains previously published triangle/BVH and guide data. Collider replacement and clear invalidate old triangle identifiers; callers must reattach roots after changing topology.

This is a **CPU geometry API**, not yet automatic MotionWeave-to-skinned-scalp streaming. `VIVID Creator` currently attaches static import proxies at authoring time. No source changes from this stage have been natively built or tested on macOS; test assertions were added but not executed. See `docs/agent-handoffs/vivid.md` for exact commits and future integration gates.


## Stage 6R — Creator scalp proxy selection (2026-11-19)

The imported-character Creator panel now exposes the optional named `scalpColliderMesh` from the v1 manifest. UI edits validate a staged manifest, preserve the last-good selection on error and request a new model/groom rebuild on success; the existing atomic manifest Save action persists the change. Empty selection falls back to the DNA ellipsoid.

This source change was committed but **not compiled or GUI-tested**. Authoring a closed/unskinned scalp proxy, transforming an imported node hierarchy into groom-local coordinates and driving deforming scalp meshes from MotionWeave remain separate tasks.


## Stage 6S — MotionWeave-driven rigid scalp refit (2026-11-19)

An imported closed rigid scalp-proxy in VIVID Creator can now follow a named MotionWeave animated node. The Creator stores original proxy positions and bind-pose transforms, converts the current node transform into the already-used head-local groom coordinate system, and transactionally refits the CPU collision BVH before presentation hair simulation. Invalid/nonfinite computed poses are rejected while retaining the last-good collider. No new skeleton scheduler is introduced.

**Scope:** only an unskinned scalp-proxy with a single owning model node and existing groom-local vertex convention. Multi-joint skinning, morph-deformed scalp geometry, arbitrary transform authoring, collision self-contacts and full character sculpting are still unfinished. This stage is committed as source but **native build, CTests, Metal GPU GUI and visual parity have NOT RUN**. Full implementation details and next-step handoff live in `docs/agent-handoffs/vivid.md`.


## Stage 6T — MotionWeave multi-joint scalp skinning adapter (2026-11-19)

A new header-only `ScalpSkinning` CPU adapter resolves imported scalp-proxy vertices against each render primitive's skin joint palette and inverse-bind matrices. It normalizes four 8-bit weights, computes linear-blend deformation from MotionWeave node-world matrices, converts points to the VIVID head-local groom coordinate system, and passes them into transactional BVH refit. Creator now detects and labels rigid-node versus multi-joint scalp modes. The older rigid path remains available; no new animation scheduler or engine/genre-specific API is introduced.

Skinned reference source and negative-case unit checks are committed but **not executed**. This is not yet native-build-verified or real-GLB/FBX visual validated. Production-quality implementation still needs per-frame scratch reuse, animated scanned-head authoring assets, high-density performance work, full sculpt/rig/LOD tooling and more physically accurate skin/eye/hair shaders. See `docs/agent-handoffs/vivid.md` Stage 6T for exact commits.


## Stage 6V–6W — non-destructive morph sculpting and semantic authoring (2026-11-19)

VIVID Creator now includes an imported sparse morph authoring panel. Numeric source-vertex position offsets can be changed without touching the original mesh, with transactional undo/redo and export to the existing open morph sidecar JSON schema. The importer exposes the original model's vertex count so automatically appended LOD vertices cannot accidentally be exported as original morph vertex IDs.

Creators may add/delete named corrective morph targets, bind one to any of the existing 52 ARKit semantic channels, adjust gains via the existing imported route UI, and save both morph sidecars and manifests. Deletion removes dangling mappings; target undo does not currently restore those mappings.

**Actual native build/CTest/Metal screenshots NOT RUN** for these additions. The panel edits numeric offsets by vertex ID; this is not yet a viewport brush/cage sculpt, rig joint editor, authored facial LOD pipeline or real MetaHuman-quality DCC tooling. See `docs/agent-handoffs/vivid.md` Stages 6V–6W for exact commits and remaining work.


## Stage 6X — imported facial sculpt falloff brush (2026-11-19)

VIVID Creator's imported sparse morph editor now supports a source-space smooth falloff brush anchored to a selected original mesh vertex ID. A stroke applies position offsets to nearby source vertices, with bounded radius/intensity, recompiles the sparse morph once, supports undo/redo as one edit, and exports to the existing open version 1 morph JSON sidecar. Named corrective target creation/deletion and 52-channel ARKit route authoring are also available.

This is **numeric/source-ID brush authoring**, not full 3D viewport picking, geodesic brush sculpting or topology-aware symmetry. The artist's original mesh stays unchanged. Stage 6V–6X source and new test assertions are committed, but **C++ build, CTest, Metal GUI and production asset verification have NOT RUN**. See `docs/agent-handoffs/vivid.md` for source commits and remaining work.


## Stage 6Y — viewport sculpt selection and mirrored brush (2026-11-19)

VIVID Creator now permits selecting an imported model mesh and arming a 3D viewport click to select the nearest original source vertex on the frontmost intersected indexed triangle of that mesh. A screen marker shows the selected vertex. The non-destructive sparse morph brush now supports optional X=0 bilateral symmetry in source-model coordinates: mirrored X offsets invert direction, other axes keep direction, and the entire stroke participates in one undo/redo transaction. Source model vertices and MotionWeave simulation remain unchanged.

**Current limitation:** selection uses source vertices transformed by pose node matrices, not GPU-morphed or multi-joint skinned final geometry; high-density picking is an O(triangle count) CPU operation. The feature also lacks topology-aware/geodesic brush falloff and per-character symmetry planes. This stage is source-authored, **not yet compiled or GPU/GUI-tested** on Mac. Full technical handoff and commit history: `docs/agent-handoffs/vivid.md` Stage 6Y.


## Stage 6Z — rig pose corrections and conservative sparse-morph LOD transfer (2026-11-19)

VIVID Creator now has an opt-in, non-destructive facial rig correction panel for selecting a named model node, authoring a bounded local XYZ translation, undo/redo and versioned JSON save/load. These corrections apply as a hierarchy-aware **presentation pose overlay after MotionWeave**, not as destructive bind-pose or inverse-bind authoring. Imported FBX/glTF source data is preserved.

A separate `MorphLodTransfer` helper carries VIVID sparse facial morph targets to a target LOD when an explicit source-vertex correspondence table is provided. An additional strict reference matcher can infer that table only for original/LOD vertices with unambiguous near-identical positions and a bounded 32K diagnostic budget. Arbitrarily decimated LOD meshes require proper barycentric surface mapping and are not supported by this matcher.

**The code and new regression assertions are not yet natively compiled or tested.** No Metal preview, CTest or production model validation was performed for these features. Outstanding tasks include true bind-pose/skin rebinding, asset-owned rig correction publication, full LOD authoring/bake/editor integration, and physically detailed skin/eyes/strand hair. See Stage 6Z in `docs/agent-handoffs/vivid.md` for implementation commits, caveats and next steps.


## Stage 7A — barycentric facial LOD transfer (2026-11-19)

VIVID now has an opt-in CPU `MorphLodSurface` authoring helper for transferring sparse facial morphs to independently decimated LOD vertex streams. It projects each LOD vertex onto an original indexed triangle, stores stable barycentric correspondence and interpolates all authored corrective position, normal and tangent deltas before recompiling the LOD-specific sparse CSR representation. The fallback rejects nonfinite/degenerate data, outside-tolerance vertex positions and authoring workloads exceeding the bounded eight-million comparison budget.

This is source-level morph transfer, **not** automatic mesh simplification, Creator authoring UI integration or runtime LOD switching. Large meshes need a spatial acceleration structure and precise region constraints to avoid false matches across different facial features. The new implementation and regression assertions have **NOT RUN** on macOS; no compiled/GPU-tested parity is claimed. Stage 7A source SHAs and next steps are in `docs/agent-handoffs/vivid.md`.


## Stage 7B — persistent facial LOD projection mapping (2026-11-19)

A versioned `MorphLodMapping` JSON sidecar now records the source mesh dimensions and each target LOD vertex's barycentric triangle projection. Stored maps can be reloaded to produce target-specific sparse morph CSR data without recalculating Stage 7A surface proximity. The loader enforces bounded dimensions, finite coordinates and barycentric values and verifies source vertex/triangle counts.

**Not yet production-safe against unrelated same-sized meshes:** a topology fingerprint and authoring asset identity remain required. Creator sidecar UI, BVH accelerated surface search, GPU LOD switching and native Mac acceptance are still outstanding. Source and regression assertions are committed, **tests not run**. Exact commits and handoff are in `docs/agent-handoffs/vivid.md`.


### Stage 7B source-identity safety update

Cached `MorphLodMapping` now additionally requires a deterministic source geometry fingerprint calculated from original vertex float bits and source triangle indices; reusing a sidecar against changed geometry fails even when vertex and triangle counts happen to match. This is accidental mismatching protection, not a cryptographic guarantee. Earlier development sidecars without fingerprints must be regenerated. Native compilation and testing remain **NOT RUN**.


## Stage 7C — BVH facial LOD bake authoring (2026-11-19)

A new `MorphLodSurface::projectAccelerated` builds a median-split triangle BVH for nearest-source-surface mapping of independently decimated LOD vertices. It retains the same barycentric mapping structure and is cross-checked in authored source tests against the scalar reference, avoiding its quadratic eight-million-query limit. Model/triangle/vertex budgets still apply.

VIVID Creator now has `Facial LOD surface bake` controls for choosing separate source/target model meshes, selecting the allowed projection distance and exporting `vivid-facial-lod.mapping.json` plus `vivid-facial-lod.morphs.json`. The mapping includes exact source fingerprint, barycentric references and sorted model-relative target vertex IDs. This is an **offline authoring/export path**; GPU actor LOD switching, correct per-LOD materials/skinning, one-transaction multi-file publication and topology-aware seam constraints are not yet complete.

New C++ source and regression tests are committed but **not compiled or run**. Stage 7C detailed handoff and implementation commits are in `docs/agent-handoffs/vivid.md`.


## Stage 7D — full-model GPU actor facial morph parity across authored LODs (2026-11-19)

The Creator now optionally publishes an authored Stage 7C LOD facial mapping into VIVID's existing **full-model** `GpuActorStream` vertex override. `MorphLodGpuAuthoring::combine` retains all original facial morph deltas, maps compact target-LOD deltas onto their exact model-relative vertex IDs, validates identical target name/order and rejects conflicting or invalid source/LOD vertices. One existing ARKit weight buffer therefore drives original and authored LOD morphs in the same Metal GPU compute dispatch; Renderer3D continues selecting authored `MeshDef::lods` normally. No GPU buffer is replaced mid-render.

Creator enables this after a successful LOD bake and provides an optional publication toggle and validated `Load baked LOD mapping` control. A normal rare GPU-idle rebuild acquires the new full-model shared asset, preserving the existing per-actor resource ownership rules. Source fingerprint is recomputed on every reimport; prepared model streams may append vertices after the stable original prefix.

This stage is currently **one authored LOD mapping per actor**, not a multi-LOD publish pipeline. It does not generate decimated meshes, skin palettes or Renderer3D LOD metadata. The VIVID actor remains Metal-only, so Vulkan/GLES compute parity is still open. New code and regression checks are committed, but **native C++ compile, CTest, real Metal GPU actor/LOD image parity and automated Creator acceptance have NOT RUN**. Consult `docs/agent-handoffs/vivid.md` Stage 7D for commit SHAs, test provenance and next tasks.


## Stage 7E — multi-LOD facial GPU morph publication (2026-11-19)

VIVID Creator now author-stages up to eight distinct facial LOD mappings instead of replacing the previous one after each bake. Each chosen target mesh gets its own `vivid-facial-lod-{meshId}.mapping.json` and `.morphs.json` sidecars, with explicit reimport and remove-level controls. A full-model `MorphLodGpuAuthoring::combineMany` pass validates compatible named shape ordering, disjoint sparse delta destinations and correct model-relative vertex ranges; one existing `GpuActorStream` dispatch therefore writes both source and all included authored-LOD facial vertices under the same ARKit weights. The existing Renderer3D LOD selectors remain responsible for drawing, without GPU buffer swaps inside frames.

The feature does **not** automatically create decimated meshes or embed a LOD-chain manifest; true imported-LOD discovery, multi-file atomic publication, Vulkan/GLES GPU morph backend implementation and visual Renderer3D LOD parity are still open. New source/tests were authored but **not built or run**. Exact commits and next-agent handoff: `docs/agent-handoffs/vivid.md` Stage 7E.


## Stage 7F — manifest-indexed facial LOD chain and immutable publication (2026-11-20)

`HumanImportManifest` now lists up to eight authored `facialLods` with named source mesh, target mesh and mapping sidecar path. Creator automatically discovers indexed maps during package import, checks source topology fingerprint and exact target IDs, preflights all sparse morph layers as one combined GPU Actor Stream CSR, and only then commits the loaded editing state. Missing, malformed, duplicate and incompatible layers reject package import.

New baked LODs publish through content-addressed immutable mapping/morph sidecars, followed by a staged atomic replacement of the **manifest last**. Removing an LOD also atomically updates the manifest index. Already published sidecar bytes are not overwritten; abandoned old sidecars can be collected separately. This gives readers a consistent package index on normal local filesystems, not a multi-file atomic transaction.

The source-level implementation and negative-case tests have been committed; **no new native C++ build, CTest, Metal Renderer3D LOD frame parity, or Vulkan/GLES compute test has been run**. True target-mesh content identity, multi-LOD performance, GPU backend coverage, full bind-pose editing, hair self-collision, and high-end skin/eye/hair rendering remain open. See `docs/agent-handoffs/vivid.md` Stage 7F for exact commits and next handoff.


## Stage 7G–7H — RHI-neutral GPU dispatch gate and GLES CPU fallback (2026-11-20)

VIVID's `GpuActorStream` no longer hard-rejects non-Metal GPUs; it requires the RHI to actually advertise compute support, a valid frame serial and healthy GPU state. Existing MSL and GLSL 4.30 CSR/mesh-packing kernels remain unchanged. This unlocks eligible compute devices at the VIVID API level but **does not by itself implement or verify Vulkan backend compute**.

VIVID Creator can now fall back to a separate `CpuActorStream` on baseline GLES3/WebGL2 or any non-compute device. That actor uses the same sparse morph source and ARKit semantic bindings to deform the full `ModelVertex` stream on CPU and upload a dynamic vertex override through standard gfx::Device; original UVs, joints, material data and model LOD vertex ranges remain intact. It requires no compute shaders or frame serial, and retains the existing safe resource-release contract. This is a CPU fallback, not GPU morphing on GLES.

A fake-GLES headless CTest for output bytes and retirement was added, but **not compiled/run**. Real Android/WebGL2 vertex-buffer update reliability, bandwidth, GPU image parity and native Vulkan compute remain to be measured/implemented. Detailed ownership, commits and test gate are in `docs/agent-handoffs/vivid.md` Stages 7G–7H.


## Stage 7J — integration with merged universal ECS animation source lifetime (2026-11-20)

The VIVID branch now contains the full `feature/universal-ecs-animation-pipeline` history via two-parent merge `8c9048fb6f0407ad87cc62d112b53fa467d43f78`. The merged animation system introduces safer model publication epochs, read leases and GPU/CPU retirement across shared native presentation hosts. Standalone VIVID Creator continues owning its private Model and World directly and does **not** borrow a `ModelResourceCache` lease that it cannot legitimately acquire.

Creator teardown now synchronously drains its GPU actor and then releases MotionWeave ECS / AnimationGraph and copied pose owners **before** releasing the old head/hair Model resources, preventing stale borrowed pointers through the new animation lifetime contract. `HumanRigBinding::compose` now rejects any nonfinite input MotionWeave node-world matrix before modifying the last-good presentation pose. A negative regression source case was authored.

**Validation status:** source committed, but merged-tree native build, CTest, GPU/Creator preview and retirement tests have NOT RUN. Full transactional replacement upload, render-thread retirement performance and animation epoch integration in shared non-Creator asset hosts remain further work. Full source SHAs, handoff and next actions: `docs/agent-handoffs/vivid.md` Stage 7J.


## Stage 7K — two-phase Creator model and GPU morph publication (2026-11-20)

VIVID Creator now stages its replacement character before tearing down the old one. All CPU validation, new `AnimationGraph` and MotionWeave `World`, head/groom GPU uploads and selected `GpuActorStream` or GLES `CpuActorStream` creation occur on local candidate state. Only a fully prepared candidate can pass the old-character retirement barrier and publish its Model, ECS animation ownership, facial actor and hair/scalp runtime into the live viewport.

This fixes the earlier unsafe ordering where a new GPU buffer allocation could fail **after** the previous character was already destroyed. With an ordinary failed candidate upload and a working GPU idle barrier, the previous visible character is kept. Shutdown still detaches ECS animation owners before releasing their Model pointers.

**Not production-ready yet:** the operation still uses a synchronous GPU-wide idle barrier at the commit boundary. If the GPU cannot confirm idle after a failure, the candidate's native resource owners are deliberately retained/leaked rather than risking premature GPU destruction; proper device-loss quarantine/recovery and nonblocking retirement remain future work. Authored DNA edits are not rolled back, only the last-good visible GPU character is protected. Source committed but **no integrated native build, CTest, GPU failure injection or Creator visual test was run**. Exact source SHA and next-agent handoff: `docs/agent-handoffs/vivid.md` Stage 7K.


## Stage 7L — nonblocking, whole-human completion-token retirement (2026-11-20)

VIVID Creator can now replace the current character without a normal full-device GPU idle stall. A new `HumanRetirementQueue` owns the **entire old character** (head/hair Models and their GPU buffers, the active GPU/CPU morph Actor Stream, MotionWeave World and AnimationGraph) while an ordered GPU completion marker is pending. A verified completed token, or explicit successful idle fallback, is mandatory before freeing the old actor and Model resources. Pending or failed markers are never treated as successful just because frames elapsed.

To ensure completion-token submission happens **after** the GPU command buffer has been submitted, `platform::AppCallbacks` has a new optional `afterFrame` callback, invoked after `endFrame()` by macOS, Linux, Windows, iOS, Android and Web platform loops. Existing applications can omit the hook. Creator uses it to progress queued retirements; no GC GPU work is emitted from inside its rendering callback. This preserves the original single MotionWeave animation owner.

**Limits and release status:** the token-supported path is nonblocking during ordinary character swaps, but a backend with no completion-token support falls back to a synchronous idle barrier. Device-loss recovery, safe handling of a failed queue drain, GPU residency backpressure and benchmarked peak VRAM use remain open. Headless fake-RHI regression source and CMake registration are committed; **C++ compilation, all CTests, graphics backend runtime testing and device-lifetime acceptance have NOT RUN** for this stage. See `docs/agent-handoffs/vivid.md` Stage 7L for commits and risk details.


## Stage 7M — retired GPU residency backpressure (2026-11-20)

`HumanRetirementQueue` now reports pending old-character count, estimated bytes in pending resident Models and VIVID actor streams, historical high-watermark and blocked rebuild attempts. Creator applies a **three-retired-human / 512 MiB estimated-residency** default admission budget before allocating the next character candidate. When a prior GPU completion token remains unresolved, requests stay pending and the old visible character remains live; only a genuinely completed token or successful GPU idle fallback releases the old model and makes room for a subsequent rebuild.

These bytes are a bounded *source-derived estimate*, not driver-accounted VRAM: shared GPU allocations, memory layout overhead and transient two-character peak require device-specific profiling. A new fake-RHI regression for pending-count/estimated-byte limits and reopening admission on fence completion was written, but **not compiled or run**; no actual native Metal/Vulkan/GLES GPU performance measurements are claimed. Stage 7M details and source commit SHAs are in `docs/agent-handoffs/vivid.md`.


## Stage 7N — native bind-pose and skin inverse-bind authoring (2026-11-20)

A new `BindPoseRebind` stage applies authored local bone translation changes to a private CPU Model candidate before its AnimationGraph is compiled or vertex buffers are uploaded. It validates the complete skeleton parent chain, finite/non-singular bind transforms, skinned node palettes and vertex joint influences. It then recalculates the bound skin transforms using `inverse(newJointWorld) * oldJointWorld * oldInverseBind`, retaining original imported glTF/FBX mesh bind frames rather than replacing them with identity-based assumptions.

Creator exposes an **opt-in skeleton rebind** checkbox in Rig correction authoring. When enabled, existing `RigAuthoring` offsets rebind the candidate Model's bind nodes and inverse-bind matrices; they are *not* added a second time as MotionWeave pose overlays. Source FBX/glTF assets and the visible last-good character are preserved, and staged GPU retirement follows the existing two-phase publication workflow.

This is **actual skeleton/inverse-bind rebinding**, not joint-weight painting, shape sculpting or animation keyframe retargeting. Source vertices/UVs/morph vertex identities are unchanged; the rest-pose skin transform stays equivalent to the original while animated deformation reflects the edited skeleton. Missing/invalid skins fail closed. New regression assertions include an imported nonidentity mesh binding frame and invalid joint palette, but **native compilation, CTest and real GPU skinning preview have NOT RUN**. Exact commit SHAs, architecture and limitations are in `docs/agent-handoffs/vivid.md` Stage 7N.


## Stage 7O — Skin Weight Authoring for imported skinned meshes (2026-11-20)

VIVID Creator now offers model-relative vertex selection, named skeleton bone selection and individual target-weight authoring through `SkinWeightAuthoring`. An explicit 0..1 bone weight is redistributed across existing influences, reduced to at most four palette slots, and deterministically quantized to a 255-byte sum. Imported source vertex position, UV, normal, tangent, morph vertex IDs and other per-vertex attributes remain untouched. All edits are preflighted transactionally on a private un-uploaded character model, then published via the existing two-phase GPU actor replacement; a failed palette/skin edit preserves the last-good visible character.

Edits can be serialized to a versioned JSON sidecar and restored through Creator. Global vertices referenced by more than one skinned primitive require compatible skin+joint-palette mappings; ambiguous shared seams are rejected rather than changing weights under the wrong skeleton. The authoring API currently stores one named-bone weight adjustment per vertex (the latest per-vertex edit); it does not yet implement weight painting brushes, seam splitting or automatic bone assignment.

**Verification:** source tests added for U8 normalization, JSON roundtrip, invalid-palette rollback and conflicting-seam rejection; actual C++ compilation, CTest and GPU image parity are **NOT RUN**. See `docs/agent-handoffs/vivid.md` Stage 7O for source commit IDs, risk discussion and next-agent work.


## Stage 7P — viewport weight painting and skin diagnostic overlay (2026-11-20)

The existing `SkinWeightAuthoring` supports a transactional brush stroke over selected model-relative mesh vertices, using a configurable local-space radius and smooth falloff, target influence, brush strength and optional X=0 bilateral mirroring. One stroke is one undo/redo record; all affected vertices are validated before the new authored edit map is published, with a provisional maximum of 4096 vertices per stroke. The actual model is updated only by rebuilding a fresh private character candidate and passing the previous skin palette/influence validation, then publishing through the already integrated GPU-safe Creator actor replacement.

The Creator exposes a selected mesh, selected named joint, weight painting controls, an arm-and-click triangle ray-picking interaction, Undo/Redo, and a screen-space red/blue diagnostic heatmap of sampled original model vertex weights. **Accuracy limitation:** this overlay uses the mesh owner's node transform and does not yet reconstruct final skinned and morphed GPU vertices. GPU skinning-aware surface picking, continuous drag paint, spatial indexing for very dense meshes and actual per-vertex GPU-accurate heatmap are still open. Mirroring assumes local X symmetry; ambiguous shared seams still reject edits.

Source-level regression assertions cover symmetric brush influence, bounded falloff, undo/redo, invalid-stroke rollback and U8 weight normalization. The changes were committed but **not compiled, run or visually tested**; see `docs/agent-handoffs/vivid.md` Stage 7P for exact source commits and remaining work.


## Stage 7Q — skin-aware viewport picking and continuous drag weight paint (2026-11-20)

`SkinnedWeightPick.h` reconstructs the current mesh's CPU-skinned world-space positions from the authoritative MotionWeave node-world matrices, imported joint palettes, U8 vertex influences and `Skin::inverseBind` matrices. VIVID Creator uses these deformed triangles for its skin-paint raycast and projects the same staged positions for the sampled weight heatmap. The brush now supports holding and dragging the left mouse button; the in-memory series of dabs becomes a single Undo/Redo record at mouse-up, with full cancellation for invalid palette/skin strokes and no per-dab model upload.

The implementation preserves the existing single source skeleton evaluator and stable model-global vertex IDs. Limitations remain significant: GPU facial morph displacement and cloth deformation are not represented in CPU picking, and this is not a GPU depth/object-ID selection pass. Selected skinned mesh must have exactly one owning Model node and consistent primitive palette mappings; ambiguous instancing/shared seams fail closed. The current CPU scratch surface is recomputed while painting/heatmap is active, so high-poly acceleration remains an optimization task.

Tests for translated-joint picking, invalid-pose staging, multi-dab undo history and cancelled drag rollback have been added as source only. **No C++ build, CTest, GUI or GPU parity run has been executed.** Exact commits and next-agent handoff are in `docs/agent-handoffs/vivid.md` Stage 7Q.
