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


## Stage 21 — optional parallel evaluation for presentation ECS (2026-11-18)

- Implementation: `dbd7990bf2ee2d84444299ef89f071bb362fb69e`,
  parity fixture correction `121793207b659c041628dab40701fd7003dbbcd6`.
- Refactored `EcsAnimationSystems.h` into `prepareAnimationPresentation()`
  on one thread and `evaluateAnimationPresentationRange()` on stable
  disjoint player pool ranges; serial wrapper remains public and default.
- New `AnimationJobScheduler.h`: graph safety scan disallows custom
  native/Lua callbacks, contact rigs and environment callbacks on worker
  lanes. `dispatchAnimationPresentation` accepts a host-owned blocking
  job dispatcher, `stepAnimationPresentationParallel` provides a
  `std::jthread` fallback. Small sets and unsafe graphs run serially.
- CMake target `AIVERSE::animation_ecs` now links Threads when available.
  Added `tests/animation/jobs.cpp` / CTest `animation_jobs`
  comparing 96 per-entity results with the serial path.
- **NOT RUN** by user instruction. Check COW pool uniquification,
  data races/TSAN, callback safety, nested graph motion matching, and
  actual frame times before enabling this path by default. Existing
  FPS/RTS use the previous serial integration.
- Next: reusable hot-reload asset registry, graph notify support, GPU
  crowd/pose-sharing profiling and full editor/run-time validation.


## Stage 22 — AnimationGraph Clip Notifies and ECS export (2026-11-18)

- Implementation `073be2eb7b99c4e79c7cd8e9914a72f73ed947c0`,
  test boundary correction `925daf6e5e35b37fdcc9916bf518a7a261a32713`.
- `AnimationGraph::Node` accepts `notifies:[{time,name}]` on
  `kind:"clip"`. Compiles bounded, validated keys, supports loops,
  speed changes, start-at-zero and multiple loops in one frame.
- `AnimationGraphInstance::update()` now preserves unwrapped per-clip
  clocks, emits per-frame `AnimationGraphNotify` with node/name,
  frame-relative phase and cycle; nested graph events are prefixed.
  Event quota is 1024/frame and overflow marks unhealthy.
- Opt-in `AnimationGraphNotifiesComponent` receives graph events in
  separate presentation ECS alongside Montage notifies.
- Regression source `tests/animation/graph_notifies.cpp`,
  CTest `animation_graph_notifies`. **NOT RUN** by user choice.
- Compatibility: previous JSON graphs remain valid; existing
  RtsView legacy clip event mechanism unaffected. Controller-specific
  state-machine notifies and audio routing still require integration.
  Notifies never directly authorize hits/damage/reload commits.
- Next: asset registry hot-reload & performance and integration.


## Stage 23 — transactional AnimationGraph hot reload (2026-11-18)

- Implementation commit: `d4342938e03ec411dbed3f107c57423a74426d00`.
- New `AnimationHotReload.h`: host-owned
  `AnimationHotReloadRegistry` with in-memory `compile(key,model,doc,revision)`,
  optional `pollFile()` timestamp loader, last-known-good graph fallback,
  error field, and `apply(presentationWorld)` rebinding matching
  `AnimationGraphKeyComponent` entities to new immutable graphs.
- No runtime swapping on the simulation thread. Existing per-entity
  graph player resets when its asset pointer changes, per established
  presentation ECS behavior; state-time migration is not attempted.
- Test source `tests/animation/hot_reload.cpp`, CTest
  `animation_hot_reload`. NOT RUN by user instruction.
- Project clients must register/poll assets explicitly; editor save by
  itself does not automatically broadcast graph updates to remote
  running clients. Model lifetime must outlive the registry/player.
- Next: GPU crowd/pose-sharing opt-in and integrate camera budget policy
  in FPS client; full test/build/visual validation later.


## Stage 24 — GPU baked crowd ECS policy (2026-11-18)

- Commit `352d3ab01ee7d741debc46e42078ee604636bcb6`.
- Added `AnimationCrowdSystems.h`: per-entity `Animator` clip clocks,
  Idle/Walk/Run/Death selection, synchronized Walk/Run blend, GPU
  `BakedSample` output; intentionally avoids CPU pose/palette evaluation.
- Uses existing `bakeAnimations()` and `Renderer3D::drawModel` instancing.
  No new GPU shader/compute backend claimed. CTest
  `animation_crowd_policy` added (NOT RUN).

## Stage 25 — FPS instanced crowd and significance integration (2026-11-18)

- Commit `3b452909ae88077cdf5869a166aaa0837b1545af`.
- `apps/fps/client.cpp` opts into baked crowd mode using
  `fps.visual.bakedCrowd=true` for simple imported clips only. Complex
  `animationGraph` remains palette. If GPU baked preparation fails,
  fallback to the existing graph/box path.
- Palette path uses shared `assignAnimationBudgets` with camera
  projected-size significance rather than separate hard-coded distance LOD.
- `docs/fps.md` now explains both variants.
- NOT RUN, FPS GPU/metal screenshot and group draw-call benchmarking needed.

## Stage 26 — unified cosmetic animation cue collection (2026-11-18)

- Commit `bc32c4452debf34b5c54a49554701d6fe5a6cc9c`.
- Public `AnimationEventDispatch.h` combines the per-frame Graph Clip
  notifies and Montage events into a stable bounded cue collection;
  supports a host callback sink, no gameplay writes.
- Added `tests/animation/cues.cpp` / CTest `animation_cues`.
  NOT RUN.

## Stage 27 — FPS positional footstep events (2026-11-18)

- Commit `cf3a0ae77f2552c4ae052efe16bf167be3fded2d`.
- FPS palette clients now attach `AnimationGraphNotifiesComponent`
  and route authored `step`/`footstep`/`land` cosmetic notifies
  to positional step audio for remote actors. Gunfire, damage and
  reload remain governed by authoritative server counters.
- NOT RUN, inspect replay/audio and event double-delivery visually.

## Stage 28 — ragdoll self-collision filtering (2026-11-18)

- Commit `fe8017c2ec435ba7d11fa487f2d9810018230b15`.
- `physics::World` JSON bodies accept validated `collisionGroup`;
  `RagdollBuildOptions::collisionGroup=-1` emits a negative Box3D
  collision group for all generated parts. Project code must give
  **different negative group IDs to different characters in the same
  physics World**. This prevents self-collision without disabling
  contact with the environment and other ragdolls.
- NOT RUN (physics disabled in typical builds).
- Next: explicit host integration parity, tests/CI, Editor build,
  real imported avatar and FPS CPU/GPU screenshots.


