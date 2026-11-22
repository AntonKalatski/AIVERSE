# LENS camera behaviours, stage B — 2026-11-22

Branch `feature/aiv20261122_camera_presets` (based on `feature/aiv20261121_no-genre` merged with main).

## What changed
- `engine/render/CameraBehavior.h` is the one home of view camera bodies:
  overhead (`CameraOverhead`, `overheadCameraFrom`, `applyOverheadCamera`; was `CameraSetup` in `CameraModes.h`),
  follow (`CameraFollowSettings`, `readCameraFollow`, `finishCameraFollow`, `CameraFollow`, `cameraClipPlanes`,
  `cameraFogScale`; was the camera half of `TopDownControls`), chase (`CameraChaseSettings`, `cameraChaseFrom`,
  `chaseCamera`), first person (`CameraLook`, `CameraLookSettings`, `cameraLookFrom`; was `render::Look`).
- `engine/render/CameraPresets.h`: named presets as JSON data; every block accepts `"preset"` (`resolveCameraPreset`).
- `CameraModes.h` deleted; `TopDownControls` keeps input only and drives `render::CameraFollow`.
- ECS: `CameraRigMode::Chase` / `FirstPerson` appended; `CameraRigEvaluator` runs `chaseCamera` math on the target's
  WorldTransform heading. Studio Cameras combo and the scene asset mode clamp know the new modes.
- FPS / actor client reads rules / map `"camera": {"chase": {...}, "firstPerson": {...}}` (map over rules).
- `lib_camera` (0.2.0): removed the unused genre-named `Mode`, `Input`, `updateTarget`, `strategicIcons`.
- Docs: `docs/camera-behaviors.md` (+ RU), links from camera-system / topdown / fps / game-format, site docs entry.

## Not done here (next stages)
- Planet camera (`engine/view/game/GamePlanetCamera.h`) is still its own body; fold it in as an "orbit-sphere"
  body when the planet view is next touched.
- Follow modifiers (look-ahead, speed zoom, bounds) are not yet ECS components; ECS rigs get Chase / FirstPerson only.
- Studio / MCP: no preset picker yet (stage C).

## Verification
See the stage log appended below (Mac build, ctest, frames).

## Stage log
- Mac (KINGSTON `AIVERSE-builds/camera-presets`, Release, tests ON) at merge with main `c09e2a5cb`: build OK;
  ctest 431/434 pass. Failures were no-genre leftovers fixed in `6d58a2f33` (skill catalog regenerated,
  `modules/fps` -> `engine/actor` in skills, `gen/NativeRegistry.cpp` in `tests/native_project_build.py`);
  `native_project_build` not re-run on Mac after the fix. `camera_behavior` and `aiverse_tests` (top-down camera
  tests) pass. One earlier ctest run had an `aiverse_tests` SegFault that did not repeat on the next full run
  (two runs overlapped in the same build dir) — watch for it.
- Pre-existing build breaks fixed by the main merge: `TerraWeaveTerrainRuntime.h` (`patch`), `RTSCamera` in tests.
- Frames: `AIVERSE-builds/camera-presets/shots` — skirmish with overhead / overhead-isometric / overhead-flat,
  topdown_arena with follow / follow-overhead / follow-telephoto; only the preset line differs. The telephoto preset
  frames a small arena far off-centre (it is meant for large maps).
- Devlog entry `2026-11-22-lens-camera-presets` (in-progress), LENS roadmap item (in-progress) + planned next step.
- After merging the architecture branch again (`local/no-genre` a8dc77891, "one gameplay host", not yet pushed by
  its owner — fetched from the Mac repo) at `462a1d715`: Mac build OK, ctest 434/434 pass (lens_metal_frame skipped
  without AIVERSE_LENS_METAL_VALIDATE). The TerraWeave sparse editor test was moved to the world-space scene
  document (`MapData::worldSpace` + `extra`), rebuilt and passed with camera_behavior and aiverse_tests.

## Stage B, finish (2026-11-22)
- `CameraFollowComponent` (sim) + cameras.json `"follow"` block (`cameraFollowFromJson` / `cameraFollowJson`,
  presets, `"bounds": [minX, minZ, maxX, maxZ]`): a Follow rig with it runs `render::CameraFollow` in
  `CameraRigEvaluator` (velocity lead only, focus height kept). Test: `camera_behavior` (lead, bounds, round trip).
- `render::CameraSphereOrbit` + `SphereViewFrame` (`engine/render/CameraSphereOrbit.h`): the planet camera math;
  `game::PlanetCamera` is a thin host (body, centres, extent). `PlanetViewFrame` renamed everywhere.
- Presets helpers moved to `CameraPresets.h` (`cameraPresets`, `cameraPresetNames`, `resolveCameraPreset`,
  `cameraPresetBody` by name prefix: overhead / follow / chase / first-person).
- MCP `lens_camera_presets` (six LENS tools now); Studio cameras window "Follow Body" with preset apply.
- Mac (merged with main `02f9337af`): build OK, ctest 433/434 then `lens_ops` fixed (tool count, ok flag) and
  re-run with camera_behavior: pass. Planet frame `shots/pl-orbit` checked by eye.
- Remaining: ECS chase / first-person still read rig fields only (no cameras.json "chase" block); no preset picker
  for game.json camera blocks in the editor UI.
