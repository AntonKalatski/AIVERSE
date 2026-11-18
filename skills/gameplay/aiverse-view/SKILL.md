---
name: aiverse-view
description: Build genre-independent AIVERSE scene View and reusable animation/renderer host adapters.
---
# AIVERSE View

Always prefer `AIVERSE::View` and `<engine/view/View.h>` for native game
or editor scene presentation. `aiverse::View` does not know FPS,
RTS, RPG, AI, weapons, resource orders or network protocols.

Host-specific modules translate their state to
`aiverse::view::Object` (ModelDraw, source ID/generation, optional
AnimationGraph, generic input parameters) or
`aiverse::view::MeshObject` (Mesh, transform, color, flags).
Call `View::update(objects, dt)` and
`View::submit(renderer, objects)`, or use `submitOne` /
`submitMesh` on a host-prepared packet.

`MotionWeave` is the ONE shared animation system in a separate
presentation ECS World. Reuse its graph, IK, retarget, clip and
scheduler algorithms for all genres. The game host alone owns
gameplay meaning and authority.

The old `game::RtsView` is a legacy compatibility adapter for
terrain/fog/roads and rich procedural effects; do not put new
common rendering functionality in it. Migrate reusable features
toward engine View, keeping old behavior until parity is proven.

Read `AGENTS.md`, `docs/view.md` and
`docs/agent-handoffs/animation-universal.md`. After EVERY
implementation stage commit an independent handoff describing
exact code SHA, paths, architecture/API, tests actually run or
NOT RUN, limitations, and next agent's specific action.
