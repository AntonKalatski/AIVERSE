# AIVERSE universal animation — agent handoff

## Stage 1: presentation ECS host — 2026-11-18

- **Branch:** `feature/universal-ecs-animation-pipeline`, forked from
  `feature/universal-entity-inspector-fps`.
- **Implementation commit:** `82d5b99bc92ac5b142e61885f32801a3c8858832`.
- **Code:** `libraries/include/aiverselib/animation/EcsAnimationSystems.h`.
- **Test:** `tests/animation/ecs_presentation.cpp`.
- **Build:** `AIVERSE::animation_ecs` public INTERFACE target;
  `aiverse_animation_ecs_tests` / CTest `animation_ecs_presentation`.
- **AI skill:** `skills/gameplay/aiverse-animation-ecs/SKILL.md`.

### Delivered

Opt-in, genre-agnostic rendering/presentation ECS for C++ projects: asset, graph
player, parameter override, pose/world matrices, LOD and diagnostics components.
`stepAnimationPresentation(World&,float)` runs exactly one graph update per
entity and selectively evaluates pose for rendering. Immutable graph is shared,
per-entity player is separate. A missing/mismatched asset clears stale pose.
Invalid dt is ignored. The test source covers two independent Worlds, play
time, LOD clocks, overrides, reset and missing assets.

**Critical isolation rule:** this World MUST be separate from
`Simulation::world()`. The deterministic game handles input, movement,
weapon firing, collisions, networking, rollback, and gameplay events; this
presentation runtime only consumes projected values. Never serialize or COW
snapshot this World: `AnimationPlayerComponent` holds a shared mutable
`AnimationGraphInstance`. This is an intentional opt-in host, NOT a live
replacement for RtsView.

### Compatibility

- Existing `RtsView`, `AnimationGraph`, model formats, JSON assets and game
  save/checksum format are unchanged.
- No gameplay/physics authority or automatic FPS/RTS view binding was added.
- The public component API is new and optional for native game modules.
- Source model/graph lifetimes are managed by the host; no raw data ownership.

### Validation

**NOT RUN:** `cmake --build build/ecs-test --target aiverse_animation_ecs_tests`
and `ctest --test-dir build/ecs-test --output-on-failure -R '^animation_ecs_presentation$'`.
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


## Stage 6: extract-only Motion Warping — 2026-11-18

- Code commit `b02d2915e33c24df942f33acce971a84272d2189`.
- Public header `libraries/include/aiverselib/animation/AnimationMotionWarp.h`.
- `warpedRootAt` and `warpedRootDelta` calculate smooth corrections of
  root translation/rotation toward an explicit target in model-local space,
  bounded by a normalized warp window. Only non-looping clips are accepted.
- No authoritative transform changes, collision checks or network prediction.
- Test `tests/animation/motion_warp.cpp` / CTest `animation_motion_warp`
  checks position, yaw target, window, delta and invalid arguments.
- Validation: NOT RUN. Build target `aiverse_animation_warp_tests` and
  run `ctest --test-dir build/ecs-test -R animation_motion_warp --output-on-failure`.
- Remaining: motor integration/reconciliation, humanoid retargeting, full
  inertialization, physical animation, editor workflow and scalability.


## Stage 7: handoff/skill Markdown normalization — 2026-11-18

- Implementation commit: `b5c4df44bf084a24ab2d253956096c6a222bba92`.
- Fixed escaped inline code tokens in the animation handoff and skill.
- Fixed literal newline escape sequences in the AGENTS.md animation note.
- No C++ API/build behavior changed.
- Verification: inspected affected Markdown source, no build/test run.
- Next: CI CMake/build failures and real FPS visual integration. Do not
  call the PR complete while any animation CTest or CI job is pending.


## Stage 8: opt-in FPS skinned character visualization — 2026-11-18

