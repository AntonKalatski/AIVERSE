---
name: aiverse-release-build
description: Package an AIVERSE game for macOS, iOS, Android, web, Windows or Linux through MCP - branding, splash, toolchain check and install, size estimate, background build and status, honest verification report.
---

# Release build

Reference: `docs/overview.md` "Building for platforms - aiversebuild". Output: `<project>/Builds/<platform>/` with a size report; never the engine checkout's `build/`.

1. Identity: `branding_get` / `branding_set` (name, icon, ids), `set_manifest`; `splash_preview` for the loading screen.
2. `toolchain_check` for the target; `toolchain_install` (or `build_start {installMissing: true}`) for what is missing - Android pulls JDK 17, SDK, NDK, Gradle.
3. `build_estimate` - size per part; shrink big assets before building.
4. `build_start {target, config: Release}` (macos, ios, android, web, windows, linux, all) -> poll `build_status`. Exit code 3 with `RTS_MISSING_TOOLCHAIN` = go back to step 2.
5. Run the result where possible: desktop/web launch to the main menu and one match; phones via the device or simulator when available.
6. Report per platform: built / launched / played / not verified (signing, store upload, devices not tried). Never claim a platform works because it compiled.

Runtime library: none. Workflow over the engine MCP build tools.
