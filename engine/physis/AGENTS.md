# PHYSIS — required agent instructions

Scope: `engine/physis/`, `tests/physis_*.cpp`, `docs/agent-handoffs/physis-*.md`.

1. **Genre independence is non-negotiable.** PHYSIS engine files must not include anything from `modules/fps`, `modules/rts`, `libraries/include/aiverselib/genre`, or any game-specific types. Native projects use the common C++ API and ECS components, not forks per genre.
2. Long-lived state lives in `aiverse::World` components/resources, authoritative math uses `Fixed`. Renderer/FX/audio may consume snapshots and emit presentation events but must not own climate state.
3. Preserve deterministic ordering, fixed step scheduling, seed/replay, rollback/COW. No wall clock, platform distributions or float/double in simulation.
4. Integrate Terra, Water, Foliage, Physics, VFX via clean interfaces and opt-in adapters. No hard link to a rendering backend or platform.
5. After **EVERY** implementation stage write an explicit handoff in `docs/agent-handoffs/` with branch, implementation SHA, changed paths, architecture/API, tests ACTUALLY RUN and their results, known gaps, and next concrete task; commit it separately from implementation. Never mark unexecuted tests as passed.
6. Existing gameplay/genre modules must remain unaffected unless their integration is explicitly tested and reviewed. Respect concurrent branches; never force push.

Latest Stage 13: `docs/agent-handoffs/physis-sampling-persistence-2026-11-18.md`; Stage 12: `docs/agent-handoffs/physis-numerical-ci-2026-11-18.md`. Stage 11: `docs/agent-handoffs/physis-forces-box3d-2026-11-18.md`. Stages 8–10: `docs/agent-handoffs/physis-{wind,gravity,storm}-2026-11-18.md`; Stage 7: `docs/agent-handoffs/physis-flood-authority-2026-11-18.md`. Stage 6 role/ownership contract: `docs/agent-handoffs/physis-ownership-terra-water-2026-11-18.md`. Stages 1–5 remain preserved; no stage claims passed native tests.

## PHYSIS / NEREID / TerraWeave coordination (2026-11-18)

Read `docs/physis-nereid-terraweave-integration.md` (root `AGENTS.md` ownership matrix) before editing flood/water/terrain integrations.
- PHYSIS agent owns weather, dynamic precipitation, surface wetness/snow/ice, fire/quake and independent nature ECS. NEREID Water agent (PR #23, `feature/nereid-water-core`) exclusively owns canonical lake/river/ocean ECS water, water-body geometry, flow, water sampling, buoyancy and water surface pipeline. TerraWeave agent (`feature/terraweave`) owns terrain heights/biomes/foliage and later consumes both through **optional** adapters; PHYSIS PR #25 is `feature/physis-nature-simulation`.
- The local `HazardCell::waterDepth` shallow flood is **experimental standalone gameplay simulation**. For NEREID-managed water bodies, opt in to `FloodMode::ExternalAuthority` via `InstallOptions::floodMode`: local depth is cleared and flood calculations bypassed; PHYSIS fire/quake still run. This switch and tests are authored but native verification is NOT RUN. Never invent competing water sources or integrate normalized PHYSIS rain intensity directly into NEREID metres.
- PHYSIS core must NOT include NEREID or TerraWeave headers; TerraWeave core must not depend on PHYSIS/NEREID. Host-level integration owns conversions: Terra signed centimetres -> common signed Fixed metres, X/Z horizontal and Y vertical; floor division for negative world positions, elevation and shoreline coherence, explicit scheduler and persistence ownership.
- PHYSIS read-only `EnvironmentalResponse` is the input for Terra terrain material, wetness/snow masks and foliage wind; NEREID `WaterSample`/water bodies are the source for Terra waterline, exclusion/shoreline and buoyancy. No presentation feedback into authoritative state.
- On each subsequent implementation stage: code + unit/cross-system tests in a code commit, **separate** handoff documentation commit, scope/owner coordination and verified native build status. Do not edit the Water agent's or Terra agent's active branches and do not merge their PRs automatically.

## Stage 14 — diagnostics ownership (2026-11-18)

- `PhysisPresentation.h` and `PhysisDiagnostics.h` are **read-only**, renderer-independent consumer APIs. They return copies/arrays for external editor/VFX/physics/Terra host code. Do not add ImGui/RHI/Box3D/genre includes here, or treat a PHYSIS heatmap as a visual renderer integration.
- `DiagnosticRaster::rawValues` are `Fixed::raw` and `valid[i]` marks active sampled cells. Do not infer real NEREID water levels from `LocalFloodMeters`. `diagnosticNormalized` is presentation-only; it must not influence authoritative simulation.
- Add post-stage handoffs; Stage 14 `docs/agent-handoffs/physis-diagnostics-2026-11-18.md`. The tests and CI are authored but native status is **NOT RUN**.