## Stage 29 — FPS AnimationGraph file hot reload (2026-11-18)

- Implementation commit `55e1106fde501998c67ebf7a5eab0f75a781874f`.
- `apps/fps/client.cpp` supports optional `fps.visual.graphFile`
  pointing to a project-local AnimationGraph JSON. The client loads and
  recompiles through the shared `AnimationHotReloadRegistry`, polls
  for changed timestamps every 30 render frames, keeps the last good
  compiled graph on failure, and propagates a valid revision to existing
  presentation entities using `AnimationGraphKeyComponent`.
- The authoritative FPS sim, save state, network snapshot/checksum and
  game mechanics are unchanged. Graph replacement restarts the visual
  player, not the gameplay actor. `docs/fps.md` documents opt-in JSON.
- **NOT RUN** by user instruction; test compilation/visual reload later.
- Next: complete remaining host/asset links, review code statically and
  keep this PR Draft until proper build/CTest/GPU validation.


## Stage 30 — shared runtime Montage asset loader (2026-11-18)

- Code commit `09ea93e785c195195039944d675feccdcfbb9214`.
- New public `AnimationAssetLoader.h` implements
  `compileMontageAsset(const Model&, const Json&)` against the same
  `sections/slot/blendIn/blendOut/maskRoot/notifies` JSON authored by the
  editor and Workspace/MCP, with strict finite numeric and bone/clip
  validation, and one immutable compiled Montage shared by all instances.
- Added deferred test `tests/animation/asset_loader.cpp` CTest
  `animation_montage_asset_loader` (NOT RUN).
- No authoritative game state or existing save/protocol modification.
- Next: connect these project assets to optional FPS visual actions
  (shot/reload/death) using confirmed replicated actor state, still cosmetic;
  preserve existing fallback and handle actor lifecycle cleanup.

## Stage 31 — explicit every-stage agent continuity rule (2026-11-18)

- Branch: `feature/universal-ecs-animation-pipeline`, draft PR #22
  targeting `feature/universal-entity-inspector-fps`.
- Implementation / policy commit: `e0c15721f97f90a074bc20971c36f590a0492d99`.
- Changed file: `AGENTS.md` only. Its active animation section now specifies
  that **every** code/editor/tool/performance stage, including partial or
  failing work, must be followed by a **separate committed handoff** in
  `docs/agent-handoffs/animation-universal.md` BEFORE another stage begins.
  Every handoff must contain exact code SHA, branch, affected files, behavior
  and design, public API / asset / save compatibility, exact test status,
  unfinished work/risks and the next agent's concrete first task.
- Existing Stage 25 **already** documents FPS baked-crowd and
  significance-budget integration (`3b452909ae88077cdf5869a166aaa0837b1545af`);
  earlier Stages 24–30 are retained, not rewritten.
- API/runtime/save changes: none; this stage is process documentation only.
- Tests: **NOT RUN** (documentation-only change; user requested to defer
  animation tests). CI passes are not asserted.
- Current feature checkpoint: latest substantive code from Stage 30,
  `09ea93e785c195195039944d675feccdcfbb9214`, provides the shared
  Montage asset compiler `AnimationAssetLoader.h`. Existing Stage 30
  lists its limitations and deferred test `animation_montage_asset_loader`.
- **Next actionable stage for another agent:** inspect
  `libraries/include/aiverselib/animation/AnimationAssetLoader.h`,
  `apps/fps/client.cpp`, and `docs/fps.md`; integrate optional authored
  Montage assets for cosmetic FPS shot/reload/death sequences driven by
  replicated authoritative state. Avoid double-firing cosmetic events,
  preserve cube/palette/baked fallbacks and remote-actor lifecycle cleanup.
  Add a focused test fixture, do not claim test success before execution,
  commit code first and append a Stage 32 handoff in a separate commit.

## Stage 32 — MotionWeave C++/CMake/editor naming (2026-11-18)

- **Branch:** `feature/universal-ecs-animation-pipeline`, PR #22 (Draft)
  targeting `feature/universal-entity-inspector-fps`.
- **Code implementation commit:** `b4b55bd3b38bd9110c072f9456b4300cdbe46872`.
- **Code paths:** `CMakeLists.txt`, `modules/fps/CMakeLists.txt`,
  `apps/fps/client.cpp`, `apps/editor/core/AnimationOps.cpp`,
  `apps/editor/windows/AnimationAssetsWindow.cpp`,
  `apps/editor/windows/AnimationGraphWindow.cpp`,
  `apps/editor/windows/AnimationRigWindow.cpp`, new
  `libraries/include/aiverselib/motionweave/MotionWeave.h`,
  `tests/animation/motionweave_api.cpp`.
- **Architecture / behavior:** system branding is **AIVERSE MotionWeave**.
  Canonical public CMake target `AIVERSE::MotionWeave` (also
  `AIVERSE::motionweave`); C++ alias `aiverse::motionweave`, facade
  `MotionWeave::update(World&,float)`. The FPS presentation client and
  Workspace/Editor source use the new branded namespace. Animation
  graph/rig/assets tab titles now show MotionWeave.
- **Backwards compatibility:** the new aliases call the **same**
  `aiverse::lib::animation` implementation and `AIVERSE::animation_ecs`
  target. Legacy C++ types, files, graph JSON, save/network format,
  existing component IDs, and editor dock window IDs were NOT renamed.
- **Documentation changed in this separate handoff commit:**
  `AGENTS.md`, `docs/fps.md`, `docs/animation-graphs.md`,
  new `docs/motionweave.md`, new
  `skills/gameplay/aiverse-motionweave/SKILL.md`, and compatibility notice
  in `skills/gameplay/aiverse-animation-ecs/SKILL.md`.
- **Validation:** **NOT RUN**, per user request to defer build/tests.
  New native API fixture `aiverse_motionweave_api_tests` /
  CTest `motionweave_api` checks branded API and alias compatibility,
  but no passing result is claimed. CI status was not verified.
- **Risks / known limitations:** entire animation feature PR still
  requires compiler checks, editor/FPS graphics preview, CTest/TSAN,
  fixed-point motor parity, and GPU crowd profiling. This step changes
  names/API access only, not unimplemented gameplay functionality.
- **Next agent's concrete task:** read current head and Stage 30
  Montage asset compiler (`AnimationAssetLoader.h`) plus
  `apps/fps/client.cpp`; connect authored shot/reload/death Montage
  cosmetics to confirmed FPS replicated state without changing
  authoritative damage/reload, with actor lifecycle cleanup and
  fallback behavior. Commit code first, then **Stage 33** handoff
  separately; mark tests NOT RUN until executed.

