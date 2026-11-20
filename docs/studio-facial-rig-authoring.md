# AIVERSE Studio HTML5 — Facial Rig authoring (MotionWeave Stage 181 companion)

**Branch:** `feature/studio-html5` · **Date:** 2026-11-20  
**Status:** source implemented in React + native StudioSession; no native compiler, macOS WebView or GPU validation yet.  
**MotionWeave runtime source:** `feature/universal-ecs-animation-pipeline`, Stage 180. **Do not assume these divergent branches are merged.**

## Feature

New dockable **Facial Rig** screen uses the approved first cinematic Studio UI kit and the existing `facial_rig` tab registration in `apps/studio-web/src/nestedDock.ts`. The `animation` tab is preserved for the future timeline. The screen includes:

- A 52-channel ARKit-name template that creates explicit `{parameter,node,target}` mappings, but does **not** claim that any specific character possesses these shapes.
- A searchable list of mappings with a gain slider for each channel; an editor for exact imported morph name, model node, gain, bias, smoothing half-life, and additive/override blending.
- A separate section for facial bone/eye IK authoring, including existing `AnimationRig` v1 operation shapes; eye-aim units can be added and removed.
- Native-only preset listing, loading, schema validation, and saving. Unsaved edits block preset switching; draft can be discarded.
- Clear empty/loading/error/validation/readonly feedback and the native-vs-browser-demo distinction.

Saved presets are **project-local** `facial-rigs/<name>.json`. The user selects only a sanitized simple name; native `StudioSession` does the filesystem work. No client-controlled arbitrary path, no direct browser disk writes, and no edits to legacy ImGui.

## Native bridge/API

Four whitelisted methods in `StudioSession` and the typed `StudioParams/StudioResult` contracts:

| Method | Params | Result |
|---|---|---|
| `studio.facialRigList` | `{}` | `{rigs: string[]}` |
| `studio.facialRigGet` | `{name}` | `{name,rig}` |
| `studio.facialRigValidate` | `{rig}` | `{valid,morphs,ikUnits,runtimeVerified:false}` |
| `studio.facialRigSave` | `{name,rig}` | `{name,rig}` |

`apps/studio-native/StudioFacialRigDocument.h` validates JSON v1 up to 256 morph bindings, 64 native IK units and 128 KiB. It rejects duplicate `(node,target)` destinations, nonfinite/out-of-range gains, biases, half-life, malformed IK operations, unsafe filenames and selected file/directory symlinks. Native save stages with same-directory `openat(O_CREAT|O_EXCL|O_NOFOLLOW)`, `fsync` and `renameat`, preserving a previous file if staging fails. `MapDoc` Undo/Redo does not undo these independent project assets.

**Important:** Studio schema validation is NOT runtime compilation of named GPU morph channels, bone/skin compatibility, or actual `FacialControlRig::compile`. Those remain the authority of MotionWeave after the branches merge.

## Native rendering boundary

The native `StudioSession` currently lacks the MotionWeave Stage 180 runtime, a bound character model and its compiled facial rig. Therefore **live 3D facial preview is not implemented**; the UI visibly explains the limitation. No synthetic face, fake 3D image, model or GPU result was introduced. Integrating this with the real native Renderer3D/WKWebView scene is the next cross-branch task after a conflict-aware merge.

Do not replace `apps/studio-native/mac/StudioMetalScene.cpp` or repaint the Scene viewport in HTML/CSS. Once the actual native Model/Graph asset is chosen, add `studio.facialRigPreviewBegin/Set/End` using real asset leases and view playback; run the morph/IK program after the ONE animation owner evaluates the pose. Do not issue preview mutations through the generic Workspace property API.

## How to try the authoring frontend

```sh
cd apps/studio-web
npm run typecheck
npm run build
npm run test:facial
npm run test:web
```

For native file validation on a Mac with configured build:

```sh
cmake --build build/<your-config> --target aiverse_studio_facial_rig_tests aiverse_studio
ctest --test-dir build/<your-config> --output-on-failure -R '^studio_facial_rig_document$'
```

**These are commands for the owner's Mac, NOT recorded PASS results.** During this stage only GitHub connector file/API audits and source commits were performed. The user requested one coordinated full native animation acceptance after feature closure.

## Key paths

