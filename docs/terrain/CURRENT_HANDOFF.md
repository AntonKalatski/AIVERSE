# TerraWeave — current agent handoff entry point

Last source stage: **69** (2026-11-19), branch `feature/terraweave`.
PR #24 is Draft, and must not merge to `main` before native QA and
cross-branch conflict reconciliation.

## Where to start

1. [Stage 69: linked NEREID confluences/cascades, 4m creek sampling, PHYSIS nature](stage-69-nereid-physis-nutrient-handoff.md)
   — current source implementation, exact commits, immutable owner boundaries,
   correctness limitations and mandatory independent QA.
2. [Stage 68: nutrient cycle, PHYSIS weather and NEREID flow](stage-68-nutrient-circulation-handoff.md)
   — conserved v2 nutrient ledger, rain leaching, uptake and same-body transport.
3. [Stage 67: deadwood/soil nutrition](stage-67-deadwood-soil-nutrients-handoff.md)
   — prior organic ledger, fallen Box3D retirement, forest/grass fertility.
4. [Stage 66: authoritative harvest](stage-66-authoritative-harvest-handoff.md)
   — persisted tree removals/replanting and ECS native authority.
5. [Stage 65: rooted forest ecology](stage-65-rooted-forest-ecology-handoff.md)
   — growth, mortality, sun competition and native seed recruitment.
6. [Full historical handoff](AGENT_HANDOFF.md) and
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
- Code is written to GitHub, but Stage69 has **not** been built/tested
  on a C++ compiler, run under native PHYSIS/NEREID/Box3D, GPU profiled,
  or save/replay verified. Existing earlier QA passes apply to older commits.
- PHYSIS's real nature API was inspected on a separate PHYSIS branch;
  the TerraWeave-only feature checkout does NOT yet contain that header.
  The Stage69 native adapter is optional, compiled conditionally, and
  explicitly refuses native opt-in when PHYSIS is unavailable.
- Do NOT merge divergent `main`/TerraWeave renderer paths blindly.
- Each new stage MUST append a standalone handoff with actual commits,
  precise APIs, tested commands and truthful statuses before work is
  passed to the next agent.

## First next action

The independent QA agent should compile Stage69 native headers/Integration
and add a dedicated deterministic test target for: genuine NEREID
confluence/cascade transfers, dry/wet and mixed-body 32m tile rejection,
4m creek subcell sampling, zero double-hop, exact conserved ledger,
rollback/order invariance, and absent-PHYSIS fail-fast behavior. Then
validate the direct PHYSIS bridge in an integration worktree where both
branches' headers and ECS scheduling are present. Record actual command
output and repair compiler/runtime faults, together with earlier Stage44–68
regressions. No tests are claimed passing for Stage69.

The next feature agent should start with Stage69's standalone handoff,
then consider Stage70 true adaptive/4m nutrient water residency with
versioned migration; never reinterpret old 32m saved indices implicitly.