## Stage 33 — persistent MotionWeave worker pool (2026-11-18)

- **Branch / PR:** `feature/universal-ecs-animation-pipeline`, PR #22 Draft;
  base `feature/universal-entity-inspector-fps`.
- **Implementation commit:** `ad5b1a854ac6009caadfc830d75a6bfee4165fa0`.
- **Code paths:** new
  `libraries/include/aiverselib/animation/MotionWeaveWorkers.h`,
  `tests/animation/motionweave_workers.cpp`, root `CMakeLists.txt`.
- **Design/API:** `MotionWeaveWorkerPool` is a host-owned fixed worker pool,
  defaulting to up to seven background workers, plus the submitting frame
  thread. `parallelFor(taskCount,callback)` runs jobs via one atomic task
  index with a blocking synchronization barrier. The pool persists across
  frames rather than constructing fresh std::jthread workers each update.
  Serialized submissions, error capture/rethrow AFTER all workers have
  completed, and WebAssembly sequential fallback are included.
- **Compatibility:** legacy `stepAnimationPresentationParallel` and
  `MotionWeave::update` are untouched in this stage; the pool is an opt-in
  reusable primitive for the next integrated scheduler stage. Gameplay
  simulation, ECS schema, save, network and editor APIs unchanged.
- **Tests:** `aiverse_motionweave_worker_tests` / CTest
  `motionweave_workers` was added for worker reuse, task completion,
  barrier, post-exception recovery, empty tasks, and single-thread fallback.
  **NOT RUN** (user explicitly deferred execution); no claim about CI.
- **Known risks:** workers do not support concurrent World structural
  mutations. No implicit thread-safe access to Lua, native callbacks or
  shared graph state; graph classification must be added before use.
  Exception propagation doesn't make partially written output transactional.
  The old scheduler still creates fresh threads and globally falls back.
- **Next stage:** read `AnimationJobScheduler.h` and
  `EcsAnimationSystems.h`, add an index-selected graph evaluation helper
  that reuses pre-detached ECS write pools. Partition worker-safe entities
  from callback/rig/environment-bound entities and run those serially only
  AFTER the worker barrier; use `MotionWeaveWorkerPool::parallelFor` for
  safe independent tasks. Commit the implementation first, then Stage 34
  handoff in a separate commit.

## Stage 34 — hybrid safe/callback animation scheduler (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22.
- **Implementation SHA:** `3ee29809508bf04225318ba787578b114a7f95a3`.
- **Files:** `EcsAnimationSystems.h` now supports evaluating selected
  dense ECS player indices after write-pool detachment;
  new `libraries/include/aiverselib/animation/MotionWeaveScheduler.h`
  classifies each entity by its compiled graph/parameters; updated
  `libraries/include/aiverselib/motionweave/MotionWeave.h` adds a host-owned
  scheduler overload; new `tests/animation/motionweave_hybrid.cpp` and
  CMake test registration.
- **API:** `MotionWeaveScheduler::update(World&,float)` returns
  `MotionWeaveScheduleStats`, with an optional worker count / scheduler
  options in constructor. New
  `MotionWeave::update(World&,float,MotionWeaveScheduler&)` calls that
  scheduler. Existing two-argument `MotionWeave::update` keeps exactly
  its old sequential behavior.
- **Threading contract:** all writable ECS pools are prepared on the
  submitting thread BEFORE dispatch. Each job owns disjoint player
  indices, can compute independent clips, controller blends, IK and
  motion matching; a persistent `MotionWeaveWorkerPool` runs chunks
  via an atomic work index. Graphs with native functions, Rig/Stack
  traits, custom environment or contacts take the **caller-thread
  serial lane**, and are evaluated only AFTER the worker barrier.
  An unsafe entity does not force the entire World to be sequential.
  Per-entity event outputs remain accessible after the barrier.
- **Compatibility:** simulation World, fixed gameplay, save/network
  schema, editors and existing RTS/FPS code unchanged. Scheduler is
  opt-in until a host explicitly passes it.
- **Tests:** deferred fixture `motionweave_hybrid` compares 100 entities,
  with six native-callback graphs, across serial and hybrid frames
  and verifies that native callback executes on the caller. **NOT RUN**
  by user request; no CI result claimed.
- **Limitations:** per-graph safety classification is deliberately
  conservative; do not run Lua/native callbacks on worker threads.
  Fixed-sized chunks have no cost-weighted balancing yet; persistent
  workers are instantiated when scheduler is constructed. Workers
  must not receive concurrent mutations/snapshot activity from host.
  Rendering and GPU uploads still occur on main thread.
- **Next:** add an adaptive cost model and variable-sized chunks
  based on graph complexity/pose budget, expose useful per-frame
  diagnostics, then wire the host-owned scheduler into FPS.
  Commit the next code stage separately, followed by Stage 35 handoff.

## Stage 35 — cost-aware dynamic work distribution and telemetry (2026-11-18)

- **Branch:** `feature/universal-ecs-animation-pipeline`, PR #22 Draft.
- **Implementation SHA:** `ad80cbc7b76304f639e45814f48be16769f3b72e`;
  deferred fixture correction `f24b51224b1bcd2f9eb86913496fedbf29aa85b3`.
- **Files:** new `libraries/include/aiverselib/animation/AnimationWorkPolicy.h`,
  updated `MotionWeaveScheduler.h` and `CMakeLists.txt`, new
  `tests/animation/motionweave_cost.cpp`.
- **Design:** `estimateAnimationWork(AnimationGraph&)` estimates workload
  from node count/bones and operation kinds (IK/controller/motion
  matching heavier than bind/clip). Optional presentation-only
  `AnimationWorkHintComponent` supplies a bounded cost multiplier or
  main-thread requirement. `partitionAnimationWork` creates contiguous
  jobs with roughly equal estimated work, not merely equal entity
  counts. The persistent pool's atomic task queue distributes jobs
  dynamically to idle workers.
- **API / telemetry:** `MotionWeaveScheduleOptions` now exposes
  `minimumParallelActors`, `minimumParallelCost`,
  `chunksPerWorker`, `minChunkActors`. `MotionWeaveScheduleStats`
  extends counters with estimated work, dispatched entities, maximum
  batch cost, and timings for parallel-safe and main-thread callback
  phases. `MotionWeaveScheduler::setOptions` supports host tuning
  between frames; original fields remain present.
- **Compatibility:** no authoritative GameModule, ECS persistence,
  save/network/renderer changes. Safe/unsafe entity classification
  and callback barrier from Stage 34 are preserved.
