# AIVERSE — Static Scene Stage 5: multi-domain prefab configurations

Date: 2026-11-19  
Repo: `chromealex/AIVERSE`  
Branch: `feature/ecs-static-prefab-scene-integration`  
Previous: `docs/agent-handoffs/ecs-static-scene-stage4-2026-11-19.md`  
Status: **new Stage 5 code, tests, runner and docs committed; NO native
compile or CTest execution for Stage 5 yet**.

## What is already verified

User-provided Mac Studio Stage 4 results:
`ecs_scene_runtime_test`, `ecs_multiworld_test`,
`ecs_scene_load_coordinator_test`, `ecs_scene_resource_pipeline_test`,
`ecs_shared_static_components_test`,
`ecs_prefab_static_catalog_test`, and
`ecs_static_scene_integration_test` all passed (**7/7**, zero failed,
0.24 seconds combined CTest elapsed). An earlier pipeline test failed
because its expected backend lease count incorrectly equaled scene refs;
the correction passed alone (1/1, 0.60 seconds). All these results are
for the **previous Stage 4 source state**, not the Stage 5 sources.

The prior Mac Studio Shared/Static Release benchmark had
200,000 entities, 64 unique 128-byte config values: Ordinary 0.309 ms,
Shared 0.089 ms, Static 0.096 ms median read passes. Payload estimate
25,600,000 vs 808,192 bytes; not process RSS.

The integration branch was previously merged with the latest known
main in commit `82b4c49753635c23a62beaa4d9ccfa9f4b5e1063`.
Do not modify the source feature branches or main without coordination.

## Stage 5 architecture and source

1. **New `engine/scene/PrefabStaticDomainRegistry.h`**:
   - `PrefabStaticDomainRegistry::addDomain<T>(domainKey,stableSchemaId,
     shared_ptr<const PrefabStaticCatalog<T>>,Reject|Skip,version)`.
   - Distinct C++ `StaticRef<T>` types are required per domain. Rejects
     duplicate domain keys, duplicate schema IDs, duplicate T types,
     invalid stable identifiers and additions after sealing.
   - `seal()` freezes a deterministic, lexically ordered map of domains.
     Registration order cannot change spawn order or native schema
     ordering. The process-local component type IDs only detect duplicate
     T registrations; they do not enter network/save data.
   - `registerPersistence(Simulation&)` registers each
     `StaticRef<T>`, `StaticTableBinding<T>` and
     `PrefabCatalogBinding<T>` under stable versioned IDs.
   - `bindWorld(World&)` preflights **all** catalog content/mapping
     fingerprints before mutating any World resource, so a stale second
     catalog cannot silently insert a partial first binding.
   - `validateWorld(const World&)` validates all domains after
     binary load/rollback.
   - `callback(instanceInitializer)` produces the real
     `SceneLoadCoordinator::SpawnPrefab` callback. It preflights
     each required/optional domain and entity reference before
     attaching any StaticRef. A successful preflight attaches refs
     in sorted domain-key order, then runs a mutable per-instance
     initializer. On failure, SceneLoadCoordinator cancels the
     entire new scene; prior active scenes survive.

2. **Updated `engine/sim/PrefabStaticCatalog.h`**:
   - Added `canBindWorld(const World&) const noexcept`:
     checks pre-existing native table and prefab-map fingerprints
     without detaching COW resources or modifying World state.
   - `bindWorld` reuses this validation; unchanged for single-domain
     callers. Documentation clarifies that catalog code has no
     dependency on scene code, which is handled by optional adapters.

3. **New `tests/ecs_static_scene_multidomain_test.cpp`**:
   - 3 typed config domains: Weapon (required), Brain (required),
     Inventory (optional), each backed by its own sealed immutable
     table and 4-byte Entity refs.
   - 2 additive scenes (persistent/global and field), a separate
     preview World using the same catalog tables; deterministic
     prefab order despite unsorted scene JSON input.
   - Refcounts/unique value counts, optional config absence,
     per-Entity mutable state (ammo) without static mutation.
   - Reject duplicate types/schema IDs and malformed domain keys.
   - Validate stale Brain catalog rejection **without partially**
     inserting other config bindings.
   - Unknown required prefab fails preflight without partially
     attaching refs; SceneLoadCoordinator replacement failure
     keeps previously active scenes.
   - Deterministic native Simulation checksum/serialized bytes
     with distinct registries created in different registration
     orders but identical content.
   - COW snapshot/restore, scene resource-only residency restore,
     native v10 binary serialization/deserialization and changed
     prefab-alias revision rejection.
   - Test is registered in the top-level CMake native ECS foreach
     as `aiverse_ecs_static_scene_multidomain_test`.
   - **Test source has not been built/run on the target Mac.**

4. **New `tools/ecs-static-scene-validate.sh`**:
   - Two opt-in modes: `smoke` (12 Scene/Static/COW/hierarchy tests)
     and `extended` (21 tests including real production RTS/FPS
     replay, native persistence, rollback session, guarded ECS
     parallelism and COW).
   - Configures `-DAIVERSE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release`,
     explicitly builds all target executables and runs a grounded
     CTest regex, not a blanket pass declaration.
   - Logs under `build/static-scene/verification`. It never
     checks out/merges refs or edits the user's active main worktree.
     Invoke explicitly using `bash` because GitHub Contents files
     may not retain executable permissions.

