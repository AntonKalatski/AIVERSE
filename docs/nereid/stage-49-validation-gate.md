# NEREID Stage 49 — repeatable CPU regression and handoff

**Date:** 2026-11-19
**Working branch:** `feature/nereid-water-core`
**Implementation commit:** `27dfb6291d4932fee75e88c1dd59762714a6553c`
**Handoff commit:** separate documentation commit containing this file (see `git log -1`).
**Pull request:** [#23](https://github.com/chromealex/AIVERSE/pull/23) — Draft, unmerged.



## Stage 49 — reproducible NEREID stabilization gate and CI

**Date:** 2026-11-19. **Branch:** `feature/nereid-water-core`, Draft PR #23. **Implementation SHA:** `27dfb6291d4932fee75e88c1dd59762714a6553c`. **Handoff:** `docs/nereid/stage-49-validation-gate.md` (committed separately after implementation per `AGENTS.md`).

**Why:** native macOS CTest found independent renderer, surface, VFX, and skill catalog failures; previous isolated fixes were committed, but not all test executables had been rebuilt for each rerun. The owner's latest targeted `ecs_nereid_renderer_contract_test` result was **PASS (1/1)** following Stage 48. No complete green 118-test CTest has been shown.

**Implemented validation:** added `tools/nereid/validate.sh` (portable Bash) with `--focused` or `--full`. It captures current Git HEAD, runs `tools/skills/catalog.py check`, routing regression and six NEREID-skill audit, configures a separate CMake build, **builds targets before tests** (focused list or *all* targets for full mode), then executes NEREID ECS/geometry/renderer/confluence, native RTS integration, three VFX graph/presentation/assets checks, and three skill CTests. Full mode additionally executes **all CTest tests**. Each stage writes a log and exit status under `<build>/nereid-logs`. A failing command returns a nonzero exit and does not get masked by `tee`. `AIVERSE_NEREID_BUILD_DIR` and `AIVERSE_NEREID_JOBS` customize execution; default build is `build/nereid-validation`. It does **not** regenerate skills, change `main`, or silently skip failed tests.

**Automated CI:** new `.github/workflows/nereid-regression.yml` runs the same focused gate on `ubuntu-24.04` and `macos-15` for NEREID branch pushes/related PR changes and offers manual workflow dispatch. The workflow builds CPU contracts without requiring a graphics Device and uploads per-stage logs. These jobs are **not** GPU/Metal shader or visual quality proof.

**Documentation fix:** specialized `aiverse-nereid-currents` and `aiverse-nereid-vortices` skills now specify that optional persistence registration functions are declared in `<aiverselib/water/NereidWaterSystems.h>` and must not be registered twice when `registerNativeWaterPipeline` enables them. There was no water runtime or ECS API change.

**Actually verified:** inspected CMake target/test names, API declarations, the existing workflow conventions, CI branch/pull request status and created the implementation commit; checked published GitHub files and handoff. **NOT RUN:** this new Bash script, CMake or native CTest in current agent runtime; not yet observed new CI running or succeeding. Existing workflow runs on old branch SHAs were queued. Do not assert PASS for the gate.

**Immediate next action:** after pulling the branch on Mac, run `AIVERSE_NEREID_BUILD_DIR=build/nereid-test bash tools/nereid/validate.sh --full` for the complete suite (or `--focused` for faster triage). Report the logs from `build/nereid-test/nereid-logs`. Fix any remaining failures, then collect real Metal/GLSL visual and performance evidence before marking PR #23 Ready.


## Code paths and contract

- `tools/nereid/validate.sh`: Bash 3.2+ compatible runner; records git SHA, stage logs and real stage exit status. Default focused validation; `--full` builds all targets and executes all CTest tests.
- `.github/workflows/nereid-regression.yml`: Linux/macOS focused CPU regression with uploaded logs; does not claim GPU correctness.
- `skills/gameplay/aiverse-nereid-currents/SKILL.md` and `skills/gameplay/aiverse-nereid-vortices/SKILL.md`: correct include ownership for persistence helper declarations.
- `docs/nereid/HANDOFF.md`, `docs/nereid/ARCHITECTURE.md`, `skills/gameplay/aiverse-nereid-water/SKILL.md` and this standalone handoff record responsibility and next steps.

## Commands to run on the owner's Mac

```bash
cd ~/Projects/AIVERSE
git switch feature/nereid-water-core
git pull --ff-only origin feature/nereid-water-core

# Full: rebuild all targets first, then targeted and complete CTest.
AIVERSE_NEREID_BUILD_DIR=build/nereid-test \
  AIVERSE_NEREID_JOBS=4 \
  bash tools/nereid/validate.sh --full

# Faster developer-only smoke run, in a dedicated fresh build dir:
bash tools/nereid/validate.sh --focused
```

On any failure capture `build/nereid-test/nereid-logs/summary.txt`, the failing stage's `.log` and `context.txt`. Re-validate on the final head SHA before merge. For performance/visual QA independently test the actual Metal renderer/GLSL path on real GPU hardware, including multi-height water, ocean origin rebasing, river join seams, foam/refraction and FPS/RTS integration.

## Actual test evidence and caveats

- **Previously reported by user:** `ecs_nereid_renderer_contract_test` passed after Stage 48, 1/1, 0 failed, 0.51 sec on Mac.
- **Agent current-stage actions:** GitHub connector source and test/CI configuration review, scripts/workflow added, handoff committed.
- **Not executed by agent:** native CMake, CTest, shell validator, CI job, GPU shader compiler, render captures or performance benchmark. All remain unverified on the new commit.
