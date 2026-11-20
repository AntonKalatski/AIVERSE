# AIVERSE MotionWeave + HTML5 Studio: real facial GPU preview (integration Stage 182)

**Date:** 2026-11-20  
**Branch:** `integration/motionweave-studio-face-20261120`  
**Status:** source integration authored; **Mac native build, WebKit UI, real GPU and CTest NOT RUN**. This is not a verified production editor.

## Exact integration strategy

- **Animation source:** `feature/universal-ecs-animation-pipeline` at `51806d41b1ab1cfd261d56f01da7a811b98fcc27`. Its renderer has `render::kMaxMorphTargets=64`, sparse GPU morph channels, imported morph semantic names and Stage 180 `<aiverselib/animation/FacialControlRig.h>`.
- **HTML5 Studio source:** `feature/studio-html5` at `5f644134905b37bed783a0a8d28b27647584692b`, approved first cinematic UI design.
- **Selective source integration:** commit `fe99231e3540e68cd25e656192d8cdc7990d12e9` grafts the complete `apps/studio-web`, `apps/studio-native`, `tools/studio`, Studio authoring docs/handoff and UI skill **on top of the MotionWeave root tree**. This is intentionally NOT a full two-parent Git merge: other Studio branch changes to gameplay/editor/rendering files are excluded until separately reconciled. This avoids wholesale regression from the older eight-channel Studio Model. Do not treat the integration commit as full branch parity.
- The root `CMakeLists.txt` now adds opt-in `AIVERSE_BUILD_STUDIO_HTML5` and enables Objective-C++ at root scope when enabled on macOS. The native Studio target links `AIVERSE::MotionWeave` and retains a real Metal/WKWebView sibling viewport.

## Actual native runtime flow

The Facial Rig tab includes a new `FacialLivePreview.tsx` section. A user opens a real project, selects an already-saved `facial-rigs/<name>.json`, and enters a **real project-relative model key**, e.g. `models/hero.glb`. The model and rig must really exist; no sample face is synthesized.

1. `studio.facialPreviewStart({name,model})` is explicitly whitelisted in `apps/studio-native/StudioSession.cpp`. It validates the project/model path under the trusted Workspace root (canonical element-by-element parent check, no traversal, 256 MiB cap, accepted GLB/glTF/FBX extensions) and loads the native rig JSON. It creates a **host-owned presentation-only** set of scalar expression inputs and named IK vector/weight controls.
2. The native `StudioHost` sends `facial.preview.requested`. The React shell keeps the real `scene` viewport open and docks the `facial_rig` panel alongside it (user layout is preserved if already separated).
3. On the owner thread, `StudioMetalScene` switches into `StudioFacialGpuPreview`. Its `ModelAssetProvider` imports/uploads the actual project model using a sandboxed native `render::FileReader` and a current `RenderDeviceLifetime` epoch.
4. `FacialControlRig::compile(*model,rig,controls)` verifies exact imported semantic morph names, real rig bones, IK operators and channel capacities. Unknown/missing/ambiguous channels or invalid Graph/IK bindings cause an exception; `Main.mm` reports `facial.preview.error` and returns to the previous real scene. No fake GPU success.
5. The host-owned **single** `render::Animator` evaluates the first native clip (if one exists), else the bind pose. The compiled FacialControlRig applies input weights and eye/jaw/head IK to **that same pose**, once. `worldMatrices` derives the final pose. `Renderer3D::drawModel`, `render(pass)`, then native `endPass/endFrame` submit the actual GPU frame. A frame-scoped `ModelReadLease` protects the CPU Model through synchronous submission.
6. `Main.mm` publishes `facial.preview.ready` only after a successful frame submission and records `gpuReady` in the C++ session for dock remounts. This is **submission evidence, not GPU pixel parity or fence-completion proof**. It does not assert any frame was actually seen on a Mac in this stage.
7. While active, the UI provides one **0–1 live expression slider per distinct bound semantic parameter** (52 if the authoring template was saved) and live named IK vector input. The scalar/vector calls do not reload the Model or recompile the immutable program.
8. `studio.facialPreviewStop` or changing the project clears the presentation state. A pre-retirement `Device::waitForResourceIdle` is required before a native provider shutdown; incomplete GPU proof quarantines rather than destroying a provider with in-flight resources.

