# SpectraFX Stage 119 — local worktree contamination isolated after Stage 118

Date: 2026-11-20
Branch: `integration/spectrafx-main-sync-20261120`
Verified remote head (before this doc): `5682c60360c29b9fc0484f4a19a9fac8d53407d5`
Base main: `e4129c5062a6ab47e7c70f3fdd483392fde1f679`

## Observed Mac Studio failure

User attempted to run `git diff --check origin/main HEAD` and
`cmake -S . -B build/spectrafx-main-qa` from their Mac.

Reported `git diff --check` showed:
- `AGENTS.md:1254: new blank line at EOF`
- `docs/agent-handoffs/texture-rhi-vulkan-upload-publication-2026-11-19.md:31: trailing whitespace`
- `docs/texture-rhi.md:2505: trailing whitespace`
- `skills/visuals/aiverse-texture-rhi/SKILL.md:1138: trailing whitespace`

Reported `cmake` failed:
```
CMake Error at CMakeLists.txt:623:
  Parse error. Expected a command name, got unquoted argument with text
  "<<<<<<<".
```

## Remote source inspection (actually executed via GitHub connector)

Directly fetched `CMakeLists.txt`, `AGENTS.md`, and the three
reported `texture-rhi` files at both published remote refs
`main` and `integration/spectrafx-main-sync-20261120`.

- **Remote integration `CMakeLists.txt` is free of `<<<<<<<`,
  `=======`, `>>>>>>>` conflict lines.** Remote Git blob SHA:
  `d37781a8322f80984206c0a036b5dd4f3c4a7043`. The exact
  remote `CMakeLists.txt` line 623 is:
  `target_compile_options(native_app_glue PRIVATE -w)`
  (with existing explanatory comment), not a conflict marker.
- `AGENTS.md` is byte-identical on both remote refs, Git blob
  `b3001d1caa3456dc24899934b6c342fbecc4ecbf`.
- None of the three `texture-rhi` paths named above exists on
  either checked remote ref (GitHub fetch_file returned NOT_FOUND).
- Stage 118 remote source has the previously fixed handoff metadata
  whitespace removed. GitHub branch still pointed at
  `5682c60360c29b9fc0484f4a19a9fac8d53407d5`
  at verification.

Therefore **the reported CMake conflict and unrelated Texture RHI
diff cannot be reproduced from the published integration commit**.
Likely causes include a locally modified working tree or unexpected
local refs / another checkout. Do not assert which exact cause
without seeing `git status`, `git rev-parse` and actual local blob.

## Isolation, not destructive cleanup

Do **not** use `git reset --hard`, `git clean -fdx`,
`git checkout -- .`, `git restore`, `git rebase --abort`,
or overwrite or stage another agent's potentially unfinished work
in the contaminated worktree. Create an additional dedicated
worktree from the exact published integration ref:

```bash
set -euo pipefail
cd ~/Projects/AIVERSE-spectrafx-validation
git fetch origin main integration/spectrafx-main-sync-20261120
git worktree add --detach ~/Projects/AIVERSE-spectrafx-final-clean-20261120 \
  origin/integration/spectrafx-main-sync-20261120
cd ~/Projects/AIVERSE-spectrafx-final-clean-20261120
git submodule update --init --recursive
git rev-parse HEAD
git rev-parse HEAD:CMakeLists.txt
git hash-object CMakeLists.txt
git status --short
git --no-pager diff --check origin/main HEAD

# Exact remote CMake conflict-marker gate.
if git grep -n -E '^(<<<<<<< |=======|>>>>>>> )' -- CMakeLists.txt; then
  echo "Unexpected conflict markers in clean integration checkout" >&2
  exit 1
fi

cmake -S . -B build/spectrafx-main-clean \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON \
  -DAIVERSE_BUILD_CLIENT=ON -DAIVERSE_BUILD_HEADLESS=ON \
  -DAIVERSE_BUILD_TOOLS=ON
cmake --build build/spectrafx-main-clean --parallel 4 \
  --target aiverse_spectrafx_header_smoke \
  aiverse_spectrafx_preview_policy_tests \
  aiverse_editor aiverse_fps_client aiverse_client
```

If focused build passes, run full native + Metal + screenshot acceptance:

```bash
AIVERSE_ACCEPTANCE_REQUIRE_METAL=1 \
AIVERSE_SPECTRAFX_RHI_STRESS_FRAMES=512 \
AIVERSE_SPECTRAFX_RHI_STRESS_CAPACITY=256 \
AIVERSE_SPECTRAFX_SCENE_CAPTURE=/tmp/spectrafx-final-clean-scene \
  bash tools/spectrafx/acceptance.sh build/spectrafx-main-clean
```

The new documentation commit after `5682c6036` does not change
the CMake code or test binaries; nevertheless record the **exact
checkout SHA actually compiled**.

## Remaining limitations and next agent step

No local Mac command has been run by this agent on the user's machine.
No full integrated native CMake / CTest / Metal evidence for the
integration branch exists yet. Prior **128/128 full CTest, 13/13
SpectraFX, 4/4 real MetalGPU, 5/5 visual** PASS is for
pre-integration SHA `ba4b126fd533cdbcb8443f46a68c6d8afe76867f`.

Next agent must check the clean worktree's exact SHA, configure
output and full acceptance summary. If the clean checkout still
shows CMake conflict markers, obtain the local `git status --short`,
`git rev-parse HEAD:CMakeLists.txt`, `git hash-object
CMakeLists.txt`, and `git show HEAD:CMakeLists.txt` around
line 623 before changing GitHub source. Do not merge to main until
real merged-source acceptance passes.
