---
name: aiverse-skill-authoring
description: Add or change an AIVERSE repository skill - pick the category, write it in English, ship the code it needs (library package, tool or engine hook) with tests, register it, regenerate the catalog, audit references, update README counts.
---

# Author a repository skill

A skill shows how to use something; the code that makes it work ships with it. A code skill is three
deliverables that land together: **the library** (or tool / engine hook), **its tests**, **the SKILL.md**.
A recipe without working code, or code without tests, is not done. Rules: AGENTS.md "Adding and updating
repository skills" and §9 "Where library code goes".

## 1. Decide what the skill needs

1. Search first: `python3 tools/skills/catalog.py search <words>` (English and Russian) and
   `python3 tools/libraries/catalog.py search <words>`; extend an existing skill / package instead of adding an
   overlapping one (bump its version, keep IDs stable).
2. Pick the kind:
   - a reusable C++ capability -> a library package (section 2);
   - a missing engine behaviour -> engine code in its own file plus a small hook, tests, unchanged template
     checksums (`skills/workflow/aiverse-new-mechanic`);
   - a repeated manual procedure -> a tool in `tools/<area>/` with unit tests registered as a CMake test;
   - only existing code and MCP tools -> a workflow recipe (no runtime library) - say so in its last line and
     register it in `WORKFLOWS` (+ `WORKFLOW_ALIASES`) of `tools/skills/catalog.py`.

## 2. The library

1. Folder by what the code is (AGENTS.md §9): a genre package gets its own `libraries/include/aiverselib/genre/<name>/`
   (policy headers, `Data.h` JSON reader, `Persistence.h` records, optional umbrella header); cross-genre blocks go to
   `systems/`, infrastructure to `runtime/`, networking to `net/`, bots to `bots/`. Never a new flat file in `genre/`,
   never an aggregate header that includes other packages.
2. Header-only, C++20, namespace after the folder (`aiverse::lib::genre`, ...). Packages share namespaces: prefix
   generic names (`SafeZoneState`, not `ZoneState`; `RoyaleLobbyDef`, not `LobbyDef`) - check with the "together"
   compile in `tests/libraries/run.py`. Do not name a namespace like an engine one it would shadow (`aiverse::lib::net`
   hides `aiverse::net` for other `lib` code).
3. Simulation code: Fixed / integer only, own PCG32 stream in the state (seeded by the owner) or the passed
   `Random&`, deterministic iteration, one step per strictly increasing tick; invalid input / repeated ticks return
   `std::nullopt` and leave the state untouched (work on a copy, commit at the end). Bound every container.
4. Persistence: `io` / `valid` / a record kind per state; reserve a free block of eight kinds in
   `docs/genre-packages.md` § Save record kinds; save / load reject other kinds, versions, truncated or trailing bytes;
   a hash function folds the canonical bytes into checksums.
5. JSON: unknown keys and out-of-range values throw `JsonError("<root>.<path>: ...")`; times in ticks, distances in
   metres or milli-units as the neighbouring packages do; ship an example file under `examples/libraries/...`.
6. Register in `libraries/catalog.json` (keep its 2-space indentation): `id`, `version`, `layer`, `status`,
   `source`, `skill`, `summary`, `notes` (authority boundaries, record kinds, what is not covered), `cmakeTarget`
   `AIVERSE::lib_<id_with_underscores>`, `runtime: "header"`, `dependencies` (every file), `linkDependencies`,
   `packageDependencies` (only what the code includes). The CMake loop creates the INTERFACE target from the entry.

## 3. The tests

1. One test file `tests/libraries/<name>_tests.cpp`: every policy's happy path, boundary values, invalid input
   (state unchanged), repeated / backward ticks, save -> load -> equal + hash equal, truncated / extended / other-kind
   bytes rejected, JSON errors with the exact path in the message, and the example file loaded and run.
2. Seeded or numeric code: add golden values (hash of a state after N steps) and check them with gcc, clang and
   `-DAIVERSE_FORCE_PORTABLE_MUL`; run once under `-fsanitize=address,undefined`.
3. Register the test twice: root `CMakeLists.txt` (`if(AIVERSE_BUILD_TESTS)` block linking `AIVERSE::lib_<id>`,
   `add_test(NAME library_<name> ...)`) and `tests/libraries/CMakeLists.txt` (standalone build with the engine sources
   it needs, `-Wall -Wextra -Werror -pedantic`).
4. Pitfall: never `for(auto e:*step(...))` - in C++20 the temporary optional dies before the loop body; store the
   result first (tests use a `val(optional)` helper).
5. Integration into a module (FPS, RTS): a test in that module's suite, a headless match on every affected template
   with `--verify-restore-at`, and proof that templates without the new block keep their checksums.

## 4. The skill file

1. `skills/<category>/<id>/SKILL.md` (categories: `CATEGORIES` in `tools/skills/catalog.py`); frontmatter
   `name` = folder; English only (Russian only in quoted UI strings).
2. Content: when to use (genres, not game names), headers and the umbrella include, data first (the example file),
   tick order for the owner, authority (what the owner reports and applies), determinism rules, persistence and
   hashing, checks (test name, tools), what is not implied, and the build selection line
   (`AIVERSE::lib_<id>`, `aiverse_native_use_libraries(target <id>)`). Only real paths in backticks.
3. Search aliases: add the package to `ALIASES` in `tools/skills/catalog.py` with English and Russian keywords;
   no commercial game names.

## 5. Register, document, verify

1. `python3 tools/skills/catalog.py generate`, then `check`; `python3 tools/libraries/catalog.py check`;
   `python3 tests/skills/catalog_test.py`; `python3 tools/skills/audit.py --strict` (backticked paths and MCP tool
   names must exist); `python3 tests/libraries/run.py` (use `CXX=clang++` where gcc 13 trips the existing
   `-Wdangling-reference` in older headers); `python3 tests/skills/isolation.py` when linking changes.
2. Docs: a section in `docs/genre-packages.md` (or the matching docs file) and a row in its package table; module
   docs when wired into a module. README: the package in the «Any genre» row and the skill count in `README.md` and
   all eight translations (`skills/workflow/aiverse-docs-upkeep`), then `python3 tools/readme/check.py`.
3. AGENTS.md: one row in the area table for your branch (paths and additive hooks).
4. Commit library + tests + skill + catalog in one change; list unverified platforms in the message.

Code: `tools/skills/audit.py` and `tools/skills/catalog.py`. No library target.