- **Tests:** new CTest `motionweave_cost_balancing` checks weighted
  chunks, cost clamps, and explicit serial pinning. **NOT RUN**
  by user decision; no green CI claimed.
- **Limits:** heuristics estimate static costs rather than measured
  per-node execution time. Thread execution order varies but per-entity
  output remains isolated. Timings are for presentation diagnosis,
  not deterministic gameplay logic. Not yet enabled in FPS.
- **Next:** inspect `apps/fps/client.cpp`, `MotionWeave.h` and
  `AnimationCrowdSystems.h`; attach a host-owned
  `MotionWeaveScheduler` to FPS palette animation, preserve baked GPU
  path for eligible crowds, and expose optional scheduler diagnostics.
  Add code/tests, then a separate Stage 36 handoff before new work.

## Stage 36 — FPS automatic MotionWeave CPU/GPU host integration (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, PR #22 Draft.
- **Code commit:** `00c066b3630615c03a617aea1b94722106871148`.
- **Changed paths:** `apps/fps/client.cpp`, new
  `libraries/include/aiverselib/animation/AnimationBackendPolicy.h`,
  `tests/animation/motionweave_backend.cpp`, root `CMakeLists.txt` and
  `docs/fps.md`.
- **Behavior/API:** native FPS palette Graph path now lazily creates
  a host-owned `MotionWeaveScheduler` once there are at least 16 remote
  presentation entities; new scheduler overload performs safe graph
  updates on reusable background workers, expensive work-balanced
  batches and Lua/native/contact work on main thread. The simple
  GPU baked-instancing path is selected through new backend policy:
  `fps.visual.animationMode` accepts `auto` (default), `cpu`, `gpu`.
  The old `bakedCrowd:true` remains a GPU preference alias.
  Auto promotes to GPU at `gpuEnterActors`=24 living remote actors and
  returns to CPU at `gpuExitActors`=12; supported only for simple
  clip-only models with an uploaded GPU bake and without a custom
  AnimationGraph/graphFile.
- **Lifecycle/performance:** switching mode clears and recreates
  only presentation ECS entities, never authoritative simulation
  actors; disappearing remote actor slots are now cleaned up from
  the animation entity/life maps. Optional
  `fps.visual.animationStats:true` shows CPU/GPU selection, worker
  count, safe/callback partitions, estimated work/jobs and CPU time.
- **Compatibility:** server-side movement, shooting, damage,
  netcode, save/checksum and editor asset formats unchanged.
  Complex animation with IK/Montage/graphFile defaults to the CPU
  Graph path; GPU baked fallback and cubes remain for unavailable
  assets. Earlier `bakedCrowd` opt-in remains recognized.
- **Tests:** `motionweave_backend_selection` CTest fixture added for
  auto hysteresis, explicit CPU/GPU and complex graph fallback.
  **NOT RUN** (user requested to defer build/tests and QA). FPS
  graphics/TSAN and performance benchmarks also NOT RUN.
- **Risks:** CPU/GPU selection currently recreates visual players
  on a backend change, so clip phase resets and a visual pop may
  occur; long-term asset/pose sharing and phase migration need a
  follow-up. The worker pool does not touch GPU rendering; baked
  skinning runs through existing Renderer3D path and only CPU
  animator clocks are processed. The FPS client's existing
  audio callbacks must remain caller-thread-only.
- **Exact next stage:** expand `docs/motionweave.md` and
  `skills/gameplay/aiverse-motionweave/SKILL.md` with the worker
  scheduler, backend-policy and FPS opt-in contracts, add better
  scheduling diagnostics and multi-scene edge fixtures; then do a
  static code audit (WITHOUT running deferred tests), amend code
  in its own commit and follow with Stage 37 handoff.

## Stage 37 — reusable graph-cost memoization and public scheduler documentation (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, PR #22 Draft.
- **Implementation SHA:** `07c0097b750f95b6a6f9671c0d412a5fcd3e9363`.
- **Files:** updated `libraries/include/aiverselib/animation/MotionWeaveScheduler.h`
  to retain dense work-array capacity across frames and memoize per-graph
  safety/cost classification once per update (no persistent raw-pointer
  cache across hot-reload boundaries); updated root `CMakeLists.txt`,
  `docs/motionweave.md` and
  `skills/gameplay/aiverse-motionweave/SKILL.md`, added
  `tests/animation/motionweave_cache.cpp`.
- **Behavior/API:** every distinct immutable graph is assessed once per
  frame rather than rewalking every node for each of potentially thousands
  of identical characters. `MotionWeaveScheduleStats` now exposes
  `uniqueGraphs` and `offscreenEntities`. Characters with valid hidden
  poses have lower *estimated* scheduling cost, while real pose/clock
  behavior is unchanged; the scheduler still runs graph updates.
  The three-argument `MotionWeave::update(World&,dt,scheduler)` is the
  recommended persistent multicore host entry point. The legacy
  two-argument call deliberately remains sequential.
- **Compatibility:** no changes to ECS gameplay, save/rollback,
  network protocol or graphics backend. Cost hints only govern how
  independent visual entities are assigned to CPU worker jobs.
- **Tests:** added deferred `motionweave_graph_cost_cache` CTest for
  repeated graphs, hidden entities and restart; **NOT RUN** (user
  explicitly requested test deferral). No passing CI claim.
- **Risks:** weighted cost model is a heuristic, not real-time
  calibrated throughput prediction; one costly actor cannot be split
  internally into several workers, but separate actors are stolen
  dynamically. No GPU uploads/Renderer3D calls on workers. Host must
  not concurrently mutate/snapshot the presentation ECS World.
- **Next agent first action:** review all Stage 33–37 source files for
  C++ portability and lockstep/ThreadSanitizer safety, focusing on
  `MotionWeaveWorkers.h`, `MotionWeaveScheduler.h` and
  `apps/fps/client.cpp`. Since user deferred tests, do not invoke
  CMake/CTest yet; document any static fixes in a new code commit,
  then append Stage 38 before advancing to a different feature.

## Stage 38 — static correctness pass: worker callable lifetime and FPS GPU release (2026-11-18)

- **Branch / PR:** `feature/universal-ecs-animation-pipeline`, PR #22 Draft.
- **Code SHA:** `029c25494e68d6145d92494942516249bf306a25`.
- **Paths:** `MotionWeaveWorkers.h`,
  `apps/fps/client.cpp`, new
  `tests/animation/motionweave_worker_reuse.cpp`, root `CMakeLists.txt`.
