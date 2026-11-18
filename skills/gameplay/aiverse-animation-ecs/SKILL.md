---
name: aiverse-animation-ecs
description: Shared C++ presentation ECS animation components and graph player usable by any game genre.
---
# Animation ECS

CMake target \`AIVERSE::animation_ecs\`; public header
\`<aiverselib/animation/EcsAnimationSystems.h>\`.

Use a SEPARATE \`aiverse::World\` for animation presentation. Never put these
floating-point components, graph players, or asset pointers in the authoritative
\`Simulation::world()\`. They are excluded from checksums and rollback.

Create an entity in presentation World; add AnimationAssetComponent (model
pointer and shared compiled graph), AnimationInputsComponent,
AnimationPlayerComponent and AnimationPoseComponent. Optionally add
AnimationBudgetComponent and AnimationDiagnosticsComponent. Project gameplay
state into AnimationInputsComponent every frame, then call
\`stepAnimationPresentation(world, frameDt)\`. Render
AnimationPoseComponent::local/matrices if valid.

This host does not yet replace existing RtsView or automatically attach to
FPS characters. Do not snapshot presentation World: AnimationPlayerComponent
owns a shared mutable graph instance. See docs/agent-handoffs/animation-universal.md.
