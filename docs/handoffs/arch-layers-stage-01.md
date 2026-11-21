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

## Stage 02 (2026-11-21)
- 2e88a870e: InputFlow core -> `engine/platform/input/` (4 L1 edges).
- d2980fa3c: `Graphs.h` -> `engine/core/`, `Progression.h`/`Loot.h` -> `engine/core/progression/`,
  `SnapshotDelta.h` -> `engine/net/` (4 L1 edges); meta package pin hashes the moved files.
- Old `aiverselib/...` paths are forwarding headers; namespaces, CMake targets, catalogs unchanged.
- Checks: g++ -fsyntax-only of moved headers, forwarders and engine users: PASS (Linux VM).
  Mac build `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/arch-layers`: NOT RUN yet
  (launcher `_tmp/build_arch_layers.command`, log `/Volumes/KINGSTON/AIVERSE-builds/arch-layers-tests.log`).
- Worktree note: a worktree created from the VM is registered with a VM path and gets removed by
  `git worktree prune` on the Mac. This worktree now uses a relative `.git` file and the Mac path in
  `.git/worktrees/arch-layers/gitdir`.

## Remaining L1 (30 edges) — needs owner decision, not changed
- 27: `engine/world/{Nereid*,TerraWeave*,PhysisNereid*,WorldScene.h}` -> `aiverselib/water/*`. NEREID core
  (23 headers, 3.2k lines) depends only on `engine/sim`/`engine/core`. Options: (a) NEREID core moves to
  `engine/world/nereid/` with forwarders (NEREID owner); (b) the bridges move up to `libraries/` (Terra owner).
- 3: `engine/view/View.h` -> MotionWeave / animation budget, which include `engine/render` and ECS. Option:
  move MotionWeave up into `engine/animation/` or move the View bridge into `libraries/` (MotionWeave owner).

## Stage 03 (2026-11-21, owner asked to finish L1)
- NEREID core (25 headers) -> `engine/world/nereid/` (27 edges); note in `docs/nereid/HANDOFF.md`, AGENTS ownership row.
- MotionWeave core (12 headers: MotionWeave, EcsAnimationSystems, schedulers, montage, playback, pose blend,
  inertialization, budget/work policies, source bridge) -> `engine/animation/` (3 edges);
  `tools/animation/check_motionweave_dependencies.py` also scans `engine/animation`: PASS.
- L1 is closed; allowlist = 19 genre-host edges (G3 16, G4 3) owned by the no-genre work.
- g++ -fsyntax-only: all moved headers and forwarders PASS except pre-existing failures that also fail on
  the parent commit (gcc size_t lambda in TerraWeaveForestEcology.h; box3d include path not given).
