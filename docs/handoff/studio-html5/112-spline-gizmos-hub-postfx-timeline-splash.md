# 112 — Spline node gizmos, brush chips, Project Hub, PostFX panel, Animation timeline, project splash with engine sky

Branch `feature/studio-html5`. Follows 111 (pushed as `fb1455979`).

## What changed
- **Road and river nodes in the Scene.** The "Edit nodes" toggle in the brush bar (`studio.splineEdit {kind}`) shows handles for road nodes and river points. Dragging a handle moves it; a drag is one History step ("Move road node" / "Move river point"). The handles are drawn by the Overlay2D pass (`StudioMetalScene::drawBrushCursor`). Handle ids are `road:<id>` and `water:<numeric body id>:<i>`.
- **Brush options as chips.** The brush bar now uses chips instead of `<select>`, because a background click can't open a native popup. Sensible defaults are picked when the bar opens.
- **Extension gizmo labels.** Lua `gizmos` can draw `label` shapes, using the 6x11 bitmap font (`mac/StudioFont6x11.h`). The example extension marks the map centre.
- **Project Hub** (`components/ProjectHub.tsx`):
  - Recent projects, with temp dirs filtered out (`studio.hub`), a templates grid, and Create… (`studio.hubCreate`).
  - Shown inline when no project is open, and as a modal from File ▸ Project Hub….
- **PostFX panel** (`panels/PostFxPanel.tsx`):
  - Game or map scope; effect cards with an eye toggle, a minQuality control, parameter sliders and add chips.
  - Presets and cost per device.
  - "All operations" falls back to the generic ToolPanel.
- **Animation timeline** (`panels/AnimationTimeline.tsx`):
  - Model and clip chips, play and scrub, key diamonds per channel.
  - Skeleton preview from the front and the side via `animation_sample`.
- **Project splash** (`components/ProjectSplash.tsx`), shown after the hub while a project opens:
  - Background: the game's `game.json` splash art if it has one; otherwise an **engine render of the PhysicalSky**, picked by the project name (`pickSky`).
  - Logo, name, path · module · maps, and staged progress: Project → Rules and map → Scene models n/m → Ready.
  - Also: elapsed time, rotating tips, "Continue while loading", and "Back to hub" on an error.
  - The brand text is "AIVERSE" only.
- **Sky renders.** `build/claude/studio-sky-shots.command` builds Studio and runs it with `AIVERSE_STUDIO_SKY_SHOTS=<dir>`. `SkyShotRunner` (Main.mm) then renders 6 stages in its own 1600x900 window (`StudioMetalScene::setSkyShot`: real PhysicalSky with clouds and god rays, sun elevation, azimuth and colour; the camera sits 320 u above the map and looks up). Five were chosen (dawn, golden, day, dusk, storm), cropped above the horizon band, and stored as `apps/studio-web/src/assets/splash/sky-*.jpg` (~240 KB in total). Note: in PhysicalSky, a lower `coverage` means more clouds.
- **Startup events are sticky.** `bridge.ts` replays the last `project.splash` / `project.loading` event to listeners added within 4 s. The startup project opens on `didFinishNavigation`, before React has subscribed, so without this the splash was missed.
- **Automation:** `AIVERSE_STUDIO_SPLASH_HOLD=<s>` keeps the splash up (`holdMs` in the payload). `build/claude/studio-relaunch.command` relaunches the built app on the private lua_lab copy with a 25 s hold.

## Verified
- Session contract PASS; web tests 125/125 (`npm run test:web`); `tsc` clean.
- On screen (private copy `studio-html5-verify/qa-build-lab`): the startup splash over the dawn sky with staged progress; the hub modal; Frame Capture; Keep play changes.
- Still to check on screen: dragging spline nodes, the PostFX panel, and the animation timeline with a rigged model.

## Windows
The Studio host is macOS-only: `mac/Main.mm` (Cocoa + WKWebView), `StudioAssetSchemeHandler.mm`, and the Metal device in `StudioMetalScene` / `StudioGameSurface`. These parts are portable:
- `StudioSession`;
- the web UI;
- `StudioMetalScene`'s renderer code, which only needs a different `gfx::Device`;
- the engine's Vulkan backend.

A Windows build therefore needs a new host, `apps/studio-native/win/`:
- WebView2, with `chrome.webview.postMessage` and `__aiverseReceive` as the bridge, and a virtual-host mapping for `studio-web`;
- a Vulkan child HWND for the Scene and Game views, plus `gfx::createVulkanDevice(HWND)`;
- Win32 file dialogs and a `windows-msvc` CMake target.

This is a separate stage. The ImGui editor already builds with the `windows-msvc` preset.
