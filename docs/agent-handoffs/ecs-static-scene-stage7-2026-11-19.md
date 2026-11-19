# AIVERSE Stage 7 — deterministic additive collision namespaces

Date: 2026-11-19  
Repo: `chromealex/AIVERSE`  
Working branch: `feature/ecs-static-prefab-scene-integration`  
Prior source: `docs/agent-handoffs/ecs-static-scene-stage6-2026-11-19.md`  
Status: **Stage 7 source committed; no new native compilation/CTest
or RSS result.** The user requested all source feature work first,
then one comprehensive final test run.

## Reason for the stage

Earlier SceneRuntime/SharedStatic agents correctly documented that two
simultaneously active `world::CollisionStream` instances could not
share a `world::SpatialWorld`: the three-argument constructor rejected
any existing ID `>=0x80000000`, and each stream assigned the same
`0x80000000 + chunkOrdinal*65536` static collider IDs. Merely
disabling this guard would overwrite colliders: `SpatialWorld::add`
removes existing colliders with a colliding ID.

The new opt-in path supports **disjoint deterministic collider ID
namespaces**, keeps legacy FPS/RTS and WorldAssets manifests unchanged,
and reuses the existing asynchronous SHA256-verified chunk reader,
triangle/terrain decode, SpatialWorld and streaming scheduler.

## Source changes and APIs

1. `engine/world/CollisionStream.h` and `.cpp`:
   - Optional fourth constructor argument
     `world::CollisionIdRange{firstSlot,slotCount}` (both uint32).
   - Each slot reserves 65,536 potential static collider IDs.
     Upper half of 32-bit ID space supports max 32,768 slots.
     Per-chunk shapes remain restricted by existing 65,535 shape
     limit. All IDs remain deterministic across replay/rollback and
     independent of job completion timing.
   - `CollisionStream(manifest,reader,spatialWorld)` with three
     arguments is source-compatible and **retains** the legacy
     high-ID guard. Passing an explicit range validates capacity
     against WorldAssets chunk catalog and rejects overlap with
     currently occupied collider IDs in its entire reservation.
     Partial invalid range arguments are rejected.
   - Original terrain seam checking within a stream, async decoding,
     SHA hashes, budgets and stream shutdown are unchanged.

2. New `engine/scene/SceneCollisionNamespaces.h`:
   - `SceneCollisionNamespaces::define(stableInstanceKey,maxChunks)`
     declares namespace reservations before async gameplay loads.
     Stable keys are logical *instance IDs*, not only scene file paths.
   - `seal()` assigns disjoint ranges by lexically sorted keys
     (not registration order or worker completion), computes a
     deterministic content-layout fingerprint and freezes the plan.
     Max 32,768 total reserved chunks, duplicate/invalid keys rejected.
   - `acquire(instanceKey)` returns an optional **move-only RAII**
     Lease with `lease.range()`; acquiring an already active
     instance key is rejected even before any collider is published.
   - Lease must outlive the corresponding CollisionStream. Destruction
     of the stream returns its colliders, then Lease releases the
     namespace. Host must keep one namespace catalog per physical
     SpatialWorld; two independent Worlds may create equivalent
     catalogs with equal deterministic fingerprint.
   - Catalog/leases live OUTSIDE authoritative ECS simulation. Host
     must validate namespace fingerprint as part of content identity
     before replay/cold deserialize.

3. New `engine/scene/SceneCollisionBridge.h`:
   - `SceneCollisionBridge(SpatialWorld&,SceneCollisionNamespaces&)`.
   - `define(resourceKey,instanceKey,worldManifest,AssetReader)`
     prevalidates WorldAssets and its maximum chunk reservation.
     Enforces one registered resource per unique instance namespace.
   - `adapter()` supplies SceneResourceAdapter acquire/release to
     `SceneLoadCoordinator`. On commit, acquire obtains a move-only
     Lease and constructs the EXISTING CollisionStream. On unload,
     the stream is destroyed before its lease is returned.
   - `prepareRegions(span<Bounds>,requests,publications)` advances
     the existing collision chunk streamers. A required physical
     region MUST be completely ready before consuming a
     Simulation tick (use `GameModule::readyToStep`).
     Per-stream budgets apply; global cross-stream physical
     preparation budgets are a later optimization.
   - A SceneLoadCoordinator using mixed render/audio/collision
     dependencies must use a routing/composition adapter;
     SceneCollisionBridge itself only knows registered collision keys.

4. CMake:
   - `aiverse_ecs_scene_collision_namespaces_test` and
     `aiverse_ecs_scene_collision_bridge_test` are added
     **after** modules/fps defines existing `aiverse_world`.
     Each links to `aiverse_world` rather than duplicating a
     collision implementation.
   - `tools/ecs-static-scene-validate.sh` includes both in
     smoke/extended. In `all`, full project build + complete
     registered CTest includes them automatically.

5. Tests (source committed, **not executed**):
   - `tests/ecs_scene_collision_namespaces_test.cpp`:
     namespace order independence, double-acquire prevention,
     actual SHA256-verified async CollisionStream publishing from
     two overlapping spatial cells into separate reserved IDs,
     existing ID collision rejection, original legacy guard,
     unloading/reloading scene A while keeping scene B.
   - `tests/ecs_scene_collision_bridge_test.cpp`:
     real SceneLoadCoordinator persistent+additive SceneAsset
     collision dependencies, physical content readiness gate,
     independent unload, replace nonpersistent scene, final
     namespace lease and collider cleanup.
   - Both tests rely on `tests/AlwaysCheck.h` so assertions work
     in Release. They must compile/link and CTest must be
     actually executed before claiming success.

