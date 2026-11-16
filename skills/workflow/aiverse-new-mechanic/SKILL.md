---
name: aiverse-new-mechanic
description: Add a new engine mechanic to AIVERSE end to end - own branch, data block and loader errors, code in a new file, tests, scenario_run proof, AI support, UI and view, docs, prototype game and unchanged checksums.
---

# New engine mechanic

Done criteria come from the project mechanics plan; all of them apply.

1. Branch `agent-mech-<code>` in its own worktree; merge `main` before every stage (`skills/workflow/aiverse-agent-branch`). Read AGENTS.md areas: touch only your area, hooks in shared files stay minimal and additive.
2. Check first that the mechanic does not exist: `rules_schema`, `docs/game-format.md` "Recipes (mechanics of known games)", library catalog (`python3 tools/skills/catalog.py search <words>`).
3. Data: a new optional block parsed in `RtsData.cpp` with readable errors; old games load unchanged.
4. Code in a new file (`modules/rts/Rts<Mechanic>.cpp`); shared files get only hook points so parallel branches do not conflict. Integer/Fixed math, engine RNG, every state field saved and hashed.
5. Tests: a test in `tests/tests.cpp`; behaviour proven with `scenario_run` (pass/fail checks).
6. AI: the bot uses the mechanic at least basically (`skills/ai/aiverse-bots-debug` part B).
7. UI control (UI agent area) and view (models/particles) if the mechanic needs them; visual changes checked on a screenshot.
8. Docs: section in `docs/game-format.md`, a "Recipes" line, `rules_schema` help.
9. Proof: a prototype game in `games/` built only through MCP, no code edits.
10. Regression: build without warnings, `aiverse_tests`, CI `determinism`, headless matches of every `templates/` game with unchanged checksums.
11. Commit to your branch; MAIN merges (`skills/workflow/aiverse-main-merge`).

Runtime library: none. Engine development workflow.
