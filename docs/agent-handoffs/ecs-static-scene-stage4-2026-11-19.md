# AIVERSE — StaticPrefabSceneBinding, integrated Stage 4 handoff

Date: 2026-11-19  
Repository: `chromealex/AIVERSE`  
Branch: `feature/ecs-static-prefab-scene-integration`  
Status: **corrected scene resource pipeline CTest passed 1/1 and the complete seven-test integration selection passed 7/7 on Mac Studio (user log, 2026-11-19). Main merge still requires broader ECS/COW/FPS/RTS regressions.**

## Source integration provenance

The integration branch was created from
`feature/scene-runtime-multiworld` at
`61c57988289f0aad157c2f0384cb19ac561077db`.

Merged in `feature/ecs-shared-static-components` at
`58a3680f658a502a378ab4bf4bf5790818a66d5f`.

True 2-parent merge commit:
`f6bd6fc56ca66f8674f0c1f5dee23d11d17a82cb`.
The merge tree retained all shared/static source, test and handoff
files from that branch, plus all SceneRuntime/SceneAsset/resource code
from the scene branch. `CMakeLists.txt` was manually reconciled
after the merge to include BOTH source-branch target lists and the new
integration test.

Both original feature branches and `main` remain untouched.

## Implemented Stage 4

- `engine/scene/PrefabStaticSceneBinding.h`:
  `StaticPrefabSceneBinding<T>` accepts one
  `shared_ptr<const storage::PrefabStaticCatalog<T>>` and optional
  per-instance `SpawnPrefab` callback. It validates bound immutable
  content fingerprints and creates an Entity's single 4-byte
  `StaticRef<T>` via the real `SceneLoadCoordinator::SpawnPrefab`
  type. Missing required prefab rejects the new scene; no duplicate
  static config payload is spawned.
  `MissingPrefabConfig::{Reject,Skip}` controls required/optional
  config domains. `PrefabSceneSpawnChain` composes multiple
  typed config/instance callbacks in deterministic registration order.
  The adapter's callback captures a shared_ptr<const Catalog>
  so no dangling pointer to the temporary binding object exists.
- `tests/ecs_static_scene_integration_test.cpp`:
  real Simulation + SceneRuntime + SceneLoadCoordinator, not a mocked
  scene loop. Tests persistent/additive scenes, parent hierarchy
  local transforms, shared dependencies and release on unload, new
  scene replacement with missing-prefab rollback preserving old
  scenes, two independent Worlds sharing one immutable config
  table and pointer identity, World snapshot COW rollback, native v10
  save/deserialize/checksum, restoration of resource residency without
  respawning configs, and rejecting changed prefab mapping with
  unchanged unique table contents.
- `CMakeLists.txt`: union of scene/ECS native target lists,
  existing shared/static bench, plus new
  `aiverse_ecs_static_scene_integration_test` target and CTest
  `ecs_static_scene_integration_test`.
- `docs/ecs-static-prefab-scene-integration.md`: full API usage,
  lifecycle and native verification contract.
- `engine/scene/AGENTS.md` and
  `skills/gameplay/aiverse-ecs-shared-static/SKILL.md`:
  instructions for the next AI agent.

## Stage 4 Git commit trail

- `f6bd6fc56ca66f8674f0c1f5dee23d11d17a82cb` — actual
  isolated merge with scene and shared/static feature parents.
- `d3f98cbd8745397503a8562f8693af3369dab654` —
  CMake test target union and new integration target.
- `5edfdf0e9176dc587a0ecd00ddd3efe13d640140` —
  StaticPrefabSceneBinding bridge + ordered callback chain.
- `4b807645b119ce469e70d112cf6143f2a42b751e` —
  integration test using real coordinator and dual Worlds.
- `36d4aac09088f8796b2171f3e03a91bf572214ef` —
  remove a placeholder assertion and require real identity checks.
- `0c2c6a527e3454c0fd7343fdc42883da4ad6a64f` —
  detailed integrated architecture documentation.
- `5f63c492eadcbdad67a94d63156551e568f92219` —
  scene-scoped AGENTS instructions.