- Implementation commit: `19ec502d7f78bb095180234b106e614d2f96158f`.
- `apps/fps/client.cpp`: optional `fps.visual.model`/`animationGraph`
  import; fallback compiled Idle/Walk/Run graph; separate presentation
  ECS World for remote actor models. Snapshot speed, moving, grounded,
  dead, reloading are projected into animation params per frame.
- A presentation entity is created for each remote actor slot; an FPS
  actor life change resets its animation player. Distance-based
  AnimationBudgetComponent throttles pose evaluation without stopping
  graph clocks. `Renderer3D::drawModel(ModelDraw)` consumes poses/matrices.
- If no model asset or compilation fails, the original cube placeholder
  path remains. First-person weapon viewmodel is untouched.
- CMake links `aiverse_fps_client` to `AIVERSE::animation_ecs`.
- Public JSON opt-in documented in `docs/fps.md`. Nothing is added to
  authoritative `fps::Module`, save/replication, or gameplay ECS.
- Validation: NOT RUN, including GPU visual preview; compile/test
  `aiverse_fps_client` alongside `aiverse_fps_tests`, then run CTest `fps`.
- Known limitations: no default FPS template contains a skinned model,
  model import still requires actual project assets, no weapon/hand IK,
  ragdoll, synchronized melee animation, authoritative Root Motion,
  montage/notify integration with FPS gameplay or animation retarget UI.
- Next: CI compile pass and optional real-model screenshot/replay fixture.
  Check imported graph parameter names (the host provides common defaults)
  and ensure no graphics regression or invalid asset handling.


## Stage 9: velocity-aware pose inertialization — 2026-11-18

- Code commit: `5e606780d6423bf526350b7ac7bab18c95b80e1b`.
- Public utility: `libraries/include/aiverselib/animation/PoseInertialization.h`.
- Added `PoseInertializer` with bounded Hermite-cubic residuals and per-bone
  local translation/scale/morph offsets plus quaternion-log angular offsets.
  Optional outgoing/incoming velocities allow continuity at transition start.
  API: `begin`, `advance`, `apply`, `active`, `reset`.
- Test: `tests/animation/inertialization.cpp`, CMake executable
  `aiverse_animation_inertial_tests`, CTest `animation_inertialization`.
- Compatibility: no existing animation graph or save format changed. The
  utility is opt-in; the native graph does NOT yet automatically execute
  inertialization on its state transitions. It is presentation-only.
- Validation: NOT RUN (GitHub-only source access). Required commands:
  `cmake --build build/ecs-test --target aiverse_animation_inertial_tests`
  and `ctest --test-dir build/ecs-test -R animation_inertialization --output-on-failure`.
- Next: validate CMake and existing FPS/RTS test suites; implement a
  presentation-only physical-pose blend adapter and shared animation budget
  with tests, then full per-genre integration and GPU stress benchmarks.


## Stage 10: external-physics pose blend adapter — 2026-11-18

- Implementation commit: `f890280d4d0d213eeaa6a58ccfb1d2b2ddbf4d6d`.
- Pure policy: `libraries/include/aiverselib/animation/PhysicalPoseBlend.h`,
  `blendPhysicalPose(Pose&, const Pose&, boneWeights, globalWeight)`.
- Validates all bone/morph data and masks before mutating output (atomic
  failure). Blends local translations, rotations, scales, morphs.
- Added optional `AnimationPhysicalPoseComponent` with externally supplied
  local skeletal pose/mask/weight to the independent presentation World.
- `stepAnimationPresentation` overlays physical pose after graph/montages
  and reevaluates every frame while a physical pose is active, regardless
  of animation LOD sampling interval.
- Test: `tests/animation/physical_pose.cpp` / executable
  `aiverse_animation_physical_pose_tests`, CTest `animation_physical_pose`.
- Does NOT create ragdoll bodies/constraints, convert world-space body
  transforms to local joints, or replace the authoritative physics solver.
  Such adapters belong to future independent stages.
