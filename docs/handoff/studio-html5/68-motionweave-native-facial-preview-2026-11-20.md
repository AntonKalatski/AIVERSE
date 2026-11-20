# AIVERSE Studio HTML5 — Stage 68: integrated native MotionWeave facial GPU preview

**Date:** 2026-11-20  
**Working branch:** `integration/motionweave-studio-face-20261120`  
**Source base:** MotionWeave `51806d41b1ab1cfd261d56f01da7a811b98fcc27`; pinned Studio HTML5 source `5f644134905b37bed783a0a8d28b27647584692b`.  
**Status:** **SOURCE IMPLEMENTED / NOT NATIVE VERIFIED**. No commit or full merge into main. Existing PR #22 remains Draft/unmerged.

## Exact implementation changes

| Commit | Paths | What actually changed |
|---|---|---|
| `fe99231e3540e68cd25e656192d8cdc7990d12e9` | Studio native/web/tools/design docs + skills tree | Selective tree graft of HTML5 Studio onto animation branch's existing 64-channel Model/Renderer3D. NOT a full peer merge. |
| `31c5974b` | root `CMakeLists.txt` | Opt-in Studio build flag, root ObjC++ enable, macOS `add_subdirectory(apps/studio-native)` |
| `1bc3ce29`, `e7227f57` | `apps/studio-native/StudioFacialPreviewState.h` | Immutable rig, trusted project model source, named scalar/vector controls, native GPU ready and generation state |
| `66ae26e7` | `StudioFacialPreviewPaths.h` | Canonical project-root file sandbox, actual GLB/glTF/FBX validation, 256MiB cap, sandboxed ModelAssetProvider read callbacks |
| `2de676c1`, `9b8db0f6` | `StudioSession.h` | Native Scene render host can read request and report actual frame readiness/errors |
| `e3a9003a`, `fe8e0e6f`, `a3fd5a63` | `StudioSession.cpp` | Five whitelisted preview IPC methods, validated project/file/rig selectors, per-expression scalar and IK target updates, C++ GPU-ready acknowledgement and fail-closed errors |
| `5c483248` | `mac/StudioFacialGpuPreview.h` | GPU owner and frame-preview public lifecycle |
| `3da18d35`, `e48ed9db`, `fb0e00ef`, `4d7d1519` | `mac/StudioFacialGpuPreview.cpp` | Real ModelAssetProvider import/upload, compiled `FacialControlRig`, ONE Animator, CPU ModelReadLease, semantic 64-channel pose/IK, Renderer3D Metal submission, conservative GPU-safe provider retirement and clear asset diagnostics |
| `2e98250a` | `mac/StudioMetalScene.h` | Opt-in facial request and Renderer3D frame confirmation API |
| `c378cb3f`, `2de7cde2` | `mac/StudioMetalScene.cpp` | Real facial frame path over the same native CAMetalLayer; preserve normal Scene/material preview, remove incompatible Studio-only Model materialRevision API |
| `0410a865`, `f24e99d5`, `eec66bf0` | `mac/Main.mm` | Real native preview status/failed-model UI events and guaranteed no synthetic viewport; actual draw-submission acknowledgement |
| `e3e79016`, `cde337aa` | `apps/studio-native/CMakeLists.txt` | Native preview TU, AIVERSE::MotionWeave link and new preview file-sandbox CTest |
| `1aec86fa` | `apps/studio-web/src/types.ts` | Typed preview Start/Stop/Scalar/Vector/State DTOs |
| `fc28e940` | `apps/studio-web/src/mock.ts` | Demo cannot pretend to show or control a native GPU model |
| `87463385`, `4be171e3`, `14a52d7b` | `apps/studio-web/src/panels/FacialLivePreview.tsx` | Save-required project model preview, 52 semantic live sliders, named IK vectors, native readiness/error, truthful GPU submission status |
| `9169ce9c` | `FacialRigPanel.tsx` | Embed dedicated real native preview; prevent editing saved rig while GPU program active |
| `76914a0b` | `FacialRigPanel.css` | Responsive native-preview UI using approved shared Studio semantic tokens |
| `e8ba5267` | `App.tsx` | Automatically dock Facial Rig alongside real Metal Scene on preview request |
| `387deda3`, `dae6b4bf` | native tests | Deferred CPU path sandbox and native StudioSession refusal-without-project fixtures |
| `b056d8b0` | `facialRigSchema.test.mjs` | Deferred browser mock proof of no simulated GPU preview |
| `17117ae2` | `docs/studio-motionweave-facial-live-preview.md` | Public owner/IPC/render lifecycle, build instructions, limitations and next action |