- `2841622e2a6b2637769c842a0fb57cc1eae03cd5` —
  shared/static skill integration update.
- `f9bdcaddb49353f4819f33a75f75c6bca0f7e4da` —
  first Stage 4 handoff.
- `e02d94aff234d5e49372f30acc63933712c3c843` —
  static source review: add direct `SceneHierarchySystem.h` include
  needed by the integration fixture's `installSceneHierarchy` call.
- `6d7291ca2f2e8584dc2480ee70d0e3f0af781c50` —
  maintain handoff trail after direct include fix.
- `db72e5b123c40b072acef5d1bea87f82b209ecc9` —
  cross-link integrated feature in SceneRuntime documentation.
- `730eb661b6ef3a1f2ddbf9b4c6267432d3496421` —
  cross-link integrated feature in Shared/Static documentation.

The handoff itself follows these commits. Fetch the actual HEAD after
pulling; never report compilation or CTest pass based on committed code.

## Main integration sync — 2026-11-19

After Stage 4 code integration, the target repository's `main` advanced
305 commits beyond the original source-branch base
`d71c6d47e532b25fb816a8bb1f96c41e5275a165`.
We compared changed files before merging. Only two implementation-branch
paths also changed in main:
`tests/ecs_cow_parallel_test.cpp` and
`tests/ecs_cow_snapshot_test.cpp`. The updated `main` uses its own
`tests/TestCheck.h` for valid always-on Release assertions.
We intentionally preserved these newer main versions.

The conflict-resolved integration tree was created from the **exact**
latest main tree at merge time, then overlaying all 32 integration-only
paths including the reconciled `CMakeLists.txt`. The two overlapping
COW test files were not overwritten.

Two-parent merge commit:
`82b4c49753635c23a62beaa4d9ccfa9f4b5e1063`.
Parents: Stage 4 integration
`3c0bd924d9bce57c633e447e40a77adfd7569b04` and main
`58e29ab052458fffcdebebffe27022b53adb9367`.

Post-merge GitHub comparison `main...feature/ecs-static-prefab-scene-integration`:
**ahead 79, behind 0** at the time of the check. Byte-level GitHub
blob comparison verified `tests/ecs_cow_parallel_test.cpp`
is identical to latest main. No source feature-branch/main refs were
moved by this operation, only the isolated integration branch.

**This is a source merge, NOT a native test result.** Its CMake/Metal,
runtime and serialization integration must still be compiled and tested
natively on the user's Mac Studio. The exact current main may advance
again independently while other agents work.

## Existing native evidence (source branches only)

- Shared/static base test: 1/1 CTest passed, Mac Studio AppleClang 17
  Release. 200k entities / 64 unique values 128 bytes: Ordinary
  0.309ms, Shared 0.089ms, Static 0.096ms median microbench.
- Corrected always-on Release COW + hierarchy tests: 4/4 passed,
  1.50s total.
