# Studio viewport: scene cache, idle frames, debounced lighting, optimized build

Date: 2026-11-21. Branch `feature/aiv20261121_studio-render-perf` (from `feature/studio-html5`).

## Problem

The Studio viewport was slow while the shared renderer itself is fast (Sponza on Metal:
about 2 ms GPU, docs/benchmarks/sponza-m3-ultra-2026-11-11.md). The cost was in the
Studio host around it:

- `tools/studio/verify_mac.sh` built the working Studio as `Debug` (-O0);
- `StudioMetalScene::frame` rebuilt the whole document scene 60 times a second:
  `studioDocumentProxies`, `syncDocumentModels` (5+ filesystem calls per entity with a
  model, on an external disk), lights, lighting settings, NEREID manifest, a per-proxy
  linear search over model instances and per-entity material resolution;
- every document edit rebuilt lighting/GI (`waitForResourceIdle` + GI snapshot);
- every tick was drawn even when nothing changed.

## Changes

- `verify_mac.sh`: `STUDIO_BUILD_TYPE` (default `RelWithDebInfo`); `STUDIO_BUILD_TYPE=Debug`
  for a debugger session. The first build after the change is a full rebuild.
- Scene cache (`SceneCacheKey`, `proxies_`, `proxyDraws_`): rebuilt when the document
  revision, project, rules object, FPS flag or level height change, every frame while
  models stream in, and otherwise once a second (hot reload of model files and
  `materials.json`, plus document changes that did not bump the revision).
- `syncDocumentModels`: one filesystem check per unique model path per pass.
- Lighting rebuilds in the interactive viewport: immediately for a single edit, at most
  every 0.3 s during an edit burst and once more 0.2 s after the last edit. Forced
  rebuilds (mode switch, models finished loading, terrain height) stay immediate.
  A document edit no longer forces the lighting reset from the terrain block.
- Idle frame skipping in the interactive viewport: a frame is drawn when the frame
  signature changes (size, document revision, camera, selection, hover, hidden ids,
  tool, drag, marquee, wind, lighting/terrain revision, loaded models), while something
  animates (tween, model streaming, pending lighting, NEREID water, wind on rigged
  models, clouds, GI converging), for 30 settle frames after a change, and once a second.
  `captureNextFrame` and the material/facial previews force the next map frame.
- `StudioMetalScene::setContinuousRedraw(true)` is the default (tests, wind shots,
  automation keep the old behaviour). `Main.mm` turns it off for the Scene viewport;
  `AIVERSE_STUDIO_CONTINUOUS_REDRAW=1` restores drawing every tick.
- Test: `StudioMetalLightingTests.mm` "Idle frame skipping PASS".

## Verification

- `g++ -std=c++20 -fsyntax-only` of `StudioMetalScene.cpp` on Linux: no errors, no new warnings.
- `python3 -m unittest tools/studio/test_build_config.py`: pass.
- Not run here: the macOS build, `ctest -R '^studio_'`, Metal tests and a measured
  before/after. Run `tools/studio/verify_mac.sh` on the Mac and compare with
  Instruments Time Profiler.

## Not done

- Rendering still runs on the main thread from an `NSTimer`; a `CAMetalDisplayLink` or a
  render thread is a separate step.
- `studioMapEntityIndex` for `entity:@id` is still a linear `findEntity`, now only on rebuild.

## Merge into main (2026-11-21)

Merged after the Assets thumbnails, Statistics, Terrain brush and Agent panel work. Their
Scene calls go through the render thread too: `requestThumbnail`, the agent screenshot's
`captureNextFrame` and terrain/placement `groundPoint` use `studioOnScene`; finished
thumbnails and the Scene frame-time statistic travel in the frame outputs
(`publishSceneFrame`); pending thumbnail jobs keep the viewport drawing (`animated`).
