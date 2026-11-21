# No genre split — stage 02 handoff (2026-11-21)

Branch: `feature/aiv20261121_no-genre` (written in the cloud branch `feature/aiv20261121_no-genre-cloud`,
fast-forwarded on the Mac worktree `~/Projects/AIVERSE-no-genre` for the build).

## Code written in this stage
- **Stage 1 / G7.** `libraries/include/aiverselib/genre` moved to `aiverselib/mechanics` with `git mv` (history kept);
  namespace `aiverse::lib::mechanics`, `aiverse::lib::genre` is an alias (`mechanics/Namespace.h`, included by every
  mechanics header); one-line forwarders at every old `<aiverselib/genre/X.h>`; CMake `AIVERSE::mechanics`,
  `aiverse_mechanics_library`, `aiverse_native_use_mechanics` (old names are aliases);
  `aiverse_native_use_bots` takes `COMMANDER` / `COMBATANT` (`RTS` / `FPS` still accepted);
  `examples/libraries/genre` -> `examples/libraries/mechanics`, `docs/genre-packages.md` ->
  `docs/mechanics-packages.md`, `docs/genre-implementation.json` -> `docs/capability-status.json`; references updated
  (history handoffs untouched). Not renamed yet: package ids `bots-rts`, `bots-fps`, `genre-bots`, `genre-kits`,
  CMake `lib_bots_rts` / `lib_bots_fps`, test target names `*genre*`.
- **Stage 2a / G2.** `editor::Project::isFps()` removed. 60 call sites use `worldSpaceMaps()`, 11 `actorSessionPlay()`,
  6 `actorSessionRules()`; `hostId()` exposes the legacy module for G1 only. Behaviour unchanged. Error texts and op
  descriptions no longer name genres. `genreText` -> `engineText`.
- **Stage 5 / G6 (part).** `fps::readConfig` accepts `royale`, `gear`, `crate`, `vehicleTypes`, `bots`, `weapons`,
  `weapon` at the top level of rules; both places at once is an error. Test added to `tests/fps/gear.inc`.
- **Stage 7 / G8.** Checked: kits carry only a display `family`; audit corrected.

## Verification
Run on Mac Studio, commit 4ac342bfc, build dir `/Volumes/KINGSTON/aiverse-build/no-genre`:
`./run-tests.sh --quick --jobs 8 --ctest-jobs 4 --build-dir /Volumes/KINGSTON/aiverse-build/no-genre`.
- Build: PASS, 454 targets (editor, editor core, fps, headless, fps server, voxel, all test executables).
- CTest: 358/359 PASS, including `aiverse_tests` (1052 s), `fps` (top-level `gear` alias test),
  `skill_library_isolation`, `ecs_real_genre_replay_test`, `voxel_*`.
- Only failure: `skill_audit` - `skills/editor/studio-ui/SKILL.md` names the placeholder path
  `docs/handoff/studio-html5/NN-*.md`; identical on origin/main, not touched by this branch.
- Not run in this pass: template checksums `tests/checksums/check.sh` (see next handoff), extra suites
  (`run-tests.sh` without `--quick`), `tests/libraries/run.py`, the native Studio (`apps/studio-native`, Xcode).

## Not started (need build-driven work, one stage per build cycle)
2b one scene document; 3 one vehicle system (`RtsDriving` + `FpsVehicles`); 4 extracting `modules/rts`-only
capabilities; 6 one simulation instead of `game.json` `module` (server arbiter runs only `rts`, headless forks to
`playFps`, modules/genres hosts are test-only).
