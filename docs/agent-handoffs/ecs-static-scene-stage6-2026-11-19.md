# AIVERSE Stage 6 — immutable prefab authoring, editor Inspector, RSS

Date: 2026-11-19  
Repository: `chromealex/AIVERSE`  
Feature branch: `feature/ecs-static-prefab-scene-integration`  
Parent handoffs:
- `docs/agent-handoffs/ecs-static-scene-stage4-2026-11-19.md`
- `docs/agent-handoffs/ecs-static-scene-stage5-2026-11-19.md`

**Status:** Planned Stage 6 source and documentation committed.
**No Stage 5 or Stage 6 native compilation/CTest/RSS results were
available in this turn.** The user's last reported passing integration
result (Stage 4) was **7/7 targeted CTests Passed**. Do not conflate
that result with code introduced in Stages 5/6.

## Request and execution policy

The user explicitly requested that **all feature work be completed
first, followed by one single comprehensive native test pass**, rather
than piecemeal build/test loops after every change. This handoff
supersedes the older smoke/extended-first validation instructions.

The assistant can commit code and docs through a GitHub integration,
but **cannot access the user's Mac Studio build directory or execute
native CTest directly**. No direct GitHub Actions workflow-dispatch
capability was available to start a full run in this conversation.

Use this ONE final command in the EXISTING isolated Mac Studio worktree
after updating the branch. This script configures, builds all default
native targets, runs the complete registered CTest suite once, and then
samples OS resident bytes in separate benchmark processes:

```sh
cd ~/Projects/AIVERSE-static-scene
git fetch origin feature/ecs-static-prefab-scene-integration
git pull --ff-only origin feature/ecs-static-prefab-scene-integration
git rev-parse HEAD
bash tools/ecs-static-scene-validate.sh build/static-scene all 8 1000000
```

Log artifacts:
- `build/static-scene/verification/ecs-static-scene-all.log` — Git
  head, CMake configure, full build and entire serial CTest results.
- `build/static-scene/verification/ecs-static-scene-rss.jsonl` —
  per-process Ordinary/Shared/Static memory+latency samples for
  unique counts 16/64/1024 and 1M entities.

A nonzero compiler/CTest/benchmark checksum exit causes the script
to fail. Never mark the suite green until the user provides the actual
full native run output. Fix any compiler/test failures in this feature
branch and update this handoff; if a fix is necessary, a new full test
pass may still be required.

## Stage 6 implementation

### 1. Real OS-backed, isolated-process RSS benchmark

`tests/ecs_shared_static_rss_bench.cpp`:

- accepts `ordinary|shared|static [entities] [unique]`;
- creates a production `aiverse::World` and a 128-byte per-entity
  configuration type, with up to 2M entities;
- ordinary mode stores inline config payloads, shared mode interns
  unique values and stores 4-byte `SharedRef<T>`, static mode builds
  sealed `StaticTable<T>` and stores 4-byte `StaticRef<T>`;
- reads two representative fields per entity in an ECS Sparse Set pass;
- outputs one JSON object with OS RSS baseline, after-build RSS, after-read
  RSS, build delta, median CPU read latency, stable read checksum;
- macOS uses Mach task info; Linux uses `/proc/self/statm`; if unsupported
  RSS reports zero explicitly rather than inventing a measurement;
- benchmark modes are separate process invocations to isolate heap
  fragmentation. The script verifies identical checksums and reports
  actual RSS deltas; it does NOT impose unreliable speed thresholds.

Target: `aiverse_ecs_shared_static_rss_bench`, built in CMake.

### 2. Typed immutable prefab authoring JSON reader

`engine/scene/PrefabStaticAuthoring.h`:

- strict version 1 document
  `{"version":1,"domain":"combat/weapon","prefabs":[
       {"id":"prefabs/archer","config":{...}}]}`;
- input limited to 4 MiB / 100k definitions and stable logical keys;
- expected domain must match, version literal must be canonical 1,
  malformed config/duplicates/empty catalog rejected with error text;
- project supplies `Decode(const Json&,T&,std::string&)` to translate
  typed authoring JSON into deterministic native T without implicit
  numeric/field coercion;
- builds and seals a shared `PrefabStaticCatalog<T>` using existing
  canonical ordering and immutable content fingerprinting.
- Explicitly does not mutate an ECS World. Editor/content import tool
  may regenerate a revision, then game host rebinds at a controlled
  migration barrier.

### 3. Read-only native Editor Inspector bridge

`engine/scene/PrefabStaticInspector.h`:

- uses actual `PrefabStaticCatalog<T>` + `StaticRef<T>`, not copies
  of a document placeholder;
- `append(const World&,Entity,Json& components,Json& schemas)`
  emits the existing `NativeModule::inspectComponents` compatible
  JSON shape; each field has `editable:false`;
- shows actual slot, unique count, exact hex catalog/table fingerprints
  and project-supplied typed `const T& -> Json` config fields;
- no raw mutable pointer, global editor writeback registration or
  implicit table mutation. A native game/module opts in by invoking
  the helper from its existing `inspectComponents` callback.
