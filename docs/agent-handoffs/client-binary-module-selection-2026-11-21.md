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
