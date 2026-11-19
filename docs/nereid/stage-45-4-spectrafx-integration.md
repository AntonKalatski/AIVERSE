# Stage 45.4 — selective SpectraFX upstream synchronization for NEREID build

**Date:** 2026-11-19
**Destination branch:** `feature/nereid-water-core` (PR #23 Draft)
**Implementation commit:** `191b22e5948b502651ec3da53777e1b9873181f7`
**Upstream SpectraFX branch:** `feature/universal-entity-inspector-fps`
**Upstream fix commit:** `3aa17d390fae819458cc3f89127f25804e3b2877`



## Stage 45.4 — SpectraFX upstream header compile fix selected for water integration

**Date:** 2026-11-19. **Branch:** `feature/nereid-water-core` (Draft PR #23, no merge to main). **Implementation commit:** `191b22e5948b502651ec3da53777e1b9873181f7`.

**User-reported build error:** Clang reports `unknown type name ParticleSystem` in `engine/fx/VfxGraphAssets.h`, then unknown `fx::ParticleSystem`/`fx::FxHandle` in `engine/fx/SpectraFX.h`, while compiling `tests/vfx_graph_compiler_test.cpp`. These are compile-time public-header dependency failures unrelated to NEREID simulation.

**Discovery:** there is no dedicated branch named SpectraFX. Active upstream VFX development is in `feature/universal-entity-inspector-fps` (Stage 40 on its head). Compared against NEREID branch: **diverged**, 520 upstream-ahead / 356 water-ahead commits at inspection; a full merge risks unresolved CMake/editor/render conflicts and changes outside NEREID scope. The exact upstream fix was already committed as `3aa17d390fae819458cc3f89127f25804e3b2877`, changing only `engine/fx/VfxGraphAssets.h` to include `"ParticleSystem.h"` after `"VfxGraph.h"`.

**Action taken:** applied the identical upstream one-line fix to the water branch without merging unrelated commits. Verified the resulting `VfxGraphAssets.h` content is byte-identical to the corresponding SpectraFX-upstream header. This supplies the complete ParticleSystem type for inline `ps_.define`, and transitively exposes `ParticleSystem`/`FxHandle` for aliases in `SpectraFX.h`. No gameplay, VFX behavior, ECS schema, GPU code or water code changed.

**Actually verified:** GitHub branch listing, branch divergence, upstream commit diff, source comparison, and remote commit/ref update.
**NOT run:** macOS Clang recompile, CTest, graph runtime checks, VFX GPU/Metal effects, full water/FPS/RTS regression. Compilation is not claimed successful. Keep Draft PR.

**Next command:** fetch/pull `feature/nereid-water-core`, reconfigure only if required by other changes, and rerun `cmake --build build/nereid-test --parallel 4`; if it succeeds, run `ctest --test-dir build/nereid-test --output-on-failure --parallel 4`. Report the first new compiler/runtime failure and continue in the same branch.


## Changed files

Implementation: `engine/fx/VfxGraphAssets.h` (adds `#include "ParticleSystem.h"`). Handoff: `docs/nereid/HANDOFF.md`, `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`, and this file.
