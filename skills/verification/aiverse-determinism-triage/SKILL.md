---
name: aiverse-determinism-triage
description: Find and fix AIVERSE determinism breaks - checksum mismatches between runs, saves or peers - locate the first differing tick and field, classify the cause, fix, and prove with re-runs.
---

# Determinism triage

1. Reproduce: the same project, map, seed twice with `aiverse_headless --project <p> --seed <n>` (or `match_run`); record checksums. Mismatch on the same build = nondeterminism; mismatch only across builds/platforms = platform dependence.
2. Narrow in time: shorter runs or periodic hashes to find the first differing tick; save at a tick before it (`--save`) and compare loads.
3. Narrow in state: export canonical snapshots from both runs and locate the first differing field (`skills/verification/aiverse-state-diff`, `docs/native-toolkit.md`).
4. Usual causes:
   - float math or platform libm in simulation (use Fixed/integers, `fx()` in Lua);
   - iteration over unordered containers or pointer order (sort by entity index / id);
   - wall clock, platform RNG, `math.random` (use `sim.rng()` / the bot's saved RNG);
   - state not saved or not hashed (new field missing from save/hash, `RtsAI::saveState`, NativeState);
   - presentation feeding the simulation (view, UI, audio, fx).
5. Fix at the cause, never by excluding the field from the hash.
6. Prove: repeated seeds equal, save/load mid-match equal, `templates/` checksums unchanged (or changed on purpose and recorded with a reason), CI `determinism`.

Runtime library: none. Workflow over existing verification tools.
