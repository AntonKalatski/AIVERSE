# AIVERSE universal animation — agent handoff

## Stage 1: presentation ECS host — 2026-11-18

- **Branch:** \`feature/universal-ecs-animation-pipeline\`, forked from
  \`feature/universal-entity-inspector-fps\`.
- **Implementation commit:** \`82d5b99bc92ac5b142e61885f32801a3c8858832\`.
- **Code:** \`libraries/include/aiverselib/animation/EcsAnimationSystems.h\`.
- **Test:** \`tests/animation/ecs_presentation.cpp\`.
- **Build:** \`AIVERSE::animation_ecs\` public INTERFACE target;
  \`aiverse_animation_ecs_tests\` / CTest \`animation_ecs_presentation\`.
- **AI skill:** \`skills/gameplay/aiverse-animation-ecs/SKILL.md\`.

### Delivered

Opt-in, genre-agnostic rendering/presentation ECS for C++ projects: asset, graph
player, parameter override, pose/world matrices, LOD and diagnostics components.
\`stepAnimationPresentation(World&,float)\` runs exactly one graph update per
entity and selectively evaluates pose for rendering. Immutable graph is shared,
per-entity player is separate. A missing/mismatched asset clears stale pose.
Invalid dt is ignored. The test source covers two independent Worlds, play
time, LOD clocks, overrides, reset and missing assets.

**Critical isolation rule:** this World MUST be separate from
\`Simulation::world()\`. The deterministic game handles input, movement,
weapon firing, collisions, networking, rollback, and gameplay events; this
presentation runtime only consumes projected values. Never serialize or COW
snapshot this World: \`AnimationPlayerComponent\` holds a shared mutable
\`AnimationGraphInstance\`. This is an intentional opt-in host, NOT a live
replacement for RtsView.

### Compatibility

- Existing \`RtsView\`, \`AnimationGraph\`, model formats, JSON assets and game
  save/checksum format are unchanged.
- No gameplay/physics authority or automatic FPS/RTS view binding was added.
- The public component API is new and optional for native game modules.
- Source model/graph lifetimes are managed by the host; no raw data ownership.

### Validation

**NOT RUN:** \`cmake --build build/ecs-test --target aiverse_animation_ecs_tests\`
and \`ctest --test-dir build/ecs-test --output-on-failure -R '^animation_ecs_presentation$'\`.
Reason: edits were pushed via the GitHub connection; no mounted AIVERSE
worktree or accessible GitHub network in the local execution container. The
test was committed but must not be claimed passing. Review CI build errors
before calling this stage validated.

### Risks and unfinished work

- Pointer-backed player is not snapshot-safe; do not place in sim ECS.
- Presentation ECS is opt-in: FPS has no scene adapter; existing RTS remains
  on its own per-entity RtsView path.
- No root-motion authority, generic graph notifies, montage slots,
  retargeting, graph hot reload, physical animation, editor upgrades, or GPU
  crowd improvements were implemented by this stage.
- LOD is a caller-provided interval/visibility, not a global budget allocator.
- Graph compilation must succeed and model must outlive graph/instances.

### Next independent stage

1. Verify first-stage compilation/CTest on macOS; fix errors before integration.
2. Implement generic AnimationNotify timeline and extract-only root motion in
   the reusable animation library, with loop/one-shot/seek tests. Never
   mutate simulation transforms from presentation.
3. Add a host-owned adapter projecting FPS/RTS input parameters into the
   shared presentation API, preserving existing RtsView behavior and
   equivalence checks.
4. Add montage, humanoid retargeting, motion warping, ragdoll and
   editor/performance work in separate bounded commits; append a dated handoff
   after each stage as required by AGENTS.md.
