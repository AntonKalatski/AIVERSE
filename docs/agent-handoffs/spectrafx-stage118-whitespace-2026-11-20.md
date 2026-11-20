# SpectraFX Stage 118 — integration diff whitespace cleanup

Date: 2026-11-20
Branch: `integration/spectrafx-main-sync-20261120`
Previous integration doc HEAD: `98e4df905683b71cd79b1613c181ba06d77ad6b1`
Main parent: `e4129c5062a6ab47e7c70f3fdd483392fde1f679`
Original two-parent merge: `7ef385060956b73e131821e66e3a8b515da6ac32`

## Trigger and root cause

On the new Mac integration worktree, `git diff --check origin/main HEAD`
reported repeated `trailing whitespace` diagnostics for SpectraFX
agent-handoff Markdown files (starting at Stage 100, lines 3–5).
These were intentional Markdown hard-break spaces on metadata rows such
as Date/Branch/Implementation SHA, but Git's whitespace validator
properly treats them as invalid trailing whitespace on new lines.

## Exact change

Normalize `[ \\t]+$` to an empty string for the affected SpectraFX
handoff Markdown files, preserving all visible text and C++ code:

- Stage 100–111: 12 handoff files, 70 whitespace bytes stripped.
- Stage 112–117 and Stages 94–96, 98–99: 11 handoff files,
  64 whitespace bytes stripped.
- Stage 97 and the remaining `spectrafx-2026-11-19.md`,
  `spectrafx-current.md` and `docs/spectrafx-gpu-runtime.md`
  were already free of trailing spaces.
- No modified engine sources, CMake, RHI, shaders, tools, tests or
  gameplay code were changed by this cleanup.

Total: **23 Markdown files, 134 trailing space bytes removed**.
Existing unchanged whitespace in `main` (e.g. two Markdown header
lines in the existing SpectraFX skill, an unchanged blank line in
FPS client) was **not modified**, because it is not newly introduced
by the integration diff.

## Actual source check

The full list of 90 files changed from `main` was inspected through
GitHub. Every one of the 79 added files was considered; the added
engine/Fx code, CTest sources, Editor windows, CI workflow and tools
had no trailing whitespace. The 11 modified paths were examined as well.
The 23 affected Markdown files were fetched from the integration
branch and transformed by a trailing-space-only operation. The
resulting Git tree was built incrementally on the existing integration
tree, leaving all nonlisted paths unchanged.

**Not yet run in a local Mac worktree on this new commit**:
`git diff --check origin/main HEAD`, native CMake, CTest, Metal GPU
or Editor GUI acceptance. The prior 128/128, 13/13, 4/4 results apply
to the pre-main-merge SpectraFX SHA
`ba4b126fd533cdbcb8443f46a68c6d8afe76867f`, not the
current integrated source.

## Next Mac integration check

```bash
cd ~/Projects/AIVERSE-spectrafx-main-qa
git status --short
git fetch origin
git switch --detach origin/integration/spectrafx-main-sync-20261120
git diff --check origin/main HEAD

# Only after the whitespace check exits 0:
cmake -S . -B build/spectrafx-main-qa \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON \
  -DAIVERSE_BUILD_CLIENT=ON -DAIVERSE_BUILD_HEADLESS=ON \
  -DAIVERSE_BUILD_TOOLS=ON
cmake --build build/spectrafx-main-qa --parallel 4 \
  --target aiverse_spectrafx_header_smoke \
  aiverse_spectrafx_preview_policy_tests \
  aiverse_editor aiverse_fps_client aiverse_client

# Then full acceptance once, on exact integration source:
AIVERSE_ACCEPTANCE_REQUIRE_METAL=1 \
AIVERSE_SPECTRAFX_RHI_STRESS_FRAMES=512 \
AIVERSE_SPECTRAFX_RHI_STRESS_CAPACITY=256 \
AIVERSE_SPECTRAFX_SCENE_CAPTURE=/tmp/spectrafx-merged-scene \
  bash tools/spectrafx/acceptance.sh build/spectrafx-main-qa
```

If `git diff --check` still reports errors, capture its complete
output without a pager (`git --no-pager diff --check origin/main HEAD`)
and correct only the affected newly introduced lines. Preserve other
agents' work. Do not merge into `main` or fast-forward the original
SpectraFX feature branch before merged-source hardware acceptance.
