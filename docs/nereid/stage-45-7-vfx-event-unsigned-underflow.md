# NEREID validation Stage 45.7 — SpectraFX false-negative event regression

**Date:** 2026-11-19
**Branch:** `feature/nereid-water-core`, PR #23 Draft
**Starting commit:** `79596d1b10ee62aa16e8a9d9502cffd268bc1084`
**Stage commit:** implementation + docs in one atomic commit.



## Stage 45.7 — SpectraFX test regression: unsigned underflow during gameplay event check

**Date:** 2026-11-19. **Branch:** `feature/nereid-water-core`, PR #23 Draft. **Actual failure:** on macOS, CTest `vfx_ecs_presentation` (109/118) aborted with `std::runtime_error: gameplay event did not play`.

**Root cause:** the test compared `size_t once >= before-20`, where `before` is an unsigned particle count from a low-rate continuous emitter. When `before<20`, subtraction wraps to an enormous `size_t` value and reports a failure even when gameplay-event emission works.

**Fix:** assert the actual `impact_flash` particle ID in a headless `ParticleSystem::snapshot`: the fixture's High-quality three-particle burst, with 0.5-second lifetime, must have exactly three impact particles after one 0.1-second update. Replaying the same tick must not add another impact burst, tested after a second 0.1-second update. Existing reset check still requires no attached or one-shot particles to remain. Added `<algorithm>` for `std::count_if`. No shipping VFX, ECS or water code changed.

**Performed:** GitHub source inspection of CPU emission and presenter, guarded edit, branch commit and handoff. **Not performed:** native C++ build/CTest rerun, GPU check or FPS/RTS/PHYSIS regressions. **Next:** pull, build `aiverse_vfx_ecs_tests`, run `ctest --test-dir build/nereid-test --output-on-failure -R '^vfx_ecs_presentation$'`; if successful, full CTest. Remain Draft.


## Changed files

- `tests/vfx_ecs_presentation_test.cpp`: replaces broken unsigned count heuristic with event-ID-filtered particle snapshot and duplicate-tick invariant
- `docs/nereid/HANDOFF.md`
- `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`
- this handoff

## Reproduce/verify

```sh
cd ~/Projects/AIVERSE
git switch feature/nereid-water-core
git pull --ff-only origin feature/nereid-water-core
cmake --build build/nereid-test --target aiverse_vfx_ecs_tests --parallel 4
ctest --test-dir build/nereid-test --output-on-failure -R '^vfx_ecs_presentation$'
```

After targeted success, run full CTest. The upstream SpectraFX owner should receive the test fix separately; do not merge an unrelated large feature branch as a workaround.