5. **Docs and agent guidance updated**:
   - `docs/ecs-static-prefab-scene-integration.md` — Stage 5
     usage and architecture, examples, validation limitations.
   - `engine/scene/AGENTS.md` and
     `skills/gameplay/aiverse-ecs-shared-static/SKILL.md` — scope
     and continuation rules.
   - `docs/agent-handoffs/ecs-static-scene-stage4-2026-11-19.md`
     — repaired an accidentally malformed/repeated old command section,
     now points to the reproducible validation script.

## Commit history for this implementation stage

- `09517cbe303ea52fcd478ffb2e34cb91d28dfdbf` — multi-domain
  registry and deterministic preflight.
- `e546428cbe7e252764ad14d69567968190172529` — read-only
  catalog binding preflight.
- `3dd7c4127d8fb262b11035ab629526e9c28a589a` — real
  multi-domain scene/World/rollback fixture.
- `4b240569571edbb245b4d348b0df68d6856433f7` — CMake target.
- `a1ac54c4724ee2d993bfc814fb4775460db66e2f` — atomic
  preflight and reversed-registration native serialization parity.
- `bd52b457b57b95323e0d214e0e244af0124dfed5` — validate
  persistent schema IDs before registration.
- `69022af11b28e7fb7e54c644690ce4adb9fec923` — reproducible
  native validation tool.
- `b82f68232f38dca2265118dfd6f4c28553976943` — handoff
  command section cleanup.
- `3b0060152e58510b4d1b448a16e084caa390d8b0` — API docs.
- `5a1dd24b47d36171b4c6f029f30e035a613323a9` —
  catalog decoupling comment.
- `06b42f42aa3102dcf04ac5f8b5152f7664c3c055` —
  scene-scoped agent rules.
- `c25971bbf7ad9a66d8c3c62870b4e79e9cfe0c06` —
  gameplay skill update.

This handoff is itself a later commit. Always use `git rev-parse HEAD`
for the current source revision and add any compiler/test fixes to
this same feature branch, followed by an updated handoff.

## Immediate next action — Mac Studio native validation

Use the EXISTING isolated Stage 4 worktree, not the main checkout:

```sh
cd ~/Projects/AIVERSE-static-scene
git fetch origin main feature/ecs-static-prefab-scene-integration
git pull --ff-only origin feature/ecs-static-prefab-scene-integration
git rev-parse HEAD

# Builds/runs 12 targeted ECS/scene/static/COW/hierarchy tests:
bash tools/ecs-static-scene-validate.sh build/static-scene smoke 8

# After smoke passes, builds/runs 21 tests including real RTS/FPS
# GameModule replay, native persistence and rollback:
bash tools/ecs-static-scene-validate.sh build/static-scene extended 8
```

Record compiler, actual test count/failures, all warnings,
exact Git SHA, per-phase log file, and any source fixes in this
handoff. On a failure, fix the implementation or test contract,
never weaken `AIVERSE_TEST_CHECK`.

## Architectural limitations / future work

- This is a typed opt-in adapter, not a replacement of production
  `engine/core/ECS.h` or existing RTS/FPS components. No production
  modules have been migrated.
- An entity can have one `StaticRef<T>` for each distinct T, and
  many different typed T domains. Two domain names using the *same*
  T are deliberately rejected rather than silently overwriting.
- Unknown optional domain keys leave that StaticRef type absent.
  Required prefab keys cause rejection BEFORE assigning any static
  components, but code exceptions in a later initializer still rely
  on SceneLoadCoordinator's new-scene cleanup. It cannot roll back
  arbitrary side effects outside the freshly-created Entity.
- A catalog is immutable during its lifetime and shared across
  independently managed Worlds. The native save stores only typed
  per-entity refs and catalog/table revision stamps, not the large
  shared table payload. A host must load the exact content revision
  and call `validateWorld` before resuming a deserialized World.
- Static configuration attributes must use canonical field-wise
  `io(Archive&,T&)` for deterministic fingerprints; mutable HP,
  ammo, inventory contents or per-Entity AI state remain ordinary
  ECS components.
- The original additive collision `CollisionStream` static collider-ID
  namespace issue remains unresolved. Do not bypass the existing
  high-ID guard or attach two independent collision streams to one
  `SpatialWorld`. Coordinate with physics/world agent.
- Real GPU/Metal scene residency, TerraWeave, NEREID, PHYSIS, editor
  scene hierarchy and actual process RSS are not validated by Stage 5.
- Full broad FPS/RTS parity, scene reload/hot-revision migration,
  multi-process lockstep and cross-compiler support must be verified
  before a merge to main.

## Recommended Stage 6 after native validation

1. Fix any Stage 5 compile/CTest failures; keep docs and agent handoff
   current, then run extended Release validation.
2. Benchmark 1M entities with 16/64/1024 unique config values,
   measure RSS, cache misses and cold/streaming loads.
3. Integrate immutable catalog revision/content hashes with editor
   prefab scene authoring and explicit version migration APIs.
4. Add safe additive collision scene ownership with deterministically
   allocated static collider ID namespaces; coordinate with PHYSIS
   and TerraWeave owners.
5. Validate actual FPS/RTS native modules using StaticRef in two
   separate games before any production conversion.
