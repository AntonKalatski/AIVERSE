# Package catalog (AIVERSE-PLUGINS) — handoff 2026-11-22

Branch `feature/aiv20261122_packages` (from GitHub main bb3d910). Implementation commit 63914f7.

## What exists

- Catalog repository https://github.com/chromealex/AIVERSE-PLUGINS (public):
  `<category>/<id>/aiverse.package.json` + README + previews; `tools/catalog.py`
  (check / pack / archive / publish / index), `tools/test_catalog.py`, workflows `check`
  (PRs) and `publish` (main: releases `<id>@<version>` + `index.json` commit), CODEOWNERS,
  CONTRIBUTING, `docs/manifest.md`. Payload modes: `files/` (<= 25 MB), `archive` (https
  zip + sha256), `source` (`{repo, commit, path}`).
- Published 1.0.0: `materials/terrain-pbr`, `materials/vegetation-pbr`,
  `textures/terrain-pbr-sources`, `textures/vegetation-pbr-sources` (built by the workflow
  from chromealex/AIVERSE@bb3d910ed149).
- Engine: `tools/pkg/aiverse_pkg.py` (+ `test_aiverse_pkg.py`, CTest `aiverse_pkg`),
  `engine/pack/Packages.h`, docs/packages.md (+ ru), skill `aiverse-packages`, skill
  frontmatter `packages:` read by `tools/skills/catalog.py`, website `catalog.html`.
- Removed from the engine: `engine/assets/materials/vegetation`, terrain textures except
  `checkerboard_*`, `tools/terrain/pbr_sources`, `tools/vegetation/pbr_sources`.

## Verified (actually run)

- `python3 tools/pkg/test_aiverse_pkg.py` PASS; `tools/test_catalog.py` PASS; catalog
  `check` and `publish --dry-run` PASS locally and in GitHub Actions (check + publish green).
- Real download of all four packages through the published index: sha256 OK; terrain
  textures byte-identical to the removed engine files (`diff -r`).
- `tests/terrain/pbr_pack_test.py` PASS (126 maps), `tests/vegetation/pbr_pack_test.py`
  PASS (100 maps) against the packages (their old hard-coded id sets were stale and are now
  supersets).
- gcc 13 `-Wall -Wextra -Wpedantic` compile of `apps/editor/core/Projects.cpp` and
  `apps/editor/Editor.cpp`: no warnings. `Packages.h` smoke with clang.
- `tools/skills/catalog.py check`, `tools/site/build.py check` (0 errors) and site build;
  catalog page screenshots (Chrome, 1440 px ru/en and 390 px) show the 4 packages.

## NOT RUN

Full editor/client build and run; macOS/Metal; creating an empty project in the editor
(the copy now takes only `textures/`); Windows paths of `Packages.h` (USERPROFILE).

## History cleanup (owner approved "after the move")

Measured on a mirror of GitHub (309 refs, 1.42 GB of blobs):
- moved materials/sources in history: engine/assets/materials 273 MB, tools/terrain/pbr_sources
  83 MB, tools/vegetation/pbr_sources 36 MB;
- the `builds` branch zips (windows/, macos/): ~880 MB — the biggest part, not touched yet.

Trial on a mirror copy:
```sh
git filter-repo --force --invert-paths \
  --path engine/assets/materials/vegetation/ \
  --path tools/terrain/pbr_sources/ --path tools/vegetation/pbr_sources/ \
  --path-regex '^engine/assets/materials/terrain/textures/(?!checkerboard_).*'
git reflog expire --expire=now --all && git gc --prune=now --aggressive
```
-> pack 1.42 GB -> 1.00 GB, in ~10 s. Steps for the real run (MAIN agent / owner):
1. Merge this branch into main. Ask every agent to push its branches (locked worktrees on
   the owner's Mac: agent-wind-masks, integration/strata-tornado-1122, agent-strata-cook,
   agent-place*, agent-replica*).
2. `git clone --mirror`, run the command above, `git push --force --mirror` (rewrites every
   branch and tag on GitHub; all SHAs change; open PRs need re-pointing).
3. Everyone re-clones (or `git fetch` + `git reset --hard origin/<branch>`); old local
   clones must not push back (they would resurrect the history).
4. Optional, separate decision: move `builds` zips to GitHub release assets and keep only
   `index.json` in git (another ~880 MB); the website releases page reads that index.

The catalog manifests' `source.commit` (bb3d910) becomes unreachable after the rewrite;
that is fine: published versions are never rebuilt.

## Next

- MCP ops `package_search/info/add/status` and a Studio "Catalog" window (Studio UI v1).
- Web viewer on the website for package models/materials (owner request 2026-11-22).
- `create` for template packages: move sample games out of `templates/` when wanted.
