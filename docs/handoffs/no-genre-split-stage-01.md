# No genre split — stage 01 handoff (2026-11-21)

Branch: `feature/aiv20261121_no-genre` (from origin/main 7f3a9fbb4 + cherry-pick of the AGENTS rule 5f61be993).

## Done
- Audit of all features and skills: `docs/no-genre-split-audit.md` (debt G1-G9, removal plan in 7 stages).
- Skills: category `genres` removed (11 skills -> `gameplay`), 9 genre/module-named skill ids renamed,
  36 skills reworded to capability + data block + legacy host as debt. `skills/catalog.json` regenerated.
- AGENTS.md skill naming rule and §9 wording; `docs/genre-implementation.json` area titles.
- No code or behaviour change; package ids, CMake targets, namespaces untouched.

## Checks (run)
- `python3 tools/skills/catalog.py generate` / `check`: 236 skills validated.
- `python3 tools/skills/audit.py --strict`: see commit message for result.
- `python3 tools/libraries/catalog.py check` fails with AssertionError on status `experimental`
  (nereid-* packages) — pre-existing on origin/main, not caused by this stage.
- Engine build / CTest: not run (no code changed).

## Next
Stage 2 of the audit plan — naming (G7) or capability queries instead of `isFps()` (G2), as the owner chooses.
Racing branch 5fb186599 must not be merged as is (`fps.mode "race"`, `skills/genres/aiverse-racing`), see G6.
