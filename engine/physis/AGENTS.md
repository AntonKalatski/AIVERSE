# PHYSIS — required agent instructions

Scope: `engine/physis/`, `tests/physis_*.cpp`, `docs/agent-handoffs/physis-*.md`.

1. **Genre independence is non-negotiable.** PHYSIS engine files must not include anything from `modules/fps`, `modules/rts`, `libraries/include/aiverselib/genre`, or any game-specific types. Native projects use the common C++ API and ECS components, not forks per genre.
2. Long-lived state lives in `aiverse::World` components/resources, authoritative math uses `Fixed`. Renderer/FX/audio may consume snapshots and emit presentation events but must not own climate state.
3. Preserve deterministic ordering, fixed step scheduling, seed/replay, rollback/COW. No wall clock, platform distributions or float/double in simulation.
4. Integrate Terra, Water, Foliage, Physics, VFX via clean interfaces and opt-in adapters. No hard link to a rendering backend or platform.
5. After **EVERY** implementation stage write an explicit handoff in `docs/agent-handoffs/` with branch, implementation SHA, changed paths, architecture/API, tests ACTUALLY RUN and their results, known gaps, and next concrete task; commit it separately from implementation. Never mark unexecuted tests as passed.
6. Existing gameplay/genre modules must remain unaffected unless their integration is explicitly tested and reviewed. Respect concurrent branches; never force push.

Current Stage 1: `docs/agent-handoffs/physis-core-2026-11-18.md`.
