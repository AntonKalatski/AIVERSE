# Substance SBSAR handoff

## 2026-11-21 — stage 1: standalone offline bake tool

Branch: `feature/studio-html5`. Implementation: `fc88afd2c`.
Starting HEAD: `1f3d53fcb0b6e7470fdf5d58e0afb62cabe3ba5a`.
Paths: `tools/substance/bake.py`, `tools/substance/README.md`.

Added an external SAT subprocess wrapper with numeric exposed parameters,
512–4096 output size, requested engine RAM/CPU budgets, timeout, SHA256-keyed
cache and staging publication. Cache outputs carry integrity hashes.
No runtime API, save/ECS format or renderer changes. Existing unrelated dirty
material/editor files were preserved. No Studio controls or PBR mapping yet.

Executed: `python3 tools/substance/bake.py --help` — PASS, CLI displays.
Executed: `python3 tools/substance/bake.py AGENTS.md --cache /tmp/aiverse-substance-check`
— expected exit 1 with explicit missing-sbsrender diagnostic, before rendering.
Real SBSAR bake/cache reuse/failure recovery/GPU/performance: NOT RUN because
Adobe SAT and SBSAR fixtures are unavailable. CI: not triggered, no CI URL.
First ordinary commit attempt failed due sandbox `.git/index.lock`; retry with
authorized escalation succeeded. This is not a full material import stage.

Risks: Adobe CLI/version compatibility unverified; memory budget is not an OS
limit; concurrent writers to the same key can fail publication safely; PNG
outputs are not yet checked for decodability or mapped by semantic usage.
Next: obtain installed SAT plus representative licensed SBSAR fixtures, verify
real CLI and cache behavior, then implement explicit semantic mapping,
normal/color-space handling, ORM packing and existing asset-pipeline compression.
Only after that wire asynchronous Studio import and preview using shared UI.
