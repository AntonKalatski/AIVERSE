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
