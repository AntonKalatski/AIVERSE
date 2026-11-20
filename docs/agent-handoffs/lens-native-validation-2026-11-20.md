# LENS native validation — 2026-11-20

Branch `codex/lens-completion`; source worktree `.worktrees/lens-completion`.
All build products, compiler temporary files and caches are on KINGSTON.

## Pass 1 — integration compile failures

Tested source `f51914352`. Command (from worktree):

```sh
env TMPDIR=/Volumes/KINGSTON/AIVERSE-builds/lens-completion/tmp CLANG_MODULE_CACHE_PATH=/Volumes/KINGSTON/AIVERSE-builds/lens-completion/clang-cache bash ./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/lens-completion/debug --jobs 4
```

CMake configure PASS (AppleClang 17, macOS). Build FAIL. CTest NOT RUN.
The runner lacks executable permission; direct execution failed with exit 126,
then explicit bash succeeded in starting configure/build.
Logs: `/Volumes/KINGSTON/AIVERSE-builds/lens-completion/debug/test-reports/20261120-141529-48810/`.

Actual failures:
- `tests/render/camera_rt_pool.cpp`: missing `engine/render/RenderTargetPool.h`.
  Also required by the production CameraRenderPipeline. Restored the existing
  source from `origin/feature/universal-multicamera-render-targets`, originally
  introduced in `3ac9f69e3`, rather than implementing a second pool.
- `CameraRenderScheduler.h:217`: remove_if lambda deduced incompatible bool
  and vector<bool>::reference returns. Explicit bool conversion fixes it.

Fix commit: `cc96533820b1c976cdaa62a81c690b7ffff032af`. Changed only the two listed engine headers.
Public API/save formats unchanged. This restores inherited pool behavior,
including externally established GPU-idle shutdown; it is not a new GPU
lifetime validation. Current fix is not yet compiled. CI NOT RUN/no URL.

Next: rerun the same command, repair subsequent compiler failures, then run
CTest. Remaining LENS all-owner rollback and transient topology budget remain
open as recorded in the Stage 48B.3B.3 handoff. No main merge or push.

## Pass 2 — confirmed GPU completion API integration

Tested `77ca1a7f6` using the same command: configure PASS, build FAIL, CTest
NOT RUN. Logs: `/Volumes/KINGSTON/AIVERSE-builds/lens-completion/debug/test-reports/20261120-142039-60482/`.
Scheduler now compiled. Restored pool revealed the original branch's absent
`Device::completedFrameSerial` dependency. Fix `54c70331b6a5102d6e20f0ab1d36797acb230230` reuses the existing
`OffscreenTargetRetirement` completion-token owner, rather than inventing
serial completion. Pool shutdown refuses failed idle proof and retains handles;
private allocation exceptions clean up only unpublished textures. Test stub
now models tokens and failed-idle retry.

`CameraRenderPipeline::collectRetiredTargets` polls both normal and screen
target pools. CameraRuntimeHost/CameraClientAdapter forward it; GameClient
calls it at its existing owner-frame retirement hook. Removed unsafe polling
inside execute (which can be called multiple times in one GPU frame). Native
hosts must invoke the new hook before submissions. Public target access and
asset/save formats unchanged. Files: these four camera headers plus target
pool, GameClient.cpp and camera_rt_pool.cpp. Actual fake-RHI/GPU tests still
NOT RUN for the fix; CI NOT RUN/no URL. Next: repeat native build/CTest.

## Pass 3 — incompatible optional batching API

Tested `149dcd8f7`: configure PASS, build FAIL, CTest NOT RUN.
Report `20261120-142337-65530` in the same KINGSTON reports directory.
Camera pool, scheduler, layout, rig, temporal, capture views/graph, scene asset,
oblique, PNG, frustum and HDR-resolve executables now compile/link (not test
execution). CameraRenderPipeline referenced CPU-MDI policy APIs absent from
this renderer branch, which has no adaptive CPU-MDI implementation. Removed
that orphan dependency; per-camera View LOD remains unchanged. Reintroduce
scoped policy integration when merging a renderer that actually owns it.
Fixed camera_client_adapter's check helper to accept its existing diagnostic
argument. Commit `23679d5be4f6106e3e2d3989fda99824b0d7182b`; two paths above. No asset/save change; CI NOT RUN.
Next: rerun full build; no camera runtime or GPU parity claim yet.

## Pass 4 — camera adapter native linkage

Tested `e95cb473e`: configure PASS, build FAIL at camera_client_adapter link;
CTest NOT RUN. Report `20261120-142525-69711`. Missing Json implementation and
PostFx/ScreenLighting vtables proved this fixture was not header-only. Fix
`22820a9ab6d64a244883dddde9493afc14dc7231` adds only this target's runtime/content-reader dependencies in shared
CMakeLists.txt and uses the runtime-provided platform entrypoint (`aiverseMain`)
in tests/render/camera_client_adapter.cpp, matching existing native fixtures.
No production source/API changes. Subsequent build/CTest NOT RUN at checkpoint.
Next: rerun the same KINGSTON command. CI NOT RUN/no URL.

## Focused camera execution — 12 PASS, 1 FAIL

Ran the unified runner with `--no-build --filter '^(ecs_camera|camera_(scheduler|rt_pool|rig|temporal|layout|scene_asset|capture_views|capture_graph|oblique|png|frustum|hdr_resolve))$'` and the same KINGSTON build dir/env. Report `20261120-142751-75133`. Source HEAD `0a552ea5f`; selected binaries compiled during prior passes and unchanged at this HEAD. 12/13 PASS; camera_rig FAIL (`blend on camera switch`).

