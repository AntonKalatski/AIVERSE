# TerraWeave — current agent handoff entry point

Last source stage: **68** (2026-11-19), branch `feature/terraweave`.
PR #24 is Draft, and must not merge to `main` before native QA and
cross-branch conflict reconciliation.

## Where to start

1. [Stage 68: nutrient cycle, PHYSIS weather and NEREID flow](stage-68-nutrient-circulation-handoff.md)
   — latest implementation, APIs, exact GitHub commits, outstanding QA and
   next source scope. This is the immediate continuation document.
2. [Stage 67: deadwood/soil nutrition](stage-67-deadwood-soil-nutrients-handoff.md)
   — prior organic ledger, fallen Box3D retirement, forest/grass fertility.
3. [Stage 66: authoritative harvest](stage-66-authoritative-harvest-handoff.md)
   — persisted tree removals/replanting and ECS native authority.
4. [Stage 65: rooted forest ecology](stage-65-rooted-forest-ecology-handoff.md)
   — growth, mortality, sun competition and native seed recruitment.
5. [Full historical handoff](AGENT_HANDOFF.md) and
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
- Code is written to GitHub, but Stage68 has **not** been built/tested
  on a C++ compiler, run under native World/Box3D, GPU profiled or
  migration-tested. Existing earlier QA passes apply to older commits.
- Do NOT merge divergent `main`/TerraWeave renderer paths blindly.
- Each new stage MUST append a standalone handoff with actual commits,
  precise APIs, tested commands and truthful statuses before work is
  passed to the next agent.

## First next action

A separate QA agent should compile Stage68 native headers and example,
run deterministic C++ tests for v1->v2 persisted resource migration,
nutrient mass conservation through all transitions, PHYSIS climate and
NEREID same-body flow, GPU shared Arbor decay tint, and Stage44–67
regressions. Record the first failing compiler diagnostic and exact
commands. The next feature agent may tackle valid NEREID confluence
nutrient transfers and small-creek spatial refinement in an isolated
TerraWeave worktree, with another handoff before relinquishing work.
