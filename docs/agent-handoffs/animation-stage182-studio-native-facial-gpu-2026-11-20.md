# AIVERSE MotionWeave — Stage 182: HTML5 Studio native 64-morph GPU integration

**Date:** 2026-11-20  
**Integration branch:** `integration/motionweave-studio-face-20261120`  
**Animation base:** `51806d41b1ab1cfd261d56f01da7a811b98fcc27` from `feature/universal-ecs-animation-pipeline`.  
**Studio source:** `5f644134905b37bed783a0a8d28b27647584692b` from `feature/studio-html5`.  
**Initial integration snapshot commit:** `fe99231e3540e68cd25e656192d8cdc7990d12e9`.  
**PR #22:** draft/unmerged. Nothing pushed to `main`.

## Stage outcome

A real selectable facial asset path and live expression/IK controls
have now been wired through HTML5 Studio to the native
`ModelAssetProvider` / `FacialControlRig` / `Renderer3D` path.
Source-level only: no Mac compiler, GPU frame, pixel or test result
has been observed. Do **not** call this fully integrated and verified
until the real Mac build and GPU acceptance.

The Studio frontend from the user's chosen first cinematic HTML5
design was imported selectively into a new independent integration
branch on top of MotionWeave's 64-channel renderer. This is NOT a
full semantic Git merge of thousands of divergent commits. All
unrelated original Studio/Animation branch histories remain intact.

## Sources, architecture and changed files

Detailed commit-by-commit provenance:
[Studio Stage 68 handoff](../handoff/studio-html5/68-motionweave-native-facial-preview-2026-11-20.md).
Public guide: [real native MotionWeave Studio preview](../studio-motionweave-facial-live-preview.md).

Key integrated pieces:

- Root `CMakeLists.txt` opt-in macOS Studio flag, root
  Objective-C++ enable and `apps/studio-native` add_subdirectory.
- `apps/studio-native/StudioFacialPreviewState.h`,
  `StudioFacialPreviewPaths.h`, `StudioSession.{h,cpp}`:
  trusted project-local GLB/glTF/FBX resolution, persisted v1
  facial-rig selection and named scalar/vector parameters.
  Native IPC methods `studio.facialPreviewStart`,
  `studio.facialPreviewStop`, `studio.facialPreviewScalar`,
  `studio.facialPreviewVector`, `studio.facialPreviewState`.
- `apps/studio-native/mac/StudioFacialGpuPreview.{h,cpp}`:
  REAL native owner for GPU model resources and compiled Stage 180
  facial runtime. Evaluates only ONE native Animator parent pose,
  then applies semantic 64-channel FacialControlRig morph/IK and
  submits an actual Renderer3D ModelDraw.
- `StudioMetalScene.{h,cpp}` and `Main.mm`:
  use existing WKWebView sibling CAMetalLayer Scene, no HTML
  generated face, actual encoded-frame status/error events. Positive
  GPU idle is required before provider destruction, failing resources
  are quarantined.
- `apps/studio-web/src/panels/FacialLivePreview.tsx`,
  `FacialRigPanel.tsx/css`, `App.tsx`, `types.ts`, `mock.ts`:
  save-selected real model preview with per-expression live sliders,
  typed IK vector controls, real Scene/Facial Rig dock split and
  honest demo fallback.
- Deferred fixtures `apps/studio-native/tests/
  StudioFacialPreviewContractTests.cpp`,
  `apps/studio-native/tests/StudioSessionTests.cpp`,
  `apps/studio-web/tests/facialRigSchema.test.mjs`.

No new simulation ECS, game module, renderer backend, network
serializer or model animation algorithm was duplicated.

## Readiness matrix

| Workstream | Source status | Native verification |
|---|---|---|
| MotionWeave 64 morphs + imported semantic names | From earlier stages | NOT RUN in this integration |
| Existing FacialControlRig + native IK | Stage 180 source | NOT RUN |
| HTML5 Studio facial authoring 52 sliders/presets | Stage 181 source grafted | NOT RUN |
| Native GPU model selection/reader sandbox | Stage 182 source implemented | NOT RUN |
| Native ModelAssetProvider GPU lifetime | Stage 182 source implemented | NOT RUN |
| Single Animator + actual FacialControlRig + Renderer3D | Stage 182 source implemented | NOT RUN |
| Typed 52 live inputs + IK, real UI docking | Stage 182 source implemented | NOT RUN |
| Git full merge across all divergent engine changes | NOT DONE; selective source sync only | REQUIRED LATER |
| Morph-aware automatic geometry LOD | NOT DONE | NEXT FEATURE STAGE |
| Metal/Vulkan/GLES, Mac Studio/WebView, ASan/crowd | NOT RUN | Final acceptance |

## What was actually verified

Read actual connected GitHub source contracts for
`Model::kMaxMorphTargets`, `Animator`, `AnimationGraph`,
`FacialControlRig`, `ModelAssetProvider`,
`RenderDeviceLifetime`, `Renderer3D`, real StudioMetalScene
and StudioSession, followed by commit readback and comparison to
the animation base. Source still uses `kMaxMorphTargets=64`;
the incompatible `Model::markMaterialChanged()` from old Studio
was removed locally rather than changing the newer Model API.
No merge conflict markers were observed in the audited changes.

**NOT RUN:** no local AIVERSE worktree/network/GPU in this
environment. No CMake, C++/ObjC++ compiler, npm typecheck,
React/WebView, native CTest, rendered face screenshot,
GPU pixel/fence completion, Vulkan/GLES RHI, sanitizer or
crowd/stall benchmark.

## Explicit next action for the following AI agent

Read Stage 182 Studio handoff and
`docs/studio-motionweave-facial-live-preview.md`.
First resolve any targeted Mac native compiler/TypeScript errors
without bypassing strict IPC/file sandbox or 64-channel Model.
Then validate real named 52-morph face with metal RHI and
one parent pose owner. Keep the user's request to defer
one comprehensive test campaign until feature closure.

Next code feature: **automatic morph-preserving model LOD
generation**, reusing `ModelLod::morphSourceForTarget` and
importer name tables. Reject invalid correspondences; never silently
truncate named morph channels or break skin/morph GPU parity.
Then run final one-pass macOS acceptance and only after that
prepare a conflict-aware wider merge or undraft PR #22.
