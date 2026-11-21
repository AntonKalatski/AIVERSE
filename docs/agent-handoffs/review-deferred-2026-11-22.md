# Review deferred items, closed (2026-11-22)

Branch `feature/aiv20261122_review_deferred` (from main `d677f8a2f`). Follows the
`review-fixes-*-2026-11-21.md` handoffs.

| Item (handoff) | Status | Change |
|---|---|---|
| physis-terraweave 13: phenology-only full resample | DONE | `WorldScene` keeps the last height snapshot + exclusion mask and reuses them when only phenology changed (`lastVegetationHeights_` / `lastVegetationMask_`, reset on shutdown). |
| nereid 5: double flood resource copy per tick | DONE | `addNereidFloodRoutingSystem` steps the COW-detached resource in place (`World::resource<T>()`); ports restored by RAII on every exit. Test `floodSystemStepsInPlace` (COW rollback + replay). |
| spectrafx 3: CPU fallback redefinition keeps old definition | DONE | `ParticleSystem::undefine`, `BatchWorld::releaseEffect` (monotonic internal names, ids never reused), `GpuPoolWorld::recountBindings` frees unbound CPU definitions. Test in `vfx_shared_batch_world`. |
| spectrafx 4: per-slot `writeSlot` uploads | DONE (no scatter kernel needed) | Writes are queued and uploaded at the next `step()` as one range per run of consecutive written slots; a range never spans an unwritten slot. Only the compute kernel reads these buffers (drawing samples the texture it writes), so the deferral is invisible. Metal test reads the buffer back after scattered kill/emit churn. |
| gfx-strata 3: Vulkan dynamic buffer whole-shadow copy | DONE (dirty-range variant) | Each `VBuffer` logs shadow writes (<= 256 ranges); a reused version copies only the ranges it missed; unsynced / dropped history / backlog >= size fall back to a full copy. Covers the STRATA arena and every dynamic buffer. `vulkan_backend` checks 12 rotating frames against GPU readbacks. |
| physis-terraweave 6: Box3D publisher not wired | DONE | Coordinator keeps a listener list (`addPatchListener`); `TerrainBox3DPublisher::attach`. GI bridge now adds instead of replacing. Test in `physis_box3d`. |
| physis-terraweave 7: remote edit unreachable | NOT DONE | Needs an authenticated host transport; AGENTS.md forbids a default anonymous admin listener. Left as API. |
| physis-terraweave 5: ecology containers | NOT DONE | Perf only, needs a benchmark first. |
| prisma-view 3: capture-mode fingerprints | NOT DONE | Needs revision counters in sim MapGrid / RtsScene. |
| editor-input 12: per-frame allocations | NOT DONE | Restructure only with benchmarks (reviewer's call stands). |
| ci-lens: MinGW full CTest | NOT DONE | Python drivers call .exe without Wine; needs CI access to iterate. |

Verification: Mac `./run-tests.sh` (KINGSTON queue) and the phone job `android_deferred`
(`aiverse_vulkan_tests`, HDR Vulkan cases); results are recorded in the merge commit / project notes.
