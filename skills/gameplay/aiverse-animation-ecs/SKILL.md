---
name: aiverse-animation-ecs
description: Shared C++ presentation ECS animation components and graph player usable by any game genre.
---
# Animation ECS

CMake target `AIVERSE::animation_ecs`; public header
`<aiverselib/animation/EcsAnimationSystems.h>`.

Use a SEPARATE `aiverse::World` for animation presentation. Never put these
floating-point components, graph players, or asset pointers in the authoritative
`Simulation::world()`. They are excluded from checksums and rollback.

Create an entity in presentation World; add AnimationAssetComponent (model
pointer and shared compiled graph), AnimationInputsComponent,
AnimationPlayerComponent and AnimationPoseComponent. Optionally add
AnimationBudgetComponent and AnimationDiagnosticsComponent. Project gameplay
state into AnimationInputsComponent every frame, then call
`stepAnimationPresentation(world, frameDt)`. Render
AnimationPoseComponent::local/matrices if valid.

This host does not yet replace existing RtsView or automatically attach to
FPS characters. Do not snapshot presentation World: AnimationPlayerComponent
owns a shared mutable graph instance. See docs/agent-handoffs/animation-universal.md.

## Optional animation policies (all presentation-only)

- `AnimationPlayback.h`: `clipNotifies` consumes unwrapped clip time and
  returns ordered cosmetic events; `extractClipRootDelta` computes rigid
  motion requests without changing the authoritative actor.
- `AnimationMontage.h`: sectioned montage, slots, bone subtree masks;
  add `AnimationMontageSlotsComponent` to overlay it after the graph.
- `GenericRetarget.h`: bind-relative local transform retargeting using
  bone full paths plus explicit aliases. NOT Unity Humanoid retargeting.
- `AnimationMotionWarp.h`: non-looping, model-local root correction toward
  an explicit goal; no collision or character motor authorization.
- `PoseInertialization.h`: `PoseInertializer` for velocity-aware decaying
  bone offsets on a graph output; caller triggers its begin/update/apply.
- `PhysicalPoseBlend.h`: `blendPhysicalPose` with optional per-bone mask;
  `AnimationPhysicalPoseComponent` can supply an external physics pose
  to the presentation system. NOT a ragdoll solver.
- `AnimationBudgetPolicy.h`: `AnimationSignificanceComponent` and
  `assignAnimationBudgets` for stable camera-priority pose throttling.
  Does NOT schedule parallel workers or perform GPU crowd animation.

### Validation gate

Configure/build the exact target and run the standalone CTest cases:
`animation_ecs_presentation`, `animation_playback`,
`animation_generic_retarget`, `animation_montage`,
`animation_motion_warp`, `animation_inertialization`,
`animation_physical_pose` and `animation_budget_policy`.
Then run existing `animation_graphs`, `unity_anim_controller`, `fps`
and RTS gameplay/render suites. Never claim these tests pass without execution.

See `docs/agent-handoffs/animation-universal.md` for commits and blockers.
