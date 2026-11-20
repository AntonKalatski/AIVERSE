# NEREID Stage 45.5 — SpectraFX test compile failure repair

**Branch:** `feature/nereid-water-core`; **parent:** `ad4ca1ff14b7ce6199926f8140b48069c82cba8e`; **implementation SHA:** same atomic commit as this handoff (inspect `git log`).



## Stage 45.5 — SpectraFX ECS presenter test missing snapshot vector (macOS compile)

**Date:** 2026-11-19. **Destination:** `feature/nereid-water-core`, PR #23 stays Draft. **Source:** owner-supplied Clang compiler failure at `tests/vfx_ecs_presentation_test.cpp:87-88` (`particles` undeclared).

**Cause:** The rollback-reset test added `ps.snapshot(particles)` and `particles.empty()`, but never declared the local collection. Checked both the water branch and current SpectraFX development branch (`feature/universal-entity-inspector-fps`): the same defect is present in both. This is a test-local type declaration, not a missing engine API or a water component issue.

**Fix:** Introduced `std::vector<fx::FxParticleInfo> particles;` immediately before `ps.snapshot(particles)` inside `scenario<Kind>()`. The source already includes `<vector>`, and `ParticleSystem::snapshot(std::vector<FxParticleInfo>&) const` is the public API. No runtime FX behavior, ECS state or NEREID game logic was changed.

**Verification performed:** compared the two branch files, inspected `ParticleSystem.h` public API and committed the one-line fix via the GitHub connector. **NOT RUN:** actual macOS build, CTest, GPU validation, full regression. Do not claim test PASS. **Next:** pull the NEREID branch, run `cmake --build build/nereid-test --parallel 4`; after a successful build, run `ctest --test-dir build/nereid-test --output-on-failure --parallel 4`. Share subsequent failures; keep PR Draft until build and tests pass. **Upstream note:** the originating SpectraFX branch has the same bug, so the SpectraFX agent should also take this patch.


Changed files: `tests/vfx_ecs_presentation_test.cpp`, `docs/nereid/HANDOFF.md`, `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`, this handoff. This does not merge SpectraFX upstream or alter its branch.
