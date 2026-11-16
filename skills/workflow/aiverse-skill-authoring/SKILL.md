---
name: aiverse-skill-authoring
description: Add or change an AIVERSE repository skill - pick the category, write it in English, ship the code it needs (library package, tool or engine hook) with tests, register it, regenerate the catalog, audit references, update README counts.
---

# Author a repository skill

A skill shows how to use something; the code that makes it work ships with it. Rules: AGENTS.md "Adding and updating repository skills".

1. Search first: `python3 tools/skills/catalog.py search <words>`; extend an existing skill instead of adding an overlapping one.
2. Decide what the skill needs:
   - a reusable C++ capability -> a library package (`libraries/include/aiverselib/...`, an independently selectable `AIVERSE::lib_*` target, tests, `libraries/catalog.json` entry; see `skills/workflow/aiverse-orchestrator`);
   - a missing engine behaviour -> engine code in its own file plus a hook, tests, unchanged template checksums (`skills/workflow/aiverse-new-mechanic`);
   - a repeated manual procedure -> a tool in `tools/<area>/` with unit tests registered as a CMake test;
   - only existing code and MCP tools -> a workflow recipe (no runtime library) - say so in its last line.
3. Place it in `skills/<category>/<id>/SKILL.md` (categories: `CATEGORIES` in `tools/skills/catalog.py`); frontmatter `name` = folder; English only.
4. Content: when to use, steps with exact commands / MCP tools / files, invariants, done criteria, what stays unverified. Reference real paths only.
5. Register: library skills through `libraries/catalog.json`; workflow skills in `WORKFLOWS` (+ `WORKFLOW_ALIASES` keywords, Russian words welcome) of `tools/skills/catalog.py`.
6. `python3 tools/skills/catalog.py generate` then `check`; `python3 tests/skills/catalog_test.py`.
7. `python3 tools/skills/audit.py --strict` - every backticked path exists, every MCP tool name is registered (CMake test `skill_audit`).
8. README: the skill count in `README.md` and all eight translations (`skills/workflow/aiverse-docs-upkeep`), `python3 tools/readme/check.py`.

Code: `tools/skills/audit.py` and `tools/skills/catalog.py`. No library target.