- Compatibility: opt-in components; existing gameplay/save/rollback and
  RtsView paths unchanged.
- Validation: NOT RUN (no local checkout). Build with
  `cmake --build build/ecs-test --target aiverse_animation_physical_pose_tests`,
  then `ctest --test-dir build/ecs-test -R animation_physical_pose --output-on-failure`.
- Next: shared camera-size/priority evaluation budget, CI build fixes,
  production FPS character preview and more authoritative motor integration.


## Stage 11: shared presentation animation budget policy — 2026-11-18

- Code commit: `bd9cbf95177c4ade4adfeacd5ce6ec6ba3815541`.
- Header: `libraries/include/aiverselib/animation/AnimationBudgetPolicy.h`.
- Opt-in `AnimationSignificanceComponent` stores host-supplied projected
  screen size, importance, visibility, and an always-evaluate override.
- `assignAnimationBudgets(World&, AnimationBudgetPolicy)` ranks entities
  by force override, significance, and stable Entity ID; assigns full-rate,
  medium-rate, low-rate or offscreen intervals to AnimationBudgetComponent.
  Forced high-priority entities may exceed full-rate slots by design.
- Test `tests/animation/budget.cpp` / CTest `animation_budget_policy`
  exercises stable tie-breaking, forced importance, visibility and
  dynamic changes in priority; no gameplay authority or physics writes.
- This is a CPU scheduling policy, NOT a GPU crowd renderer, multithreaded
  evaluation implementation or actual measured frame-time budget.
- Compatibility: entirely opt-in; existing client assigns its own distance
  thresholds and does NOT yet call this significance allocator.
- Validation: NOT RUN. Build target `aiverse_animation_budget_tests` and
  execute `ctest --test-dir build/ecs-test -R animation_budget_policy --output-on-failure`.
- Next: comprehensive CMake build, CTest and screenshot CI; integrate budget
  policy with real camera visibility and GPU performance counters.


## Stage 12: AI skill API surface update — 2026-11-18

- Documentation commit `b796b09b115f8167e51315b8a5c09a916ea5abc7`.
- Updated `skills/gameplay/aiverse-animation-ecs/SKILL.md` to cover the
  current component/policy API: notifies, root extraction, montage, generic
  retarget, motion warping, inertialization, physical pose and budget.
- Documented non-authoritative boundaries and 8 new CTest fixtures plus
  necessary existing animation/FPS/RTS regression suites.
- No runtime/source changes; validation NOT RUN.
- Next action: CI green build & actual screenshot/profile tests before
  merging. PR remains Draft.


## Stage 13 — deterministic ECS root-motion motor adapter (2026-11-18)

- Code: `06f77b48940c4bbcccc0663bc98b025c3f3d5a41`.
- Added `libraries/include/aiverselib/animation/EcsRootMotionSystems.h`:
  fixed-point pre-quantized root-motion tracks, serializable intent, state,
  result and resource types plus `consumeRootMotion()` and
  `addRootMotionMotorSystem()`. Root translation is rotated through
  deterministic `fxSinTurns/fxCosTurns`; the authoritative transform is
  committed only after a host-supplied deterministic collision motor accepts
  its displacement. No float pose sampling inside `Simulation`.
- `RootMotionIntentComponent::sequence` must change to restart the same clip.
  Tick rate must match the authored track. Rejected movement advances the
  track clock but does not modify position/yaw. Downstream animation consumes
  the resulting authoritative transform, never the other way around.
- Public components must be explicitly registered for save/rollback by their
  host module; `io(Archive&,...)` supplied. New system is opt-in and not yet
  wired into FPS/RTS movers (which could otherwise move an actor twice).
- Test source `tests/animation/root_motor.cpp` and CTest
  `animation_root_motor` added. **NOT RUN (user deferred tests).**
  Build target: `aiverse_animation_root_motor_tests`.