The fixture changed local `second.priority` after copying it into `candidates`;
CameraDirector correctly continued selecting the unchanged high-priority entry.
Fix `329f65c89aa6303e7168b4225bc0eb8b043c6b8e` changes the actual candidate snapshot instead; production
CameraDirector is untouched. Test source only, new binary/retest pending.
Full build of `0a552ea5f` is separately running; this fixture was already built
before the edit and needs a later explicit rebuild. No GPU parity or full-suite
pass claim. Next: complete build and rerun this fixture.

## Pass 5 — animation budget public ECS API

Full quick runner at `0a552ea5f` configured successfully but failed to build
AnimationBudgetPolicy's private World::poolIfExists call; CTest NOT RUN.
KINGSTON report `20261120-142736-74339`. Fix `bc7c34f7d2054a0a3e8d686a13c6368d8a5fb8a0` uses
countExisting and eachExistingConst instead, preserving read-only significance
queries and prepared budget writes. Only AnimationBudgetPolicy.h changed; no
save format or public API change. Branch codex/lens-completion. CI NOT RUN.
Next: repeat full quick runner on KINGSTON, including rebuilt camera_rig.
Terrain GPU parity, transient budget and complete owner rollback remain open.

## Pass 6 — MotionWeave queries and epoch contracts

Quick runner at `05741cdac`: configure PASS, build FAIL, CTest NOT RUN.
KINGSTON report `20261120-143539-92785`. Commit `f0c227f4e866a9dddb4c8c6ded7f115314794d7e` fixes
remaining private pool queries in MotionWeaveScheduler and AnimationEventDispatch
using public const queries, and updates terrain/sprite compile-only API fixtures
for the existing expectedEpoch argument. No production signature/save change.
Branch codex/lens-completion; CI NOT RUN. Camera rig binary rebuilt this pass,
retest pending. Next: repeat full build on KINGSTON; same feature gaps remain.

## Pass 7 — public animation headers

At `a8e3cb6e9`, focused camera runner with the same 13-test filter: **13/13 PASS**,
KINGSTON report `20261120-143716-97866`, including rebuilt camera_rig.
Full quick runner report `20261120-143706-97258`: configure PASS, build FAIL
in HumanoidRetarget's missing findNodePath declaration; full CTest NOT RUN.
Commit `daff707930b69d0e2eb2329b09eb75f8752c49f9` explicitly includes ModelClips.h in HumanoidRetarget.h
and AnimationAssetLoader.h (same dependency), without API/format changes.
Branch codex/lens-completion, CI NOT RUN. Next: rerun native build on KINGSTON;
new terrain tests and real GPU checks remain pending.

## Pass 8 — root motion tool header

Quick runner `6f15a0e59`, KINGSTON report `20261120-143832-1521`:
configure PASS, build FAIL at tools/animation/root_motion_bake.cpp missing
findNodePath declaration. Humanoid/ragdoll targets now compile. Full CTest
NOT RUN. Commit `8aec846519fa902f5e1932493fa57f84ca13ed99` includes ModelClips.h in that tool; no
API/format change. Branch codex/lens-completion; CI NOT RUN. Next: repeat
native build on KINGSTON. Feature and GPU validation gaps remain as above.

## Pass 9 — View shader graph linkage

Quick runner at `4403028dc`, report `20261120-144000-4957` on KINGSTON:
configure PASS, build FAIL linking three appearance fixtures (graph::compile).
Full CTest NOT RUN. Commit `5effe65ed6dd2f2f90e15a90f45c700ded2b8c61` adds aiverse_shader_graph to
the public aiverse_view INTERFACE because its inline appearance authoring API
requires it. CMakeLists.txt only; no runtime/save change. Branch codex/lens-completion;
CI NOT RUN. Next: repeat native build on KINGSTON; retain all open feature gates.

## Pass 10 — fixture contracts

At `c7241947d`, KINGSTON report `20261120-144146-9957`: three camera
model-snapshot/terrain/sprite prepared API tests PASS. Report
`20261120-144208-11202`: animation_budget_policy and animation_humanoid_retarget
PASS, animation_ragdoll_asset FAIL (fixture omitted Neck but expected Chest body).
Full quick runner `20261120-144131-9314`: configure PASS, build FAIL at
model_image_transaction stub's obsolete setIndexBuffer signature; CTest NOT RUN.
Commit `0dc0b409b3611f97a48540b469f17e1e73dd4686` updates that stub and gives ragdoll fixture an explicit Neck,
plus sparse-avatar checks (2 bodies, no invalid joints). Production unchanged;
branch codex/lens-completion; CI NOT RUN. Next: rebuild/retest on KINGSTON.

## Pass 11 — hybrid fixture syntax

Quick runner at `773ef2ea9`, KINGSTON report `20261120-144337-14469`:
configure PASS, build FAIL at motionweave_hybrid.cpp missing Json::parse close
parenthesis; CTest NOT RUN. Commit `7b376345c82ac078a2f107f8500ef629ee93f7de` fixes only that syntax.
Branch codex/lens-completion; no API/save change; CI NOT RUN. Next: repeat
full quick runner with MAKEFLAGS=-k to collect independent compiler failures
in one pass; outputs still exclusively on KINGSTON.
