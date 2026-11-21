# Lighting stage 42: scrolling GI cascades (2026-11-21)

Branch `feature/aiv20261121_vulkan_gi_android` (base: `feature/aiv20261121_fix_release_link`).

## What

- `GiMainCpuConfig::scroll` (GPU bridge only; the CPU bridge refuses it, `DynamicGiHost`
  reads `"scroll"` and drops it on CPU fallback with a note).
- `GiUpdateScheduler::scroll(dx,dy,dz,reason)`: whole-cell origin move with a toroidal
  ring offset (`slot = (cell + ring) % dims`, the shader already addressed this way).
  Kept slots keep their world cell; returns the recycled slots, marked dirty.
  `position()` and `invalidate()` follow the ring; `ring(axis)`, `origin()`.
- `GiGpuSmallScene::clear(probes)`: same kernel with zero samples writes zero
  coefficients; own job buffer (one more storage buffer), so a clear and a trace can
  share a frame.
- `GiMainGpuBridge`: `recenter(focus)` in `updateFrame` (only when the atlas is not
  read or written by the GPU — `canMutate`), truncating the cell offset (one cell of
  dead band); zeroes recycled probes; re-plans residency when the outer cascade leaves
  the box planned with half-size margins (`planLattice`). GPU geometry revisions now
  count uploads (`gpuRevision_`) so a re-plan can reuse the scene revision.
  `cascade(k)` returns the moved origin, `cascadeRing(k)`, `scrollStats()`.
  `canPresent()` in scroll mode stays true after the first full convergence.
- Renderer U6 ring.xyz from `cascadeRing`.
- User decision: recycled probes are zeroed (option 3), not kept stale or blanked.

- `updateFrame` in scroll mode waits only for the compute fence, not for the
  fence of views that sampled the atlas: the atlas changes only through GPU
  dispatches, which the queue orders after those draws. Without this a view
  presenting every frame with frames in flight starved the updates.
- Android (Vulkan, Adreno 610) first run: the return scroll came a frame or more
  after the away probes finished, the test sampled before it; the test now waits
  for every cascade to be back at its home lattice.

## Tests

- `lighting_gi_scrolling` (CPU, fake device): scheduler ring, recycled faces,
  invalidation through the ring, negative/full shifts; bridge dead band, clear then
  trace in one frame, presentation during updates, residency re-plan picks up a far
  chunk. Ran locally (g++ 13, Linux): PASS. All other gi_*/lighting_* CPU tests PASS
  (`gi_gpu_bvh_sparse_upload` expects 7 buffers now).
- `lighting_compute_gi_gpu_scroll` (Metal) and `lighting_compute_gi_scroll_vulkan`
  (Vulkan host): focus walks away and back, pixel must match the CPU reference.
  NOT RUN yet (queued on the Mac / Android).

## Next

Run Metal Lighting and the Android Vulkan job; then merge to main.
