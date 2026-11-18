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


## Stage 2: playback policies — 2026-11-18

- **Code commit:** `f4fca50699eefeb9dc86971237b1c6db3f15ac1b`.
- **Files:** `libraries/include/aiverselib/animation/AnimationPlayback.h`,
  `tests/animation/notifies_root_motion.cpp`, CMake test registration.
- **API:** `clipNotifies(keys, duration, from, to, loop, maxEvents)` returns
  ordered notifies or `nullopt` on invalid/overflow. Clock values must be
  absolute, unwrapped clip seconds; seek backwards intentionally emits none.
- **API:** `extractClipRootDelta(model, clip, rootNode, from, to, loop)`
  returns an optional rigid model-space delta. Loop extraction composes
  root rigid transforms across cycles, including rotation. No authoritative
  transforms are touched; the caller must decide visual vs motor request.
- **Tests added:** multi-loop events, ordering, one-shots, seek suppression,
  overflow/invalid keys, root translation and turning loops.
- **Validation:** NOT RUN. GitHub-only checkout; no accessible worktree/build.
  Commands: `cmake --build build/ecs-test --target aiverse_animation_playback_tests`
  then `ctest --test-dir build/ecs-test --output-on-failure -R animation_playback`.
- **Compatibility:** pure new public header, no model/save schema changes, no
  existing RtsView hook altered. Events are presentation only, not gameplay
  hit/damage authority. Unity events are not imported automatically.
- **Limits:** no event track serialization/UI, montage routing, skeleton
  retargeting, deterministic motor integration or runtime GPU benchmark.
- **Next:** validate both tests; wire generic playback/parameters into an FPS
  presentation adapter without disturbing current RTS behavior. Build
  separate montage and retarget stages with explicit tests and handoffs.


## Stage 3: compatible-rig skeleton retarget — 2026-11-18

- Code commit: `4e6bd2944547e99401ffef01fc481b81ac165078`.
- New public algorithm: `libraries/include/aiverselib/animation/GenericRetarget.h`
  with `GenericRetargetMap::compile()` and `apply()`.
- Full ancestry paths are matched by default; caller may alias renamed bones.
  Output pose is initialized to destination bind pose, then mapped local
  translation/rotation deltas are transferred. Bone-length translation
  scaling is optional. Scale/morph copying is opt-in.
- Standalone regression test: `tests/animation/generic_retarget.cpp` /
  CTest `animation_generic_retarget`: aliases, bind rotation, relative
  translation, unmapped bones, invalid input and optional channels.
- Compatibility: no Model layout or serialized animation format changed.
  Works only on compatible local-space transform rigs. Does NOT implement
  Unity Humanoid muscles, retarget IK, humanoid poses, twist distribution
  or root-motion authority. Avoid claiming full humanoid retarget support.
- Validation: NOT RUN. Command:
  `cmake --build build/ecs-test --target aiverse_generic_retarget_tests`
  and `ctest --test-dir build/ecs-test -R animation_generic_retarget --output-on-failure`.
- Next: implement a reusable montage/slot overlay driven by clip sections;
  then run all animation tests and add FPS visual integration with real
  optional models. Continue separate handoffs for every stage.


## Stage 4: montage sections, overlay and notifies — 2026-11-18

- Implementation: `c13e4174550f242d0600a5d41c138b6dee26d68d`.
- Added: `libraries/include/aiverselib/animation/AnimationMontage.h`,
  `MontageDefinition::compile()` and `MontagePlayer`.
- Montages define slot name, clip sections, chaining/looping, explicit
  notifies and optional bone subtree mask. A bounded playback update
  crosses section boundaries and returns ordered events for audio/VFX.
- `MontagePlayer::apply()` overrides only the played clip channels on top
  of an existing AnimationGraph pose, with optional blend-in/out weight.
- Added optional `AnimationMontageSlotsComponent` to the separate
  presentation World. `stepAnimationPresentation()` updates these players
  and evaluates overlays after graph evaluation, before world matrices.
- Test `tests/animation/montage.cpp` / CTest `animation_montage` exercises
  section progression, chaining, looping, bounded cycle guard, overlays
  and composition with ECS AnimationGraph host.
- Compatibility: no existing RtsView behavior changed; FPS is not
  connected. Montages are view-only. No automatic notify import, runtime
  animation graph montage node, network authority, additive montage
  blending, montage preview UI, or synced slot group orchestration.
- Validation: NOT RUN. Build with
  `cmake --build build/ecs-test --target aiverse_animation_montage_tests`
  and `ctest --test-dir build/ecs-test -R animation_montage --output-on-failure`.
- Next: run CMake/tests/CI for all new targets, fix build failures, then
  integrate first real FPS character model and character animation path.
  Future stages still needed for Humanoid retarget, motion warping,
  inertialization, physical animation, editor and performance.


## Stage 5: standalone test-link parity — 2026-11-18

- Implementation commit: `d1b88bed0e032a7dbc70d334f68347ad706ca3f6`.
- Added the same `platform::readContentFile` host stub used by the
  existing `aiverse_animation_tests` and `aiverse_unity_anim_tests`
  to four new standalone animation tests; these do not link the
  platform runtime. No production code changes.
- Validation: NOT RUN. GitHub Actions workflow is queued as of this handoff;
  a queued workflow is NOT a passing result.
- Next: inspect CI, fix actual compilation and runtime failures before
  claiming the new libraries are production-ready. Then integrate FPS
  character models/animation and continue with outstanding features.