6. Documentation:
   - `docs/scene-collision-namespaces.md` — API and host contract.
   - `docs/scene-worlds.md` / `docs/ecs-static-prefab-scene-integration.md`
     — updated additive collision capabilities and still-unverified
     boundaries.
   - `engine/scene/AGENTS.md` — local agent ownership/invariants.

## Stage 7 implementation commits

- `a72407238a426fb1c44b8924cf80a0225ef85ef6` —
  add explicit optional collider ID range API.
- `620e03d41b934af435746d09d134355939686a70` —
  implement explicit ID range reservation in CollisionStream.
- `159184f692d4c9e246b4152ec96018c7deb8a81b` —
  SceneCollisionNamespaces builder, stable layout, RAII leases.
- `5735e2f2f1e17329577c687f21f7e6790adf0b34` —
  namespace overlap/legacy/hash/unload test.
- `658aa7cc9e1f8e07c1e9ff80ba073a162e7e77b6` —
  SceneCollisionBridge for SceneLoadCoordinator dependencies.
- `82a86dc1b7a50f33f335502dca6336ae9796340b` —
  real scene collision bridge CTest.
- `c7f56a780354cd08e4d7f502bcb2db45dab79b53` —
  CMake registered both against aiverse_world.
- `ac7d866b09b2450d3a855fdf99a9609acd94ddb7` —
  include new tests in validation script.
- `523f95a1234579836f8e6b4688765ce1e9f525c6` —
  reject malformed partial namespace parameters.
- `27ce6df7046f4abd6b1b842c26ae17a8dde5d5e3` —
  detailed collision namespace architecture.
- `23b3f9f9d49bdad4d194df4eeb23eb69a1906772` —
  agent contracts for namespace leases.
- `98f6d931e41461f85e410aade64278714892d091` —
  scene worlds documentation.
- `d0c9a728f931d289ea1d89b6e6cd411a601dd6a3` —
  ECS/static prefab integrated documentation.
- `bdd7f3319ef3c28044a49679465b126e5c1be424` —
  initial Stage 7 agent handoff.
- `aa44beb47965f32be06b7390fea918a626b37813` —
  collision namespace overflow and partial range CTest cases.
- `0025b2781d3df18ad3f83021c3e618d31ddb7ddd` —
  agent skill for additive collider ownership.
- `8b0629c3e64359cacde7237b86a6d9ed888f67db` —
  Stage 6 handoff links to Stage 7 continuation.
- `760c9094dca6162b13cfb7a067480e7526d47958` —
  add direct fixed-width integer include to CollisionStream API.

The handoff itself is a later commit. Use `git rev-parse HEAD`
after updating the Mac Studio worktree. No native Stage 7 result
was observed, and no actual physical frame/latency benchmark
was collected here.

## Final one-shot test gate (Stage 5/6/7 together)

Use the existing *isolated* Mac Studio worktree:

```sh
cd ~/Projects/AIVERSE-static-scene
git fetch origin feature/ecs-static-prefab-scene-integration
git pull --ff-only origin feature/ecs-static-prefab-scene-integration
git rev-parse HEAD

bash tools/ecs-static-scene-validate.sh build/static-scene all 8 1000000
```

The script configures and performs one Release full-target build,
the entire registered CTest suite once, then 9 separate-process
RSS/CPU read tests (1M entities × 16/64/1024 unique configs ×
Ordinary/Shared/Static storage). It logs compiler/test outputs and
checks equivalent numerical checksums across storage modes.

Previous validated user results are Stage 4 seven targeted CTests
passing 7/7. This is not a substitute for the new Stage 5/6/7
native regression suites. If the user supplies errors, fix them on
the isolated integration branch and rerun the complete gate before
proposing a main merge.

## Known limitations / safe integration boundaries

- Explicit collision range support assumes all SceneCollisionBridges
  in one SpatialWorld use one shared host collision namespace
  catalog. A second independent catalog would not coordinate
  as-yet-unpublished ID reservations, though the CollisionStream
  constructor still catches collisions that already exist.
- Existing collision data and async streamers are unchanged;
  **cross-scene terrain seam checks** are not implemented.
  Existing per-stream seams remain validated; the editor
  or owning project must ensure correct adjacent authored terrain.
- The same scene asset loaded in two active copies requires distinct
  stable instance keys and different namespaces. There is no
  automatic reallocation based on loaded scene ID or wall time.
- Global publication budgets across multiple collision streams
  (as opposed to each stream's own budget) and automatic
  replication of namespace fingerprints into lobby/save protocols
  are future work.
- Physics/gameplay host must **not step** into a region until
  SceneCollisionBridge returns ready; otherwise missing collision
  chunks could cause incorrect deterministic gameplay.
- No production FPS/RTS GameModule was silently rewired to use
  additive collision namespaces. That requires explicit title/
  host integration and native network parity.
- Native Stage 5/6 test execution, real RSS results, full GPU/
  Metal/NEREID/PHYSIS/TerraWeave coupling, and editor terrain seam
  authoring remain outside validated scope.
- The immutable prefab StaticTable remains read-only in gameplay.
  Do not change core ECS or Simulation scheduling to implement
  collision namespaces.

## Next agent after final CTest gate

1. Fix any source or test compiler failures revealed by the actual
   full build/CTest, including any warnings; record commits and logs.
2. Verify RTS/FPS regression/replay and one production host demo
   that uses the namespace catalog (not a mocked streamer).
3. Gate network multiplayer against scene/world content revisions
   and collision namespace fingerprint, then save/restore parity.
4. Profile WorldAssets streaming, collision CPU budgets and
   COW/native memory with real hardware and multiple active scenes.
5. Coordinate editor terrain seam authoring and PHYSIS/TerraWeave
   physics with responsible agents before a main merge.
