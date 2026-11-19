# PHYSIS Stage 79 — environmental structural stress and Stage 78 freshness hardening

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Working branch: `feature/physis-nature-simulation`
PR: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Previous handoff: `docs/agent-handoffs/physis-stage78-ecs-nature-contacts-2026-11-19.md`
Stage 78 safety fix commit: `fa0b00d6d60a4fb6b6057610806c1d040fab6897`
Stage 79 implementation commit: `21c016ede31107f48c50ecc5247cfc0cf98ae56a`
Handoff commit: inspect `git log -1 -- docs/agent-handoffs/physis-stage79-nature-stress-2026-11-19.md`.
Main remains unchanged.

## Scope and motivation

After Stage78's opt-in nature probes, the user approved the next
PHYSIS development stage: generic environmental forces on
vegetation/objects, then later biome, water and physics reactions.
Avoid a second wind computation or genre-specific damage solver.
Nature stress is an ECS policy layer over PHYSIS force and
nature-contact outputs. Hosts own the real physical consequences.

## Changes and public API

1. `engine/world/PhysisNereidNatureBridge.h` — Stage78 freshness
   fix: an old PHYSIS/NEREID contact pair with matching stamps
   is still invalid if PHYSIS climate has already advanced to
   a newer tick. Checks `NatureContact::climateTick ==
   State::tick` before accepting. Test added to
   `tests/physis_nereid_nature_contacts_test.cpp` by advancing
   the authoritative climate clock without re-sampling and
   requiring the bridge to reject the old pair.
2. `engine/physis/PhysisNatureStress.h` — opt-in,
   renderer/genre/Box3D-independent policy:
   - `NatureStressReceiverComponent`: leverArmMeters, forceLimitN,
     bendingLimitNm, fatigueRate, recoveryRate, fireDamageRate,
     quakeDamageRate, catastrophicLoadRatio and enabled.
     Material parameters are **per PHYSIS climate tick**, not
     absolute seconds.
   - `NatureStressOutputComponent`: aerodynamicForceN,
     windLoadN, bendingMomentNm, normalizedLoad,
     accumulatedDamage (0..1), sampledClimateTick, fractured,
     newlyFractured and valid.
   - `advanceNatureStress(World&)`: read PHYSIS climate,
     ForceReceiver/ForceOutput and nature probes/contacts,
     validate matching sample tick and position, consume
     the *existing* aerodynamic force, and publish a bounded
     load/stress/damage sample. Gravity is intentionally
     excluded to avoid double-counting host physics.
     Load is max(total Force/force limit,
     horizontal bending Nm/strength limit).
     Excess load, fire and quake add wear; low safe loads
     allow recovery. Instant catastrophic load or damage 1
     latches fractured; a transition sets newlyFractured.
     Re-sampling the same valid climate tick is idempotent.
     Invalid/disabled input suppresses the current load
     but preserves persistent damage/fracture state.
   - `io(Archive&,...)` for both components (no legacy
     per-entity state mutation unless explicitly installed).
3. `engine/physis/PhysisIntegration.h` — add opt-in
   `InstallOptions::enableNatureStress=false`; reject
   enableNatureStress unless enableForces AND
   enableNatureContacts (validate before initialization).
   Register persistence IDs
   `physis.nature_stress_receiver` and
   `physis.nature_stress_output` v1 only when enabled.
   Scheduler order: `physis.environment` then
   `physis.forces` then `physis.nature_contacts` then
   `physis.nature_stress`. No genre branch or physics step.
4. `tests/physis_nature_stress_test.cpp` —
   registered CTest `physis_nature_stress`, target
   `aiverse_physis_nature_stress_test` (links aiverse_engine).
   Exercises high wind/fracture exactly once, preservation
   and replay from World COW snapshot, invalid force proxy
   location, fire exposure without aerodynamic force,
   file save/load/checksum and Simulation snapshot/rollback
   in both ordinary and COW modes.
5. `CMakeLists.txt` adds test registration, PHYSIS and ECS labels.
6. Agent docs: root AGENTS, `engine/physis/AGENTS.md`,
   `skills/world/aiverse-physis/SKILL.md`,
   `docs/physis.md`, cross-system ownership contract
   and this handoff.

## Boundaries and safety

- The policy never updates rigid-body motion, graphics handles,
  terrain/foliage descriptors, water bodies, fluid currents,
  game health or events. It reports *potential* failure and
  accumulated stress. The native host must apply any fracture
  effect to TerraWeave/Box3D at an explicit deterministic barrier.
- Host MUST deduplicate `newlyFractured` reactions by
  `(Entity, sampledClimateTick)` across reconnect/save/replay.
  A bool alone is NOT globally exactly-once event delivery.
- For individual trees use authored species/age/root strength,
  moment arm and force proxy; for shrubs shorter arms,
  for grass use shared patches/clusters, NOT one ECS object
  per blade. Native physics body activation is *not built*
  in this stage.
- A `NatureStressOutputComponent` is persistent ECS state
  only if the optional flag is enabled; this is not a
  breaking mutation of older persistent registry schemas.
- NEREID remains sole water authority, PHYSIS uses
  `FloodMode::ExternalAuthority` when both are installed.

## Verification — exact status

- GitHub source/branch/CMake declarations were inspected,
  and implementation commits were made with branch-head
  expected-SHA checks. No source merge into main.
- The agent's container cannot clone GitHub
  (`git ls-remote` fails `Could not resolve host: github.com`).
  GitHub PR workflows/statuses were checked for the Stage78
  documentation head: zero confirmed runs/statuses, NOT PASS.
- **NOT RUN** after Stage78 and Stage79:
  `physis_nereid_contacts`, `physis_nature_stress`,
  full PHYSIS/NEREID CTest, full engine, actual GUI, Metal,
  native platform or host-authoritative Box3D/TerraWeave.
- Previous user-reported Stage73 40/40 PHYSIS/NEREID/VFX
  CTest is from an older source and DOES NOT validate these
  new components. Keep PR #25 Draft.

## Next native validation command

```sh
cd ~/Projects/AIVERSE &&
git fetch origin &&
git switch feature/physis-nature-simulation &&
git pull --ff-only origin feature/physis-nature-simulation &&
cmake -S . -B build/physis-nereid-validation \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON &&
cmake --build build/physis-nereid-validation \
  --target aiverse_physis_nereid_contacts_test \
           aiverse_physis_nature_stress_test --parallel 4 &&
ctest --test-dir build/physis-nereid-validation \
  --output-on-failure \
  -R '^physis_(nereid_contacts|nature_stress)$'
```

If actual errors occur, fix them before broad testing or
claiming PASS. On green, rerun `ctest ... -R '^physis_'` and
`ctest ... -R '^(physis_|ecs_nereid_)'`.
Then implement a host-owned Box3D/tree break adapter with
deterministic once-only effect events; TerraWeave owns root
generation and per-species stability, NEREID owns bank erosion.
Publish a new standalone agent handoff on completion.