- Next: create controlled offline clip->fixed-track exporter, then wire
  into one chosen motor replacing, not adding to, its existing movement source.
  Humanoid retargeting and ragdoll assets remain open.


## Stage 14 — semantic Humanoid Avatar Retargeting (2026-11-18)

- Implementation commits: `56adfa8a30b85c018391f66db08687dad2d2a433`, fixture correction `93e585e94c7536ef674bf97dab5bd4d8c8200d37`.
- Added `HumanoidRetarget.h`: explicit semantic bone roles for body, arms,
  legs, head, optional root/toes; `HumanoidAvatar::compile()` validates
  ancestry, duplicate bindings and pose order. `HumanoidRetargeter`
  transfers source rotations relative to source global bind orientation onto
  destination global bind orientation and compensates changed parents in
  destination local rotation. Pelvis/root motion transfer is opt-in, scaled
  from average measured hip-to-foot bind length when both feet are mapped.
- `placeFeet()` optionally uses existing TwoBoneIK for direct thigh/shin/foot chains.
- Test fixture: `tests/animation/humanoid_retarget.cpp`, CTest `animation_humanoid_retarget` registered. **NOT RUN** per user request.
- Caveats: semantic maps are explicit; no automatic detection/Unity muscle
  value import, twist-chain redistribution, universal different-axis
  coordinate conversion or production DCC-compatible Avatar profile yet.
  No direct integration into Unity import pipeline or Editor window yet.
- Next: authored fixed root-track export, ragdoll definition/pose bridge,
  avatar editor workflow and integration/optimization.


## Stage 15 — procedural Humanoid Ragdoll asset and optional physics constraints (2026-11-18)

- Code commit: `a8551175fee87a0beb9cc642db381b9168fbe5a2`.
- Added `libraries/include/aiverselib/animation/RagdollAsset.h`:
  generates capsule/sphere rigidbody `physics::World` scene JSON using
  semantic humanoid bone links; body-to-bind offsets allow conversion of
  simulated world rigid poses back into model-local skeletal bone poses.
  Features joint anchors, authored body radii/density/friction, tick rate.
- Extended optional `engine/physics/World.cpp` hinge scene parsing with
  validated `"limits":[min,max]` and Box3D angular joint limits. Existing
  scenes without limits keep their prior behavior.
- Added `tests/animation/ragdoll.cpp`, CTest `animation_ragdoll_asset`.
- **NOT RUN** (user requested deferred tests). Physical tests require
  `AIVERSE_ENABLE_PHYSICS=ON`, rigidbody state readback bridge, and an
  actual long-running stability scenario.
- Limits: generated ragdoll does not yet implement self-collision filtering,
  full 3-axis ball/socket constraints, bone drive / motors, transition to get-up
  animation or server-authoritative physics. Ragdoll is a separate opt-in
  Box3D world; animation runtime only consumes returned poses.
- Next: add explicit physics body readback and recovery bridge; then
  animation authoring tools and parallel/LOD integration.


## Stage 16 — offline fixed Root Motion track authoring (2026-11-18)

- Code commits: `8eb063100eba103076165ddfad9158d30df48d8b`,
  include fix `e47b7eea119f8497f19e4e03cc7f6b90f5c90c88`.
- New `RootMotionBaker.h` samples an authored `AnimClip` at tick
  boundaries, extracts rigid root deltas, quantizes each XY/Z and yaw to
  fixed-point Q16 and validates maximum displacement/rate.
- `encodeRootMotionTracks`/`decodeRootMotionTracks` use a versioned
  `aiverse-root-motion-v1` JSON with raw integers, suitable to load once
  during authoritative game setup. The simulation still uses fixed tracks
  only; no floating point evaluation in game ticks.
- Host tool `aiverse_root_motion_bake MODEL CLIP ROOT --id N --rate N
  [--loop] [--out FILE]`. Test target
  `aiverse_root_motion_bake_tests` CTest `animation_root_motion_bake`.
