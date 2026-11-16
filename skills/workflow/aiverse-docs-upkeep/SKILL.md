---
name: aiverse-docs-upkeep
description: Keep AIVERSE documentation current when a feature ships - README and its eight translations, translated docs, game-format sections, project docs - and pass tools/readme/check.py.
---

# Documentation upkeep

Rules: AGENTS.md section 11.

1. Decide: update README only when users or agents can do something new (genre package or module, template, platform or plugin, tool, editor window, MCP area, importer) or a quoted count changes (templates, libraries, skills, MCP tools). Bug fixes and refactors do not touch it.
2. Where: the matching existing place - "Any genre" table, "Ready-made templates", "What's inside", "Coming from Unity", "Quick start", "Status" (verified only), "Documentation" row. Never a new section for one feature.
3. How: `README.md` and all eight translations (`ru`, `zh-CN`, `ja`, `ko`, `es`, `pt-BR`, `de`, `fr`) in the same commit, same structure; code and identifiers untranslated; every claim backed by the repository; one line per item.
4. Docs: `docs/<name>.md` is canonical English; `overview`, `game-wizard`, `engine-import`, `genre-packages` also update `docs/<lang>/` in the same commit. Data formats go to `docs/game-format.md` (+ `rules_schema` help).
5. Screenshots: `docs/images/readme/`, real renders, <= 1 MB.
6. `python3 tools/readme/check.py` must report 0 problems.
7. Project knowledge (status, decisions, plans) goes to the claude.ai project docs, not into the repository.

Runtime library: none. Repository process workflow.