- **Worker implementation:** a `parallelFor` call now creates one
  submitting-thread-owned std::function, and workers borrow a pointer
  under the scheduler barrier instead of copying std::function on every
  wake-up (which could allocate/throw in a noexcept worker). The pointer
  remains valid until ALL background threads have completed the
  outstanding batch. The pool is still persistent; concurrency, errors
  and World mutation restrictions remain the same.
- **FPS implementation:** `loadAnimatedCharacter` now keeps a
  `pendingModel` unique_ptr alive across import, GPU upload, graph
  compilation and bake. If an intermediate step throws, partially
  uploaded GPU model resources are released instead of silently leaking.
  Existing cube fallback remains. Asset, save and network formats
  unchanged; rendering and GPU resource lifetime stay on main thread.
- **Test:** CTest `motionweave_worker_reuse` added for 40 repeated
  dispatch frames, per-index exactly-once work, worker exception
  propagation and reuse. **NOT RUN**, per user's test deferral.
- **Remaining limitations:** this was static inspection, NOT compilation
  or TSAN; no claim the new scheduler outperforms serial graph updates.
  Actual editor/FPS builds, GPU screenshots and concurrent snapshot
  misuse checks must still be executed later. The old opt-in helper
  `stepAnimationPresentationParallel` still uses per-frame jthreads;
  new code must use the persistent `MotionWeaveScheduler` instead.
- **Next first task:** review CMake target link/dependency placement
  (desktop/Android/WebAssembly) and introduce a performance smoke
  fixture to compare serial vs persistent workers at realistic actor
  counts, without executing it yet; document that stage separately.

## Stage 39 — parallel pure Rig / FullBodyIK classification (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, PR #22 Draft.
- **Implementation SHA:** `e45337af0cd664ef6ade261c3fcb4f6b60e1cec8`.
- **Files:** updated `libraries/include/aiverselib/animation/AnimationJobScheduler.h`,
  new `tests/animation/motionweave_parallel_rig.cpp`, root `CMakeLists.txt`.
- **Architecture:** compile-time Rig units of kinds Transform/Copy/Aim,
  TwoBoneIK/Limit/AngularLimit and FullBodyIK **without contact constraints**
  may now run in the persistent worker pool. This code was inspected:
  `AnimationRig::execute` and contact-free `FullBodyIK::solve` use
  per-instance state and local scratch, not a shared callback registry.
  Rig units containing `SurfaceLocomotion` or contact-aware FullBodyIK
  are still classified as caller-thread work. Graph
  `Native`, `Stack`, Lua trait callbacks and externally supplied
  `AnimationEnvironment` remain strictly serial.
- **Public API:** `workerSafeGraph` now includes Rig/FullBodyIK headers
  for explicit unit inspection; existing MotionWeave API and FPS
  three-argument scheduler overload are unchanged.
- **Compatibility:** all existing save/network/schema formats remain.
  No simulation ECS or physics authority changes. Workers only
  touch presentation pose state owned by their entities.
- **Tests:** `motionweave_parallel_rig` compares serial and multi-lane
  solutions of 80 identical imported FullBodyIK graph instances.
  **NOT RUN** per user instruction (including TSAN/Metal rendering).
- **Risk:** pure-rig safety is based on the currently implemented
  rig and IK operations. Any future Rig unit that invokes host callbacks
  must be added to the unsafe classification. Input/World mutations
  during worker evaluation remain forbidden.
- **Next:** add an opt-in executable CPU-only benchmark for actor
  counts ~100/1000/5000 and representative graph/IK workloads.
  Do not execute it yet; document benchmark command and limitations.
  Commit implementation first, then Stage 40 handoff separately.

## Stage 40 — standalone CPU multithreaded crowd/IK benchmark (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, PR #22 Draft.
- **Code commits:** `d189ae73d80ae7d59265063cde3f6ff05c6a0159`,
  syntax/include correction `9c15fe95694902baa99916316c3343c5c2236329`.
- **Files:** new `tools/animation/motionweave_bench.cpp`,
  changed root `CMakeLists.txt` and `docs/motionweave.md`.
- **API/performance setup:** opt-in desktop/tool executable
  `aiverse_motionweave_bench` creates paired presentation ECS Worlds
  with identical compiled clip Graph or pure FullBodyIK Rig; warms both,
  then measures mean serial vs persistent pool frame times over requested
  entity/frame counts, checks representative pose parity, and reports
  worker/job counts. Flags: `--actors N`, `--frames N`, optional `--ik`.
  Does not need GPU, texture files or a game server.
- **Build entry:** configure tools, then
  `cmake --build build/ecs-test --target aiverse_motionweave_bench`.
  Representative manual runs:
  `./build/ecs-test/aiverse_motionweave_bench --actors 1000 --frames 120`,
  `./build/ecs-test/aiverse_motionweave_bench --actors 5000 --frames 120 --ik`.
- **Compatibility:** benchmark only, no changes to runtime simulation,
  save/protocol, editor asset formats, or worker behavior. Not registered
  in CTest because speed/worker scheduling depends on hardware.
- **Validation:** **NOT RUN** (user deferred builds/measurements).
  No claimed performance gain or TSAN/golden screenshot result.
- **Risk:** synthetic graphs are not a substitute for large models,
  mobile workloads, real contact IK, GPU bandwidth or actual FPS/RTS
  animation frames. Pure FullBodyIK worker safety needs TSAN validation.
- **Next:** compare planned work to user's checklist, update PR
  scope/API documentation and inspect latest CMake/CI status *without*
  claiming queued or cancelled jobs passed. Preserve draft status
  until explicit test stage; any further implementation must be followed
  by a separate Stage 41 handoff.

## Stage 41 — authoritative snapshot to cosmetic montage signals (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22, base `feature/universal-entity-inspector-fps`.
- **Code SHAs:** `7c7eb66c3988af7832f972bad0a4e45cb98296b7` and include fix `07bdc76f3e0d56327f9759bce6c3d365690fd67e`.
- **Paths:** `libraries/include/aiverselib/animation/ReplicatedAnimationActions.h` (new),
  `tests/animation/replicated_cues.cpp` (new), `CMakeLists.txt`.
- **Architecture:** `ReplicatedAnimationActions::observe(ReplicatedAnimationState)` compares confirmed tick/life/shot/reloadUntil/health counters to emit **cosmetic-only** fire, reload, death or respawn action edges. First-sighting, repeated and out-of-order snapshots are suppressed; same slot new life resets edge history.
  `playAnimationSlot(AnimationMontageSlotsComponent&,shared_ptr<const MontageDefinition>)` restarts the same slot or adds an independent masked slot (bounded 8).
