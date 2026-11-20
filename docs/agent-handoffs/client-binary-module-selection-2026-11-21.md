# Client binary module selection — 2026-11-21

Branch: `feature/studio-html5`. Implementation commit: `09201bb4d`.

## Scope

`CMakeLists.txt`: `aiverse_client_runtime` archives the existing `aiverse_runtime`
objects. Packaged `aiverse_client` and Android `main` link this archive, so the
linker extracts only referenced runtime translation units. Editor and test
targets retain their previous direct OBJECT runtime linkage. The macOS client
also links with `-dead_strip`, removing unreachable code from directly linked
objects. No public API, content format, saved data, or runtime behaviour was
intentionally changed. Existing client feature sources remain direct inputs.

## Verification actually run

- `cmake -S . -B /private/tmp/aiverse-client-modules-check -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_TOOLS=OFF -DAIVERSE_BUILD_TESTS=OFF -DAIVERSE_BUILD_HEADLESS=OFF -DAIVERSE_BUILD_SERVER=OFF -DAIVERSE_BUILD_STRATA=OFF -DAIVERSE_VULKAN=OFF -DAIVERSE_RUNTIME_AUTOLOD=OFF`: PASS (macOS, AppleClang, diagnostic non-Release configuration).
- `cmake --build /private/tmp/aiverse-client-modules-check --target aiverse_client -j 6`: PASS; repeated after adding `-dead_strip`: PASS.
- Re-linked the same compiled objects with the previous direct runtime OBJECT linkage into `/private/tmp/aiverse-client-full-runtime`: PASS. Baseline executable 86,847,912 bytes; new executable 80,871,688 bytes. Difference 5,976,224 bytes (about 6.9%). This comparison includes archive selection and macOS dead stripping. A separate archive-only link measured 86,809,456 bytes.
- `git diff --check`: PASS (line-ending warnings only).

## Limits and next step

This stage reduces the final client binary, not compilation work. The large
`apps/client/Client*.cpp` source set remains directly linked and `GameClient`
currently references many features. Dynamic registrations and configuration
driven behaviour need explicit roots before further code removal. Emscripten,
Android, Windows, Linux, release optimisation, runtime smoke tests, and CI were
NOT RUN. Emscripten tools were absent from PATH. No CI URL exists for this local
stage.

Next: inspect the client feature source graph and introduce explicit per-game
module declarations for capabilities selected by data, retaining a safe default
for older games. Verify an actual HTML5 Release build and compare `index.wasm`
before and after, then validate startup and representative game modes.

## 2026-11-21 continuation: explicit packaged modules

Branch `feature/studio-html5`; implementation commit `6223f33c2`.
Affected paths: `tools/aiversebuild/main.cpp`,
`platform/android/template/app/build.gradle`, `docs/overview.md`.
`packaging.engineModules` (or a target-specific override) now selects the
existing optional CMake modules `strata`, `runtime-autolod`, and `vulkan` for
packaged clients. An absent field preserves legacy defaults. An explicit empty
array sets all three OFF. Invalid names and Vulkan on unsupported targets fail
before building. Web/macOS/iOS/Windows/Linux CMake invocations, Windows source
export, and Android Gradle project generation receive the selected flags.
Packaged builds also set `AIVERSE_BUILD_FPS_CLIENT=OFF` because that separate
reference executable is not shipped with the game. No runtime API/save format
changed. Core GameClient and its direct source files were not made selectable.

Actual checks: `cmake --build build/studio-html5-verify --target aiversebuild -j 6`
PASS. `aiversebuild --project /private/tmp/aiverse-module-fixture --target web
--check-engine-modules --out /private/tmp/aiverse-module-output` PASS, printing
all three OFF for an empty list. The same command for Windows PASS, printing
Vulkan ON from the per-target override. `templates/skirmish` with no list PASS,
printing `legacy defaults`. An unknown module fixture exits 1 with the intended
error. `git diff --check` PASS (line-ending warnings only). Android APK, iOS
export, Windows/Linux client binaries, actual WebAssembly compile, browser
startup and size comparison NOT RUN. CI: no URL or status for this local stage.

Risk: CMake's existing cache values are intentionally overridden by explicit
flags on every packaged invocation; manually building an exported source tree
without its generated build command may use CMake defaults. Games relying on
runtime-generated LODs must include `runtime-autolod`. Next: decouple the
remaining GameClient feature translation units and make their dynamic roots
explicit, then measure an HTML5 Release `index.wasm` with Emscripten and test
representative game modes. Do not claim full per-game module pruning yet.

## 2026-11-21 continuation: selectable Spine client module

Branch `feature/studio-html5`; implementation commit `bd525329d`.
Affected paths: `CMakeLists.txt`, `apps/client/ClientSpine.h`,
`ClientSpineDisabled.cpp`, `ClientScript.cpp`, `tools/aiversebuild/main.cpp`,
`docs/overview.md`. `spine` is now a fourth explicit
`packaging.engineModules` name. Without an explicit list the previous Spine
client remains enabled. With an explicit list omitting `spine`, packaged
clients use a small adapter, omit `SpineRender.cpp`, `ClientSpine.cpp` and the
`aiverse_spine` library, and do not register the `spine` UI Lua table. The
adapter reports attempted creation as unavailable. The packager rejects a
nonempty `game.json.spine` declaration unless the module is selected.
Script-only use must be declared by the game author. The editor's hosted
client always retains Spine. No saved data or public content format changed
apart from the optional packaging field.

Actual checks: CMake configure with `-DAIVERSE_CLIENT_WITH_SPINE=OFF` PASS;
`cmake --build /private/tmp/aiverse-client-modules-check --target aiverse_client
-j 6` PASS both OFF and ON. macOS diagnostic executable with Spine ON:
80,871,688 bytes; OFF: 79,962,480 bytes, saving 909,208 bytes on that build.
The direct-source-to-archive experiment using the same 23 client objects
linked successfully but saved zero bytes, confirming that a mere archive
conversion cannot prune these referenced features. `cmake --build
build/studio-html5-verify --target aiversebuild -j 6` PASS. `--check-engine-modules`
with empty list prints Spine OFF; a fixture with `game.json.spine` but no
selected module exits 1 with the intended error; adding `spine` prints ON.
`git diff --check` PASS (line-ending warnings only). WebAssembly, Android,
Windows, Linux, iOS, browser startup, runtime UI visual parity and CI NOT RUN.
Emscripten remains absent (`aiversebuild --check-toolchain web --json` reported
missing `emsdk`); no CI URL exists for this stage.

Next: isolate other optional GameClient features behind explicit build
boundaries and content validation, one subsystem at a time. Then run real
WebAssembly Release size and startup comparisons when Emscripten is available.
Do not describe the remaining monolithic client as fully pruned.
