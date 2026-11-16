---
name: aiverse-rules-files
description: Read, change and write back a game's split rules.json (include files) from Python import / install scripts, and split a growing rules.json into topic and type files by a per-game plan, with tools/rules (rules_io, split_rules).
---

# rules-files

Format: `docs/game-format.md` § Splitting rules.json. Tools: `tools/rules/rules_io.py`, `tools/rules/split_rules.py`; tests `tools/rules/test_rules.py` (CTest rules_files). C++ readers use `readComposedJson` from `engine/data/JsonCompose.h`.

1. Scripts never `open()` / copy `rules.json` alone: it holds only the root and its `include` list. Use
   `sys.path.insert(0, '<engine>/tools/rules'); import rules_io`, then `doc = rules_io.load(project)` ... `rules_io.save(project, doc)`, or the path-like `rules_io.rules_file(project)` (`read_text` / `write_text`) for old code. Every top-level key and member goes back to the file it came from; a new member goes next to the one before it. When copying a project, copy its `rules/` too.
2. `resolve(doc)` gives what the engine loads (`extends` / `abstract` applied); `load` keeps them as written.
3. Splitting: `python3 tools/rules/split_rules.py --project <game> [--check]`. The game's plan is `<game>/rules-split.json`: `ownFile` (top-level blocks with their own file), `typeLimitKB`, `prefixes` (kinds of types by id prefix). Types keep their order (it is part of the game); the composed document is asserted unchanged; stale files are listed, not deleted.
4. After a split: the game's headless match and checksums unchanged; `--check` reports "current".

Python stdlib only. No library target.