- **Compatibility:** not wired into FPS rendering yet. No gameplay commands, damage/hit logic, network schema or deterministic simulation changed.
- **Tests:** new CTest `motionweave_replicated_cues` for initial state, duplicate snapshot, delayed events, life reset and stale ticks. **NOT RUN**; user explicitly deferred all tests.
- **Risks:** event edge state lives in client-only presentation; repeat counts greater than one intentionally collapse into a single visual action per observed snapshot. No interpolation / montage phase migration from GPU crowds yet.
- **Next agent action:** update `apps/fps/client.cpp` to load opt-in `fps.visual.montages` (project-local shot/reload/death files) via `compileMontageAsset`, attach `AnimationMontageSlotsComponent` to CPU actor entities, use `ReplicatedAnimationActions` keyed by replicated remote slot, preserve historic box/GPU fallbacks and render death montage while active. Commit code and then Stage 42 separately.

## Stage 42 — FPS cosmetic Montage runtime wiring (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22, base `feature/universal-entity-inspector-fps`.
- **Code SHA:** `5d0dc6fe244cac121cdf46374dd16efef7617e31`.
- **Paths:** `apps/fps/client.cpp`, `docs/fps.md`.
- **Architecture/API:** project-config opt-in `fps.visual.montages` with
  `shot`, `reload`, `death` relative JSON asset paths. The client loads
  `compileMontageAsset` models using current skinned character, logs and
  skips invalid montage assets (retaining fallback). Each remote actor
  gets optional `AnimationMontageSlotsComponent`. Confirmed replicated
  snapshot counters are consumed by `ReplicatedAnimationActions` to
  start/restart matching masked `MontagePlayer` sections. Montage
  notifies feed existing cosmetic cue stream, not gameplay writes.
- **Lifecycle:** newly seen actors never fire phantom shot/montage
  cues; actor slot disappearance erases state, and life change
  clears active montage players. Active death montage can continue
  rendering after server health reaches zero; actor hides once ended.
  Valid montage configuration pins CPU Graph skinning rather than
  GPU-baked, ensuring mask/overlays are available.
- **Compatibility:** no network, save, weapon timer, damage, hit
  resolution or server-side movement schema changes. Absent assets
  preserve prior FPS palette/GPU/cube presentation fallback.
- **Tests:** none executed (user explicitly requested postponing
  builds/tests); deferred `motionweave_replicated_cues` covers event
  deduplication, but FPS end-to-end montage/audio/render fixtures
  and live model screenshots are still required.
- **Risks:** effect events use the confirmed snapshot's coarse tick,
  not exact animation start times; missed packets collapse repeated
  shots to one visual montage. GPU↔CPU phase migration and RTS native
  MotionWeave adapter remain unfinished.
- **Next step:** implement a reusable conversion between CPU Graph
  clip-clock phase and baked Crowd animator phase; migrate FPS
  presentation states during backend switching rather than resetting
  entity worlds; preserve the above cosmetic event history. Commit
  code and then Stage 43 handoff.

## Stage 43 — CPU Graph/Baked Crowd normalized phase transfer primitives (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22.
- **Implementation SHA:** `168666b1372c0e47c1d491875d4a3372c1eee358`.
- **Files:** `engine/render/Animation.h`, `Animation.cpp`,
  `AnimationGraph.h`, `AnimationGraph.cpp`,
  `libraries/include/aiverselib/animation/EcsAnimationSystems.h`,
  `AnimationCrowdSystems.h`, root `CMakeLists.txt`, new
  `tests/animation/motionweave_phase.cpp`.
- **API:** `render::Animator::seek(layer,seconds)` and
  `render::AnimationGraphInstance::clipTime(nodeName)` /
  `seekClipNode(nodeName,seconds)` allow cosmetic clock observation
  and setting without firing animation notifies. The presentation
  `AnimationPhaseSeedComponent::normalized` (0..1, -1 unused)
  seeds all Clip nodes only once on their first Graph update. Optional
  `AnimationCrowdComponent::initialNormalizedPhase` seeds baked
  Animator on its first matching clip. All clocks handle duration
  clamping/looping.
- **Compatibility:** old Animator and Graph methods and serialized
  clips remain compatible; no modification to authoritative gameplay,
  networking, save/checksum or hit validation.
- **Tests:** `motionweave_phase_seed` added for clip seek, no phantom
  notifies, baked Animator seek and ECS one-shot normalized seeding;
  **NOT RUN** due to user-requested deferred validation.
- **Limits:** clock seeding does not preserve blend controller and
  motion-matching internal state; only equivalent simple authored
  idle/move clips can be transferred. FPS still must be wired to
  capture old normalized phase before resetting its presentation
  World on backend swap.
- **Next step:** in `apps/fps/client.cpp`, capture normalized active
  clip phase per slot before CPU/GPU backend changes, seed new
  presentation actor components via these APIs and keep
  `ReplicatedAnimationActions` deduplication state. Add a backend
  handoff fixture, commit code then Stage 44 handoff.

## Stage 44 — FPS CPU/GPU phase continuity and event-history preservation (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, PR #22 Draft.
- **Implementation SHA:** `f7599ee5e827f2f29cc44da2b8f82173498cc1c7`.
- **Changed files:** `apps/fps/client.cpp`; new
  `libraries/include/aiverselib/animation/AnimationPhaseTransfer.h`;
  `tests/animation/backend_phase_transfer.cpp`; root
  `CMakeLists.txt` and `docs/fps.md`.
- **Architecture/API:** `captureGraphClipPhase` for the generated
  `idle`/`move` Graph nodes and `captureBakedClipPhase` for
  `Animator` provide normalized phase within the original clip.
  FPS captures each remote visual actor's phase before auto
  CPU↔GPU backend replacement, seeds the new
  `AnimationPhaseSeedComponent` (CPU) or
  `AnimationCrowdComponent::initialNormalizedPhase` (baked)
  and preserves `ReplicatedAnimationActions` event history.
- **Backward compatibility:** keeps existing lookups and
  `fps.visual.animationMode` config, save/network protocol and
  authoritative counters unchanged. Death/shot/reload Montage
  assets are pinned to CPU Graph mode, so no upper-body overlay
  is lost to baked GPU limitations.
- **Tests:** `motionweave_backend_phase_transfer` is added and
  verifies representative Graph/Animator normalized phase mapping.
  **NOT RUN**, including visual tests and CI (user deferred).
- **Limits:** no guarantee of visually identical poses across
  different authored clip blends; the simple fallback uses
  Idle/Walk/Run names and only continuous clip phase, not a
  crossfade of the previous and next skinning pipelines. Advanced
  controllers/IK/motion matching stay CPU-only.
