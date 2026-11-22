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
