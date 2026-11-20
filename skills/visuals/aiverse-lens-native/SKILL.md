---
name: aiverse-lens-native
description: Integrate existing AIVERSE LENS into a native presentation host, replay a prepared scene into secondary cameras, schedule outputs and record frames with safe GPU lifetimes. Use for native multi-camera integration and LENS capture debugging, not asset authoring or gameplay camera input.
---

# Native LENS integration

Read `docs/camera-system.md` and the latest
`docs/agent-handoffs/lens-native-validation-2026-11-20.md` first.
Author data through `skills/visuals/aiverse-lens/SKILL.md`.
This recipe references existing engine APIs; do not generate a parallel camera
manager, genre-specific renderer or new simulation world.

- `engine/sim/CameraSceneAsset.h` loads version-1 camera authoring data. Update
  SceneHierarchy before reading WorldTransform. GPU handles and capture queues
  belong to presentation, outside deterministic saves and gameplay checksums.
- `engine/render/CameraClientAdapter.h` is the existing client integration.
  Enable lens.enabled explicitly in the project's game.json and provide
  cameras.json with texture outputs only; GameClient owns the screen camera.
  Review `apps/client/GameClient.cpp` for the actual host sequence.
- Custom hosts compose `engine/render/CameraRuntimeHost.h`,
  `engine/render/CameraRenderScheduler.h` and
  `engine/render/CameraRenderPipeline.h`. Retain separate per-camera temporal
  state and declare dependencies. Do not sample the same target being written.
- Update simulation and animation once. Capture a successfully prepared visual
  epoch and replay it using `engine/render/CameraModelSnapshot.h`. Never call a
  stateful draw/update again for each secondary view. Prepare all participating
  owners before publication; failure must preserve last-good state.
- Mesh metadata ownership is distinct from GPU ownership. Retain buffers,
  textures and model owners through submission completion; poll retirement at
  host frame boundaries and shut down after confirmed GPU idle. Camera cuts,
  resize and re-enable must invalidate relevant temporal history.
- Use `engine/render/CameraFrameRecorder.h` for asynchronous readback and PNG.
  Check pending/error state and retain callback owners through completion.
  Native Studio's working example is
  `apps/editor/windows/StudioCamerasWindow.cpp`. Do not promise EXR/video output,
  physical exposure/DOF or stereo from this recorder.

Verification uses the existing CMake build, no new library target. Build and
run camera_scene_asset, camera_scheduler, camera_model_snapshot and lens_ops.
For actual Metal acceptance build aiverse_lens_metal_frame and run:

```sh
AIVERSE_LENS_METAL_VALIDATE=1 ctest --test-dir "$LENS_BUILD" -R '^lens_metal_frame$' --output-on-failure
```

Set LENS_BUILD to the build on KINGSTON and put TMPDIR/module caches there too
when required. A skipped GPU fixture is not acceptance. Check replay parity,
a changed second-camera image, unchanged source epoch, allocation failure/retry
and capture errors. Preserve `tests/render/lens_sprite_gpu_fault_injection.cpp`.
Report only executed platforms; Metal evidence does not establish Vulkan/GLES
parity or a performance gain. Append actual results and exact commit to the
LENS handoff in a separate commit after each completed stage.

Existing-engine reference workflow; no independently installed runtime library.