## Architecture and API

The new `facial_rig` dock remains the same HTML5 Studio UI kit,
not an ImGui renderer. The actual Scene surface remains a native Metal
CAMetalLayer attached beside WKWebView; the React panel contains no
canvas/WebGL/placeholder face image. The game/simulation ECS has not
been modified and no gameplay state is authored by preview inputs.

A trusted open project supplies a real existing model file path and a
saved v1 facial rig JSON. `StudioSession` validates the project-relative
GLB/FBX/glTF and passes a versioned `StudioFacialPreviewState` to the
main-thread `StudioMetalScene`:
`studio.facialPreviewStart/Stop/Scalar/Vector/State`.
`StudioFacialGpuPreview` owns its own real GPU ModelAssetProvider and
RenderDeviceLifetime. On an asset revision, it retires any old Model
only after a positive device idle barrier, imports new GPU Model
resources with the same sandboxed project reader, compiles the real
Stage 180 semantic `FacialControlRig` against importer morph names,
evaluates ONE native Animator source pose, applies the program's
morph/IK corrections to that pose, computes world matrices and sends
one native `Renderer3D::drawModel` packet. CPU ModelReadLease spans the
synchronous Renderer3D submission. Successful `endFrame` emits the
UI notification and sets the session GPU-ready bit, but does **not**
prove a GPU fence completion or visual pixel parity.

Preview failure does not trigger an authoritative scene change. The
native UI receives an error, clears the preview state and reverts to
the real map Scene on its next frame. No cross-genre types, root-motion
authority or network serializer changes. The app's studio-specific
old Model materialRevision API was deliberately removed rather than
downgrading the new 64-morph engine Model.

## Testing truth and remaining risks

**ACTUALLY PERFORMED:** connected GitHub audit of Studio and
MotionWeave branch HEADs, source/API checks of native Model/Animation/
Renderer3D/MotionWeave and WKWebView, SHA-guarded selective tree import
and commits, verification that kMaxMorphTargets is still 64,
readback of new CMake/UI/backend/renderer paths with no conflict
markers. No local private repository clone was accessible through
the model's runtime due absent GitHub network DNS.

**NOT RUN:** `npm run typecheck`, `npm run build`,
`npm run test:facial`, all CMake configure/build/link, native Studio
file/path CTests, MotionWeave facial runtime CTest, real macOS
Studio window/video, Apple Metal GPU pixel comparison,
Vulkan/GLES backend validation, RHI-fence tests, sanitizers,
multi-scene rollback, crowd/culling/perf/VRAM metrics. No build or
runtime "PASS" is claimed. A real face GLB with imported 52
semantic morph targets was not provided, so real expressive rendering
has NOT been visually observed.

The integration is deliberately a selective **source graft**, NOT
the complete historical Studio+Animation two-parent merge. Peer
changes in older Studio engine/client/editor and other agents'
branches must be reviewed before main. These are real risks,
not unspecified "later tweaks". Do not undraft PR #22.

## Next actionable step

Run targeted macOS strict UI and native builds from
`docs/studio-motionweave-facial-live-preview.md`.
If compilation/link or real GPU import fails, fix the exact
native source and preserve model-provider GPU retirement rules.
Check a real facial GLB/FBX with 52 named shapes, test slider/IK
pose updates, narrow 240px dock and render frame status, and confirm
one parent Animator writer.

Then finish automatic morph-preserving geometry LOD simplification
using the semantic morph mapping already in the engine.
After functionality closes, execute the user's requested single
comprehensive native Mac acceptance (CMake/CTest, Studio WebKit,
Metal/Vulkan/GLES, ASan/TSan/UBSan, GPU fault and 1k–100k crowd/perf).
Only then consider conflict-aware broader merges and release.