- `apps/studio-web/src/panels/FacialRigPanel.tsx`, `FacialRigPanel.css`, `facialRigSchema.ts`
- `apps/studio-web/src/types.ts`, `mock.ts`, `src/nestedDock.ts`, `src/panels/StudioPanelRenderer.tsx`
- `apps/studio-native/StudioFacialRigDocument.h`, `StudioSession.cpp`, `CMakeLists.txt`
- `apps/studio-web/tests/facialRigSchema.test.mjs`
- `apps/studio-native/tests/StudioFacialRigDocumentTests.cpp`

## Next

1. First run TS strict typecheck and native document CTest in the real Studio worktree. Fix source/compiler errors before enabling defaults.
2. Conflict-aware merge of `feature/universal-ecs-animation-pipeline` with Studio; do not replace either branch's native renderer or animation runtime.
3. Add actual model selection and a live GPU facial preview via MotionWeave's existing `ModelAssetProvider`, `AnimationGraph/Animator`, `FacialControlRig` and one pose owner.
4. Complete facial artist authoring controls (presets, channel alias discovery, jaw/gaze sliders, morph diagnostics), automatic morph-preserving LOD geometry, and final one-pass Mac acceptance. Keep both features unmerged to main until verified.


## Stage 184 — native GUID binding and staged input relay (source-only)

An isolated integration branch `integration/motionweave-studio-facial-stage184` now connects the **existing shared** `AssetReferencePicker` to the Facial Rig panel. It supports Assets search, drag/drop, .meta GUID identity and a panel Lock/Unlock so unrelated selection will not overwrite a locked facial model reference. Every native bind resolves `studio.facialPreviewBind({guid,rig})` through `StudioAssetCatalog`, validates the current project/content path without symlinks, checks the v1 JSON authoring schema, and holds typed facial scalar input state. `studio.facialPreviewControls({revision,values})` atomically accepts only known 0..1 inputs and rejects stale revision and invalid data; `studio.facialPreviewInfo({})` reports the current binding. The UI has an explicit **Send native inputs** action and never claims an absent GPU render.

**Important:** this Studio branch still has only `kMaxMorphTargets=8` in `Model.h`; it does not yet contain MotionWeave Stage 180/183's real 64-channel `FacialControlRig`/`FacialPreviewSession`. Therefore `runtimeReady=false` / `runtimeVerified=false` are intentional. The new binding is neither imported Model validation nor a GPU facial preview. Next step is conflict-aware true 64-channel renderer/Model provider integration on the isolated branch, then native Studio 3D facial viewport and the final Mac test campaign. See `docs/handoff/studio-html5/78-motionweave-stage184-facial-preview-binding-2026-11-20.md`.

**NOT RUN:** npm typecheck/build, C++/CMake/CTest, macOS WKWebView, Metal/Vulkan/GLES, sanitizer/crowd/VRAM. Native test source `studio_facial_preview_binding` is committed but not executed.


## Stage 184 continuation — typed IK vectors and atomic updates

The original Stage 184 code commit sequence now also includes:
- `7206ef7fe319c4d5e824b54dbe440b14012b82a6`: `StudioFacialPreviewBinding` collects named IK vectors from the authored `rig` operations (`target`, `pole`, transform translation/rotation/scale parameters) alongside scalar expressions and IK blend weights. Whitelisted `studio.facialPreviewControls` accepts optional `vectors: Record<string,[number,number,number]>`. All finite components must be within +/-1,000,000, and scalar/vector updates commit atomically at the same binding revision. HTML5 supplies explicit XYZ inputs. This still **does not evaluate** IK or render a GPU face.
- `2921bf31fcea11af332c75baa94b198ad8c47a70`, `d854af4922eaa693ab1fec9fe4e535cb85d3eb98`: correct and strengthen the deferred native C++ fixture, including a named eye Aim vector, valid target update and rollback on invalid scalar/vector. The fixture uses explicit `Json::object` / array construction to avoid ambiguous test source expressions.
- `f29a7665d5637014632932365e0b468024d4b759`: self-contained C++ includes, unambiguous scalar/vector type-conflict inspection, and renderer capacity diagnostics from the **actual** native `render::kMaxMorphTargets`.

**Validation remains NOT RUN**: these are authored source commits; C++ compilation, the new CTest and HTML5 typecheck/build/UI remain unverified. This addendum deliberately does not change the earlier claims that Studio GPU facial rendering is blocked by the separate 64-slot MotionWeave runtime/renderer branch. The follow-on merge must consume `FacialPreviewSession::vector` for real typed eye gaze/IK without reintroducing a second solver. Stage 184 completion here means the **host-asset and input-binding boundary only**, not the full live visual preview.