- **Next step:** complete RTS opt-in MotionWeave adapter, preserving
  current existing RtsView animation authoring and environmental
  IK semantics. Avoid replacing universal RtsView's live legacy
  graph state unless per-entity feature parity is established;
  write code commit and Stage 45 handoff.

## Stage 45 — generic source bridge and strict genre-free core (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`,
  Draft PR #22, targeting `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `068ff38a0df5edfb0a4256de1dfc35d0c669a4d1`.
- **Changed code:** new
  `libraries/include/aiverselib/animation/MotionWeaveSourceBridge.h`,
  new `tests/animation/motionweave_source_bridge.cpp`,
  root `CMakeLists.txt` and generic comment in
  `libraries/include/aiverselib/animation/EcsAnimationSystems.h`.
- **Architecture/API:** the animation engine consumes ONLY source IDs,
  generations, model/compiled graph and graph-authored
  AnimationParameters (not game genre, controls, AI, combat or
  network state). `MotionWeaveSourceBridge::sync(World&,span,removeAbsent)`
  manages presentation ECS creation/update/restart/removal, returning
  `AnimationBridgeStats`. `find(sourceId)`, `size()` and `clear(World&)`
  allow host integration. Duplicate or invalid source entries are
  rejected before changing World. No runtime graph update is performed
  by this bridge; the caller executes normal MotionWeave systems.
- **Compatibility:** no change to authoritative gameplay, save/checksum,
  networking, existing graph JSON and original public animation APIs.
- **Deferred tests:** CTest `motionweave_source_bridge` added with
  create/update/generation/reset/duplicate/remove scenarios;
  **NOT RUN** per user instruction (no CI pass claimed).
- **Separate documentation changes:** `AGENTS.md`,
  `docs/motionweave.md`, `skills/gameplay/aiverse-motionweave/SKILL.md`
  plus this Stage 45 entry formally ban genre dependencies in core.
- **Known remaining architectural violation:** legacy
  `libraries/include/aiverselib/animation/ReplicatedAnimationActions.h`
  currently interprets FPS-specific shot/reload/death counters.
  A following stage MUST relocate FPS-specific semantics to the FPS
  host and retain only generic named action/counter mechanics in core.
- **Next actionable task:** add a generic action-edge API under
  `libraries/include/aiverselib/animation` and a host adapter under
  `modules/fps/view`. Update `apps/fps/client.cpp` and
  `tests/animation/replicated_cues.cpp` accordingly. After committing
  that change, append Stage 46 in a separate handoff commit.

## Stage 46 — remove FPS action semantics from MotionWeave core (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`,
  draft PR #22, base `feature/universal-entity-inspector-fps`.
- **Code SHA:** `2d809da0267463b0bf7be8b460d79f6dfd6d0eb8`.
- **Code paths:** new `libraries/include/aiverselib/animation/AnimationActionEdges.h`;
  new `modules/fps/view/FpsAnimationEvents.h`;
  `libraries/include/aiverselib/animation/EcsAnimationSystems.h`;
  `apps/fps/client.cpp`, root `CMakeLists.txt` and
  `modules/fps/CMakeLists.txt`; new `tests/animation/action_edges.cpp` and
  `tests/fps/animation_actions.cpp`.
  Deleted FPS-only `libraries/include/aiverselib/animation/ReplicatedAnimationActions.h`
  and moved its old regression from `tests/animation/replicated_cues.cpp`
  into the FPS test directory.
- **Core design/API:** `AnimationActionFrame` contains opaque
  `generation`, `revision` and named counters/booleans.
  `AnimationActionEdges::observe` reports generic increased counters,
  flag activation/deactivation and source-generation change, with
  stale-revision suppression. It knows NO gameplay terms or FPS/RTS
  types. `playAnimationSlot` was placed alongside
  `AnimationMontageSlotsComponent` in the common ECS header.
- **Host-only behavior:** `aiverse::fps::view::ReplicatedAnimationActions`
  translates FPS `tick/life/shots/reloadUntil/health` to generic
  named action edges and retains existing cosmetic shot/reload/death
  flags. FPS client now includes the FPS-host header.
- **Compatibility:** no gameplay, rendering math, animation Graph,
  server/net protocol, save/rollback, or editor schema changes.
  This unmerged feature branch intentionally **relocates** the
  FPS-specific experimental helper; consumers of the old
  `aiverselib/animation/ReplicatedAnimationActions.h` must adopt the
  FPS-host include `modules/fps/view/FpsAnimationEvents.h`.
  The core is now independent of this genre-specific event logic.
- **Tests:** `motionweave_action_edges` (generic) and
  `fps_animation_actions` (FPS host) are registered. **NOT RUN**
  because the user deferred builds/tests. CI status not asserted.
- **Risks/limitations:** generic event edges compare counters and
  boolean states rather than interpreting absolute gameplay actions;
  a host must use a stable revision/generation and do its own mapping.
  Old external consumers of the deleted unmerged header need migration.
- **Next concrete action:** use `MotionWeaveSourceBridge.h` to build a
  *genre-independent* animation host adapter for the RTS presentation
  pipeline. Keep RTS order/unit data translation confined to
  `modules/rts/view`, project generic named graph values, and keep
  its existing contact IK/procedural effects until parity is known.
  No new FPS/RTS conditionals or include dependencies in MotionWeave.
  Commit code first, then Stage 47 handoff before another stage.

## Stage 47 — automated MotionWeave genre dependency guard (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22,
  base `feature/universal-entity-inspector-fps`.
- **Implementation commit:** `663ab7d2988aa9bbd2ff8ad69b788e3d55366c46`.
- **Files:** new `tools/animation/check_motionweave_dependencies.py`,
  root `CMakeLists.txt` CTest registration.
- **Architecture:** the automated source checker scans
  `libraries/include/aiverselib/animation`,
  `libraries/include/aiverselib/motionweave` and
  `engine/render/Animation*.{h,cpp}` for forbidden direct
  includes from game modules (modules, apps, projects/examples,
  genre trees), and typed FPS/RTS/game namespace dependencies.
  This enforces the user requirement that the animation engine
  never branches on the game genre. Host-specific adapters remain
  OUTSIDE the core.
- **API:** no C++ runtime API, ECS schema, save, protocol or graph
  JSON changes. Optional CTest `motionweave_genre_boundary` is
  registered if Python3 interpreter is available.
- **Tests:** **NOT RUN**, per user's explicit deferred-test policy.
  When approved, run
  `ctest --test-dir build/ecs-test --output-on-failure -R '^motionweave_genre_boundary$'`
  or invoke the script with the repository root path.
