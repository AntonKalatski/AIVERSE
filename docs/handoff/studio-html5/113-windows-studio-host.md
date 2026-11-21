# 113 — Studio on Windows: shared StudioHost, Win32 + WebView2 + Vulkan shell

Branch `feature/studio-html5`. Follows 112.

## What changed

The host was a single macOS file (`mac/Main.mm`, ~2.5k lines: Cocoa + WKWebView + Metal plus all host logic).

### New layout

- **`StudioHost.{h,cpp}`** — portable host core, shared by both shells. It owns:
  - `StudioSession`, the Scene renderer and the hosted Game;
  - the frame tick (agent link, extension gizmos, spline handles, agent screenshots, Stats / Frame Capture history, model streaming progress);
  - the whole bridge dispatch (viewport input, game, hub, open / save / import, thumbnails, `gpuStats`, `keepPlayChanges`, …);
  - project opening plus the splash payload;
  - every Scene operation (select, marquee, move / rotate / scale commit, brushes, spline drag, asset / type drop).

  Images (splash art, Assets thumbnails, agent screenshots) now go through stb. macOS keeps an ImageIO fallback for TIFF / WebP (`decodeImage`).
- **`StudioHostPlatform`** — the interface each shell implements:
  - `evaluateScript`, `createDevice`, `surfaceDrawable`, `placeSurface`;
  - dialogs, background work;
  - cache dir, last project, window requests, log.
- **`StudioScene`** (was `mac/StudioMetalScene`), `StudioGameSurface`, `StudioFacialGpuPreview` and `StudioFont6x11.h` moved out of `mac/`. They take a `gfx::Device` (Metal: CAMetalLayer; Vulkan: HWND) instead of a CAMetalLayer.
- **`mac/Main.mm`** — now a Cocoa shell around the core:
  - the same views, windows, floating panels, drag & drop, sheets and sky / wind shot runners;
  - `MacPlatform` implements the interface.

  Behaviour is meant to be unchanged.
- **`win/Main.cpp`** — the Windows shell.
  - **UI:** a Win32 window with a WebView2 control.
    - The UI is served from `studio-web/` next to the exe as `https://studio.aiverse/`.
    - A script injected at document creation maps `window.webkit.messageHandlers.aiverse` to `chrome.webview.postMessage`, so the web build is identical on both platforms.
    - Only that origin, or `--dev` with `http://127.0.0.1:4173/`, may post messages or navigate.
  - **Scene and Game views:** two child windows (Vulkan swapchains) placed over the DOM rects, above the WebView like the macOS Metal views. HTML overlays hide them the same way (`viewportRect`).
  - **Input:**
    - Scene: `StudioHost::sceneMouse` / `sceneKey` (the Unity mapping, with Ctrl as Cmd). The Mac view keeps its own copy of that mapping, so the two must be kept in sync.
    - Game: `platform::Event`.
  - **Native features:**
    - IFileDialog for Open Project (folder), Save Map, Import, Create from template;
    - MessageBox for discard prompts;
    - Explorer `/select` for Reveal;
    - registry `HKCU\Software\AIVERSE\Studio\LastProject`;
    - `%LOCALAPPDATA%\AIVERSE\Studio\studio.log` and the `WebView2` profile.
  - **Manifest** (`win/AiverseStudio.manifest`): per-monitor DPI v2, UTF-8 active code page (std::string paths are UTF-8), long paths.
- **CMake** (`win/StudioWindows.cmake`):
  - `AiverseStudio.exe`, with `WebView2Loader.dll` and `studio-web/` copied next to it on every build.
  - WebView2 SDK: NuGet package (pinned SHA-256), or `AIVERSE_WEBVIEW2_DIR`.
  - SQLite: MSVC links Windows' `winsqlite3`; MinGW builds the amalgamation (`AIVERSE_SQLITE_AMALGAMATION_DIR` or a download).
  - The root option `AIVERSE_BUILD_STUDIO_HTML5` defaults ON on Windows too once `apps/studio-web/dist` exists.
