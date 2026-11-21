# Architecture layers — stage 01 handoff (2026-11-21)

Branch `feature/aiv20261121_arch-layers` from main d84e958b1. Split of work with the no-genre branch:
`docs/architecture-layers.md` § Coordination.

## Done
- `tools/layers/check_layers.py` + `tools/layers/allowlist.json` (57 edges: G3 16, G4 3, L1 38), CI job `layers`
  in `.github/workflows/ci.yml` (python only, no build).
- `docs/architecture-layers.md`: layer order, debt ids, rules, coordination.

## Checked and dropped
- Stable ECS component ids: not needed now. Saves, replays and hashes use string ids
  (`engine/sim/PersistentEcsRegistry`); the numeric `componentTypeId` is a per-process pool index; the only
  loop over pools in id order (`World::destroy`) is order-independent; native modules are static OBJECT
  libraries (no DLL copies of the static counter). Revisit only if dynamic modules appear.

## Found for the no-genre work (not changed here)
- Native module build `aiverse_native_module` lives in `modules/rts/native/Build.cmake` — shared
  infrastructure inside a genre host.

## Checks
- `python3 tools/layers/check_layers.py` -> `layers: OK` (run on the user's machine VM).
- Engine build / CTest: NOT RUN (no C++ changed; no cmake in this environment).

## Next (L1, needs the Mac build)
engine/ -> libraries/ edges, by group; each group is its own stage, then `check_layers.py --update`:
1. `engine/world/{Nereid*,TerraWeave*,PhysisNereid*,WorldScene.h}` (31 edges) include `aiverselib/water/*`.
   Either move `aiverselib/water` core (geometry, systems, query index) down to `engine/world/nereid/` with
   forwarding headers in `aiverselib/water/`, or move these bridges up to `libraries/`. Coordinate with
   NEREID / TerraWeave owners (AGENTS.md cross-agent ownership).
2. `engine/view/View.h` -> MotionWeave / animation budget (3): same choice with the MotionWeave owner.
3. `engine/platform/*Input*` -> `aiverselib/gameplay/InputFlow.h` (4): move InputFlow core to `engine/platform`.
4. `engine/meta`, `engine/render/graph` -> `aiverselib/Graphs.h`, `Loot.h`; `engine/net` -> `SnapshotDelta.h` (4).
Then: split `engine/render` into CMake sub-targets; shrink AGENTS.md after no-genre is merged into main.