- Editor UI still needs the owning game's typed hook; this library
  supplies the reusable bridge, not magic reflection for arbitrary T.
  Source asset revision changes require explicit reimport/migration.

### 4. New authoring + Inspector correctness CTest

`tests/ecs_static_scene_editor_contract_test.cpp`:
- deterministic catalog when JSON prefab definitions arrive in
  different authoring order;
- duplicate prefab IDs, invalid relative paths, fractional numeric
  fields and mismatched schema/domain rejected;
- identical config values map to one unique static payload;
- same immutable pointer in two independent Worlds;
- Inspector snapshot fields and `editable:false` schema;
- unknown entity does not fabricate a Static component;
- World snapshot/restore retains same immutable value.

Registered as `aiverse_ecs_static_scene_editor_contract_test` in
the existing native ECS test foreach.

### 5. One-shot full validation script

Updated `tools/ecs-static-scene-validate.sh`:

- `all`: configure one CMake Release build, build every default native
  target, run **all registered** CTest cases once (serial for isolation),
  then execute 3 modes × 3 unique-count process RSS samples.
- `smoke`/`extended` are retained for *later debugging only*, not
  required for the user's one-shot final validation.
- prints checked-out Git SHA, branch and status, saves complete log;
- parses resulting JSONL with Python 3 stdlib to verify checksums
  across storage modes; does not claim a particular memory saving
  until an actual process RSS measurement exists;
- no repository branch mutation, no background work or task scheduler.

### 6. Docs and agent rules

Updated `docs/ecs-static-prefab-scene-integration.md`,
`engine/scene/AGENTS.md`, and
`skills/gameplay/aiverse-ecs-shared-static/SKILL.md` with Stage 6
contract, examples and the required single final run.

## Stage 6 commits

- `59926cc8e9acec770bfd1439e1af267bfa82ea7e` — RSS benchmark.
- `19102c137bdee56d52f876afa133cb0abddd24ee` — avoid dummy
  static table allocation in non-static RSS modes.
- `e3d99a5d4d96c73a515f932cadde210d3e0837f2` — JSON authoring.
- `c9268e7bcf6670c5b36e00615644f18feb261966` —
  use public stable scene asset key validator.
- `46989085e84b8bd00b043d11a112e6b9fd4f1e50` —
  read-only runtime editor Inspector snapshots.
- `d6db8e0a4415bc6a21ea8f5c1588af6f8e3f6bda` —
  safe JSON value wrapping in inspector.
- `dd00edf4b81cde386851e67abd1ebe64835cb1d6` —
  new authoring/Inspector CTest fixture.
- `6a7972fd0b9f4de428b83215248086f5311adc99` —
  register editor contract and RSS benchmark targets.
- `19c849a5f8712185b3f02e4aa311a237b913679d` —
  single-command complete build+CTest+RSS runner.
- `3e890e088260ca1ad4254d8eadeeeab55d518ce7` —
  Stage 6 architecture documentation.
- `b8e79daaaee7548e9cf7f38b6c90bff1f66d56d5` —
  scene-scoped AGENTS continuation rules.
- `492a474c00096a7a9ea0e9aa524c46c82617e28e` —
  gameplay agent skill.

This handoff is a later commit. Use `git rev-parse HEAD` after pulling
to identify the exact unverified Stage 6 source snapshot.

## Structural and correctness constraints

- The earlier Stage 5 multi-domain catalog remains unverified natively.
  The full test runner must catch failures in either stage.
- Keep `engine/core/ECS.h` and `Simulation::step()` unchanged.
  No production FPS/RTS gameplay was converted to StaticRef; these
  remain opt-in extensibility APIs.
- Native editor only supports explicitly registered/readable modules.
  `PrefabStaticInspector` does not authorize editing a sealed table.
- Cold deserialize needs the matching sealed catalog revision and
  explicit `validateWorld` before running a tick; the catalog's 64-bit
  stable fingerprint is not a cryptographic content signature.
- Raw `World::add<StaticRef<T>>` can still bypass the high-level
  bind-once wrapper. Guarding all raw ECS structural writes requires
  separate coordinated ECS core work.
- The additive `CollisionStream` static collider ID collision remains
  unresolved. No new GPU/Metal, physics, TerraWeave/PHYSIS/NEREID,
  postFX or editor scene manipulation integration has been validated.
- The one-shot `all` gate covers default project CMake options and
  registered tests, not every optional graphics/platform configuration.
  Any full-build failure must be treated as a failure, not skipped.

## Concrete next step after the one-shot run

When the actual Mac Studio log is available:
1. Record git SHA, CMake compiler/build mode, full CTest pass/fail and
   warnings in this handoff.
2. Record 9 RSS JSONL rows, actual measured resident deltas and
   read-time ratios for 16/64/1024 unique values. Compare with
   previous theoretical payload savings without conflating them.
3. Fix all build/runtime failures in this isolated branch, rerun the
   comprehensive gate if code changes, then review main merge readiness
   with active ECS/renderer/world agents.
