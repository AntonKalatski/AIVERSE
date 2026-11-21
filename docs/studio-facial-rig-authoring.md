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
