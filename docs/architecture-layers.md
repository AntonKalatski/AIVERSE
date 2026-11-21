# Source layers (checked in CI)

```
engine/  <  libraries/  <  modules/<name>/  <  apps/, tools/, tests/
```

A file includes only its own layer or a lower one. One module never includes another module.
Check: `python3 tools/layers/check_layers.py` (CI job `layers`). It fails on a **new** edge and on an
allowlisted edge that is **gone**, so `tools/layers/allowlist.json` only shrinks.

## Debt in the allowlist

| Id | What | Owner |
|---|---|---|
| G3 | `engine/net/city`, `libraries/.../systems/{Persistence,EngineMechanics,NativeCommands}.h`, `voxel*Persistence.h`, `bots/Rts.h`, `modules/strategic` include `modules/rts` | no-genre work, `docs/no-genre-split-audit.md` stage 4 (extract `modules/rts`-only capabilities) |
| G4 | `engine/net/authority/Session.h`, `bots/Fps.h`, `modules/genres` include `modules/fps` | no-genre work, stages 3/6 |
| L1 | 30 edges left (was 38): `engine/` (mostly `engine/world/Nereid*`, `engine/view`, `engine/platform`) includes `libraries/` while `libraries/` includes `engine/` in 118 files: a cycle | architecture-layers work: move the included pieces down into `engine/` or the including code up into `libraries/` |

## Rules

- Do not add an edge to the allowlist to make CI pass. `--update` is for removing fixed edges or, after
  agreement, recording debt with its id.
- Removing an edge (an extraction stage) must also run `--update` in the same commit.
- Genre-host debt (G*) is closed by the no-genre stages; do not fix it here in parallel with a different design.

## Coordination (2026-11-21)

Two parallel efforts, split so they never edit the same thing:
- **no-genre** (`feature/aiv20261121_no-genre`): G1-G9 — capability queries instead of `isFps()`,
  one scene document, one vehicle system, extraction of `modules/rts`/`modules/fps`-only capabilities
  (including moving `Kingston*` and `RtsBwAI*` out of the engine), shared rule blocks, one simulation host.
- **architecture-layers** (`feature/aiv20261121_arch-layers`): this layer check, L1 (engine/libraries cycle),
  stable ECS component ids (`engine/core/ECS.h`), splitting `engine/render` targets, and the AGENTS.md
  shrink (only after no-genre is merged, because both edit AGENTS.md).
