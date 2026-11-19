# Prisma + MotionWeave integration — 2026-11-20

Branch: `integration/prisma-motionweave-view`.
Initial merge commit: `fc46059ee6523cbc1f04f8c412d44e4fdae9398a`.
Parents: Prisma preflight `db607e5879347d78ccab44ad5457c007a54c9145` and
MotionWeave `6bf45b99d88f60badf8ba39ac9c077e53facd2fa`.

## Scope and resolution
Continued the existing local merge in `~/Projects/AIVERSE-prisma-motionweave`.
Preserved the 21 previously staged conflict resolutions. Resolved the remaining:
- `apps/client/GameClient.cpp`: Prisma initialization with shared GPU epoch;
  collect model/image/paint/renderer retirement and Prisma temporal resources.
- `apps/editor/Editor.cpp`, `Editor.h`: one Viewport destructor and one
  ViewportGpuRetirement owner; retain device-generation checks, renderer warmup,
  native preview retirement and shutdown completion barriers. Removed the
  overlapping offscreenTargets owner and adjusted SceneWindow, GameWindow and
  StatsWindow call sites (otherwise retained their prior resolutions).
- `apps/editor/animation/AnimationEditor.h`: one shared Portrait/Model/Graph GPU
  bundle retirement path; preserve preview-clock snapshots, named playback
  rebind, parameter values, upload-failure retirement and explicit reset.
- `apps/editor/windows/ModelWindow.cpp`: preserve raw ModelAssetProvider/View
  inspection and shared Portrait retirement without duplicate Portrait members.
- `engine/render/PostFx.cpp`, `PostFx.h`, `PostFxShaders.h`: legacy shims/aliases
  only. Incoming underwater policy/composite/shader and GPU epoch APIs moved
  into canonical `engine/render/Prisma.h`, `Prisma.cpp`, `PrismaShaders.h`.
  No second postprocess implementation or target owner. Underwater bits follow
  AutoExposure; depth slot 5 is enabled only without a ShaderGraph; AE keeps
  its original 8/9 mapping. Intermediate passes use scratch depth, preserving
  scene depth. Shader source remains shared GLSL/MSL with typed depth sampling.
- `engine/render/Renderer3D.cpp`: retain Prisma object velocity/reactive/history
  and canonical resolvePrisma alongside incoming GPU epoch checks and program/
  target retirement. resolvePostFx remains the existing forwarding API.
- `modules/rts/view/RtsFog.h`: aliases to generic VisibilityField; move existing
  GPU memory accounting into `engine/view/VisibilityField.h`, not a second class.

Universal View, AnimationGraph, GPU submission fences and independent resource
retirement tokens retained. No simulation, save/JSON schema, feature defaults or
source-branch changes. Legacy PostFx underwater APIs forward to Prisma. Internal
editor Viewport callers use the explicit viewportRetirement owner.

## Actual checks
- `git diff --check` on the edited working files: clean.
- `git diff --cached --check`: reports inherited documentation trailing whitespace
  and blank EOF lines (animation stages 119/130, SpectraFX, FPS, NEREID and two
  skills); deliberately not cleaned as unrelated to merge.
- `git diff --name-only --diff-filter=U`: empty after staging.
- `rg -n '^(<<<<<<<|=======|>>>>>>>)' --glob '*.{h,cpp,mm}' apps engine modules`:
  no conflict markers. Source/API inspection only, not compiler validation.
- Build, CMake, CTest, GPU tests, benchmarks, sanitizers: **NOT RUN**, explicitly
  excluded by the user's merge-only request. Existing source-branch test results
  are not evidence for this merge. CI not requested or verified; no green claim.

## Next integration action
Merge remote integration documentation through `5709e43456c4fcea0301837551dab3b727b2b3d8`
and current MotionWeave through `ea28f227b1da3fcfb32da2abc49e60f588861786`.
Reconcile the new Attachments viewport call with the single editor viewport owner.
Publish only the integration branch by ordinary push. No main/source mutations.
Native runtime and visual compatibility remain unverified until separately authorized.