## New typed bridge operations

| Method | Request | Synchronous C++ result |
|---|---|---|
| `studio.facialPreviewStart` | `{name,model}` | preview status (GPU not ready until a subsequent frame) |
| `studio.facialPreviewStop` | `{}` | preview status |
| `studio.facialPreviewScalar` | `{parameter,value}` | preview status; value 0–1 |
| `studio.facialPreviewVector` | `{parameter,x,y,z}` | preview status; each coordinate within ±100 |
| `studio.facialPreviewState` | `{}` | `{active,name,model,generation,controlsRevision,gpuReady,error}` |

`StudioParams`/`StudioResult` in `apps/studio-web/src/types.ts` describe every operation. `mock.ts` deliberately rejects live changes when native Studio is not connected. `FacialRigPanel` prevents authoring changes while the real GPU preview is active; stop before editing or saving a new source rig. Stage 181's project-local save and strict JSON validation remain unchanged.

**GPU preview and model authoring are not gameplay authority.** No changes enter the deterministic `Simulation::world()`, networking or save formats. The core animation still has exactly one base pose owner; no second AnimationGraph evaluator is installed.

## Build on the owner's Mac — deferred commands

```sh
# From the integration branch working tree:
git switch integration/motionweave-studio-face-20261120
cd apps/studio-web
npm install
npm run typecheck
npm run test:facial
npm run build
cd ../..

cmake -S . -B build/studio-motionweave -DCMAKE_BUILD_TYPE=Release \
  -DAIVERSE_BUILD_STUDIO_HTML5=ON \
  -DAIVERSE_BUILD_TOOLS=ON \
  -DAIVERSE_BUILD_TESTS=ON
cmake --build build/studio-motionweave --target \
  aiverse_studio \
  aiverse_studio_facial_preview_contract_tests \
  aiverse_studio_facial_rig_tests \
  aiverse_animation_facial_control_rig_tests -j 8

ctest --test-dir build/studio-motionweave --output-on-failure \
  -R '^(studio_facial_preview_contract|studio_facial_rig_document|animation_facial_control_rig)$'
```

Open the native `AiverseStudioHTML5.app` that CMake generates. Open a real project containing a face model with actual named morph targets, then load a saved facial rig and click **Render real model**. Native Studio should split the Scene and Facial Rig panes; use 52 expression sliders and gaze to inspect real GLB/FBX geometry. Test shader/skin parity at least on Metal, and the underlying MotionWeave renderer on Vulkan/GLES in their real device hosts separately.

**These commands are provided for later Mac acceptance. They were NOT executed in this stage.** The local tools had no clone access to the private repository or a graphics device, so build/link, WebView screenshots, native CTests, GPU readbacks, Metal/Vulkan/GLES backend parity, sanitizers, 1k/10k/100k crowd VRAM/performance and GPU fault-injection are all **NOT RUN**. A native compiler may reveal interface errors requiring correction.

## Deliberately unfinished / next stage

- This is a **selective cross-branch integration**, not a complete historical merge of Studio and MotionWeave; recheck peer renderer/editor changes before integration into main.
- No real artist face GLB with 52 named morph targets was provided, so a verified expression screenshot is not available.
- The Studio HTML5 shell renders via the existing macOS **Metal** host. Vulkan/GLES are supported by MotionWeave renderer source but need separate device-host integration/validation; this demo does not claim platform UI parity.
- Facial IK supports compiled existing rig ops; more advanced cornea/eyelid contact, expression/viseme controllers and authoring visualization remain future stages.
- Source-level GPU lease and full-idle retirement are written but not runtime-proven. Actual completion and error-path robustness require fault/ASan/Metal acceptance.
- Automatic **morph-preserving mesh LOD simplification** remains the next missing functional stage. After it is implemented, run one comprehensive Mac acceptance (CMake/CTest, Studio native/WebKit, real assets, Metal/Vulkan/GLES, sanitizers, GPU fences, crowd/VRAM).

Do not change `main` or undraft PR #22 solely on these source changes. See the integration stage handoffs under `docs/agent-handoffs/` and `docs/handoff/studio-html5/`.
