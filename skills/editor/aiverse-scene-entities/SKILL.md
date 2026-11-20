---
name: aiverse-scene-entities
description: Work with AIVERSE scene entities and document components through native MCP, including stable Light IDs, Transform edits, Inspector ownership and persistence.
---

# Scene entities and components

Read `AGENTS.md`, `docs/studio-light-entities.md`, and the latest lighting handoff.
All placed objects must be entities with components. Do not reproduce a flat
light Properties list or create a separate mutable Lighting-owned object model.

## Agent workflow

Use the existing AIVERSE MCP server, not file writes or UI scripting, for supported
native authoring operations. Connected editor and MCP server must come from
the updated engine build; restart an older connected editor before using new tools.
Call `open_game` with the intended project first.
`scene_entity_list` returns entityId, stableId and component descriptors/data.
`scene_entity_get` reads one entity. Retain returned IDs; never infer an ID from
an array position. Only lights currently report stableId=true; legacy other
objects retain provisional IDs. Re-list after structural edits to those objects.

`scene_component_set` takes entityId, componentId and patch. Transform position
is [x,y,z]; RTS light Y is height above terrain, FPS light Y is world position.
Light type/radius/intensity/color/flags belong to Light, not Transform. Read the
returned component data before choosing a patch. Unsupported fields or read-only
components must not be bypassed by directly modifying project JSON.

`scene_light_create` accepts optional name, position and light component patch;
returns the persistent entityId. `scene_light_delete` accepts that ID, not an
ordinal. Changes use native Undo history. Use existing `undo`/`redo` to reverse
edits and `map_save` to persist requested work. Reopen and get by ID when verifying
persistence. A missing/deleted ID is an error, not permission to edit another item.

## Implementation boundary

`apps/editor/DocumentEntityIdentity.h` owns document ID formatting;
`apps/editor/EditorEntityAdapter.h` owns component access;
`apps/editor/core/SceneEntityOps.h` exposes those APIs to the Workspace/MCP.
Studio uses the same adapter and component identity. lights[] is the sole legacy
canonical payload plus entityId metadata; never add a second state mirror.
Do not put float lighting/GPU resources into deterministic simulation ECS.
Generic attach/remove-component support is not implemented; do not invent tools.

Validate actual stdio transport with `tests/editor/scene_entity_mcp.py`, using an
external fixture/output directory on KINGSTON. Follow project per-stage handoffs.