- **Limitations:** the guard is intentionally a lightweight include /
  namespace source scan, not a complete compile-time dependency-graph
  analyzer. It cannot detect indirect violations through new
  shared libraries that are mislabeled or hidden in generated code;
  PR review must still enforce reverse-only host dependencies.
- **Next step:** make the RTS **host** consume the universal
  `MotionWeaveSourceBridge` in `modules/rts/view` where safe,
  rather than introducing any RTS class/conditional inside
  `libraries/include/aiverselib/animation`. Compare event/IK and
  procedural needs before bypassing legacy RtsView animation; do
  not double-evaluate or change gameplay authority. Commit code
  separately and append Stage 48.

## Stage 48 — new engine-level, genre-independent AIVERSE View (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, PR #22 Draft;
  based on `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `ec2a803cd0e0972805351f2db87792969446ad31`.
- **Changed files:** new `engine/view/View.h`, new
  `tests/view/view.cpp`; root `CMakeLists.txt`.
- **Architecture/API:** new canonical `aiverse::View`
  (also `aiverse::view::View`), `AIVERSE::view` INTERFACE CMake
  target and engine-level `aiverse::view::Object` data contract.
  A ViewObject contains only stable source ID/generation, a generic
  `render::ModelDraw` packet, optional immutable AnimationGraph,
  named AnimationParameters, significance and visibility.
  `View::update(span<Object>,float)` reconciles objects with
  `MotionWeaveSourceBridge`, assigns shared animation budgets,
  updates optional per-object animation in a PRIVATE presentation
  ECS World, and reports generic stats. `View::submit(Renderer3D&,
  span<Object>)` forwards models to the existing renderer with
  optional evaluated pose/matrices. No RTS/FPS/AI/gameplay types,
  order IDs, weapons, server counters or genre conditionals.
  `View::clear` and `presentationEntity` expose controlled lifetime
  and mapping. Unanimated static models render through the same
  View draw packet path.
- **Compatibility:** existing `game::RtsView` remains as the
  pre-migration compatibility adapter for extensive RTS-only
  terrain, fog, unit event mappings, scripted effects and contact
  queries. This code stage introduces a functional, independent
  engine-level `View`; it does NOT yet rename or remove the legacy
  type, and does not claim all legacy RtsView features are migrated.
  Existing FPS clients and simulation/network schemas unchanged.
- **Tests:** new CTest `view_genre_independent` checks generic
  static/animated object projection, named arbitrary input,
  generation restart, removal and lifetime. **NOT RUN** per user
  request, including CMake/editor/FPS/RTS smoke builds and TSAN.
- **Risks:** submit() borrows host-owned ModelDraw pointers and must
  be called after update() while those models/materials remain alive.
  The default View constructor currently specifies zero background
  workers; this conservative default avoids new worker threads in
  small/static scenes, but the host can explicitly pass worker count.
  No generic terrain/fog/camera or shared RTS feature extraction
  has happened yet. The eventual RtsView rename must be handled
  with a source-compatible shim while removing RTS semantics.
- **Next actionable stage:** add a `modules/rts/view`-owned
  adapter that projects safe compiled RtsView animation objects
  into `aiverse::View` or the generic SourceBridge without
  duplicating evaluation. Keep complex procedural and contact IK
  on their existing code path until parity; start with opt-in
  static/simple Graph scene integration. Commit code then separate
  Stage 49 handoff. Do not add RTS includes to engine/view.

## Stage 49 — legacy RTS presentation delegates simple Graph poses to engine View (2026-11-18)

- **Branch / PR:** `feature/universal-ecs-animation-pipeline`, Draft PR #22,
  base `feature/universal-entity-inspector-fps`.
- **Code commit SHA:** `76af8b0bc6ae9f4e85401e0af01fc5e47e196933`.
- **Changed files:** `modules/rts/view/RtsView.h`,
  `modules/rts/view/RtsView.cpp`,
  `modules/rts/view/RtsAnimate.cpp` and root `CMakeLists.txt`.
- **Architecture:** the older `game::RtsView` is now a **host adapter**
  which may delegate a narrow, safe subset of pure pose-graph entities
  to the *same* engine-level `aiverse::View` used by any genre.
  It projects only generic `aiverse::view::Object` identity, model,
  immutable Graph and named scalar values. The engine has zero
  reverse dependency on RTS. The default generic path is enabled,
  but a project may opt out through
  `RtsView::setGenericViewEnabled(false)`.
- **Eligibility:** only visible, ordinary 3D scene models with
  stable variant, a compiled single-state Bind/Clip/Blend/Additive
  Graph, known speed/moving/dead scalar parameters, no transitions,
  traits, runtime per-entity animation overrides, contact rigs,
  authored procedural effects, attachments, stage/LOD replacements,
  foot IK, baked skinning or parenting are migrated now. All other
  content retains the existing specialized RtsView animation path.
- **Runtime handoff:** at the start of the view frame the host
  gathers eligible generic objects, then `View::update` evaluates
  them through the shared MotionWeave ECS, while the existing
  `RtsViewDraw` continues applying terrain placement, look,
  materials, outlines, shadows and effects. `RtsView::animate`
  consumes the shared pose/matrix result without advancing its
  legacy GraphInstance again. Returning to unsupported legacy
  content resets the old Graph player to prevent stale clocks.
  The host clears all generic animation presentation entities
  during init/shutdown before releasing models.
- **Compatibility:** all RTS simulation, save/network/checksum,
  terrain/fog, scene serialization and existing RtsView API remain
  functional. No genre-specific code added to engine View or
  MotionWeave. Crucially, `RtsView` still exists as a compatibility
  wrapper; it is **NOT yet merely a type alias to a finished
  universal View**.
- **Tests / actual commands:** GitHub source commits only.
  **NOT RUN**: CMake, CTest, TSAN, editor and real game rendering.
  The stage requires later targeted comparison of a simple RTS
  Graph unit against legacy clip timing, offscreen LOD and shadow
  correctness. Do not claim these pass.
- **Known risks:** simple Graph projection uses the most recently
  smoothed speed, not yet full legacy input parity; only the strict
  eligibility subset is delegated. Stage/corpse/planet rendering
  and event-heavy scene objects still use the old path. One
  previous graph's timing may reset when switching adapters.
- **Next exact stage:** extract a reusable scene-object **draw packet
  builder** from `RtsViewDraw.cpp` so the game adapter emits generic
  `View::Object` without duplicating renderer layout. Move additional
  generic props (model instance, transforms, LOD, materials, light/
  shadows) to engine View, then gradually retire genre-specific
  RtsView. Implement separately and append Stage 50 handoff.