- NOT RUN by user instruction. The project must explicitly import/validate
  the exported JSON, register authoritative resource persistence, and
  replace an existing motor during opt-in integration. Unsupported: curve
  compression, bulk exporter, auto-rebind to imported controllers.
- Next: physics-world readback adapter & recovery, editor tooling and
  parallel evaluation integration; later full validation/CI.


## Stage 17 — physics ragdoll readback and get-up recovery (2026-11-18)

- Implementation commit `e9c42cbc080194707447bedd1dfc23369d777281`.
- New optional `PhysicsRagdollBridge.h` reads Box3D ragdoll rigid
  transforms after fixed physics ticks and reconstructs local bone
  poses via `RagdollAsset::applyBodyPoses`. Requires the opt-in
  `AIVERSE::animation_physics` target / `AIVERSE_ENABLE_PHYSICS=ON`.
- `AnimationRecoveryComponent` extends the independent presentation
  ECS. On get-up request, disable physical-pose blending, supply the
  last pose in `from`, set `duration` and `requested=true`.
  `stepAnimationPresentation` cross-fades to the current AnimationGraph
  output through velocity-aware `PoseInertializer`, ignoring LOD
  throttling during recovery. No authoritatively moved entity.
- Added `tests/animation/recovery.cpp`, CTest `animation_recovery`.
- **NOT RUN** per user preference. Requires visual get-up animation,
  Box3D fixed step tests and collision checks before production.
- No RtsView change or gameplay protocol/save format change.
- Next: clip/retarget/ragdoll authoring UI, asset pipeline and
  independent animation evaluation scheduling.


## Stage 18 — Humanoid Profile authoring data (2026-11-18)

- Commit `f3a4d3838c8a185ba7ad2ad2224bbf71f76aebec`.
- `HumanoidProfile.h` names semantic roles, conservatively suggests
  full bone paths from source node names, and reads/writes
  `aiverse-humanoid-v1` JSON profiles. Ambiguous/unmatched roles
  remain editable rather than inventing skeleton correspondences.
- Added `tests/animation/humanoid_profile.cpp` / CTest
  `animation_humanoid_profile`. NOT RUN by user request.

## Stage 19 — Workspace/MCP advanced animation asset operations (2026-11-18)

- Commit `5ef0bda9166f06dbf39da1eb2d3a9f8848a64029`.
- Extended `apps/editor/core/AnimationOps.cpp` with
  `animation_avatar_suggest`, `animation_avatar_validate`,
  `animation_ragdoll_generate`, `animation_root_motion_generate`,
  `animation_montage_validate`, `animation_asset_get` and
  `animation_asset_save`. Native projects and agents share authoring.
- Assets stored under `animations/{avatar,ragdoll,root_motion,montage}/`,
  validation before writes, bounded size, project root confinement,
  optimistic concurrency `expected` on updates.
- NOT RUN. Editor and MCP integration builds remain to be checked.

## Stage 20 — visual asset authoring window (2026-11-18)

- Commits `672475876f35e348aa043e571621117f6a86349a`,
  string-input correction `ab5517cf5b40b439709157b527efe8c20510df6c`.
- New dockable `apps/editor/windows/AnimationAssetsWindow.cpp`
  registers `animation_assets_open`. Editor tabs:
  Humanoid Avatar bone-role assignment; procedural ragdoll generation
  with dimensions/density/joint-angle controls; root-motion fixed
  clip baker; montage sections/notifies/transition editing. Save/load
  invokes the same Workspace operations as MCP agents. JSON preview
  and concurrency tracking included.
- NOT RUN. TODO: build editor, exercise UI interactions and constraints,
  verify the Avatar/Ragdoll/Root/Montage project assets end to end.
  This is an authoring UI, not a runtime MontageGraph node/Unreal editor clone.
- Next: multithreaded presentation evaluation with thread-safe split stages,
  graph hot reload, and performance/golden screenshots.
