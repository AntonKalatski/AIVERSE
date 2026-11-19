# TerraWeave — current agent handoff entry point

Last source stage: **70** (2026-11-19), branch `feature/terraweave`.
PR #24 is Draft, and must not merge to `main` before native QA and
cross-branch conflict reconciliation.

## Current integration/QA stage — source features frozen

**Read first:** [2026-11-19 full native QA handoff](qa-2026-11-19-full-integration-handoff.md).
The existing TerraWeave GCC/Clang workflow had an unterminated CTest
regex; this has been fixed. It now builds the Stage70 pure nutrient
test and a native NEREID/ECS/optional-PHYSIS header compile smoke.
A second Linux+macOS workflow configures/builds all enabled native
targets and runs all registered CTests in one batch. At creation,
both GitHub Actions runs were QUEUED, so **no new tests are yet
confirmed passing**. Do not begin Stage71 before real QA results,
source fixes, and cross-branch integration.

## Where to start

1. [Stage 70: sparse body-tagged 4m nutrient ledger and v3 save migration](stage-70-sparse-micro-nutrients-handoff.md)
   — latest source, exact commits, new CMake test target, native APIs,
   explicit lossless migration, actual QA status and next acceptance.
2. [Stage 69: linked NEREID confluences/cascades, 4m creek sampling, PHYSIS nature](stage-69-nereid-physis-nutrient-handoff.md)
   — previous coarse-water Stage69 topology and weather implementation.
3. [Stage 68: nutrient cycle, PHYSIS weather and NEREID flow](stage-68-nutrient-circulation-handoff.md)
   — conserved v2 nutrient ledger, rain leaching, uptake and same-body transport.
4. [Stage 67: deadwood/soil nutrition](stage-67-deadwood-soil-nutrients-handoff.md)
   — prior organic ledger, fallen Box3D retirement, forest/grass fertility.
5. [Stage 66: authoritative harvest](stage-66-authoritative-harvest-handoff.md)
   — persisted tree removals/replanting and ECS native authority.
6. [Stage 65: rooted forest ecology](stage-65-rooted-forest-ecology-handoff.md)
   — growth, mortality, sun competition and native seed recruitment.
7. [Full historical handoff](AGENT_HANDOFF.md) and
   [TerraWeave architecture](TERRAWEAVE.md).

## Immutable design boundaries

- NEREID alone owns water flow, flood, waves and sediment; TerraWeave can
  follow water currents for *nutrient tracer* movement, but cannot create
  another hydrological authority.
- PHYSIS alone owns weather simulation; Stage68 accepts deterministic
  callbacks for temperature, rain and soil moisture from a completed tick.
- TerraWeave owns Earthworks height and persistent root/grass/ecology/
  harvesting/deadwood nutrient data. Rendering/Box3D/NavMesh operate at
  their respective post-ECS owner barriers.
- Native game modules configure optional features; never hardcode RTS/FPS
  into generic TerraWeave.
- Stage70 v3 source, native mode and the `terraweave_micro_nutrient`
  test **source** are committed to GitHub. C++ compilation, execution,
  native v1/v2→v3 save migration and NEREID/PHYSIS regression verification
  have NOT been observed; previous green tests apply only to older commits.
- Historical v1/v2 32m dissolved water stays a fully conserved UNASSIGNED
  legacy pool. Body-tagged 4m migration is explicit and validates every
  NEREID wet tile; never automatically give an old coarse amount to one
  of two possible rivers.
- PHYSIS's real nature API was inspected on a separate PHYSIS branch;
  the TerraWeave-only feature checkout does NOT yet contain that header.
  The Stage69 native adapter is optional, compiled conditionally, and
  explicitly refuses native opt-in when PHYSIS is unavailable.
- Do NOT merge divergent `main`/TerraWeave renderer paths blindly.
- Each new stage MUST append a standalone handoff with actual commits,
  precise APIs, tested commands and truthful statuses before work is
  passed to the next agent.

## First next action

Use an isolated worktree on `feature/terraweave`; first compile
`aiverse_terraweave_micro_nutrient_test` and execute CTest
`terraweave_micro_nutrient`, then add native ECS NEREID two-stream
tests with a real confluence inside one former 32m tile, exact
v1/v2→v3 archive migrations, rollback, input-order determinism,
legacy claim failure/atomicity, strict mass conservation, 65k
memory/performance bounds and coarse-mode opt-out regression.
Do not declare Stage70 passing without exact logs.

Only after the separate QA agent fixes failures should the engine's
larger previously deferred TerraWeave/PHYSIS/NEREID tests be
run as a coordinated batch and `main` merge conflicts reconciled.

The next feature agent may consider Stage71 per-body flow weighting
and optional 1m local nutrient tiles, but must not invent a dense
world grid or silently change the v3 archive. Each stage requires
its own accurate handoff and committed SHA.