- PrefabStaticCatalog native test: 1/1 passed, 0.59s test,
  0.60s total (the user's log did not include exact checked-out SHA).
- **SceneRuntime / SceneLoadCoordinator / SceneResourceBackend tests on
  their own branch have not been reported as executed.**
- **The combined static-scene integration test and corrected seven-test suite now passed on Mac Studio**. The separately tracked prior failure remains documented below for traceability.
- No full FPS/RTS lockstep parity, cross-platform results or process
  RSS data exist for the combined branch. Do not extrapolate the
  earlier source-branch microbenchmark to full game scenes.

## Native integrated verification — user log, 2026-11-19

Source worktree: `/Users/aleksandrfeer/Projects/AIVERSE-static-scene`.
Build tree: `build/static-scene`.

All seven selected test executables were built and linked. Compiler
emitted two `-Wmissing-field-initializers` warnings in
`engine/scene/SceneResourcePipeline.h` at the two
`Pending{ticket}` constructions.

CTest result (before correction):

| Test | Result | Time |
| --- | --- | ---: |
| `ecs_scene_runtime_test` | Passed | 0.53 s |
| `ecs_multiworld_test` | Passed | 0.39 s |
| `ecs_scene_load_coordinator_test` | Passed | 0.33 s |
| `ecs_scene_resource_pipeline_test` | Failed | 0.33 s |
| `ecs_shared_static_components_test` | Passed | 0.34 s |
| `ecs_prefab_static_catalog_test` | Passed | 0.38 s |
| `ecs_static_scene_integration_test` | Passed | 0.37 s |

Summary: **6/7 passed, 1 failed, 2.68 s total**.

Failure: test line 115 asserted
`resources.leaseCount("render/shared")==2`. Source inspection
confirmed that there are deliberately TWO layers of deduplication:

- `SceneLoadCoordinator::refcounts_` owns one reference per scene
  for the same dependency (`2` for persistent/global and village).
- `SceneResourceBackend::leaseCount` counts leases held by external
  consumers of a unique dependency. One SceneLoadCoordinator leases
  `render/shared` **once** from the backend, no matter how many of its
  scenes reference it (`1`).

No evidence of a production resource leak or double-release was found
in this failure. The test mixed the two counters. Source fixes:

1. `8ed3c16f23821c422b20292f79dc0ea2df3e9f4b`: update four
   shared-resource assertions to require
   `loader.resourceRefCount("render/shared")==2` and
   `resources.leaseCount("render/shared")==1`, and verify the
   transition to one remaining scene then zero references at final unload.
2. `8ebe389cd1844519ed659e6d8bf112bdd0024d76`: add explicit
   `Pending(SceneTicket)` constructor so both ticket creation paths
   initialize remaining fields using defaults without Clang
   `-Wmissing-field-initializers` warnings.
3. `4e78817d9ee0826fa3cd40a0003cf11b594e0d28`: assert
   exactly two publish/retire cycles for `render/shared` across
   rollback invalidation followed by restored scene unload.
4. `39fc2f66e5c4711329fbcae13a1d4f0af9745b1f`: document the
   two-layer lease/reference ownership contract next to
   `SceneResourceBackend::coordinatorAdapter` to prevent future
   tests from confusing unique external leases with scene references.

**Corrected native verification now supplied; see the following 2026-11-19 passing run.** The user
provided no compiler version or exact worktree SHA with this run, so
do not infer either from previous Mac Studio builds.

### Corrected Mac Studio result — 2026-11-19

After pulling the integration branch, the user rebuilt
`aiverse_ecs_scene_resource_pipeline_test`. The compiler and linker
completed successfully and the supplied output showed no warnings
for that target. The targeted CTest completed:

```text
ecs_scene_resource_pipeline_test ... Passed 0.60 sec
100% tests passed, 0 failed out of 1
Total Test time (real) = 0.61 sec
```

Then the full seven-test integration selection was run in
`/Users/aleksandrfeer/Projects/AIVERSE-static-scene/build/static-scene`:

| Native CTest | Result | Time |
| --- | --- | ---: |
| `ecs_scene_runtime_test` | Passed | 0.03 s |
| `ecs_multiworld_test` | Passed | 0.03 s |
| `ecs_scene_load_coordinator_test` | Passed | 0.03 s |
| `ecs_scene_resource_pipeline_test` | Passed | 0.03 s |
| `ecs_shared_static_components_test` | Passed | 0.03 s |
| `ecs_prefab_static_catalog_test` | Passed | 0.03 s |
| `ecs_static_scene_integration_test` | Passed | 0.03 s |

**Aggregate: 7/7 passed, 0 failed, 0.24 s total.**

Documentation commits recording this native evidence:
- `facf69799954444d19fbf0b20c9f1bb231aa1322` — handoff result.
- `89d7b45c248e36d560fe999652520b4d051f7eb7` — integrated architecture status.
- `f338f157e74c4d67399b5256bbb4a7c18aa2993c` — scene agent rules.
- `cc316590c64d4931e7fc59e94d07fda7ce6ae936` — ECS skill status.

This closes the previously failing assertion at
`tests/ecs_scene_resource_pipeline_test.cpp:115` in the test
configuration represented by the supplied logs.
The fix distinguishes one deduplicated backend lease from two
SceneLoadCoordinator scene references; no backend allocation model
was changed. The two `-Wmissing-field-initializers` warnings were
not present in the shown rebuild excerpt after adding the
`Pending(SceneTicket)` constructor. No full compiler log, current
`git rev-parse HEAD`, or CMake cache/compiler identification was
included in the new result; exact local tested SHA/build mode
cannot be independently asserted. The branch before this
documentation change was
`57c0490b446b1b927955406c57883e7d30e23db6`.

**Still unverified:** full native ECS/COW/hierarchy and FPS/RTS
gameplay/replay checks, actual GPU/Metal, physics/collision
streaming, memory RSS and production editor scene workflow.
The seven passes do not establish those integrations.

## Reproducible Stage 4/5 validation on Mac Studio

Stage 4's corrected 7/7 targeted suite was recorded above. The following
commands are for subsequent source changes and broader parity, and have
**not** been run in the present stage. Use the existing isolated worktree:

```sh
cd ~/Projects/AIVERSE-static-scene
git fetch origin feature/ecs-static-prefab-scene-integration
git pull --ff-only origin feature/ecs-static-prefab-scene-integration
git rev-parse HEAD

# Reconfigure to register added tests, then execute the complete
# targeted and broader native ECS/COW/FPS/RTS suites.
bash tools/ecs-static-scene-validate.sh build/static-scene extended 8
```

Use `smoke` instead of `extended` for the narrower Scene/Static/COW
suite during iterative code fixes. The script writes the build/CTest
log under `build/static-scene/verification`.

The Stage 5 handoff `docs/agent-handoffs/ecs-static-scene-stage5-2026-11-19.md`
records later multi-domain implementation and test status. Never
infer that an unrun new test is passing from older Stage 4 results.

## Known boundaries and hazards

- `StaticRef<T>` values remain immutable after binding through the
  public catalog API. Raw `World::add<StaticRef<T>>` is still a
  low-level escape hatch in production Sparse Set; this branch
  does not rewrite core ECS to forbid raw mutation.
- `SceneLoadCoordinator` instantiates gameplay Entities only when
  the game host calls `commit(ticket)` at a deterministic structural
  barrier. Never auto-commit on I/O completion in lockstep networking.
  Reader callbacks capture valid worker-safe content readers.
- If a second config domain is optional for certain prefab keys, use
  `MissingPrefabConfig::Skip` with an ordered spawn chain. A complete
  multi-domain integration fixture and additional dynamic override
  policies are future work.
- Prefab instance `LocalTransform` and `Parent` binary persistence
  are still host-registered separately from the static prefab/scene
  schemas. Never misrepresent the new static integration as having
  migrated all hierarchy serialization.
- Source-branch SceneResourceBackend and WorldScene bridge exist but
  are still native-unverified. The scene branch has a known
  additive `CollisionStream` high-ID conflict when two streams share
  one `SpatialWorld`. Do not bypass the existing collider namespace
  check; coordinate safe deterministic ID management with the physics
  agent.
- The immutable table is external to ECS and is retained by a shared_ptr
  in game/module/callback owners. Scene unloading only removes Entities
  and scene resource leases. Do not unconditionally destroy config table
  when one scene unloads while another World still references it.
- A save binding fingerprint detects a different content revision but
  is a deterministic 64-bit checksum, not cryptographic file integrity.
  Existing SHA256 content manifest verification remains responsible
  for authenticity.
- CMake target union, template interaction and native save semantics
  require actual Mac Studio Release validation before main merge.

## Next implementation priorities

1. Compile integrated tests, fix any actual failures in integration
   branch and document exact results.
2. Full scene + COW + multiworld + FPS/RTS parity and regression tests.
3. Add deterministic multi-domain prefab config composition and
   per-instance mutable override tests.
4. Resolve additive static collision ID namespace with world/physics
   owners; later integrate real TerraWeave/PHYSIS/NEREID/SpectraFX.
5. Complete editor scene tree and preview-world provenance/inspector.
 \
  --output-on-failure
```

After that test passes, rerun all seven and the wider ECS/COW/FPS/RTS
parity suite; preserve actual warning/error output and exact SHA.

## Immediate next agent action: native build and CTest

Use a **new isolated worktree**, so neither main checkout nor other
agents' worktrees are modified:

```sh
cd ~/Projects/AIVERSE
git fetch origin main feature/ecs-static-prefab-scene-integration

git worktree add -b verify-static-scene \
  ../AIVERSE-static-scene \
  origin/feature/ecs-static-prefab-scene-integration

cd ../AIVERSE-static-scene
git pull --ff-only origin feature/ecs-static-prefab-scene-integration
git rev-parse HEAD

cmake -S . -B build/static-scene \
  -DAIVERSE_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build/static-scene --parallel 8 --target \
  aiverse_ecs_static_scene_integration_test \
  aiverse_ecs_prefab_static_catalog_test \
  aiverse_ecs_shared_static_components_test \
  aiverse_ecs_scene_runtime_test \
  aiverse_ecs_scene_load_coordinator_test \
  aiverse_ecs_multiworld_test \
  aiverse_ecs_scene_resource_pipeline_test

ctest --test-dir build/static-scene --output-on-failure \
  -R '^(ecs_static_scene_integration_test|ecs_prefab_static_catalog_test|ecs_shared_static_components_test|ecs_scene_runtime_test|ecs_scene_load_coordinator_test|ecs_multiworld_test|ecs_scene_resource_pipeline_test)$'
```

Run COW/hierarchy then full ECS and RTS/FPS replay/rollback save tests
before offering this as a production merge. After each compiler fix,
commit code to this *integration* branch and update this handoff
with actual CTest results, warnings, exact SHA and next step.

## Known boundaries and hazards

- `StaticRef<T>` values remain immutable after binding through the
  public catalog API. Raw `World::add<StaticRef<T>>` is still a
  low-level escape hatch in production Sparse Set; this branch
  does not rewrite core ECS to forbid raw mutation.
- `SceneLoadCoordinator` instantiates gameplay Entities only when
  the game host calls `commit(ticket)` at a deterministic structural
  barrier. Never auto-commit on I/O completion in lockstep networking.
  Reader callbacks capture valid worker-safe content readers.
- If a second config domain is optional for certain prefab keys, use
  `MissingPrefabConfig::Skip` with an ordered spawn chain. A complete
  multi-domain integration fixture and additional dynamic override
  policies are future work.
- Prefab instance `LocalTransform` and `Parent` binary persistence
  are still host-registered separately from the static prefab/scene
  schemas. Never misrepresent the new static integration as having
  migrated all hierarchy serialization.
- Source-branch SceneResourceBackend and WorldScene bridge exist but
  are still native-unverified. The scene branch has a known
  additive `CollisionStream` high-ID conflict when two streams share
  one `SpatialWorld`. Do not bypass the existing collider namespace
  check; coordinate safe deterministic ID management with the physics
  agent.
- The immutable table is external to ECS and is retained by a shared_ptr
  in game/module/callback owners. Scene unloading only removes Entities
  and scene resource leases. Do not unconditionally destroy config table
  when one scene unloads while another World still references it.
- A save binding fingerprint detects a different content revision but
  is a deterministic 64-bit checksum, not cryptographic file integrity.
  Existing SHA256 content manifest verification remains responsible
  for authenticity.
- CMake target union, template interaction and native save semantics
  require actual Mac Studio Release validation before main merge.

## Next implementation priorities

1. Compile integrated tests, fix any actual failures in integration
   branch and document exact results.
2. Full scene + COW + multiworld + FPS/RTS parity and regression tests.
3. Add deterministic multi-domain prefab config composition and
   per-instance mutable override tests.
4. Resolve additive static collision ID namespace with world/physics
   owners; later integrate real TerraWeave/PHYSIS/NEREID/SpectraFX.
5. Complete editor scene tree and preview-world provenance/inspector.
