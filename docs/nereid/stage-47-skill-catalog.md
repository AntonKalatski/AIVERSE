# NEREID Stage 47 — de-duplicated skills and catalog regeneration

**Date:** 2026-11-19
**Branch:** `feature/nereid-water-core`, PR #23 Draft
**Parent:** `a65ad8ca8127ae7dae8f2d4b66cd04469e5b76bd`
**Implementation:** same atomic commit as this handoff; use `git log -1` for SHA.



## Stage 47 — repair duplicate NEREID skill IDs and regenerate the catalog

**Date:** 2026-11-19. **Branch:** `feature/nereid-water-core`, PR #23 Draft, parent `a65ad8ca8127ae7dae8f2d4b66cd04469e5b76bd`. **Actual macOS failures:** `skill_routing` fails with `ValueError: duplicate skill id` from `tools/skills/catalog.py validate`; `skill_catalog` reports `stale skill catalog; run generate`.

**Root cause:** six separately registered native NEREID package IDs (`nereid-water/buoyancy/interactions/cascades/currents/vortices`) all pointed at the same `skills/gameplay/aiverse-nereid-water/SKILL.md`. The generator creates one catalog entry **per package** and derives the entry ID from skill-directory name, producing the ID `aiverse-nereid-water` six times. The committed generated JSON was also stale (only the original `nereid-water` entry, and appended unsorted newer entries).

**Fix:** give each of the five specialized packages its own stable skill ID, frontmatter and `SKILL.md` while retaining the shared water skill and original library targets. Updated only those five `libraries/catalog.json` mappings. Add concise package-specific instructions, sources and CMake targets; include the full NEREID skill handoff as a common reference. Preserve package dependencies on the core skill. Explicit routing aliases for all six water skills are in `tools/skills/catalog.py`. Rebuilt `skills/catalog.json` from current catalog entries and all native package metadata, sorted by ID, with 219 unique IDs and original unmodified meta/workflow entries. This follows `generate`'s entry fields/ordering and JSON indentation.

**Source-level checks performed:** duplicate scan now finds zero; all 134 library packages have one skill ID and corresponding catalog entry; all dependencies resolve; five new SKILL frontmatter names match their catalog IDs; target names correspond to packages. **NOT RUN:** native `python3 tools/skills/catalog.py generate/check`, `tests/skills/catalog_test.py`, full skill audit or CTest (no runnable AIVERSE checkout); if generator reports differences, regenerate on a real checkout and commit its exact output. **Next:** pull branch, `python3 tools/skills/catalog.py check`, `python3 tests/skills/catalog_test.py`, CTest skill tests; then complete all pending NEREID/VFX regressions. Do not merge Draft PR until verified.


## Changed files

- `libraries/catalog.json`: five specialized package-to-skill mappings
- `tools/skills/catalog.py`: six explicit NEREID routing aliases
- `skills/catalog.json`: regenerated unique/sorted entries
- Five new `skills/gameplay/aiverse-nereid-*/SKILL.md` files (other than core water)
- `skills/gameplay/aiverse-nereid-water/SKILL.md`
- `docs/nereid/HANDOFF.md`, `docs/nereid/ARCHITECTURE.md`, this handoff

## Local commands

```sh
python3 tools/skills/catalog.py check
python3 tests/skills/catalog_test.py
python3 tools/skills/audit.py
ctest --test-dir build/nereid-test --output-on-failure -R '^(skill_catalog|skill_routing|skill_audit)$'
```

**Limitations:** Python tests have not run. All game/C++ APIs are unchanged; only skill metadata/documentation was modified. If other concurrent agents change library metadata, re-run the generator before shipping.
