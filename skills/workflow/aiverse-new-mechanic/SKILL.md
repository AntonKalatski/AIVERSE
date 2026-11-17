---
name: aiverse-new-mechanic
description: Add a genre-independent composable ECS mechanic to AIVERSE: reusable components, systems, native API, deterministic tests, optional RTS/FPS adapters, skill/docs and examples.
---

# New mechanic — build once, compose everywhere

Read **AGENTS.md → Genre-independent ECS mechanics contract** and
`docs/editor-unification-handoff.md` before modifying code. The gameplay engine
is **not RTS-specific**. A user assembles a game by combining reusable mechanics
and changing configuration; genre never determines an algorithm's owner.

## Architecture

1. **Inventory before implementation.** Search the library catalog, ECS
   components/systems, existing skills and `docs/game-format.md`. Extend
   compatible shared features; do **not** generate a second algorithm in FPS
   or RTS because it is convenient.
2. **Data components.** Define minimal public C++ ECS components for
   configuration, runtime state and result/events in `libraries/include/aiverselib`
   (or `engine/sim` for genuine core primitives). Use deterministic
   `Fixed` sim values, Entity handles, stable IDs. Do not store authoritative
   state in genre-specific vectors when an ECS component can own it.
3. **Small independent systems.** Implement reusable systems operating on
   explicit component query/read/write sets. Distinguish planning, immutable
   sampling, simulation, command/event collection and **ordered commit**.
   Never change ECS topology during parallel iteration. Support optional
   registration and per-project configuration.
4. **Public native API.** Make component and system headers directly available
   to each native project without editing engine internals, depending on
   FPS or RTS GameModule classes, Lua, the editor or rendering backend.
   Provide registration and composition examples.
5. **Genre adapters only.** FPS/RTS/racing/etc. may map input, target selection,
   entities and scenario configuration to shared systems. They must not own
   separate copies of the shared trajectory, damage, collision or AI logic.

## Shared projectile example

Use one projectile/trajectory stack for bullets, rockets, grenades, lasers
and spells, regardless of RTS/FPS:

- `ProjectileMotion` (position, velocity, lifetime, gravity, guidance),
  `ProjectileTarget`, `CollisionShape`, `ImpactDamage`, plus optional
  `Penetration`, `Splash`, `Homing` and `Trail` components.
- Shared trajectory, collision/impact and damage systems; optional components
  select behavior. Deferred canonical impact application determines identical
  results under different worker counts.
- FPS muzzle input and RTS turret firing become adapters producing the same
  projectile entities. Rendering/trails/audio are presentation systems and
  cannot alter authoritative simulation.
- **Reuse existing projectile libraries where available.** This is an interface
  example, not authorization to implement duplicate models just to match names.

Apply the same pattern to locomotion, physics, vehicles, gear, inventory,
crafting, health, status effects, economy, UI/meta, navigation, bots and AI.

## Required handoff + tests

6. **Serialization, replay and rollback.** Serialize/hash every authoritative
   component, explicitly define persistence versions, generation-safe
   references and deterministic RNG streams. If migrating old gameplay,
   maintain parity and a documented deprecation path for compatibility State.
7. **Tests.** Test components and systems in isolation, native-project
   compilation, cross-genre use with different configuration, deterministic
   per-tick checksums, snapshot restore and exception handling. Add ASan/TSan
   and actual timing tests when concurrency is involved. Tests that have not
   run are **unverified**, never PASS.
8. **Agent docs/skill.** Add concise public API examples, configuration
   reference, dependency/registration graph, one matching AI skill and
   `AGENTS.md` pointer or catalog entry. Prefer reusing an existing skill
   instead of adding duplicate instructions.
9. **Visual/UI work only when needed.** Keep editor Inspector, render and
   audio adapters optional and independent. Validate real screenshots if a
   change is visual.
10. **Document after every code commit.** Append the commit SHA, changed
    files, current architecture, actual verification and exact next step to
    `docs/editor-unification-handoff.md` in the working branch.
11. **Merge discipline.** Work in your assigned branch/worktree, reconcile
    other agents' changes without overwriting them, and never merge into
    `main` before requested verification and explicit authorization.

**Definition of done:** one generic ECS library + feature systems + public
native entry points + two distinct host configurations + tests + docs/skill.
Genre-specific duplicated algorithm code is grounds for rejecting the work.
