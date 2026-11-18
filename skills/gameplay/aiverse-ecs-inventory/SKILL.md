---
name: aiverse-ecs-inventory
description: Use one transactional ECS inventory system for RTS/FPS/RPG items, pickups and transfers with stable command ordering, capacity checks and replay.
---

# Universal ECS inventory and transfers

Read `AGENTS.md`, `docs/ecs-all-features-migration-plan.md`,
`docs/ecs-mechanic-composition.md` and the inventory package's
existing skill.

Include `<aiverselib/genre/EcsInventorySystems.h>`, link
`AIVERSE::lib_ecs_inventory`, and attach `InventoryComponent` with
a `gameplay::InventoryConfig` and item stacks to any Entity. Register
an immutable `InventoryCatalog` of `gameplay::ItemType` definitions
in World, plus `InventoryCommandQueue` and `InventoryResultQueue`
resources.

From a **validated** player Command, bot or interaction, call
`enqueueInventoryAction(world, InventoryAction{...})` for Add, Remove
or Transfer with generation-safe Entities. Register the
`addInventoryTransactionSystem` after permission checks and before
equipment, crafting, loot/despawn and UI event projection.

All transactions use the existing atomic `addItem/removeItem/
transferItem` algorithms. The system reuses that code and never mutates
two bags partially on insufficient weight/slots. Request IDs and result
order are stable; queues, inventories and the sequencer are authoritative
ECS state and must participate in snapshots and hashes.

**Security:** the shared system enforces content/weight/slots; the host
must validate ownership, distance, faction, cost and permissions before
enqueuing actions. Never accept raw network IDs as authorized transfers.
Do not route account money/store receipts through this World: account
meta remains a transactional server-side service.

Run `ecs_cross_genre_inventory_test`: RTS store + FPS bag with
identical results, capacity rejection, ordered action IDs, snapshots
and COW rollback. Extend with mixed gameplay (pickup, loot drop,
equipment, crafting, shop) and parity with built-in FPS/RTS saves,
replays and network before deleting old paths.

This package is currently independent and opt-in; **built-in FPS and RTS
inventory logic has not been switched**. Never claim otherwise. Log every
migration step to `docs/editor-unification-handoff.md`.