- **Portability fixes:**
  - The POSIX `openat` / `renameat` / `fsync` staged writes in `StudioFacialRigDocument.h`, `StudioHumanLookDocument.h`, `StudioHumanCharacterBuild.h` and `engine/character/vivid/HumanCreatorWorkbench.h` got `_WIN32` branches. They use the new `engine/platform/StagedWrite.h`: exclusive create, `_commit`, rename-over, no symlinked folders. The POSIX code is untouched.
  - `StudioAssetTransfers.cpp`: string + `path::preferred_separator`.
  - GCC: `StudioScene::setTransformTool` / `clearTransformPreview` moved out of line (nested-type NSDMI).
- **Bug fix (both platforms):** the extension-gizmo loop iterated `session.extensionGizmos().items()`, a reference into a temporary that is destroyed before the loop body runs in C++20.
- **Engine fixes (also broken on `main`):** in `engine/gfx/gl/GLApi.h` every Windows engine build failed.
  - The Windows GL function table listed `glFenceSync` / `glClientWaitSync` / `glDeleteSync` twice (a merge leftover): "redefinition".
  - It lacked `GL_FRAMEBUFFER_BINDING`, `GL_MAP_WRITE_BIT`, `GL_MAP_INVALIDATE_RANGE_BIT` and `GL_MAP_UNSYNCHRONIZED_BIT`, which `GfxGL.cpp` now uses.
- **Copying text (user request):** the UI was `user-select: none` everywhere and the Mac app had no Edit menu, so nothing in the Console could be selected or copied (⌘C / ⌘V did nothing anywhere).
  - Console lines are selectable now. ⌘C / Ctrl+C copies the selection, ⌘A / Ctrl+A selects all lines, each line has a copy button on hover, and the toolbar button copies the selection or every shown line, with a short "copied" note.
  - `src/clipboard.ts` `copyText()` falls back to `execCommand('copy')` where `navigator.clipboard` is unavailable (the `aiverse-studio://` origin). Every copy button uses it: Console, Assets, Inspector, Import settings, Character Creator.
  - `mac/Main.mm` installs an Edit menu (Cut / Copy / Paste / Select All, no Undo: Studio owns ⌘Z).
- **No more "Native bridge timeout" (user request):** `bridge.ts` rejected every native call after 8 s, although the host still answered later. Long operations and a busy main thread while models stream in hit it.
  - Calls now wait for their reply. After 30 s a `studio.webDiagnostic` line is written to the host log; the call still resolves when the answer comes.
  - In exchange, `StudioHost::handleMessage` answers every request that has an id with an error when anything throws, so no call is left unanswered.
  - `tests/bridge.test.mjs` updated.
- **Build entry points:**
  - `studio_windows.cmd [project]`: npm build, VS 2022 configure, build, open.
  - CI job `windows-native` in `studio-html5-web.yml`: MSVC build of `aiverse_studio` and `aiverse_studio_session_tests`, the session contract, and the exe + loader + UI uploaded as the `aiverse-studio-windows` artifact.

## Verified

- Linux g++ and MinGW-w64 (cross, x86_64) compile every Studio source:
  - `StudioHost`, `StudioScene`, `StudioSession`, catalog / transfers, facial preview, `win/Main.cpp`, `tests/StudioSessionTests.cpp`.
- MinGW full build of `aiverse_studio` (engine + Studio) in a Linux container. Result: see the end of this file.
- Not verified yet:
  - **macOS:** the Mac build after the refactor (the VM shell on the Mac was down; rebuild with `studio-rebuild-run.command`, then the session contract and an on-screen check).
  - **Windows:** a run on a real Windows machine (WebView2 + Vulkan). The CI job builds it but cannot open a window.

## Not on Windows yet

- Floating panel windows (`studio.windowOpen` answers `{opened:false}`; the tab stays docked).
- Drag & drop from Explorer onto Assets (external drops are disabled so they cannot navigate the UI away). Use Import.
- Assets panel → Scene drag (OLE drop target on the Scene window).
- Device bezel / safe-area overlay on the Game view.
- A GL fallback when there is no Vulkan driver (the Scene reports "Vulkan is required").
- Agent panel: `claude` CLI and `aiversemcp.exe` paths are the same code; untested on Windows.
