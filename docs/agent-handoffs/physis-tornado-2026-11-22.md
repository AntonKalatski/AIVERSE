# PHYSIS tornado (2026-11-22)

**Branch:** `agent-physis-tornado` (worktree `/Volumes/KINGSTON/AIVERSE-builds/worktrees/agent-physis-tornado`),
from `origin/main`, origin/main merged before each stage. Not merged into main.

## Commits

| SHA | What |
|---|---|
| `3c0d43973` | `engine/physis/PhysisTornado.h`, `PhysisTornadoAssets.h`, install/persistence/system, `GameModule::environmentDamage` + RTS override, presentation (`PhysisTornadoCues.h`, frame, FX/audio hosts, runtime, NEREID wind), tests `physis_tornado` / `physis_tornado_cues`, sample `templates/skirmish/maps/tornado_valley.json` |
| `dcada22a3` | docs/physis.md "Tornadoes", PHYSIS skills, AGENTS.md bullet |
| `d8c127456` | ru names of the sample map (fixes `aiverse_tests` i18n_game_data_names), narrower funnel base |

## Verified (user's Mac, AppleClang, arm64 Release, Unix Makefiles, build `/Volumes/KINGSTON/AIVERSE-builds/physis-tornado/mac`)

- `ctest -R '^(aiverse_tests|physis_)'`: **40/40 PASS** (incl. `physis_tornado`, `physis_tornado_cues`, `physis_authoring`, `physis_mcp`).
- Full build with `make -k` + full `ctest`: 459/463 before the i18n fix; failures: `aiverse_tests` (i18n keys of the new
  map, fixed in d8c127456 and re-run green) and 3 **Not Run** whose sources do not compile on current main, untouched
  by this branch: `terraweave_review_fixes` (`TerraWeaveTerrainRuntime.h:281` undeclared `patch`),
  `terraweave_metal_forest` (`render::RTSCamera` renamed), `terraweave_native_micro_compile`.
- Headless `templates/skirmish --map maps/tornado_valley.json`: same seed twice -> same checksum (4793c4fc87b625d5);
  record + `--replay` -> "Replay OK: 7991 ticks"; `--verify-restore-at 300 --verify-restore-ticks 400` -> snapshot,
  serialized and fresh-serialized restore OK. Default skirmish map seeds 1–3 equal `tests/checksums/baseline.txt`.
- Client `AiverseGame --map 2 --spectate` (AIVERSE_CONTENT = template): funnel + debris visible, `tornadoes 1 debris 9..24`
  in `AIVERSE_PHYSIS_DEBUG`, 2 audio voices. Screenshots `/Volumes/KINGSTON/AIVERSE-builds/physis-tornado/shots/*.png`.
- Linux VM g++ 11 (-O0/-O1, hand-linked): `physis_tornado`, `physis_storm`, `physis_nature_presentation`,
  `physis_nature_runtime`, `physis_presentation`, `physis_forces` PASS.

## Not done / not verified

- No Lua API (PHYSIS has none); MCP works through `physis_validate`/`physis_set` (shared parser), not a live spawn.
- Unit damage only in RTS (override of `environmentDamage`); FPS and other modules take none until they override it.
  Damage amount in the sample run was not inspected unit by unit (CTest covers the hook; headless shows a different
  game than the plain valley).
- Debris is a point cloud: no collision with units/buildings, no damage from debris.
- Vegetation breaking relies on NatureStress receivers (not present in the sample; RTS trees are invulnerable).
- Grass bending near the funnel and NEREID water wind were not judged visually; editor nature preview draws no funnel.
- Windows/Linux/Web CI not run; audio not listened to (null audio in captures).

## Next

1. `environmentDamage` in FPS/other modules; Lua `physis.spawn_tornado` once PHYSIS gets a Lua table.
2. Debris impacts through the shared impact/damage package; NatureStress receivers on TerraWeave trees in a sample.
3. Funnel look: project override `fx/physis_tornado.json`, condensation colour by humidity.
