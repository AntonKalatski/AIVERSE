---
name: aiverse-ecs-shared-static
description: Use AIVERSE SharedRef and StaticRef immutable component storage to deduplicate gameplay config, preserve native save/rollback, and benchmark memory/read cost without rewriting ECS core.
---

# AIVERSE shared/static immutable component storage

**Branch:** `feature/ecs-shared-static-components` (opt-in feature).
Read `docs/ecs-shared-static-components.md` and
`docs/agent-handoffs/ecs-shared-static-stage1-2026-11-19.md`
before integrating it. Also read `docs/ecs-agent-handoff.md`,
`docs/ecs-mechanic-composition.md` and root `AGENTS.md`.

## Use the right storage

- Keep frequent mutable data as ordinary `World::add<T>` components.
- Repeated configuration values that can change *per Entity* through
  assignment/reinterning: `SharedRef<T>` + `SharedStore<T>`.
- Prefab/asset-defined immutable configuration: `StaticRef<T>` +
  `StaticTableBuilder<T>::seal()` / `StaticTable<T>`.
- Do not confuse static configuration with a static/static-mobility entity:
  the latter may have dynamic components but read immutable config.
- Shared values and static tables always return **const T**. Never add a
  mutable pointer or reference accessor. Do not store native pointers or
  external scene-asset handles in serializable ECS config.

## Contract

- Public headers: `engine/sim/SharedStaticComponents.h` and
  `engine/sim/SharedStaticPersistence.h`. Do not change `ECS.h` without
  separate ECS branch coordination/parity testing.
- `T` defines a stable field-wise `io(Archive&,T&)` overload. Deterministic
  contents, no raw pointer serialization, no unordered container iteration.
- `SharedRef<T>` and `StaticRef<T>` are 4-byte World-local indices.
  SharedStore is a World resource, copied in snapshots with immutable
  payloads shared; StaticTable is external immutable content shared across
  Worlds. Persistent saves store the static table's fingerprint/binding,
  NOT its full content.
- Register schemas during `GameModule::setup` using
  `registerSharedPersistence<T>` and
  `registerStaticPersistence<T>` with stable type IDs.
  Check `validateShared<T>(world)` and
  `validateStatic<T>(world,table)` after load.
- `setStatic` binds once. A new static config requires a new sealed table
  and an explicit content/version/world migration.
- Use `SharedReadView<T>`/`StaticReadView<T>` to amortize store lookups
  in hot loops. Do not retain views across World copy/rollback/compaction.
- `compactShared<T>` may run only at a deterministic structural/tick
  barrier after flush, without worker jobs or external cached SharedRef IDs;
  previous snapshots keep their own ref mapping.
- Performance is O(1), but latency may differ from regular components.
  Never claim faster/same speed without Release per-platform benchmarks.
- Run CTest `ecs_shared_static_components_test` and target
  `aiverse_ecs_shared_static_bench` on target hardware, then capture actual
  result, compiler, git SHA and any warnings in the stage handoff.
- Keep RTS/FPS behavior and save protocols unchanged until a migration
  has verified checksum/replay and serial format parity.

## Follow-up integrations

- Prefer StaticRef for types/prototypes from shared asset definitions.
  Use SharedRef for large repeated runtime config settings. Both work in
  any genre: FPS, RTS, RPG, racing, city builders, server simulation.
- When SceneAsset/SceneLoadCoordinator creates many instances of a prefab,
  bind a single static table once per ECS World and attach 4-byte refs.
  Do not duplicate config objects during additive level loading.
- Keep skill, architecture doc, AGENTS rules and stage handoff updated
  after each implementation step. Never mark unrun tests as passing.
